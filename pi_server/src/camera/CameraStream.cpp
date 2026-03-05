#include "CameraStream.hpp"
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <jpeglib.h>   // libjpeg

CameraStream::CameraStream(const std::string& device,
                           uint16_t httpPort,
                           int width, int height,
                           int fps, int jpegQuality)
    : device_(device), httpPort_(httpPort),
      width_(width), height_(height), fps_(fps), jpegQuality_(jpegQuality) {}

CameraStream::~CameraStream() { stop(); }

// ---------- V4L2 helpers ------------------------------------

bool CameraStream::openV4L2() {
    v4l2Fd_ = ::open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (v4l2Fd_ < 0) {
        std::cerr << "[cam:" << device_ << "] open failed: " << strerror(errno) << "\n";
        return false;
    }
    // Set format — try YUYV first (universal), fall back to MJPEG from device
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width_;
    fmt.fmt.pix.height      = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    if (ioctl(v4l2Fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[cam:" << device_ << "] S_FMT failed\n";
        return false;
    }
    // Frame rate
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = fps_;
    ioctl(v4l2Fd_, VIDIOC_S_PARM, &parm);

    // Request mmap buffers
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2Fd_, VIDIOC_REQBUFS, &req) < 0) return false;
    bufCount_ = req.count;

    for (int i = 0; i < bufCount_; i++) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(v4l2Fd_, VIDIOC_QUERYBUF, &buf) < 0) return false;
        mapLen_[i] = buf.length;
        mapBuf_[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                          MAP_SHARED, v4l2Fd_, buf.m.offset);
        if (ioctl(v4l2Fd_, VIDIOC_QBUF, &buf) < 0) return false;
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(v4l2Fd_, VIDIOC_STREAMON, &type) >= 0;
}

void CameraStream::closeV4L2() {
    if (v4l2Fd_ < 0) return;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2Fd_, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < bufCount_; i++)
        if (mapBuf_[i]) munmap(mapBuf_[i], mapLen_[i]);
    ::close(v4l2Fd_);
    v4l2Fd_ = -1;
}

// Convert YUYV to RGB then compress to JPEG via libjpeg
static std::vector<uint8_t> yuyv2jpeg(const uint8_t* src, int w, int h, int quality) {
    std::vector<uint8_t> rgb(w * h * 3);
    for (int i = 0; i < w * h / 2; i++) {
        int y0 = src[4*i+0], u = src[4*i+1], y1 = src[4*i+2], v = src[4*i+3];
        auto clamp = [](int x) { return x < 0 ? 0 : x > 255 ? 255 : x; };
        int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
        rgb[6*i+0] = clamp((298*c0 + 409*e + 128) >> 8);
        rgb[6*i+1] = clamp((298*c0 - 100*d - 208*e + 128) >> 8);
        rgb[6*i+2] = clamp((298*c0 + 516*d + 128) >> 8);
        rgb[6*i+3] = clamp((298*c1 + 409*e + 128) >> 8);
        rgb[6*i+4] = clamp((298*c1 - 100*d - 208*e + 128) >> 8);
        rgb[6*i+5] = clamp((298*c1 + 516*d + 128) >> 8);
    }
    // Compress to JPEG
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    unsigned char* outBuf = nullptr;
    unsigned long  outLen = 0;
    jpeg_mem_dest(&cinfo, &outBuf, &outLen);
    cinfo.image_width  = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = &rgb[cinfo.next_scanline * w * 3];
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::vector<uint8_t> result(outBuf, outBuf + outLen);
    free(outBuf);
    return result;
}

std::vector<uint8_t> CameraStream::captureJpeg() {
    // Wait for a frame
    fd_set fds;
    FD_ZERO(&fds); FD_SET(v4l2Fd_, &fds);
    timeval tv{2, 0};
    if (select(v4l2Fd_ + 1, &fds, nullptr, nullptr, &tv) <= 0) return {};

    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2Fd_, VIDIOC_DQBUF, &buf) < 0) return {};

    auto jpeg = yuyv2jpeg(static_cast<uint8_t*>(mapBuf_[buf.index]),
                          width_, height_, jpegQuality_);

    ioctl(v4l2Fd_, VIDIOC_QBUF, &buf);
    return jpeg;
}

