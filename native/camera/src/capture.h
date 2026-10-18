#pragma once

#include <linux/videodev2.h>
#include <string>
#include <vector>

namespace camera {

struct V4l2Control {
    int id;
    std::string name;
    std::string type;
    int min;
    int max;
    int step;
    int def;
    int value;
};

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
    bool openControls();
    void close();
    bool started() const { return fd_ >= 0 && streaming_; }
    bool grab(CaptureFrame* frame, int timeoutMs = 200);
    bool release(int index);
    bool setControl(int id, int value);
    bool getControl(int id, int* value);
    bool queryControls();
    bool setFocusPoint(float nx, float ny);
    bool focusRegionSupported() const { return focusRegion_; }

    std::string device() const { return device_; }
    int width() const { return width_; }
    int height() const { return height_; }
    std::string lastError() const { return error_; }
    const std::vector<V4l2Control>& controls() const { return controls_; }
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
    bool focusRegion_;
    std::string device_;
    std::string error_;
    std::vector<Buffer> buffers_;
    std::vector<V4l2Control> controls_;
};

}  // namespace camera
