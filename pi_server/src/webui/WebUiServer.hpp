#pragma once
// ============================================================
//  WebUiServer — serves the static web control UI over HTTP
//  and upgrades /ws to a WebSocket relay to the rover's
//  command bus (same as WifiServer but with HTTP file serving
//  on the same port so browsers can connect without CORS).
//
//  Port 8080: GET /           → index.html
//             GET /static/*   → files from webui dir
//             GET /ws         → WebSocket upgrade (control)
//             GET /api/status → last telemetry JSON snapshot
// ============================================================
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <map>
#include <cstdint>
#include <vector>

class WebUiServer {
public:
    using MessageCb = std::function<void(const std::string& json)>;

    WebUiServer(uint16_t port, const std::string& webuiDir);
    ~WebUiServer();

    bool start();
    void stop();

    // Push a JSON status update to all connected browser WebSocket clients
    void broadcast(const std::string& json);

    // Store the latest telemetry for /api/status polling
    void setLatestStatus(const std::string& json);

    // Store the latest drive tune for new-client handshake
    void setLatestTune(const std::string& json);

    // Set login credentials (from config)
    void setCredentials(const std::string& user, const std::string& pass);

    void onMessage(MessageCb cb) { messageCb_ = std::move(cb); }

private:
    void acceptLoop();
    void handleClient(int fd);

    // HTTP helpers
    std::string readRequest(int fd);
    void        serveFile(int fd, const std::string& path);
    void        serveStatus(int fd);
    bool        tryWebSocketUpgrade(int fd, const std::string& request);

    // WebSocket (server → client, unmasked)
    static std::vector<uint8_t> wsEncodeFrame(const std::string& payload);
    // WebSocket (client → server, masked)
    static bool wsDecodeFrame(int fd, std::string& out);
    static std::string wsAcceptKey(const std::string& key);

    void wsClientLoop(int fd);

    uint16_t           port_;
    std::string        webuiDir_;
    int                serverFd_{-1};
    std::atomic<bool>  running_{false};
    std::thread        acceptThread_;

    std::mutex         wsMtx_;
    std::set<int>      wsClients_;

    std::mutex         statusMtx_;
    std::string        latestStatus_;

    std::mutex         tuneMtx_;
    std::string        latestTune_;

    MessageCb          messageCb_;

    // Authentication
    std::mutex         authMtx_;
    std::string        authUser_ = "Ogun";
    std::string        authPass_ = "Tayo1";
    int                authenticatedFd_{-1};

    static std::string jsonField(const std::string& json, const std::string& key);

    // MIME type map
    static std::string mimeType(const std::string& path);
};
