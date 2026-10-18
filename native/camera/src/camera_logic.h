#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace camera {

enum CameraState {
    CamIdle = 0,
    CamStartingPreview,
    CamPreviewing,
    CamStartingRecording,
    CamRecording,
    CamStoppingRecording,
    CamViewingMedia,
    CamStopping,
    CamError,
    CamReleased
};

const char* cameraStateName(CameraState state);
bool cameraTransitionOk(CameraState from, CameraState to);

struct CropRect {
    int left;
    int top;
    int width;
    int height;
    bool applied;
};

CropRect aspectCrop(int boundsW, int boundsH, int outW, int outH);
CropRect mapPanelRect(int x, int y, int w, int h, int panelW, int panelH,
    int crtcW, int crtcH, int rotation);
double rotatedAspect(int width, int height, int rotation);
CropRect fitPhysicalViewport(const CropRect& hole, int width, int height, int rotation,
    bool cover, double zoom = 1, double centerX = 0.5, double centerY = 0.5);
CropRect zoomCrop(int width, int height, double zoom);
bool zoomNv12(const uint8_t* src, int width, int height, int stride, double zoom,
    std::vector<uint8_t>* dst, int* outStride);
int h264LevelForFrame(int width, int height, int fps);
bool recordFrameDue(uint64_t nowNs, int fps, uint64_t* nextNs);
bool resampleNv12Viewport(const uint8_t* y, const uint8_t* uv, int width, int height,
    int yStride, int uvStride, uint8_t* dstY, uint8_t* dstUv, int dstW, int dstH, int pitch,
    int imageX, int imageY, int imageW, int imageH, int rotation);
bool resampleRgbaViewport(const uint8_t* src, int width, int height, uint8_t* dst,
    int dstW, int dstH, int imageX, int imageY, int imageW, int imageH, int rotation);

struct SensorPoint {
    int x;
    int y;
    bool inside;
};

SensorPoint mapUiToSensor(double nx, double ny, int cropLeft, int cropTop, int cropW, int cropH,
    int rotation, bool mirrorX, bool cover, double holeAspect, double imageAspect);
SensorPoint mapHolePointToSensor(double nx, double ny, int width, int height,
    int direction, int correction, bool cover, double physicalHoleAspect);

struct RecordSession {
    bool recording;
    bool threadRunning;
    bool encoderOpen;
    bool muxerOpen;
    bool partExists;
    const char* code;
};

bool commitRecordThread(RecordSession* session, bool threadStarted);

const char* storageCodeForErrno(int err);
bool chunkOffsetsNeedCo64(uint64_t maxOffset);
bool mdatSizeNeeds64(uint64_t dataBytes);

std::string formatUniqueMediaName(const char* prefix, const char* ext, int64_t sec, int ms, int counter);
bool mediaFileNameSafe(const std::string& name);

bool rotateNv12CW90(const uint8_t* src, int width, int height, int yStride,
    std::vector<uint8_t>* dst, int* outW, int* outH, int* outStride);
bool rotateNv12QuarterTurn(const uint8_t* src, int width, int height, int yStride, int rotate,
    std::vector<uint8_t>* dst, int* outW, int* outH, int* outStride);
bool rotateNv12(const uint8_t* src, int width, int height, int yStride, int rotate,
    std::vector<uint8_t>* dst, int* outW, int* outH, int* outStride);
bool scaleNv12(const uint8_t* src, int srcW, int srcH, int srcStride,
    std::vector<uint8_t>* dst, int dstW, int dstH, int* outStride);
bool nv12BytesFit(size_t bytes, int width, int height, int yStride);
bool packNv12Planes(const uint8_t* yPlane, const uint8_t* uvPlane, int width, int height,
    int yStride, int uvStride, std::vector<uint8_t>* packed);

// Byte layouts, not FourCC names: RGBA -> BGRA (DRM ARGB8888 little-endian).
// Supports in-place conversion; keeps green and alpha unchanged.
void rgbaToBgra(const uint8_t* src, uint8_t* dst, size_t pixels);

bool finalizePartFile(bool writeOk, const std::string& partPath, const std::string& finalPath, int* errOut);

}  // namespace camera
