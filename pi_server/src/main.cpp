#include "Config.hpp"
#include "Protocol.hpp"
#include "serial/TeensyBridge.hpp"
#include "wifi/WifiServer.hpp"
#include "webui/WebUiServer.hpp"
#include "camera/CameraStream.hpp"
#include "gpio/GpioController.hpp"
#include "ota/TeensyOta.hpp"
#include "module/ModuleRegistry.hpp"
#include "module/ModuleConf.hpp"

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
#include <algorithm>
#include <systemd/sd-daemon.h>
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
                                             bool precheckOk,
                                             bool teensyConnected) {
    std::ostringstream ss;
    ss << "{\"type\":\"telemetry\","
       << "\"enc_l\":"   << s.enc_l   << ","
       << "\"enc_r\":"   << s.enc_r   << ","
       << "\"volt\":"    << s.voltage << ","
       << "\"curr_l\":"  << s.current_l << ","
       << "\"curr_r\":"  << s.current_r << ","
       << "\"curr_t\":"  << s.current_t << ","
       << "\"temp\":"    << s.temp    << ","
         << "\"started\":" << (started ? "true" : "false") << ","
         << "\"estop\":" << (estopLatched ? "true" : "false") << ","
         << "\"precheck_ok\":" << (precheckOk ? "true" : "false") << ","
         << "\"teensy_connected\":" << (teensyConnected ? "true" : "false")
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
    bool invertTurnMotor{false};
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
                                    int maxPwm,
                                    int minPwm,
                                    float rampSec,
                                    bool invertLeft,
                                    bool invertRight,
                                    int turnMaxPwm,
                                    bool invertTurn,
                                    float turnSlowdown,
                                    float turnRampSec,
                                    std::string* err)
{
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        return s.substr(a, b - a + 1);
    };
    auto keyIntLine = [&](const std::string& key, int value) {
        return key + " = " + std::to_string(value);
    };
    auto keyFloatLine = [&](const std::string& key, float value) {
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(2);
        ss << value;
        return key + " = " + ss.str();
    };
    auto keyBoolLine = [&](const std::string& key, bool value) {
        return key + " = " + (value ? "true" : "false");
    };

    std::vector<std::pair<std::string, std::string>> entries = {
        {"teensy_max_pwm", keyIntLine("teensy_max_pwm", maxPwm)},
        {"teensy_min_pwm", keyIntLine("teensy_min_pwm", minPwm)},
        {"teensy_ramp_sec", keyFloatLine("teensy_ramp_sec", rampSec)},
        {"invert_left_motor", keyBoolLine("invert_left_motor", invertLeft)},
        {"invert_right_motor", keyBoolLine("invert_right_motor", invertRight)},
        {"teensy_turn_max_pwm", keyIntLine("teensy_turn_max_pwm", turnMaxPwm)},
        {"invert_turn_motor", keyBoolLine("invert_turn_motor", invertTurn)},
        {"teensy_turn_slowdown", keyFloatLine("teensy_turn_slowdown", turnSlowdown)},
        {"teensy_turn_ramp_sec", keyFloatLine("teensy_turn_ramp_sec", turnRampSec)}
    };
    // Remove legacy keys
    std::vector<std::string> legacyKeys = {
        "teensy_drive_max_fwd", "teensy_drive_max_rev", "teensy_turn_max",
        "teensy_throttle_expo", "teensy_turn_expo",
        "teensy_accel_up_per_s", "teensy_accel_down_per_s"
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

    // Strip legacy tuning keys
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& line) {
        for (const auto& lk : legacyKeys) {
            if (isKeyLine(line, lk)) return true;
        }
        return false;
    }), lines.end());

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
       << "\"t_rpwm\":" << cfg.teensy_t_rpwm_pin << ","
       << "\"t_lpwm\":" << cfg.teensy_t_lpwm_pin << ","
       << "\"t_en\":"   << cfg.teensy_t_en_pin << ","
       << "\"enc_la\":" << cfg.teensy_enc_la_pin << ","
       << "\"enc_lb\":" << cfg.teensy_enc_lb_pin << ","
       << "\"enc_ra\":" << cfg.teensy_enc_ra_pin << ","
       << "\"enc_rb\":" << cfg.teensy_enc_rb_pin << ","
       << "\"vbat_adc\":" << cfg.teensy_vbat_adc_pin << ","
       << "\"curr_l_adc\":" << cfg.teensy_curr_l_adc_pin << ","
       << "\"curr_r_adc\":" << cfg.teensy_curr_r_adc_pin << ","
       << "\"curr_t_adc\":" << cfg.teensy_curr_t_adc_pin << ","
       << "\"temp_adc\":" << cfg.teensy_temp_adc_pin << ","
       << "\"vbat_div\":" << cfg.teensy_vbat_div_ratio << ","
       << "\"curr_zero_mv\":" << cfg.teensy_curr_zero_mv << ","
       << "\"curr_sens_mv_per_a\":" << cfg.teensy_curr_sens_mv_per_a << ","
       << "\"max_pwm\":" << cfg.teensy_max_pwm << ","
       << "\"min_pwm\":" << cfg.teensy_min_pwm << ","
       << "\"ramp_sec\":" << cfg.teensy_ramp_sec << ","
       << "\"invert_left\":" << (cfg.invert_left_motor ? 1 : 0) << ","
       << "\"invert_right\":" << (cfg.invert_right_motor ? 1 : 0) << ","
       << "\"turn_max_pwm\":" << cfg.teensy_turn_max_pwm << ","
       << "\"invert_turn\":" << (cfg.invert_turn_motor ? 1 : 0) << ","
       << "\"turn_slowdown\":" << cfg.teensy_turn_slowdown << ","
       << "\"turn_ramp_sec\":" << cfg.teensy_turn_ramp_sec << ","
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

