#include "ModLights.hpp"
#include "gpio/GpioController.hpp"
#include <sstream>
#include <iostream>

bool ModLights::onLoad(const std::map<std::string, std::string>& conf,
                       const ModuleContext& ctx) {
    ctx_ = ctx;

    auto get = [&](const std::string& key) -> std::string {
        auto it = conf.find(key);
        return (it != conf.end()) ? it->second : "";
    };

    std::string hp = get("headlight_pin");
    std::string tp = get("taillight_pin");

    if (hp.empty() && tp.empty()) {
        std::cout << "[mod_lights] no pins configured — module inactive\n";
        return true;   // load OK, just no-op
    }

    if (!hp.empty()) headlightPin_ = std::stoi(hp);
    if (!tp.empty()) taillightPin_ = std::stoi(tp);

    // Register pins with the GPIO controller so libgpiod claims them
    if (ctx_.gpio) {
        if (headlightPin_ >= 0) ctx_.gpio->set(headlightName_, false);
        if (taillightPin_ >= 0) ctx_.gpio->set(taillightName_, false);
    }

    std::cout << "[mod_lights] loaded  headlight=" << headlightPin_
              << "  taillight=" << taillightPin_ << "\n";
    return true;
}

void ModLights::onShutdown() {
    headlightsOn_ = false;
    taillightsOn_ = false;
    applyGpio();
}

bool ModLights::onCommand(const std::string& type, const std::string& json) {
    if (type == "lights") {
        if (json.find("\"headlights\"") != std::string::npos)
            headlightsOn_ = jBool(json, "headlights");
        if (json.find("\"taillights\"") != std::string::npos)
            taillightsOn_ = jBool(json, "taillights");
        applyGpio();
        broadcastState();
        return true;
    }
    if (type == "lights_status") {
        broadcastState();
        return true;
    }
    return false;
}

void ModLights::applyGpio() {
    if (!ctx_.gpio) return;
    if (headlightPin_ >= 0) ctx_.gpio->set(headlightName_, headlightsOn_);
    if (taillightPin_ >= 0) ctx_.gpio->set(taillightName_, taillightsOn_);
}

void ModLights::broadcastState() {
    if (!ctx_.broadcast) return;
    std::ostringstream ss;
    ss << "{\"type\":\"lights\","
       << "\"headlights\":" << (headlightsOn_ ? "true" : "false") << ","
       << "\"taillights\":" << (taillightsOn_ ? "true" : "false")
       << "}";
    ctx_.broadcast(ss.str());
}
