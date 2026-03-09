#include "TeensyOta.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <array>
#include <stdexcept>
namespace fs = std::filesystem;

TeensyOta::TeensyOta(const std::string& workDir,
                     const std::string& flashCmd,
                     const std::string& mmcu)
    : workDir_(workDir), flashCmd_(flashCmd), mmcu_(mmcu) {}

bool TeensyOta::begin(int totalChunks) {
    if (active_) abort();
    totalChunks_ = totalChunks;
    rxChunks_    = 0;
    chunks_.assign(totalChunks, "");
    active_      = true;
    fs::create_directories(workDir_);
    std::cout << "[ota] session started, expecting " << totalChunks << " chunks\n";
    return true;
}

bool TeensyOta::addChunk(int index, const std::string& base64Data) {
    if (!active_ || index < 0 || index >= totalChunks_) return false;
    if (!chunks_[index].empty()) return true; // already got it
    chunks_[index] = base64Decode(base64Data);
    rxChunks_++;
    return true;
}

bool TeensyOta::flash(ProgressCb cb) {
    if (!active_ || rxChunks_ != totalChunks_) {
        if (cb) cb(0, "Not all chunks received");
        return false;
    }
    // Assemble .hex file
    std::string hexPath = workDir_ + "/firmware.hex";
    {
        std::ofstream out(hexPath, std::ios::binary);
        for (auto& chunk : chunks_) out.write(chunk.data(), chunk.size());
    }
    if (cb) cb(10, "Firmware assembled");

    // Build flash command. -s enables soft reboot for Teensy 3.x/4.x.
    std::string cmd;
    if (flashCmd_.find("teensy_loader_cli") != std::string::npos) {
        cmd = flashCmd_ + " --mcu=" + mmcu_ +
              " -w -s -v \"" + hexPath + "\" 2>&1";
    } else {
        cmd = flashCmd_ + " --mcu=" + mmcu_ +
              " -w -v \"" + hexPath + "\" 2>&1";
    }
    if (cb) cb(20, "Flashing Teensy...");

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (cb) cb(0, "popen failed");
        return false;
    }
    std::array<char, 256> buf;
    std::string output;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
    }
    int rc = pclose(pipe);

    if (rc == 0) {
        if (cb) cb(100, "Flash complete");
        std::cout << "[ota] flash successful\n";
    } else {
        if (cb) cb(0, "Flash failed: " + output);
        std::cerr << "[ota] flash failed: " << output << "\n";
    }
    active_ = false;
    return rc == 0;
}

void TeensyOta::abort() {
    active_  = false;
    rxChunks_ = 0;
    chunks_.clear();
}

// ---- Minimal Base64 decode ---------------------------------
static const std::string B64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string TeensyOta::base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)B64_CHARS[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}
