#include "TeensyBridge.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sstream>
// Simple JSON parse helpers (no external dependency)
#include <regex>

TeensyBridge::TeensyBridge(const std::string& port, uint32_t baud)
    : port_(port), baud_(baud) {}

TeensyBridge::~TeensyBridge() { close(); }

static speed_t baudRate(uint32_t baud) {
    switch(baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

bool TeensyBridge::configurePort(speed_t baud) {
    struct termios tty;
    if (tcgetattr(fd_, &tty) != 0) return false;
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;
    return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

bool TeensyBridge::open() {
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        std::cerr << "[teensy] open " << port_ << " failed: " << strerror(errno) << "\n";
        return false;
    }
    if (!configurePort(baudRate(baud_))) {
        std::cerr << "[teensy] configure failed\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    running_ = true;
    rxThread_ = std::thread(&TeensyBridge::rxThread, this);
    std::cout << "[teensy] connected on " << port_ << "\n";
    return true;
}

void TeensyBridge::close() {
    running_ = false;
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    if (rxThread_.joinable()) rxThread_.join();
}

bool TeensyBridge::writeLine(const std::string& s) {
    std::lock_guard<std::mutex> lk(writeMtx_);
    if (fd_ < 0) return false;
    std::string line = s + "\n";
    ssize_t n = ::write(fd_, line.c_str(), line.size());
    return n == (ssize_t)line.size();
}

void TeensyBridge::sendDrive(float left, float right) {
    // clamp
    auto clamp = [](float v) { return v < -1.f ? -1.f : v > 1.f ? 1.f : v; };
    left = clamp(left); right = clamp(right);
    std::ostringstream ss;
    ss << "{\"cmd\":\"drive\",\"l\":" << left << ",\"r\":" << right << "}";
    if (fd_ >= 0) writeLine(ss.str());
}

void TeensyBridge::sendStop() {
    if (fd_ >= 0) writeLine("{\"cmd\":\"stop\"}");
}

void TeensyBridge::requestSensors() {
    if (fd_ >= 0) writeLine("{\"cmd\":\"sensor_req\"}");
}

void TeensyBridge::sendRaw(const std::string& json) {
    if (fd_ >= 0) writeLine(json);
}

TeensySensors TeensyBridge::latestSensors() const {
    std::lock_guard<std::mutex> lk(sensorMtx_);
    return latest_;
}

void TeensyBridge::parseLine(const std::string& line) {
    // Minimal JSON parse using regex to avoid external deps
    auto getFloat = [&](const std::string& key) -> float {
        std::regex re("\"" + key + "\":([\\-0-9.]+)");
        std::smatch m;
        if (std::regex_search(line, m, re)) return std::stof(m[1].str());
        return 0.f;
    };
    // Only parse sensor frames
    if (line.find("\"type\":\"sensors\"") == std::string::npos) return;

    TeensySensors s;
    s.enc_l   = getFloat("enc_l");
    s.enc_r   = getFloat("enc_r");
    s.voltage   = getFloat("volt");
    s.current_l = getFloat("curr_l");
    s.current_r = getFloat("curr_r");
    s.temp      = getFloat("temp");
    {
        std::lock_guard<std::mutex> lk(sensorMtx_);
        latest_ = s;
    }
    if (sensorCb_) sensorCb_(s);
}

void TeensyBridge::rxThread() {
    std::string buf;
    char ch;
    while (running_) {
        ssize_t n = ::read(fd_, &ch, 1);
        if (n <= 0) {
            if (!running_) break;
            usleep(1000);
            continue;
        }
        if (ch == '\n') {
            if (!buf.empty()) parseLine(buf);
            buf.clear();
        } else if (ch != '\r') {
            buf += ch;
            if (buf.size() > 512) buf.clear(); // guard against garbage
        }
    }
}
