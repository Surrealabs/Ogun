#pragma once
// ============================================================
//  BleServer — BlueZ D-Bus GATT application (peripheral role)
//  Implements the rover control GATT profile via sdbus-c++
//
//  Requires: BlueZ >= 5.50, sdbus-c++ >= 2.0, libdbus-1
//
//  GATT Profile:
//    Service  0000ABCD-...  "Rover"
//      CONTROL  0000ABD0-...  Write / WriteWithoutResponse
//      STATUS   0000ABD1-...  Read / Notify
//      OTA      0000ABD2-...  Write / WriteWithoutResponse
// ============================================================
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>

// Forward declarations (sdbus types)
namespace sdbus { class IConnection; class IObject; }

class BleServer {
public:
    using CommandCb = std::function<void(const std::string& json)>;
    using OtaChunkCb = std::function<void(const std::vector<uint8_t>& data)>;

    BleServer(const std::string& bleName,
              const std::string& hciDevice = "hci0");
    ~BleServer();

    bool start();
    void stop();

    // Send a JSON status notification to subscribed clients
    void notifyStatus(const std::string& json);

    void onCommand(CommandCb cb)  { commandCb_  = std::move(cb); }
    void onOtaChunk(OtaChunkCb cb){ otaChunkCb_ = std::move(cb); }

private:
    void runLoop();
    bool setupBlueZ();
    void registerApp();
    void startAdvertising();

    std::string bleName_;
    std::string hciDevice_;

    std::unique_ptr<sdbus::IConnection> conn_;
    std::unique_ptr<sdbus::IObject>     appObj_;
    std::unique_ptr<sdbus::IObject>     serviceObj_;
    std::unique_ptr<sdbus::IObject>     controlCharObj_;
    std::unique_ptr<sdbus::IObject>     statusCharObj_;
    std::unique_ptr<sdbus::IObject>     otaCharObj_;

    std::thread       loopThread_;
    std::atomic<bool> running_{false};
    bool              notifyEnabled_{false};
    int               statusNotifyFd_{-1};  // epoll fd for notify

    CommandCb   commandCb_;
    OtaChunkCb  otaChunkCb_;

    // Latest status bytes (UTF-8 JSON)
    std::vector<uint8_t> latestStatus_;
};
