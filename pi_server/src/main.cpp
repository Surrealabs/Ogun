#include "Config.hpp"
#include "Protocol.hpp"
#include "serial/TeensyBridge.hpp"
#include "wifi/WifiServer.hpp"
#include "webui/WebUiServer.hpp"
#include "camera/CameraStream.hpp"
#include "gpio/GpioController.hpp"
#include "ota/TeensyOta.hpp"

#include <iostream>
#include <sstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <regex>
#include <mutex>
// Simple JSON helpers (no dependency)
#include <map>

static std::atomic<bool> gRunning{true};

void sigHandler(int) { gRunning = false; }

// ---- Minimal JSON value extractor (single-key) -------------
static std::string jsonStr(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch m;
    return std::regex_search(json, m, re) ? m[1].str() : "";
}
static float jsonFloat(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*([\\-0-9.]+)");
    std::smatch m;
    return std::regex_search(json, m, re) ? std::stof(m[1].str()) : 0.f;
}
static bool jsonBool(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(json, m, re)) return false;
    return m[1].str() == "true";
}
static int jsonInt(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*([\\-0-9]+)");
    std::smatch m;
    return std::regex_search(json, m, re) ? std::stoi(m[1].str()) : 0;
}

// ---- Build a telemetry JSON from sensors + gpio state ------
static std::string buildTelemetry(const TeensySensors& s,
                                  GpioController& gpio) {
    std::ostringstream ss;
    ss << "{\"type\":\"telemetry\","
       << "\"enc_l\":"   << s.enc_l   << ","
       << "\"enc_r\":"   << s.enc_r   << ","
       << "\"volt\":"    << s.voltage << ","
       << "\"curr\":"    << s.current << ","
       << "\"temp\":"    << s.temp    << ","
       << "\"horn\":"    << (gpio.getState(RoverGpio::HORN) ? "true" : "false") << ","
       << "\"led_fwd\":" << (gpio.getState(RoverGpio::LED_FWD) ? "true" : "false")
       << "}";
    return ss.str();
}

// ---- OTA progress JSON -------------------------------------
static std::string otaProgress(int pct, const std::string& msg) {
    std::ostringstream ss;
    ss << "{\"type\":\"ota_prog\",\"pct\":" << pct
       << ",\"msg\":\"" << msg << "\"}";
    return ss.str();
}

struct PowerState {
    bool camerasOn{true};
    bool sleeping{false};
};

static std::string powerStateJson(const PowerState& p) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << RoverStatus::POWER << "\"," 
       << "\"sleeping\":" << (p.sleeping ? "true" : "false") << ","
       << "\"cameras_on\":" << (p.camerasOn ? "true" : "false")
       << "}";
    return ss.str();
}

// ---- Teensy firmware runtime-config command ----------------
static std::string teensyFwConfigCommand(const RoverConfig& cfg) {
    std::ostringstream ss;
    ss << "{"
       << "\"cmd\":\"fw_cfg\"," 
       << "\"l_rpwm\":" << cfg.teensy_l_rpwm_pin << ","
       << "\"l_lpwm\":" << cfg.teensy_l_lpwm_pin << ","
       << "\"l_en\":"   << cfg.teensy_l_en_pin << ","
       << "\"r_rpwm\":" << cfg.teensy_r_rpwm_pin << ","
       << "\"r_lpwm\":" << cfg.teensy_r_lpwm_pin << ","
       << "\"r_en\":"   << cfg.teensy_r_en_pin << ","
       << "\"enc_la\":" << cfg.teensy_enc_la_pin << ","
       << "\"enc_lb\":" << cfg.teensy_enc_lb_pin << ","
       << "\"enc_ra\":" << cfg.teensy_enc_ra_pin << ","
       << "\"enc_rb\":" << cfg.teensy_enc_rb_pin << ","
       << "\"vbat_adc\":" << cfg.teensy_vbat_adc_pin << ","
       << "\"curr_adc\":" << cfg.teensy_curr_adc_pin << ","
       << "\"temp_adc\":" << cfg.teensy_temp_adc_pin << ","
       << "\"vbat_div\":" << cfg.teensy_vbat_div_ratio << ","
       << "\"curr_zero_mv\":" << cfg.teensy_curr_zero_mv << ","
       << "\"curr_sens_mv_per_a\":" << cfg.teensy_curr_sens_mv_per_a << ","
       << "\"watchdog_ms\":" << cfg.teensy_watchdog_ms << ","
       << "\"telem_ms\":" << cfg.teensy_telem_interval_ms
       << "}";
    return ss.str();
}

