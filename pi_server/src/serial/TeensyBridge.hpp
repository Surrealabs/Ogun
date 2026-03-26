#pragma once
// ============================================================
//  TeensyBridge — USB serial link to Teensy sensor/motor hub
// ============================================================
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <termios.h>

struct TeensySensors {
    float enc_l     = 0.f;   // left encoder ticks
    float enc_r     = 0.f;   // right encoder ticks
    float voltage   = 0.f;   // battery voltage (V) — not wired
    float current_l = 0.f;   // left motor current (A)
    float current_r = 0.f;   // right motor current (A)
    float current_t = 0.f;   // turning motor current (A)
    float temp      = 0.f;   // board temperature — not wired
};

class TeensyBridge {
public:
    using SensorCallback    = std::function<void(const TeensySensors&)>;
    using ReconnectCallback = std::function<void()>;

    explicit TeensyBridge(const std::string& port, uint32_t baud = 115200);
    ~TeensyBridge();

    bool open();
    void close();
    bool isOpen() const { return fd_ >= 0; }

    // Drive command: l/r in range -1.0 .. 1.0, turn in range -1.0 .. 1.0
    void sendDrive(float left, float right, float turn = 0.f);
    void sendStop();
    void requestSensors();

    // Raw JSON send (for future commands)
    void sendRaw(const std::string& json);

    // Register a callback for incoming sensor frames
    void onSensors(SensorCallback cb) { sensorCb_ = std::move(cb); }

    // Called after a successful reconnect (use to re-push fw config)
    void onReconnect(ReconnectCallback cb) { reconnectCb_ = std::move(cb); }

    // Latest sensor snapshot (thread-safe)
    TeensySensors latestSensors() const;

private:
    void rxThread();
    void parseLine(const std::string& line);
    bool writeLine(const std::string& s);
    bool configurePort(speed_t baud);
    bool tryReopen();   // attempt to reopen the serial port

    std::string       port_;
    uint32_t          baud_;
    int               fd_{-1};
    std::thread       rxThread_;
    std::atomic<bool> running_{false};

    std::mutex           writeMtx_;     // serialize all serial writes
    mutable std::mutex   sensorMtx_;
    TeensySensors        latest_;
    SensorCallback       sensorCb_;
    ReconnectCallback    reconnectCb_;
};
