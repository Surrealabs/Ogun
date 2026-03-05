#pragma once
// ============================================================
//  Rover shared protocol definitions
//  Used by both the Pi server and referenced by Teensy firmware
// ============================================================
#include <cstdint>
#include <string>

// ---- Inbound command types (Phone → Pi) --------------------
namespace RoverCmd {
    constexpr const char* DRIVE   = "drive";   // x, y, rot  (-1..1)
    constexpr const char* GPIO    = "gpio";    // pin, state (bool)
    constexpr const char* AUDIO   = "audio";   // file, volume
    constexpr const char* OTA     = "ota";     // chunk, total, data (base64)
    constexpr const char* STATUS  = "status";  // request a status snapshot
    constexpr const char* ESTOP   = "estop";   // emergency stop
}

// ---- Outbound status types (Pi → Phone) --------------------
namespace RoverStatus {
    constexpr const char* TELEMETRY = "telemetry"; // sensors from Teensy
    constexpr const char* GPIO_ACK  = "gpio_ack";
    constexpr const char* OTA_PROG  = "ota_prog";  // progress 0-100
    constexpr const char* ERROR     = "error";
}

// ---- Pi ↔ Teensy Serial protocol ---------------------------
// Simple newline-terminated JSON over 115200 baud USB Serial
// Pi → Teensy:
//   {"cmd":"drive","l":0.5,"r":-0.3}\n
//   {"cmd":"stop"}\n
//   {"cmd":"sensor_req"}\n
// Teensy → Pi:
//   {"type":"sensors","enc_l":123,"enc_r":456,
//    "volt":12.4,"curr":2.1,"temp":38.5}\n

// ---- BLE GATT UUIDs ----------------------------------------
namespace BleUUID {
    // 128-bit UUIDs
    constexpr const char* SERVICE   = "0000ABCD-0000-1000-8000-00805F9B34FB";
    constexpr const char* CONTROL   = "0000ABD0-0000-1000-8000-00805F9B34FB"; // Write
    constexpr const char* STATUS_CH = "0000ABD1-0000-1000-8000-00805F9B34FB"; // Notify
    constexpr const char* OTA_CH    = "0000ABD2-0000-1000-8000-00805F9B34FB"; // Write
}

// ---- GPIO pin names ----------------------------------------
namespace RoverGpio {
    constexpr const char* HORN    = "horn";
    constexpr const char* LED_FWD = "led_fwd";
    constexpr const char* LED_REV = "led_rev";
    constexpr const char* AUX1    = "aux1";
    constexpr const char* AUX2    = "aux2";
}

// ---- WiFi WebSocket / HTTP ports ---------------------------
namespace RoverPorts {
    constexpr uint16_t WEBSOCKET = 9000;
    constexpr uint16_t CAM0_HTTP = 8081;  // MJPEG camera 0
    constexpr uint16_t CAM1_HTTP = 8082;  // MJPEG camera 1
}
