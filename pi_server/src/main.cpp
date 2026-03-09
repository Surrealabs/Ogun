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
#include <fstream>
#include <vector>
#include <cctype>
#include <cstdio>
// Simple JSON helpers (no dependency)
#include <map>

static std::atomic<bool> gRunning{true};
static std::atomic<bool> gUpdateCheckBusy{false};

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
                                             GpioController& gpio,
                                             bool started,
                                             bool estopLatched,
                                             bool precheckOk) {
    std::ostringstream ss;
    ss << "{\"type\":\"telemetry\","
       << "\"enc_l\":"   << s.enc_l   << ","
       << "\"enc_r\":"   << s.enc_r   << ","
       << "\"volt\":"    << s.voltage << ","
       << "\"curr\":"    << s.current << ","
       << "\"temp\":"    << s.temp    << ","
         << "\"started\":" << (started ? "true" : "false") << ","
         << "\"estop\":" << (estopLatched ? "true" : "false") << ","
         << "\"precheck_ok\":" << (precheckOk ? "true" : "false") << ","
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

struct ControlState {
    bool started{false};
    bool estopLatched{false};
    bool invertLeftMotor{false};
    bool invertRightMotor{false};
};

static std::string powerStateJson(const PowerState& p) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << RoverStatus::POWER << "\"," 
       << "\"sleeping\":" << (p.sleeping ? "true" : "false") << ","
       << "\"cameras_on\":" << (p.camerasOn ? "true" : "false")
       << "}";
    return ss.str();
}

static std::string updateStateJson(const std::string& status, const std::string& detail) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << RoverStatus::UPDATE << "\"," 
       << "\"status\":\"" << status << "\"," 
       << "\"detail\":\"" << detail << "\"}";
    return ss.str();
}

