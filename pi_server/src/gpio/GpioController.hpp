#pragma once
// ============================================================
//  GpioController — manages named GPIO outputs via libgpiod
// ============================================================
#include <string>
#include <map>
#include <memory>

// Forward-declare libgpiod types to avoid header pollution
struct gpiod_chip;
struct gpiod_line;

class GpioController {
public:
    // pinMap: name → BCM pin number
    explicit GpioController(const std::map<std::string, int>& pinMap,
                            const std::string& chipName = "gpiochip0");
    ~GpioController();

    bool init();
    void shutdown();

    // Set a named output high/low
    bool set(const std::string& name, bool high);

    // Pulse a named output for durationMs milliseconds (non-blocking thread)
    void pulse(const std::string& name, int durationMs);

    // Toggle
    bool toggle(const std::string& name);

    bool getState(const std::string& name) const;

private:
    std::string chipName_;
    std::map<std::string, int> pinMap_;

    struct PinState {
        gpiod_line* line{nullptr};
        bool        state{false};
    };
    std::map<std::string, PinState> lines_;
    gpiod_chip*                     chip_{nullptr};
};