// ---- Main command dispatcher (called from WiFi and WebUI) ----
// Forward declare — WebUiServer* is optional (may be null)
class WebUiServer;
static void broadcastAll(WifiServer& ws, WebUiServer* webui, const std::string& json);

static void dispatchCommand(const std::string& json,
                            TeensyBridge& teensy,
                            GpioController& gpio,
                            TeensyOta& ota,
                            WifiServer& ws,
                            WebUiServer* webui,
                            const RoverConfig& cfg,
                            CameraStream& cam0,
                            CameraStream& cam1,
                            PowerState& power,
                            std::mutex& powerMtx)
{
    std::string type = jsonStr(json, "type");

    // --- Camera power ---
    if (type == RoverCmd::CAMERAS) {
        bool enabled = jsonBool(json, "enabled");
        std::lock_guard<std::mutex> lk(powerMtx);
        if (enabled && !power.camerasOn) {
            cam0.start();
            cam1.start();
            power.camerasOn = true;
        } else if (!enabled && power.camerasOn) {
            cam0.stop();
            cam1.stop();
            power.camerasOn = false;
        }
        broadcastAll(ws, webui, powerStateJson(power));
        return;
    }

    // --- Sleep / wake ---
    if (type == RoverCmd::SLEEP) {
        std::lock_guard<std::mutex> lk(powerMtx);
        power.sleeping = true;
        if (power.camerasOn) {
            cam0.stop();
            cam1.stop();
            power.camerasOn = false;
        }
        teensy.sendStop();
        gpio.set(RoverGpio::LED_FWD, false);
        gpio.set(RoverGpio::LED_REV, false);
        broadcastAll(ws, webui, powerStateJson(power));
        return;
    }
    if (type == RoverCmd::WAKE) {
        std::lock_guard<std::mutex> lk(powerMtx);
        power.sleeping = false;
        if (!power.camerasOn) {
            cam0.start();
            cam1.start();
            power.camerasOn = true;
        }
        broadcastAll(ws, webui, powerStateJson(power));
        return;
    }

    {
        std::lock_guard<std::mutex> lk(powerMtx);
        if (power.sleeping && type != RoverCmd::STATUS && type != RoverCmd::ESTOP) {
            // Ignore normal action commands while sleeping.
            return;
        }
    }

    // --- ESTOP (highest priority) ---
    if (type == RoverCmd::ESTOP || type == "estop") {
        teensy.sendStop();
        gpio.set(RoverGpio::LED_FWD, false);
        gpio.set(RoverGpio::LED_REV, false);
        return;
    }
    // --- Drive ---
    if (type == RoverCmd::DRIVE) {
        float x   = jsonFloat(json, "x");   // lateral (unused for tank)
        float y   = jsonFloat(json, "y");   // forward/backward
        float rot = jsonFloat(json, "rot"); // rotation
        (void)x;

        // Tank drive mixing
        float left  = y + rot;
        float right = y - rot;

        // Apply deadband
        auto db = [&cfg](float v) {
            return std::abs(v) < cfg.deadband ? 0.f : v;
        };
        left  = db(left);
        right = db(right);

        // Cap to max speed
        auto cap = [&cfg](float v) {
            return std::max(-cfg.max_motor_speed,
                            std::min(cfg.max_motor_speed, v));
        };
        left  = cap(left);
        right = cap(right);

        teensy.sendDrive(left, right);

        // LED direction indicators
        bool fwd = (y > 0.05f);
        bool rev = (y < -0.05f);
        gpio.set(RoverGpio::LED_FWD, fwd);
        gpio.set(RoverGpio::LED_REV, rev);
        return;
    }
    // --- GPIO ---
    if (type == RoverCmd::GPIO) {
        std::string pin = jsonStr(json, "pin");
        bool state = jsonBool(json, "state");
        if (pin == RoverGpio::HORN) {
            // Pulse horn for 1 second instead of holding
            gpio.pulse(pin, 1000);
        } else {
            gpio.set(pin, state);
        }
        return;
    }
    // --- Audio ---
    if (type == RoverCmd::AUDIO) {
        std::string file = jsonStr(json, "file");
        // Use aplay for WAV or mpg123 for MP3
        std::string cmd = "aplay " + cfg.audio_dir + "/" + file + " &";
        std::system(cmd.c_str());
        return;
    }
    // --- OTA begin ---
    if (type == "ota_begin") {
        int total = jsonInt(json, "total");
        ota.begin(total);
        return;
    }
    // --- OTA chunk ---
    if (type == RoverCmd::OTA) {
        int chunk = jsonInt(json, "chunk");
        std::string data = jsonStr(json, "data");
        if (ota.addChunk(chunk, data)) {
            // Check if complete
            if (ota.rxChunks() == ota.totalChunks()) {
                // Stop Teensy before flashing
                teensy.sendStop();
                ota.flash([&ws, webui](int pct, const std::string& msg) {
                    std::string j = otaProgress(pct, msg);
                    broadcastAll(ws, webui, j);
                });
            }
        }
        return;
    }
    // --- Status request ---
    if (type == RoverCmd::STATUS) {
        teensy.requestSensors();
        return;
    }

    std::cerr << "[main] unknown command type: " << type << "\n";
}