static bool saveDriveTuneConfigFile(const std::string& path,
                                    float maxFwd,
                                    float maxRev,
                                    float maxTurn,
                                    float throttleExpo,
                                    float turnExpo,
                                    float accelUp,
                                    float accelDown,
                                    bool invertLeft,
                                    bool invertRight,
                                    std::string* err)
{
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        return s.substr(a, b - a + 1);
    };
    auto fmt = [](float v) {
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(2);
        ss << v;
        return ss.str();
    };
    auto keyLine = [&](const std::string& key, float value) {
        return key + " = " + fmt(value);
    };
    auto keyBoolLine = [&](const std::string& key, bool value) {
        return key + " = " + (value ? "true" : "false");
    };

    std::vector<std::pair<std::string, std::string>> entries = {
        {"teensy_drive_max_fwd", keyLine("teensy_drive_max_fwd", maxFwd)},
        {"teensy_drive_max_rev", keyLine("teensy_drive_max_rev", maxRev)},
        {"teensy_turn_max", keyLine("teensy_turn_max", maxTurn)},
        {"teensy_throttle_expo", keyLine("teensy_throttle_expo", throttleExpo)},
        {"teensy_turn_expo", keyLine("teensy_turn_expo", turnExpo)},
        {"teensy_accel_up_per_s", keyLine("teensy_accel_up_per_s", accelUp)},
        {"teensy_accel_down_per_s", keyLine("teensy_accel_down_per_s", accelDown)},
        {"invert_left_motor", keyBoolLine("invert_left_motor", invertLeft)},
        {"invert_right_motor", keyBoolLine("invert_right_motor", invertRight)}
    };

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    auto isKeyLine = [&](const std::string& line, const std::string& key) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') return false;
        if (t.rfind(key, 0) != 0) return false;
        if (t.size() == key.size()) return true;
        char next = t[key.size()];
        return std::isspace(static_cast<unsigned char>(next)) || next == '=';
    };

    for (const auto& kv : entries) {
        bool replaced = false;
        for (auto& line : lines) {
            if (isKeyLine(line, kv.first)) {
                line = kv.second;
                replaced = true;
                break;
            }
        }
        if (!replaced) lines.push_back(kv.second);
    }

    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out.is_open()) {
            if (err) *err = "cannot write temp config";
            return false;
        }
        for (const auto& line : lines) out << line << "\n";
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        if (err) *err = "cannot replace config file";
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
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
    << "\"drive_max_fwd\":" << cfg.teensy_drive_max_fwd << ","
    << "\"drive_max_rev\":" << cfg.teensy_drive_max_rev << ","
    << "\"turn_max\":" << cfg.teensy_turn_max << ","
    << "\"throttle_expo\":" << cfg.teensy_throttle_expo << ","
    << "\"turn_expo\":" << cfg.teensy_turn_expo << ","
    << "\"accel_up_per_s\":" << cfg.teensy_accel_up_per_s << ","
    << "\"accel_down_per_s\":" << cfg.teensy_accel_down_per_s << ","
       << "\"watchdog_ms\":" << cfg.teensy_watchdog_ms << ","
       << "\"telem_ms\":" << cfg.teensy_telem_interval_ms << ","
       << "\"input_deadband\":" << cfg.deadband << ","
       << "\"require_arm\":1"
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
                            std::mutex& powerMtx,
                            ControlState& control,
                            std::mutex& controlMtx)
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
        teensy.sendRaw("{\"cmd\":\"disarm\"}");
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            control.started = false;
        }
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

    // --- Manual update check/apply (non-blocking) ---
    if (type == RoverCmd::UPDATE_CHECK) {
        if (gUpdateCheckBusy.exchange(true)) {
            broadcastAll(ws, webui, updateStateJson("busy", "Update check already running"));
            return;
        }
        broadcastAll(ws, webui, updateStateJson("queued", "Starting rover-auto-update.service"));
        std::thread([&ws, webui]() {
            int rc = std::system("systemctl start rover-auto-update.service >/dev/null 2>&1");
            if (rc == 0) {
                broadcastAll(ws, webui, updateStateJson("started", "Update service started"));
            } else {
                broadcastAll(ws, webui, updateStateJson("error", "Failed to start update service"));
            }
            gUpdateCheckBusy = false;
        }).detach();
        return;
    }

    // --- Runtime drive tuning (optional persist to rover.conf) ---
    if (type == RoverCmd::DRIVE_TUNE || type == RoverCmd::DRIVE_TUNE_SAVE) {
        const bool persist = (type == RoverCmd::DRIVE_TUNE_SAVE);
        auto clampf = [](float v, float lo, float hi) {
            return std::max(lo, std::min(hi, v));
        };
        const float maxFwd = clampf(jsonFloat(json, "max_fwd"), 0.0f, 1.0f);
        const float maxRev = clampf(jsonFloat(json, "max_rev"), 0.0f, 1.0f);
        const float maxTurn = clampf(jsonFloat(json, "max_turn"), 0.0f, 1.0f);
        const float throttleExpo = clampf(jsonFloat(json, "throttle_expo"), 0.2f, 4.0f);
        const float turnExpo = clampf(jsonFloat(json, "turn_expo"), 0.2f, 4.0f);
        const float accelUp = clampf(jsonFloat(json, "accel_up_per_s"), 0.05f, 20.0f);
        const float accelDown = clampf(jsonFloat(json, "accel_down_per_s"), 0.05f, 30.0f);
        bool currentInvertLeft = cfg.invert_left_motor;
        bool currentInvertRight = cfg.invert_right_motor;
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            currentInvertLeft = control.invertLeftMotor;
            currentInvertRight = control.invertRightMotor;
        }
        const bool invertLeft = (json.find("\"invert_left\"") != std::string::npos)
            ? jsonBool(json, "invert_left")
            : currentInvertLeft;
        const bool invertRight = (json.find("\"invert_right\"") != std::string::npos)
            ? jsonBool(json, "invert_right")
            : currentInvertRight;

        {
            std::lock_guard<std::mutex> ck(controlMtx);
            control.invertLeftMotor = invertLeft;
            control.invertRightMotor = invertRight;
        }

        std::ostringstream fw;
        fw << "{"
           << "\"cmd\":\"fw_cfg\"," 
           << "\"drive_max_fwd\":" << maxFwd << ","
           << "\"drive_max_rev\":" << maxRev << ","
           << "\"turn_max\":" << maxTurn << ","
           << "\"throttle_expo\":" << throttleExpo << ","
           << "\"turn_expo\":" << turnExpo << ","
           << "\"accel_up_per_s\":" << accelUp << ","
           << "\"accel_down_per_s\":" << accelDown
           << "}";
        teensy.sendRaw(fw.str());

        bool saved = false;
        std::string saveErr;
        if (persist) {
            saved = saveDriveTuneConfigFile(
                "/etc/rover/rover.conf",
                maxFwd, maxRev, maxTurn, throttleExpo, turnExpo, accelUp, accelDown,
                invertLeft, invertRight,
                &saveErr);
        }

        std::ostringstream ack;
        ack << "{\"type\":\"" << RoverStatus::DRIVE_TUNE << "\"," 
            << "\"ok\":true,"
            << "\"saved\":" << ((persist && saved) ? "true" : "false") << ","
            << "\"persist\":" << (persist ? "true" : "false") << ","
            << "\"detail\":\"";
        if (persist) {
            ack << (saved ? "Saved to /etc/rover/rover.conf" : "Save failed: " + saveErr);
        } else {
            ack << "Applied runtime tuning";
        }
        ack << "\"," 
            << "\"max_fwd\":" << maxFwd << ","
            << "\"max_rev\":" << maxRev << ","
            << "\"max_turn\":" << maxTurn << ","
            << "\"throttle_expo\":" << throttleExpo << ","
            << "\"turn_expo\":" << turnExpo << ","
            << "\"accel_up_per_s\":" << accelUp << ","
            << "\"accel_down_per_s\":" << accelDown << ","
            << "\"invert_left\":" << (invertLeft ? "true" : "false") << ","
            << "\"invert_right\":" << (invertRight ? "true" : "false")
            << "}";
        broadcastAll(ws, webui, ack.str());
        return;
    }

    {
        std::lock_guard<std::mutex> lk(powerMtx);
        if (power.sleeping && type != RoverCmd::STATUS && type != RoverCmd::ESTOP && type != RoverCmd::DRIVE_TUNE && type != RoverCmd::DRIVE_TUNE_SAVE) {
            // Ignore normal action commands while sleeping.
            return;
        }
    }

    // --- Ignition start (explicit arm; default is disarmed) ---
    if (type == RoverCmd::IGNITION_START) {
        const bool precheckOk = teensy.isOpen();
        if (!precheckOk) {
            broadcastAll(ws, webui, "{\"type\":\"error\",\"msg\":\"Pre-ignition check failed: Teensy offline\"}");
            return;
        }
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            if (control.estopLatched) {
                broadcastAll(ws, webui, "{\"type\":\"error\",\"msg\":\"Clear E-Stop before starting\"}");
                return;
            }
            control.started = true;
        }
        teensy.sendRaw("{\"cmd\":\"arm\"}");
        teensy.sendStop();
        return;
    }

    // --- Clear E-STOP latch (stays disarmed until ignition_start) ---
    if (type == RoverCmd::ESTOP_CLEAR) {
        teensy.sendRaw("{\"cmd\":\"estop_clear\"}");
        teensy.sendStop();
        std::lock_guard<std::mutex> ck(controlMtx);
        control.estopLatched = false;
        control.started = false;
        return;
    }

    // --- ESTOP (highest priority) ---
    if (type == RoverCmd::ESTOP || type == "estop") {
        teensy.sendRaw("{\"cmd\":\"estop\"}");
        teensy.sendStop();
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            control.estopLatched = true;
            control.started = false;
        }
        gpio.set(RoverGpio::LED_FWD, false);
        gpio.set(RoverGpio::LED_REV, false);
        return;
    }
    // --- Drive ---
    if (type == RoverCmd::DRIVE) {
        bool invertLeft = false;
        bool invertRight = false;
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            if (!control.started || control.estopLatched) {
                teensy.sendStop();
                return;
            }
            invertLeft = control.invertLeftMotor;
            invertRight = control.invertRightMotor;
        }
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

        if (invertLeft) left = -left;
        if (invertRight) right = -right;

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
                // Ask firmware to reboot into bootloader for self-upgrade.
                teensy.sendRaw("{\"cmd\":\"bootloader\"}");
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
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
        teensy.sendRaw("{\"cmd\":\"disarm\"}");
        std::cout << "[main] drivetrain starts disarmed (explicit ignition required)\n";
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

    // ---- Shared runtime state ----
    PowerState power;
    std::mutex powerMtx;
    ControlState control;
    std::mutex controlMtx;
    control.invertLeftMotor = cfg.invert_left_motor;
    control.invertRightMotor = cfg.invert_right_motor;

    // ---- Wire up sensor data → telemetry broadcast --------
    teensy.onSensors([&](const TeensySensors& s) {
        bool sleeping = false;
        {
            std::lock_guard<std::mutex> lk(powerMtx);
            sleeping = power.sleeping;
        }
        bool started = false;
        bool estopLatched = false;
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            started = control.started;
            estopLatched = control.estopLatched;
        }
        const bool precheckOk = teensy.isOpen() && !sleeping;
        std::string json = buildTelemetry(s, gpio, started, estopLatched, precheckOk);
        broadcastAll(ws, webuiPtr, json);
        webui.setLatestStatus(json);
    });

    // ---- Command handler shared by WiFi and WebUI ---

    auto cmdHandler = [&](const std::string& json) {
        dispatchCommand(json, teensy, gpio, ota, ws, webuiPtr, cfg,
                        cam0, cam1, power, powerMtx, control, controlMtx);
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
    teensy.sendRaw("{\"cmd\":\"disarm\"}");
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
