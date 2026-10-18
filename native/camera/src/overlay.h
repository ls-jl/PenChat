#pragma once

#include <cstdint>
#include <pthread.h>
#include <string>
#include <vector>

namespace camera {

struct HoleRect {
    int x;
    int y;
    int w;
    int h;
    int panelW;
    int panelH;
    int rotation; // Framework screen.direction; maps logical rects and displayed pixels.
    int turn;     // Independent optical correction, zero for already-corrected media.
    int fit;
    int offsetX;
    int offsetY;
    HoleRect(int px = 0, int py = 0, int pw = 0, int ph = 0, int panelWidth = 960, int panelHeight = 266,
        int direction = 0, int correction = 0, int mode = 0, int ox = 0, int oy = 0)
        : x(px), y(py), w(pw), h(ph), panelW(panelWidth), panelH(panelHeight), rotation(direction), turn(correction), fit(mode), offsetX(ox), offsetY(oy) {}
};

class Overlay {
public:
    Overlay();
    ~Overlay();

    bool open();
    void close();
    bool ready() const { return fd_ >= 0 && planeId_ != 0; }
    bool nv12() const { return useNv12_; }
    uint32_t planeId() const { return planeId_; }
    uint32_t zpos() const { return zpos_; }
    int crtcWidth() const { return crtcW_; }
    int crtcHeight() const { return crtcH_; }
    std::string lastError() const { return error_; }

    void setHole(const HoleRect& hole);
    void setViewport(double zoom, double centerX = 0.5, double centerY = 0.5);
    HoleRect hole() const { return hole_; }
    void mapToCrtc(int* dx, int* dy, int* dw, int* dh) const;

    bool presentNv12(const uint8_t* y, int width, int height, int yStride, int uvStride,
        const uint8_t* uvPlane = 0);
    // Decoded/stb pixels are four-byte RGBA; only the DRM upload converts to BGRA.
    bool presentRgba(const uint8_t* rgba, int width, int height);
    void disable();

private:
    void mapRectToCrtc(int x, int y, int w, int h, int* dx, int* dy, int* dw, int* dh, bool clamp) const;
    int imageRotation() const;
    void fitSourceToCrtc(int srcW, int srcH, int* dx, int* dy, int* dw, int* dh) const;
    bool commitPlacedBgra(const uint8_t* bgra, int cw, int ch, int cx, int cy);
    struct DrmBuf {
        uint32_t handle;
        uint32_t fbId;
        uint32_t pitch;
        uint32_t height;
        size_t size;
        void* map;
        int primeFd;
        uint32_t format;
    };

    bool loadDrm();
    bool pickResources();
    bool allocBuf(DrmBuf* buf, int width, int height, uint32_t format);
    void freeBuf(DrmBuf* buf);
    bool commit(uint32_t fbId, int x, int y, int w, int h, int srcW, int srcH);
    uint32_t findProp(uint32_t objId, uint32_t objType, const char* name, uint64_t* value = 0);
    bool addProp(void* req, uint32_t objId, uint32_t propId, uint64_t value);

    int fd_;
    void* drm_;
    uint32_t crtcId_;
    uint32_t connId_;
    uint32_t planeId_;
    uint32_t zpos_;
    uint32_t minZpos_;
    uint32_t maxZpos_;
    int crtcW_;
    int crtcH_;
    bool useNv12_;
    bool atomic_;
    bool enabled_;
    HoleRect hole_;
    double viewZoom_ = 1, viewCenterX_ = 0.5, viewCenterY_ = 0.5;
    DrmBuf bufs_[2];
    int cur_;
    std::string error_;
    std::vector<uint8_t> scratch_;
    pthread_mutex_t mu_;
    uint32_t propFb_;
    uint32_t propCrtc_;
    uint32_t propSrcX_;
    uint32_t propSrcY_;
    uint32_t propSrcW_;
    uint32_t propSrcH_;
    uint32_t propCrtcX_;
    uint32_t propCrtcY_;
    uint32_t propCrtcW_;
    uint32_t propCrtcH_;
    uint32_t propZpos_;
};

void nv12ToBgra(const uint8_t* yPlane, const uint8_t* uvPlane, int width, int height,
    int yStride, int uvStride, uint8_t* dst, int dstW, int dstH, int rotate);
void rgbaContain(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH,
    int* outX, int* outY, int* outW, int* outH);

}  // namespace camera
