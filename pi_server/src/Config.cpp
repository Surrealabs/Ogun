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
        else if (key == "teensy_push_fw_config") cfg.teensy_push_fw_config = (val == "1" || val == "true" || val == "yes");
        else if (key == "teensy_l_rpwm_pin") cfg.teensy_l_rpwm_pin = std::stoi(val);
        else if (key == "teensy_l_lpwm_pin") cfg.teensy_l_lpwm_pin = std::stoi(val);
        else if (key == "teensy_l_en_pin") cfg.teensy_l_en_pin = std::stoi(val);
        else if (key == "teensy_r_rpwm_pin") cfg.teensy_r_rpwm_pin = std::stoi(val);
        else if (key == "teensy_r_lpwm_pin") cfg.teensy_r_lpwm_pin = std::stoi(val);
        else if (key == "teensy_r_en_pin") cfg.teensy_r_en_pin = std::stoi(val);
        else if (key == "teensy_enc_la_pin") cfg.teensy_enc_la_pin = std::stoi(val);
        else if (key == "teensy_enc_lb_pin") cfg.teensy_enc_lb_pin = std::stoi(val);
        else if (key == "teensy_enc_ra_pin") cfg.teensy_enc_ra_pin = std::stoi(val);
        else if (key == "teensy_enc_rb_pin") cfg.teensy_enc_rb_pin = std::stoi(val);
        else if (key == "teensy_vbat_adc_pin") cfg.teensy_vbat_adc_pin = std::stoi(val);
        else if (key == "teensy_curr_l_adc_pin") cfg.teensy_curr_l_adc_pin = std::stoi(val);
        else if (key == "teensy_curr_r_adc_pin") cfg.teensy_curr_r_adc_pin = std::stoi(val);
        else if (key == "teensy_temp_adc_pin") cfg.teensy_temp_adc_pin = std::stoi(val);
        else if (key == "teensy_vbat_div_ratio") cfg.teensy_vbat_div_ratio = std::stof(val);
        else if (key == "teensy_curr_zero_mv") cfg.teensy_curr_zero_mv = std::stof(val);
        else if (key == "teensy_curr_sens_mv_per_a") cfg.teensy_curr_sens_mv_per_a = std::stof(val);
        else if (key == "teensy_max_pwm") cfg.teensy_max_pwm = std::stoi(val);
        else if (key == "teensy_min_pwm") cfg.teensy_min_pwm = std::stoi(val);
        else if (key == "teensy_ramp_sec") cfg.teensy_ramp_sec = std::stof(val);
        // Legacy keys — silently accepted but ignored
        else if (key == "teensy_drive_max_fwd" || key == "teensy_drive_max_rev" ||
                 key == "teensy_turn_max" || key == "teensy_throttle_expo" ||
                 key == "teensy_turn_expo" || key == "teensy_accel_up_per_s" ||
                 key == "teensy_accel_down_per_s") {}
        else if (key == "invert_left_motor") cfg.invert_left_motor = (val == "1" || val == "true" || val == "yes");
        else if (key == "invert_right_motor") cfg.invert_right_motor = (val == "1" || val == "true" || val == "yes");
        else if (key == "teensy_watchdog_ms") cfg.teensy_watchdog_ms = std::stoi(val);
        else if (key == "teensy_telem_interval_ms") cfg.teensy_telem_interval_ms = std::stoi(val);
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
        else if (key == "webui_port") cfg.webui_port = std::stoul(val);
        else if (key == "webui_dir")  cfg.webui_dir = val;
        else if (key == "webui_user") cfg.webui_user = val;
        else if (key == "webui_pass") cfg.webui_pass = val;
        else {
            std::cerr << "[config] unknown key: " << key << "\n";
        }
    }
    return cfg;
}