// ============================================================
//  main()
// ============================================================
int main(int argc, char* argv[]) {
    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);

    std::string cfgPath = (argc > 1) ? argv[1] : "/etc/rover/rover.conf";
    RoverConfig cfg = loadConfig(cfgPath);

    std::cout << "=== Rover Starting ===\n";

    // ---- Teensy serial bridge -----
    TeensyBridge teensy(cfg.teensy_port, cfg.serial_baud);
    bool teensyOnline = teensy.open();
    if (!teensyOnline) {
        std::cerr << "[main] WARNING: Teensy not connected on " << cfg.teensy_port << "\n";
    } else if (cfg.teensy_push_fw_config) {
        // Let the Teensy finish USB CDC startup, then push runtime config.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        teensy.sendRaw(teensyFwConfigCommand(cfg));
        std::cout << "[main] pushed teensy fw config from rover.conf\n";
    }

    // ---- GPIO controller ----------
    GpioController gpio(cfg.gpio_pins);
    gpio.init();

    // ---- OTA ----------------------
    TeensyOta ota(cfg.ota_work_dir, cfg.ota_flash_cmd, cfg.teensy_mmcu);

    // ---- Cameras ------------------
    CameraStream cam0(cfg.cam0_device, cfg.cam0_port,
                      cfg.cam_width, cfg.cam_height,
                      cfg.cam_fps, cfg.cam_jpeg_quality);
    CameraStream cam1(cfg.cam1_device, cfg.cam1_port,
                      cfg.cam_width, cfg.cam_height,
                      cfg.cam_fps, cfg.cam_jpeg_quality);
    cam0.start();
    cam1.start();

    // ---- WiFi WebSocket -----------
    WifiServer  ws(cfg.ws_port);

    // ---- Web UI server (HTTP + WebSocket on :8080) --------
    WebUiServer webui(cfg.webui_port, cfg.webui_dir);
    WebUiServer* webuiPtr = &webui;

    // ---- Wire up sensor data → telemetry broadcast --------
    teensy.onSensors([&](const TeensySensors& s) {
        std::string json = buildTelemetry(s, gpio);
        broadcastAll(ws, webuiPtr, json);
        webui.setLatestStatus(json);
    });

    // ---- Command handler shared by WiFi and WebUI ---
    PowerState power;
    std::mutex powerMtx;

    auto cmdHandler = [&](const std::string& json) {
        dispatchCommand(json, teensy, gpio, ota, ws, webuiPtr, cfg,
                        cam0, cam1, power, powerMtx);
    };
    ws.onMessage(cmdHandler);
    webui.onMessage(cmdHandler);

    // ---- Start servers ------------
    ws.start();
    webui.start();

    // ---- Telemetry request loop ---------------------------
    int telPeriodMs = (cfg.telemetry_hz > 0) ? (1000 / cfg.telemetry_hz) : 100;
    std::cout << "[main] All systems running. Press Ctrl+C to stop.\n";
    std::cout << "[main] Web UI:    http://<ip>:" << cfg.webui_port << "\n";
    std::cout << "[main] Cameras:   http://<ip>:" << cfg.cam0_port
              << "  http://<ip>:" << cfg.cam1_port << "\n";
    std::cout << "[main] WebSocket: ws://<ip>:" << cfg.ws_port << "\n";

    while (gRunning) {
        bool sleeping = false;
        {
            std::lock_guard<std::mutex> lk(powerMtx);
            sleeping = power.sleeping;
        }
        if (!sleeping) teensy.requestSensors();
        std::this_thread::sleep_for(std::chrono::milliseconds(telPeriodMs));
    }

    std::cout << "\n[main] Shutting down...\n";
    teensy.sendStop();
    ws.stop();
    webui.stop();
    cam0.stop();
    cam1.stop();
    gpio.shutdown();
    teensy.close();
    return 0;
}

// Broadcast to all active transports (WiFi WS + WebUI WS)
static void broadcastAll(WifiServer& ws, WebUiServer* webui, const std::string& json) {
    ws.broadcast(json);
    if (webui) webui->broadcast(json);
}