static bool dispatchCommand(const std::string& json,
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
        return true;
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
        broadcastAll(ws, webui, powerStateJson(power));
        return true;
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
        return true;
    }

    // --- Manual update check/apply (non-blocking) ---
    if (type == RoverCmd::UPDATE_CHECK) {
        if (gUpdateCheckBusy.exchange(true)) {
            broadcastAll(ws, webui, updateStateJson("busy", "Update check already running"));
            return true;
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
        return true;
    }

    // --- Runtime drive tuning (optional persist to rover.conf) ---
    if (type == RoverCmd::DRIVE_TUNE || type == RoverCmd::DRIVE_TUNE_SAVE) {
        const bool persist = (type == RoverCmd::DRIVE_TUNE_SAVE);
        const int maxPwm = std::max(0, std::min(255, jsonInt(json, "max_pwm")));
        const int minPwm = std::max(0, std::min(255, jsonInt(json, "min_pwm")));
        const float rampSec = std::max(0.05f, std::min(30.0f, jsonFloat(json, "ramp_sec")));
        const int turnMaxPwm = std::max(0, std::min(255, jsonInt(json, "turn_max_pwm")));
        const float turnSlowdown = std::max(0.0f, std::min(1.0f, jsonFloat(json, "turn_slowdown")));
        const float turnRampSec = std::max(0.01f, std::min(10.0f, jsonFloat(json, "turn_ramp_sec")));
        bool currentInvertLeft = cfg.invert_left_motor;
        bool currentInvertRight = cfg.invert_right_motor;
        bool currentInvertTurn = cfg.invert_turn_motor;
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            currentInvertLeft = control.invertLeftMotor;
            currentInvertRight = control.invertRightMotor;
            currentInvertTurn = control.invertTurnMotor;
        }
        const bool invertLeft = (json.find("\"invert_left\"") != std::string::npos)
            ? jsonBool(json, "invert_left")
            : currentInvertLeft;
        const bool invertRight = (json.find("\"invert_right\"") != std::string::npos)
            ? jsonBool(json, "invert_right")
            : currentInvertRight;
        const bool invertTurn = (json.find("\"invert_turn\"") != std::string::npos)
            ? jsonBool(json, "invert_turn")
            : currentInvertTurn;

        {
            std::lock_guard<std::mutex> ck(controlMtx);
            control.invertLeftMotor = invertLeft;
            control.invertRightMotor = invertRight;
            control.invertTurnMotor = invertTurn;
        }

        std::ostringstream fw;
        fw << "{"
           << "\"cmd\":\"fw_cfg\"," 
           << "\"max_pwm\":" << maxPwm << ","
           << "\"min_pwm\":" << minPwm << ","
           << "\"ramp_sec\":" << rampSec << ","
           << "\"invert_left\":" << (invertLeft ? 1 : 0) << ","
           << "\"invert_right\":" << (invertRight ? 1 : 0) << ","
           << "\"turn_max_pwm\":" << turnMaxPwm << ","
           << "\"invert_turn\":" << (invertTurn ? 1 : 0) << ","
           << "\"turn_slowdown\":" << turnSlowdown << ","
           << "\"turn_ramp_sec\":" << turnRampSec
           << "}";
        teensy.sendRaw(fw.str());

        bool saved = false;
        std::string saveErr;
        if (persist) {
            saved = saveDriveTuneConfigFile(
                "/etc/rover/rover.conf",
                maxPwm, minPwm, rampSec,
                invertLeft, invertRight,
                turnMaxPwm, invertTurn,
                turnSlowdown, turnRampSec,
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
            << "\"max_pwm\":" << maxPwm << ","
            << "\"min_pwm\":" << minPwm << ","
            << "\"ramp_sec\":" << rampSec << ","
            << "\"invert_left\":" << (invertLeft ? "true" : "false") << ","
            << "\"invert_right\":" << (invertRight ? "true" : "false") << ","
            << "\"turn_max_pwm\":" << turnMaxPwm << ","
            << "\"invert_turn\":" << (invertTurn ? "true" : "false") << ","
            << "\"turn_slowdown\":" << turnSlowdown << ","
            << "\"turn_ramp_sec\":" << turnRampSec
            << "}";
        broadcastAll(ws, webui, ack.str());
        if (webui) webui->setLatestTune(ack.str());
        return true;
    }

    {
        std::lock_guard<std::mutex> lk(powerMtx);
        if (power.sleeping && type != RoverCmd::STATUS && type != RoverCmd::ESTOP && type != RoverCmd::DRIVE_TUNE && type != RoverCmd::DRIVE_TUNE_SAVE) {
            // Ignore normal action commands while sleeping.
            return true;
        }
    }

    // --- Ignition start (explicit arm; default is disarmed) ---
    if (type == RoverCmd::IGNITION_START) {
        const bool precheckOk = teensy.isOpen();
        if (!precheckOk) {
            broadcastAll(ws, webui, "{\"type\":\"error\",\"msg\":\"Pre-ignition check failed: Teensy offline\"}");
            return true;
        }
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            if (control.estopLatched) {
                broadcastAll(ws, webui, "{\"type\":\"error\",\"msg\":\"Clear E-Stop before starting\"}");
                return true;
            }
            control.started = true;
        }
        teensy.sendRaw("{\"cmd\":\"arm\"}");
        teensy.sendStop();
        return true;
    }

    // --- Clear E-STOP latch (stays disarmed until ignition_start) ---
    if (type == RoverCmd::ESTOP_CLEAR) {
        if (teensy.isOpen()) {
            teensy.sendRaw("{\"cmd\":\"estop_clear\"}");
            teensy.sendStop();
        }
        // Clear local state regardless — Teensy will get the command
        // on reconnect via the fw_config push.
        std::lock_guard<std::mutex> ck(controlMtx);
        control.estopLatched = false;
        control.started = false;
        return true;
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
        return true;
    }
    // --- Drive (car-style: both motors get same throttle, turn motor steers) ---
    if (type == RoverCmd::DRIVE) {
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            if (!control.started || control.estopLatched) {
                teensy.sendStop();
                return true;
            }
        }
        float y = jsonFloat(json, "y");   // forward/backward throttle
        float rot = jsonFloat(json, "rot"); // turning motor input

        // Apply deadband
        if (std::abs(y) < cfg.deadband) y = 0.f;
        if (std::abs(rot) < cfg.deadband) rot = 0.f;

        // Cap to max speed
        y = std::max(-cfg.max_motor_speed, std::min(cfg.max_motor_speed, y));
        rot = std::max(-1.0f, std::min(1.0f, rot));

        // Car chassis: both motors get identical throttle.
        // Turn motor gets rot independently.
        // Motor inversion handled by Teensy firmware.
        teensy.sendDrive(y, y, rot);
        return true;
    }
    // --- GPIO (no pins wired yet — commands accepted but no-op) ---
    if (type == RoverCmd::GPIO) {
        return true;
    }
    // --- Audio ---
    if (type == RoverCmd::AUDIO) {
        std::string file = jsonStr(json, "file");
        // Use aplay for WAV or mpg123 for MP3
        std::string cmd = "aplay " + cfg.audio_dir + "/" + file + " &";
        std::system(cmd.c_str());
        return true;
    }
    // --- OTA begin ---
    if (type == "ota_begin") {
        int total = jsonInt(json, "total");
        ota.begin(total);
        return true;
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
        return true;
    }
    // --- Status request ---
    if (type == RoverCmd::STATUS) {
        teensy.requestSensors();
        broadcastAll(ws, webui, teensy.linkDiagJson());
        return true;
    }

    // --- Teensy serial-link diagnostics ---
    if (type == RoverCmd::LINK_DIAG) {
        broadcastAll(ws, webui, teensy.linkDiagJson());
        return true;
    }

    // --- Pin diagnostics (passthrough to Teensy) ---
    if (type == RoverCmd::PIN_DIAG) {
        teensy.sendRaw("{\"cmd\":\"pin_diag\"}");
        return true;
    }
    if (type == RoverCmd::PIN_SET) {
        int pin = jsonInt(json, "pin");
        int val = jsonInt(json, "val");
        std::ostringstream ps;
        ps << "{\"cmd\":\"pin_set\",\"pin\":" << pin << ",\"val\":" << val << "}";
        teensy.sendRaw(ps.str());
        return true;
    }

    // --- Reboot Pi ---
    if (type == RoverCmd::REBOOT) {
        broadcastAll(ws, webui, "{\"type\":\"update\",\"status\":\"rebooting\",\"detail\":\"Pi rebooting now...\"}");
        teensy.sendStop();
        teensy.sendRaw("{\"cmd\":\"disarm\"}");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::system("sudo reboot");
        return true;
    }

    return false;  // not handled by core — let modules try
}

