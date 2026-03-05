#pragma once
// ============================================================
//  WifiServer — WebSocket server for rover control over WiFi
//  Pure POSIX implementation (no external WebSocket library)
//  Handles RFC 6455 framing with text/binary frames.
// ============================================================
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <cstdint>

class WifiServer {
public:
    using MessageCb = std::function<void(const std::string& json)>;

    explicit WifiServer(uint16_t port);
    ~WifiServer();

    bool start();
    void stop();

    // Broadcast a JSON string to all connected WebSocket clients
    void broadcast(const std::string& json);

    // Register handler for incoming messages from any client
    void onMessage(MessageCb cb) { messageCb_ = std::move(cb); }

private:
    void acceptLoop();
    void handleClient(int fd);

    // WebSocket handshake helpers
    static std::string wsAcceptKey(const std::string& clientKey);
    bool        doHandshake(int fd, std::string& outPath);

    // WebSocket frame encode/decode
    static std::vector<uint8_t> encodeFrame(const std::string& payload, bool binary = false);
    static bool decodeFrame(int fd, std::string& outPayload);

    uint16_t           port_;
    int                serverFd_{-1};
    std::atomic<bool>  running_{false};
    std::thread        acceptThread_;

    std::mutex         clientsMtx_;
    std::set<int>      clients_;

    MessageCb          messageCb_;
};
