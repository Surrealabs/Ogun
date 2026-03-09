#include "GpioController.hpp"
#include <gpiod.h>
#include <iostream>
#include <thread>
#include <chrono>

GpioController::GpioController(const std::map<std::string, int>& pinMap,
                               const std::string& chipName)
    : chipName_(chipName), pinMap_(pinMap) {}

GpioController::~GpioController() { shutdown(); }

bool GpioController::init() {
    if (pinMap_.empty()) {
        std::cout << "[gpio] no pins configured\n";
        return true;
    }
    chip_ = gpiod_chip_open_by_name(chipName_.c_str());
    if (!chip_) {
        std::cerr << "[gpio] cannot open chip " << chipName_ << "\n";
        return false;
    }
    for (auto& [name, bcm] : pinMap_) {
        gpiod_line* line = gpiod_chip_get_line(chip_, bcm);
        if (!line) {
            std::cerr << "[gpio] cannot get line " << bcm << " for " << name << "\n";
            continue;
        }
        if (gpiod_line_request_output(line, "rover", 0) < 0) {
            std::cerr << "[gpio] cannot request output for " << name << "\n";
            continue;
        }
        lines_[name] = {line, false};
    }
    std::cout << "[gpio] initialized " << lines_.size() << " pins\n";
    return !lines_.empty();
}

void GpioController::shutdown() {
    for (auto& [name, ps] : lines_) {
        if (ps.line) {
            gpiod_line_set_value(ps.line, 0);
            gpiod_line_release(ps.line);
        }
    }
    lines_.clear();
    if (chip_) { gpiod_chip_close(chip_); chip_ = nullptr; }
}

bool GpioController::set(const std::string& name, bool high) {
    auto it = lines_.find(name);
    if (it == lines_.end()) {
        std::cerr << "[gpio] unknown pin: " << name << "\n";
        return false;
    }
    int rc = gpiod_line_set_value(it->second.line, high ? 1 : 0);
    if (rc == 0) it->second.state = high;
    return rc == 0;
}

bool GpioController::toggle(const std::string& name) {
    auto it = lines_.find(name);
    if (it == lines_.end()) return false;
    return set(name, !it->second.state);
}

bool GpioController::getState(const std::string& name) const {
    auto it = lines_.find(name);
    return it != lines_.end() && it->second.state;
}

void GpioController::pulse(const std::string& name, int durationMs) {
    // Fire-and-forget thread for non-blocking pulse
    std::thread([this, name, durationMs]() {
        set(name, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
        set(name, false);
    }).detach();
}
