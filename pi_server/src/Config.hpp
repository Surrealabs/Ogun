#pragma once
// ============================================================
//  Rover runtime configuration
//  Edit this file or override via /etc/rover/rover.conf
// ============================================================
#include <cstdint>
#include <string>
#include <map>

struct RoverConfig {
    // --- Teensy serial bridge ---
    std::string teensy_port    = "/dev/ttyACM0";  // USB serial to Teensy
    uint32_t    serial_baud    = 115200;
    bool        teensy_push_fw_config = true;

    // --- Teensy firmware runtime config (sent over serial on boot) ---
    int teensy_l_rpwm_pin = 21;
    int teensy_l_lpwm_pin = 19;
    int teensy_l_en_pin   = 18;
    int teensy_r_rpwm_pin = 22;
    int teensy_r_lpwm_pin = 23;
    int teensy_r_en_pin   = 16;

    int teensy_enc_la_pin = 8;
    int teensy_enc_lb_pin = 9;
    int teensy_enc_ra_pin = 10;
    int teensy_enc_rb_pin = 11;

    int teensy_vbat_adc_pin = 14; // A0 on Teensy
    int teensy_curr_adc_pin = 17; // primary motor current sense
    int teensy_temp_adc_pin = 20; // optional secondary analog sense

    float teensy_vbat_div_ratio = 4.03f;
    float teensy_curr_zero_mv = 1650.0f;
    float teensy_curr_sens_mv_per_a = 66.0f;

    // Teensy drive behavior tuning (applied in firmware)
    float teensy_drive_max_fwd = 1.0f;
    float teensy_drive_max_rev = 1.0f;
    float teensy_turn_max = 1.0f;
    float teensy_throttle_expo = 1.0f;
    float teensy_turn_expo = 1.0f;
    float teensy_accel_up_per_s = 3.0f;
    float teensy_accel_down_per_s = 5.0f;

    int teensy_watchdog_ms = 500;
    int teensy_telem_interval_ms = 100;

    // --- BLE ---
    std::string ble_device     = "hci0";
    std::string ble_name       = "Rover";

    // --- WiFi WebSocket ---
    std::string ws_bind_addr   = "0.0.0.0";
    uint16_t    ws_port        = 9000;

    // --- Camera streams ---
    std::string cam0_device    = "/dev/video0";
    std::string cam1_device    = "/dev/video2";
    uint16_t    cam0_port      = 8081;
    uint16_t    cam1_port      = 8082;
    int         cam_width      = 640;
    int         cam_height     = 480;
    int         cam_fps        = 30;
    int         cam_jpeg_quality = 80;  // 0-100

    // --- GPIO (BCM numbering) ---
    std::map<std::string, int> gpio_pins = {
        {"horn",    17},
        {"led_fwd", 27},
        {"led_rev", 22},
        {"aux1",    23},
        {"aux2",    24},
    };

    // --- Audio ---
    std::string audio_device   = "default";    // ALSA device
    std::string audio_dir      = "/opt/rover/sounds";

    // --- OTA ---
    std::string ota_work_dir   = "/tmp/rover_ota";
    // teensy_loader_cli or avrdude path:
    std::string ota_flash_cmd  = "teensy_loader_cli";
    std::string teensy_mmcu    = "TEENSY41";   // or TEENSY40, TEENSY36 ...

    // --- Web UI ---
    uint16_t    webui_port     = 8080;
    std::string webui_dir      = "/opt/rover/webui";

    // --- Drive mixing ---
    float max_motor_speed      = 1.0f;   // cap on |l| and |r|
    float deadband             = 0.05f;  // joystick dead zone

    // --- Telemetry ---
    int   telemetry_hz         = 10;     // how often to push status to clients
};

// Load from optional config file (INI-like key=value, # comments)
RoverConfig loadConfig(const std::string& path = "/etc/rover/rover.conf");
