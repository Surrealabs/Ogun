#pragma once
// ============================================================
//  CameraStream — V4L2 capture + HTTP MJPEG server
//  Each instance manages one camera device on its own port.
// ============================================================
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <set>

class CameraStream {
public:
    CameraStream(const std::string& device,
                 uint16_t httpPort,
                 int width, int height,
                 int fps, int jpegQuality);
    ~CameraStream();

    bool start();
    void stop();
    bool isRunning() const { return running_; }

    // Snapshot: returns latest JPEG bytes (thread-safe)
    std::vector<uint8_t> latestFrame() const;

private:
    void captureLoop();   // V4L2 capture → internal JPEG buffer
    void httpLoop();      // accepts TCP connections, streams MJPEG

    bool openV4L2();
    void closeV4L2();
    std::vector<uint8_t> captureJpeg();   // returns one JPEG frame

    // HTTP MJPEG helpers
    void handleClient(int clientFd);

    std::string  device_;
    uint16_t     httpPort_;
    int          width_, height_, fps_, jpegQuality_;

    int          v4l2Fd_{-1};
    bool         sourceIsMjpeg_{false};
    void*        mapBuf_[4]{};   // mmap buffers
    size_t       mapLen_[4]{};
    int          bufCount_{0};

    std::atomic<bool>        running_{false};
    std::thread              captureThread_;
    std::thread              httpThread_;

    mutable std::mutex       frameMtx_;
    std::vector<uint8_t>     latestJpeg_;

    int serverFd_{-1};
    std::mutex               clientsMtx_;
    std::set<int>            clients_;   // active client FDs
};
