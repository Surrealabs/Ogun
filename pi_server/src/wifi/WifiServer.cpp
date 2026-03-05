#include "WifiServer.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// ---- Base64 encode (for WebSocket key) ---------------------
static std::string base64Encode(const unsigned char* buf, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, mem);
    BIO_write(b64, buf, len);
    BIO_flush(b64);
    BUF_MEM* ptr;
    BIO_get_mem_ptr(b64, &ptr);
    std::string result(ptr->data, ptr->length);
    BIO_free_all(b64);
    return result;
}

std::string WifiServer::wsAcceptKey(const std::string& clientKey) {
    std::string magic = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(magic.data()), magic.size(), hash);
    return base64Encode(hash, SHA_DIGEST_LENGTH);
}

// ---- Constructor / Destructor ------------------------------

WifiServer::WifiServer(uint16_t port) : port_(port) {}
WifiServer::~WifiServer() { stop(); }

// ---- start / stop ------------------------------------------

bool WifiServer::start() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) return false;
    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);
    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[ws] bind port " << port_ << " failed: " << strerror(errno) << "\n";
        return false;
    }
    listen(serverFd_, 16);
    running_ = true;
    acceptThread_ = std::thread(&WifiServer::acceptLoop, this);
    std::cout << "[ws] WebSocket server on port " << port_ << "\n";
    return true;
}

void WifiServer::stop() {
    running_ = false;
    if (serverFd_ >= 0) { ::close(serverFd_); serverFd_ = -1; }
    if (acceptThread_.joinable()) acceptThread_.join();
    std::lock_guard<std::mutex> lk(clientsMtx_);
    for (int fd : clients_) ::close(fd);
    clients_.clear();
}

// ---- Accept loop -------------------------------------------

void WifiServer::acceptLoop() {
    while (running_) {
        fd_set fds; FD_ZERO(&fds); FD_SET(serverFd_, &fds);
        timeval tv{1, 0};
        if (select(serverFd_ + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        int clientFd = accept(serverFd_, nullptr, nullptr);
        if (clientFd < 0) continue;
        int flag = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        std::thread(&WifiServer::handleClient, this, clientFd).detach();
    }
}

// ---- HTTP Upgrade handshake --------------------------------

bool WifiServer::doHandshake(int fd, std::string& outPath) {
    char buf[2048] = {};
    int len = recv(fd, buf, sizeof(buf)-1, 0);
    if (len <= 0) return false;
    std::string req(buf, len);

    // Extract Sec-WebSocket-Key
    std::string keyHdr = "Sec-WebSocket-Key: ";
    auto pos = req.find(keyHdr);
    if (pos == std::string::npos) return false;
    pos += keyHdr.size();
    auto end = req.find("\r\n", pos);
    std::string key = req.substr(pos, end - pos);

    // Extract path
    auto p1 = req.find("GET ");
    auto p2 = req.find(" HTTP");
    outPath = (p1 != std::string::npos && p2 != std::string::npos)
              ? req.substr(p1 + 4, p2 - p1 - 4) : "/";

    std::string accept = wsAcceptKey(key);
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    return send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL) > 0;
}

// ---- WebSocket frame encode --------------------------------

std::vector<uint8_t> WifiServer::encodeFrame(const std::string& payload, bool binary) {
    std::vector<uint8_t> frame;
    frame.push_back(binary ? 0x82 : 0x81);  // FIN + opcode (2=binary, 1=text)
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back((uint8_t)len);
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((len >> (8*i)) & 0xFF);
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

// ---- WebSocket frame decode (client → server, masked) ------

bool WifiServer::decodeFrame(int fd, std::string& outPayload) {
    uint8_t hdr[2];
    if (recv(fd, hdr, 2, MSG_WAITALL) != 2) return false;
    // bool fin    = (hdr[0] & 0x80) != 0;
    uint8_t opcode = hdr[0] & 0x0F;
    if (opcode == 0x8) return false;  // close frame
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126) {
        uint8_t ext[2];
        if (recv(fd, ext, 2, MSG_WAITALL) != 2) return false;
        len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (recv(fd, ext, 8, MSG_WAITALL) != 8) return false;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
    }
    if (len > 64 * 1024) return false;  // cap at 64 KB

    uint8_t mask[4] = {};
    if (masked && recv(fd, mask, 4, MSG_WAITALL) != 4) return false;

    std::vector<uint8_t> data(len);
    if (recv(fd, data.data(), len, MSG_WAITALL) != (ssize_t)len) return false;

    if (masked)
        for (size_t i = 0; i < len; i++) data[i] ^= mask[i & 3];

    outPayload.assign(data.begin(), data.end());
    return true;
}

// ---- Per-client handler ------------------------------------

void WifiServer::handleClient(int fd) {
    std::string path;
    if (!doHandshake(fd, path)) { ::close(fd); return; }
    {
        std::lock_guard<std::mutex> lk(clientsMtx_);
        clients_.insert(fd);
    }
    std::cout << "[ws] client connected\n";

    while (running_) {
        std::string payload;
        if (!decodeFrame(fd, payload)) break;
        if (messageCb_) messageCb_(payload);
    }
    std::cout << "[ws] client disconnected\n";
    ::close(fd);
    std::lock_guard<std::mutex> lk(clientsMtx_);
    clients_.erase(fd);
}

// ---- Broadcast ---------------------------------------------

void WifiServer::broadcast(const std::string& json) {
    auto frame = encodeFrame(json, false);
    std::lock_guard<std::mutex> lk(clientsMtx_);
    for (int fd : clients_)
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}
