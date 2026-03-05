#include "WebUiServer.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// ---- Base64 encode ----------------------------------------
static std::string b64Enc(const unsigned char* buf, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, mem);
    BIO_write(b64, buf, (int)len);
    BIO_flush(b64);
    BUF_MEM* p; BIO_get_mem_ptr(b64, &p);
    std::string r(p->data, p->length);
    BIO_free_all(b64);
    return r;
}

// ---- MIME types -------------------------------------------
std::string WebUiServer::mimeType(const std::string& path) {
    auto ext = path.substr(path.rfind('.') + 1);
    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "js")   return "application/javascript";
    if (ext == "css")  return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "png")  return "image/png";
    if (ext == "ico")  return "image/x-icon";
    return "application/octet-stream";
}

// ---- Constructor / Destructor -----------------------------
WebUiServer::WebUiServer(uint16_t port, const std::string& webuiDir)
    : port_(port), webuiDir_(webuiDir) {}
WebUiServer::~WebUiServer() { stop(); }

// ---- start / stop -----------------------------------------
bool WebUiServer::start() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) return false;
    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);
    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[webui] bind port " << port_ << " failed: " << strerror(errno) << "\n";
        return false;
    }
    listen(serverFd_, 16);
    running_ = true;
    acceptThread_ = std::thread(&WebUiServer::acceptLoop, this);
    std::cout << "[webui] Web UI at http://0.0.0.0:" << port_ << "\n";
    return true;
}

void WebUiServer::stop() {
    running_ = false;
    if (serverFd_ >= 0) { ::close(serverFd_); serverFd_ = -1; }
    if (acceptThread_.joinable()) acceptThread_.join();
    std::lock_guard<std::mutex> lk(wsMtx_);
    for (int fd : wsClients_) ::close(fd);
    wsClients_.clear();
}

// ---- Accept loop ------------------------------------------
void WebUiServer::acceptLoop() {
    while (running_) {
        fd_set fds; FD_ZERO(&fds); FD_SET(serverFd_, &fds);
        timeval tv{1, 0};
        if (select(serverFd_ + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        int cfd = accept(serverFd_, nullptr, nullptr);
        if (cfd < 0) continue;
        int flag = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        std::thread(&WebUiServer::handleClient, this, cfd).detach();
    }
}

// ---- Read full HTTP request --------------------------------
std::string WebUiServer::readRequest(int fd) {
    std::string req;
    char buf[4096];
    // Read until we see the end of headers (or 4 KB)
    while (req.size() < 4096) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        req.append(buf, n);
        if (req.find("\r\n\r\n") != std::string::npos) break;
    }
    return req;
}

// ---- WebSocket helpers ------------------------------------
std::string WebUiServer::wsAcceptKey(const std::string& key) {
    std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)magic.data(), magic.size(), hash);
    return b64Enc(hash, SHA_DIGEST_LENGTH);
}

std::vector<uint8_t> WebUiServer::wsEncodeFrame(const std::string& payload) {
    std::vector<uint8_t> f;
    f.push_back(0x81);  // FIN + text opcode
    size_t len = payload.size();
    if (len <= 125)      { f.push_back((uint8_t)len); }
    else if (len<=65535) { f.push_back(126); f.push_back(len>>8); f.push_back(len&0xFF); }
    else {
        f.push_back(127);
        for (int i = 7; i >= 0; i--) f.push_back((len>>(8*i))&0xFF);
    }
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

bool WebUiServer::wsDecodeFrame(int fd, std::string& out) {
    uint8_t hdr[2];
    if (recv(fd, hdr, 2, MSG_WAITALL) != 2) return false;
    uint8_t opcode = hdr[0] & 0x0F;
    if (opcode == 0x8) return false;  // close
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126) {
        uint8_t e[2]; if (recv(fd,e,2,MSG_WAITALL)!=2) return false;
        len = ((uint64_t)e[0]<<8)|e[1];
    } else if (len == 127) {
        uint8_t e[8]; if (recv(fd,e,8,MSG_WAITALL)!=8) return false;
        len=0; for(int i=0;i<8;i++) len=(len<<8)|e[i];
    }
    if (len > 64*1024) return false;
    uint8_t mask[4]={};
    if (masked && recv(fd,mask,4,MSG_WAITALL)!=4) return false;
    std::vector<uint8_t> data(len);
    if (recv(fd,data.data(),len,MSG_WAITALL)!=(ssize_t)len) return false;
    if (masked) for(size_t i=0;i<len;i++) data[i]^=mask[i&3];
    out.assign(data.begin(),data.end());
    return true;
}

