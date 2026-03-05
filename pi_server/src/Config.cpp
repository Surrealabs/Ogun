#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

RoverConfig loadConfig(const std::string& path) {
    RoverConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;  // use defaults

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // trim whitespace
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key); trim(val);

        if (key == "teensy_port")      cfg.teensy_port = val;
        else if (key == "serial_baud") cfg.serial_baud = std::stoul(val);
        else if (key == "ble_name")    cfg.ble_name = val;
        else if (key == "ws_port")     cfg.ws_port = std::stoul(val);
        else if (key == "cam0_device") cfg.cam0_device = val;
        else if (key == "cam1_device") cfg.cam1_device = val;
        else if (key == "cam_width")   cfg.cam_width = std::stoi(val);
        else if (key == "cam_height")  cfg.cam_height = std::stoi(val);
        else if (key == "cam_fps")     cfg.cam_fps = std::stoi(val);
        else if (key == "cam_jpeg_quality") cfg.cam_jpeg_quality = std::stoi(val);
        else if (key == "telemetry_hz") cfg.telemetry_hz = std::stoi(val);
        else if (key == "max_motor_speed") cfg.max_motor_speed = std::stof(val);
        else if (key == "ota_flash_cmd") cfg.ota_flash_cmd = val;
        else if (key == "teensy_mmcu") cfg.teensy_mmcu = val;
        else {
            std::cerr << "[config] unknown key: " << key << "\n";
        }
    }
    return cfg;
}
