#pragma once

#include <linux/videodev2.h>
#include <string>
#include <vector>

namespace camera {

struct CaptureFrame {
    const uint8_t* data;
    const uint8_t* uv;
    size_t size;
    size_t uvSize;
    int width;
    int height;
    int yStride;
    int uvStride;
    int nplanes;
    int index;
};

class Capture {
public:
    Capture();
    ~Capture();

    bool open(const std::string& device, int width, int height);
    void close();
    bool started() const { return fd_ >= 0 && streaming_; }
    bool grab(CaptureFrame* frame, int timeoutMs = 200);
    bool release(int index);

    std::string device() const { return device_; }
    int width() const { return width_; }
    int height() const { return height_; }
    std::string lastError() const { return error_; }
    static std::vector<std::string> listDevices();

private:
    struct PlaneMap {
        void* start;
        size_t length;
    };
    struct Buffer {
        PlaneMap planes[VIDEO_MAX_PLANES];
        int nplanes;
    };

    int fd_;
    bool streaming_;
    bool mplane_;
    int bufType_;
    int nplanes_;
    int width_;
    int height_;
    int yStride_;
    int uvStride_;
    std::string device_;
    std::string error_;
    std::vector<Buffer> buffers_;
};

}  // namespace camera