// ---- Try to upgrade a connection to WebSocket -------------
bool WebUiServer::tryWebSocketUpgrade(int fd, const std::string& req) {
    if (req.find("Upgrade: websocket") == std::string::npos &&
        req.find("Upgrade: Websocket") == std::string::npos) return false;

    // Extract key
    auto kh = req.find("Sec-WebSocket-Key: ");
    if (kh == std::string::npos) return false;
    kh += 19;
    auto ke = req.find("\r\n", kh);
    std::string key = req.substr(kh, ke - kh);

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + wsAcceptKey(key) + "\r\n\r\n";
    send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);

    // Hand off to WebSocket loop
    wsClientLoop(fd);
    return true;
}

void WebUiServer::wsClientLoop(int fd) {
    { std::lock_guard<std::mutex> lk(wsMtx_); wsClients_.insert(fd); }
    std::cout << "[webui] browser WS connected\n";
    while (running_) {
        std::string payload;
        if (!wsDecodeFrame(fd, payload)) break;
        if (messageCb_) messageCb_(payload);
    }
    std::cout << "[webui] browser WS disconnected\n";
    ::close(fd);
    std::lock_guard<std::mutex> lk(wsMtx_);
    wsClients_.erase(fd);
}

// ---- Serve a static file ----------------------------------
void WebUiServer::serveFile(int fd, const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
        send(fd, r, strlen(r), MSG_NOSIGNAL);
        return;
    }
    std::ostringstream ss; ss << f.rdbuf();
    std::string body = ss.str();
    std::string mime = mimeType(filePath);
    std::ostringstream hdr;
    hdr << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << mime << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-cache\r\n\r\n";
    send(fd, hdr.str().c_str(), hdr.str().size(), MSG_NOSIGNAL);
    send(fd, body.data(), body.size(), MSG_NOSIGNAL);
}

void WebUiServer::serveStatus(int fd) {
    std::string body;
    { std::lock_guard<std::mutex> lk(statusMtx_); body = latestStatus_; }
    if (body.empty()) body = "{}";
    std::ostringstream hdr;
    hdr << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n\r\n";
    send(fd, hdr.str().c_str(), hdr.str().size(), MSG_NOSIGNAL);
    send(fd, body.data(), body.size(), MSG_NOSIGNAL);
}

// ---- Per-client handler -----------------------------------
void WebUiServer::handleClient(int fd) {
    std::string req = readRequest(fd);
    if (req.empty()) { ::close(fd); return; }

    // Extract path from "GET /path HTTP/1.1"
    std::string path = "/";
    auto g = req.find("GET ");
    auto h = req.find(" HTTP");
    if (g != std::string::npos && h != std::string::npos)
        path = req.substr(g + 4, h - g - 4);

    // WebSocket upgrade
    if (tryWebSocketUpgrade(fd, req)) return;  // fd handled by wsClientLoop

    // Route HTTP
    if (path == "/" || path == "/index.html") {
        serveFile(fd, webuiDir_ + "/index.html");
    } else if (path == "/api/status") {
        serveStatus(fd);
    } else if (path.substr(0, 8) == "/static/") {
        // sanitise: no ".."
        if (path.find("..") == std::string::npos)
            serveFile(fd, webuiDir_ + path.substr(7));
        else {
            const char* r="HTTP/1.1 403 Forbidden\r\nContent-Length:0\r\n\r\n";
            send(fd,r,strlen(r),MSG_NOSIGNAL);
        }
    } else {
        const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(fd, r, strlen(r), MSG_NOSIGNAL);
    }
    ::close(fd);
}

// ---- Public API -------------------------------------------
void WebUiServer::broadcast(const std::string& json) {
    auto frame = wsEncodeFrame(json);
    std::lock_guard<std::mutex> lk(wsMtx_);
    for (int fd : wsClients_)
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}

void WebUiServer::setLatestStatus(const std::string& json) {
    std::lock_guard<std::mutex> lk(statusMtx_);
    latestStatus_ = json;
}