// ============================================================
//  main()
// ============================================================
int main(int argc, char* argv[]) {
    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);
    std::signal(SIGPIPE, SIG_IGN);  // ignore broken pipe (Teensy USB disconnect)

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
    webui.setCredentials(cfg.webui_user, cfg.webui_pass);
    WebUiServer* webuiPtr = &webui;

    // ---- Shared runtime state ----
    PowerState power;
    std::mutex powerMtx;
    ControlState control;
    std::mutex controlMtx;
    control.invertLeftMotor = cfg.invert_left_motor;
    control.invertRightMotor = cfg.invert_right_motor;
    control.invertTurnMotor = cfg.invert_turn_motor;

    // ---- Seed initial tune so new WS clients get it immediately ----
    {
        std::ostringstream t;
        t << "{\"type\":\"" << RoverStatus::DRIVE_TUNE << "\","
          << "\"ok\":true,\"saved\":true,\"persist\":false,"
          << "\"max_pwm\":" << cfg.teensy_max_pwm << ","
          << "\"min_pwm\":" << cfg.teensy_min_pwm << ","
          << "\"ramp_sec\":" << cfg.teensy_ramp_sec << ","
          << "\"invert_left\":" << (cfg.invert_left_motor ? "true" : "false") << ","
          << "\"invert_right\":" << (cfg.invert_right_motor ? "true" : "false") << ","
          << "\"turn_max_pwm\":" << cfg.teensy_turn_max_pwm << ","
          << "\"invert_turn\":" << (cfg.invert_turn_motor ? "true" : "false") << ","
          << "\"turn_slowdown\":" << cfg.teensy_turn_slowdown << ","
          << "\"turn_ramp_sec\":" << cfg.teensy_turn_ramp_sec
          << "}";
        webui.setLatestTune(t.str());
    }

    // ---- Load modules -----------------------------------------
    auto modules = ModuleRegistry::createAll();
    {
        ModuleContext mctx;
        mctx.broadcast = [&](const std::string& j) { broadcastAll(ws, webuiPtr, j); };
        mctx.teensy = &teensy;
        mctx.gpio   = &gpio;
        for (auto& mod : modules) {
            auto conf = loadModuleConf("/etc/rover/modules/" + std::string(mod->name()) + ".conf");
            if (!mod->onLoad(conf, mctx))
                std::cerr << "[modules] " << mod->name() << " failed to load\n";
        }
    }

    // ---- Wire up Teensy reconnect → re-push fw config + disarm ----
    teensy.onReconnect([&]() {
        if (cfg.teensy_push_fw_config) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            teensy.sendRaw(teensyFwConfigCommand(cfg));
            std::cout << "[main] re-pushed teensy fw config after reconnect\n";
        }
        teensy.sendRaw("{\"cmd\":\"disarm\"}");
        {
            std::lock_guard<std::mutex> ck(controlMtx);
            control.started = false;
            control.estopLatched = false;
        }
    });

    // ---- Wire up sensor data → telemetry broadcast --------
    teensy.onRawLine([&](const std::string& line) {
        broadcastAll(ws, webuiPtr, line);
    });
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
        std::string json = buildTelemetry(s, gpio, started, estopLatched, precheckOk, teensy.isOpen());
        broadcastAll(ws, webuiPtr, json);
        webui.setLatestStatus(json);
    });

    // ---- Command handler shared by WiFi and WebUI ---

    auto cmdHandler = [&](const std::string& json) {
        if (dispatchCommand(json, teensy, gpio, ota, ws, webuiPtr, cfg,
                            cam0, cam1, power, powerMtx, control, controlMtx))
            return;
        // Let modules handle unrecognised commands
        std::string type = jsonStr(json, "type");
        for (auto& mod : modules) {
            if (mod->onCommand(type, json)) return;
        }
        std::cerr << "[main] unknown command type: " << type << "\n";
    };
    ws.onMessage(cmdHandler);
    webui.onMessage(cmdHandler);

    // ---- Start servers ------------
    ws.start();
    webui.start();

    // ---- Telemetry request loop ---------------------------
    int telPeriodMs = (cfg.telemetry_hz > 0) ? (1000 / cfg.telemetry_hz) : 100;
    if (telPeriodMs < 20) telPeriodMs = 20;
    std::cout << "[main] All systems running. Press Ctrl+C to stop.\n";
    std::cout << "[main] Web UI:    http://<ip>:" << cfg.webui_port << "\n";
    std::cout << "[main] Cameras:   http://<ip>:" << cfg.cam0_port
              << "  http://<ip>:" << cfg.cam1_port << "\n";
    std::cout << "[main] WebSocket: ws://<ip>:" << cfg.ws_port << "\n";
    sd_notify(0, "READY=1");  // tell systemd we're up — start watchdog clock

    // Keep systemd watchdog independent from telemetry/serial work.
    std::thread watchdogThread([]() {
        while (gRunning) {
            sd_notify(0, "WATCHDOG=1");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    while (gRunning) {
        bool sleeping = false;
        {
            std::lock_guard<std::mutex> lk(powerMtx);
            sleeping = power.sleeping;
        }
        if (!sleeping) {
            if (teensy.isOpen()) {
                teensy.requestSensors();
            } else {
                // Teensy offline — still broadcast a status frame so
                // the WebUI knows the link is down.
                bool started, el;
                {
                    std::lock_guard<std::mutex> ck(controlMtx);
                    started = control.started;
                    el = control.estopLatched;
                }
                std::string json = buildTelemetry(
                    TeensySensors{}, gpio, started, el, false, false);
                broadcastAll(ws, webuiPtr, json);
                webui.setLatestStatus(json);
            }
        }
        for (auto& mod : modules) mod->onTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(telPeriodMs));
    }

    if (watchdogThread.joinable()) watchdogThread.join();

    std::cout << "\n[main] Shutting down...\n";
    for (auto& mod : modules) mod->onShutdown();
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
