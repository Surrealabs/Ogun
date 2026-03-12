#pragma once
// ============================================================
//  mod_lights — example module: GPIO-driven lights
//
//  Config keys (in /etc/rover/modules/lights.conf):
//    headlight_pin = <BCM>     # GPIO pin for headlights
//    taillight_pin = <BCM>     # GPIO pin for taillights
//
//  WS commands:
//    {"type":"lights","headlights":true}
//    {"type":"lights","taillights":false}
//    {"type":"lights_status"}          → broadcasts current state
// ============================================================
#include "module/RoverModule.hpp"
#include <atomic>

class ModLights : public RoverModule {
public:
    const char* name() const override { return "lights"; }

    bool onLoad(const std::map<std::string, std::string>& conf,
                const ModuleContext& ctx) override;
    void onShutdown() override;
    bool onCommand(const std::string& type,
                   const std::string& json) override;

private:
    ModuleContext ctx_;
    std::string headlightName_{"headlight"};
    std::string taillightName_{"taillight"};
    int headlightPin_ = -1;
    int taillightPin_ = -1;
    std::atomic<bool> headlightsOn_{false};
    std::atomic<bool> taillightsOn_{false};

    void applyGpio();
    void broadcastState();
};
