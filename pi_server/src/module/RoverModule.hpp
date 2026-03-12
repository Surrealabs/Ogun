#pragma once
// ============================================================
//  RoverModule — base class for all rover plug-in modules
//
//  Each module lives in src/modules/mod_<name>/ and registers
//  itself via a static initializer in mod_<name>_loader.cpp.
//  The core loads every registered module at startup, reads
//  its config from /etc/rover/modules/<name>.conf, and calls
//  the lifecycle hooks below.
// ============================================================
#include <string>
#include <map>
#include <functional>
#include <regex>

class TeensyBridge;
class GpioController;

// Context passed to every module — gives access to core services
struct ModuleContext {
    std::function<void(const std::string&)> broadcast;   // send to all WS clients
    TeensyBridge*    teensy = nullptr;
    GpioController*  gpio   = nullptr;
};

class RoverModule {
public:
    virtual ~RoverModule() = default;

    // Unique short name, e.g. "lights".  Also used as conf filename.
    virtual const char* name() const = 0;

    // Called once at startup with the module's conf and system context.
    // Return false to abort loading this module.
    virtual bool onLoad(const std::map<std::string, std::string>& conf,
                        const ModuleContext& ctx) {
        (void)conf; (void)ctx; return true;
    }

    // Called on clean shutdown.
    virtual void onShutdown() {}

    // Called every telemetry tick (~100 ms when rover is awake).
    virtual void onTick() {}

    // Try to handle an inbound WS/WiFi command.
    // Return true if this module consumed the command.
    virtual bool onCommand(const std::string& type,
                           const std::string& json) {
        (void)type; (void)json; return false;
    }

    // ---- Tiny JSON helpers (so modules don't need a dep) ----
    static std::string jStr(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch m;
        return std::regex_search(json, m, re) ? m[1].str() : "";
    }
    static float jFloat(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*([\\-0-9.]+)");
        std::smatch m;
        return std::regex_search(json, m, re) ? std::stof(m[1].str()) : 0.f;
    }
    static int jInt(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*([\\-0-9]+)");
        std::smatch m;
        return std::regex_search(json, m, re) ? std::stoi(m[1].str()) : 0;
    }
    static bool jBool(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
        std::smatch m;
        if (!std::regex_search(json, m, re)) return false;
        return m[1].str() == "true";
    }
};
