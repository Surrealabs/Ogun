#pragma once
// ============================================================
//  TeensyOta — receive firmware chunks and flash the Teensy
//  via teensy_loader_cli (or avrdude for Arduino-compatible)
// ============================================================
#include <string>
#include <vector>
#include <functional>
#include <atomic>

class TeensyOta {
public:
    using ProgressCb = std::function<void(int percent, const std::string& msg)>;

    TeensyOta(const std::string& workDir,
              const std::string& flashCmd,
              const std::string& mmcu);

    // Begin a new OTA session (totalChunks is the expected chunk count)
    bool begin(int totalChunks);

    // Feed a base64-encoded chunk.  Returns false on bad input.
    bool addChunk(int index, const std::string& base64Data);

    // When all chunks are received, assemble and flash.
    // Callback is called with progress [0-100] and status messages.
    bool flash(ProgressCb cb);

    // Abort and clean up temp files
    void abort();

    bool isActive()   const { return active_; }
    int  totalChunks() const { return totalChunks_; }
    int  rxChunks()    const { return rxChunks_; }

private:
    std::string assembleHex();
    static std::string base64Decode(const std::string& in);

    std::string              workDir_;
    std::string              flashCmd_;
    std::string              mmcu_;
    std::atomic<bool>        active_{false};
    int                      totalChunks_{0};
    int                      rxChunks_{0};
    std::vector<std::string> chunks_;   // decoded binary per chunk
};