// ---------- Capture thread ----------------------------------

void CameraStream::captureLoop() {
    if (!openV4L2()) {
        std::cerr << "[cam:" << device_ << "] V4L2 open failed, capture disabled\n";
        return;
    }
    while (running_) {
        auto frame = captureJpeg();
        if (!frame.empty()) {
            std::lock_guard<std::mutex> lk(frameMtx_);
            latestJpeg_ = std::move(frame);
        }
    }
    closeV4L2();
}

std::vector<uint8_t> CameraStream::latestFrame() const {
    std::lock_guard<std::mutex> lk(frameMtx_);
    return latestJpeg_;
}

// ---------- HTTP MJPEG server --------------------------------

static const std::string BOUNDARY = "roverframe";

void CameraStream::handleClient(int clientFd) {
    // TCP_NODELAY for lower latency
    int flag = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Read HTTP request (we don't care about path)
    char req[1024];
    recv(clientFd, req, sizeof(req)-1, 0);

    // Send multipart header
    std::string hdr = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: multipart/x-mixed-replace;boundary=" + BOUNDARY + "\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Connection: close\r\n\r\n";
    send(clientFd, hdr.c_str(), hdr.size(), MSG_NOSIGNAL);

    while (running_) {
        std::vector<uint8_t> frame = latestFrame();
        if (frame.empty()) { usleep(30000); continue; }

        std::ostringstream part;
        part << "--" << BOUNDARY << "\r\n"
             << "Content-Type: image/jpeg\r\n"
             << "Content-Length: " << frame.size() << "\r\n\r\n";
        std::string partHdr = part.str();

        ssize_t s1 = send(clientFd, partHdr.c_str(), partHdr.size(), MSG_NOSIGNAL);
        ssize_t s2 = send(clientFd, frame.data(), frame.size(), MSG_NOSIGNAL);
        send(clientFd, "\r\n", 2, MSG_NOSIGNAL);
        if (s1 < 0 || s2 < 0) break;  // client disconnected

        usleep(1000000 / fps_);
    }
    ::close(clientFd);
    std::lock_guard<std::mutex> lk(clientsMtx_);
    clients_.erase(clientFd);
}

void CameraStream::httpLoop() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(httpPort_);
    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[cam] bind port " << httpPort_ << " failed\n";
        return;
    }
    listen(serverFd_, 8);
    std::cout << "[cam:" << device_ << "] MJPEG on http://0.0.0.0:" << httpPort_ << "\n";

    while (running_) {
        fd_set fds; FD_ZERO(&fds); FD_SET(serverFd_, &fds);
        timeval tv{1, 0};
        if (select(serverFd_ + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;

        int clientFd = accept(serverFd_, nullptr, nullptr);
        if (clientFd < 0) continue;
        {
            std::lock_guard<std::mutex> lk(clientsMtx_);
            clients_.insert(clientFd);
        }
        std::thread(&CameraStream::handleClient, this, clientFd).detach();
    }
    // Close remaining clients
    std::lock_guard<std::mutex> lk(clientsMtx_);
    for (int fd : clients_) ::close(fd);
    ::close(serverFd_);
}

// ---------- start / stop ------------------------------------

bool CameraStream::start() {
    running_ = true;
    captureThread_ = std::thread(&CameraStream::captureLoop, this);
    httpThread_    = std::thread(&CameraStream::httpLoop, this);
    return true;
}

void CameraStream::stop() {
    running_ = false;
    if (captureThread_.joinable()) captureThread_.join();
    if (httpThread_.joinable())    httpThread_.join();
}
