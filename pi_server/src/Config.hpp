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

    // --- Drive mixing ---
    float max_motor_speed      = 1.0f;   // cap on |l| and |r|
    float deadband             = 0.05f;  // joystick dead zone

    // --- Telemetry ---
    int   telemetry_hz         = 10;     // how often to push status to clients
};

// Load from optional config file (INI-like key=value, # comments)
RoverConfig loadConfig(const std::string& path = "/etc/rover/rover.conf");
