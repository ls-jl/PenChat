#include "camera_logic.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cmath>

namespace camera {
void rgbaToBgra(const uint8_t* src, uint8_t* dst, size_t pixels)
{
    if (!src || !dst) return;
    for (size_t i = 0; i < pixels; ++i, src += 4, dst += 4) {
        const uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = a;
    }
}

namespace {

int evenDown(int value)
{
    if (value < 2) return 2;
    return value & ~1;
}

void clamp01(double* v)
{
    if (*v < 0) *v = 0;
    if (*v > 1) *v = 1;
}

}  // namespace

const char* cameraStateName(CameraState state)
{
    switch (state) {
    case CamIdle: return "Idle";
    case CamStartingPreview: return "StartingPreview";
    case CamPreviewing: return "Previewing";
    case CamStartingRecording: return "StartingRecording";
    case CamRecording: return "Recording";
    case CamStoppingRecording: return "StoppingRecording";
    case CamViewingMedia: return "ViewingMedia";
    case CamStopping: return "Stopping";
    case CamError: return "Error";
    case CamReleased: return "Released";
    }
    return "Error";
}

CropRect mapPanelRect(int x, int y, int w, int h, int panelW, int panelH,
    int crtcW, int crtcH, int rotation)
{
    CropRect r = {x, y, w, h, false};
    if (panelW <= 0 || panelH <= 0 || crtcW <= 0 || crtcH <= 0 || w <= 0 || h <= 0) return r;
    const int rot = (rotation % 360 + 360) % 360;
    if (rot == 90) r = {panelH - y - h, x, h, w, false};
    else if (rot == 270) r = {y, panelW - x - w, h, w, false};
    else if (rot == 180) r = {panelW - x - w, panelH - y - h, w, h, false};
    const int rw = rot == 90 || rot == 270 ? panelH : panelW;
    const int rh = rot == 90 || rot == 270 ? panelW : panelH;
    r.left = static_cast<int>(static_cast<int64_t>(r.left) * crtcW / rw);
    r.top = static_cast<int>(static_cast<int64_t>(r.top) * crtcH / rh);
    r.width = static_cast<int>(static_cast<int64_t>(r.width) * crtcW / rw);
    r.height = static_cast<int>(static_cast<int64_t>(r.height) * crtcH / rh);
    r.applied = true;
    return r;
}

double rotatedAspect(int width, int height, int rotation)
{
    if (width <= 0 || height <= 0) return 1;
    const int rot = (rotation % 360 + 360) % 360;
    return rot == 90 || rot == 270 ? static_cast<double>(height) / width : static_cast<double>(width) / height;
}

int h264LevelForFrame(int width, int height, int fps)
{
    const int64_t blocks = ((static_cast<int64_t>(width) + 15) / 16) * ((static_cast<int64_t>(height) + 15) / 16);
    const int64_t rate = blocks * (fps > 0 ? fps : 30);
    if (blocks <= 3600 && rate <= 108000) return 31;
    if (blocks <= 8192 && rate <= 245760) return 40;
    if (blocks <= 8704 && rate <= 522240) return 42;
    return 51;
}

CropRect fitPhysicalViewport(const CropRect& hole, int width, int height, int rotation,
    bool cover, double zoom, double centerX, double centerY)
{
    CropRect out = hole;
    if (hole.width < 1 || hole.height < 1 || width < 1 || height < 1) return out;
    const bool quarter = rotation % 180 != 0;
    const double sw = quarter ? height : width, sh = quarter ? width : height;
    const double sx = hole.width / sw, sy = hole.height / sh;
    const double factor = (cover ? std::max(sx, sy) : std::min(sx, sy)) * zoom;
    out.width = std::max(1, static_cast<int>(sw * factor + 0.5));
    out.height = std::max(1, static_cast<int>(sh * factor + 0.5));
    // Centers are expressed in unrotated source coordinates, not CRTC coordinates.
    double x = centerX, y = centerY;
    const int rot = (rotation % 360 + 360) % 360;
    if (rot == 90) { x = 1 - centerY; y = centerX; }
    else if (rot == 180) { x = 1 - centerX; y = 1 - centerY; }
    else if (rot == 270) { x = centerY; y = 1 - centerX; }
    out.left = hole.left + static_cast<int>(hole.width / 2.0 - x * out.width);
    out.top = hole.top + static_cast<int>(hole.height / 2.0 - y * out.height);
    out.left = out.width <= hole.width ? hole.left + (hole.width - out.width) / 2
        : std::max(hole.left + hole.width - out.width, std::min(hole.left, out.left));
    out.top = out.height <= hole.height ? hole.top + (hole.height - out.height) / 2
        : std::max(hole.top + hole.height - out.height, std::min(hole.top, out.top));
    return out;
}

CropRect zoomCrop(int width, int height, double zoom)
{
    const double safe = std::isfinite(zoom) ? std::max(1.0, std::min(4.0, zoom)) : 1;
    const int w = std::max(2, static_cast<int>(width / safe) & ~1);
    const int h = std::max(2, static_cast<int>(height / safe) & ~1);
    const CropRect rect = {((width - w) / 2) & ~1, ((height - h) / 2) & ~1, w, h, true};
    return rect;
}

bool zoomNv12(const uint8_t* src, int width, int height, int stride, double zoom,
    std::vector<uint8_t>* dst, int* outStride)
{
    if (!src || !dst || width < 2 || height < 2 || (width | height) & 1 || stride < width) return false;
    const CropRect r = zoomCrop(width, height, zoom);
    std::vector<uint8_t> cropped;
    if (!packNv12Planes(src + r.top * stride + r.left,
            src + stride * height + (r.top / 2) * stride + r.left,
            r.width, r.height, stride, stride, &cropped)) return false;
    return scaleNv12(cropped.data(), r.width, r.height, r.width, dst, width, height, outStride);
}

bool recordFrameDue(uint64_t nowNs, int fps, uint64_t* nextNs)
{
    if (!nextNs) return false;
    const uint64_t interval = std::max<uint64_t>(1, 1000000000ull / (fps > 0 ? fps : 15));
    const uint64_t tolerance = std::min<uint64_t>(2000000, interval / 8);
    if (!*nextNs) *nextNs = nowNs;
    if (nowNs < *nextNs && *nextNs - nowNs > tolerance) return false;
    *nextNs += interval;
    // Preserve phase across normal capture jitter. Reset only after a genuine stall,
    // not merely because a frame arrived a fraction of a millisecond late.
    if (nowNs > *nextNs && nowNs - *nextNs > interval) *nextNs = nowNs + interval;
    return true;
}

bool resampleNv12Viewport(const uint8_t* y, const uint8_t* uv, int width, int height,
    int yStride, int uvStride, uint8_t* dstY, uint8_t* dstUv, int dstW, int dstH, int pitch,
    int imageX, int imageY, int imageW, int imageH, int rotation)
{
    if (!y || !uv || !dstY || !dstUv || width < 2 || height < 2 || (width | height | dstW | dstH) & 1 ||
        dstW < 2 || dstH < 2 || yStride < width || uvStride < width || pitch < dstW || imageW < 1 || imageH < 1) return false;
    const int rot = (rotation % 360 + 360) % 360;
    const bool quarter = rot == 90 || rot == 270;
    std::vector<int> xm(dstW, -1), ym(dstH, -1);
    for (int dx = 0; dx < dstW; ++dx) {
        int local = dx - imageX;
        if (local < 0 || local >= imageW) continue;
        if (rot == 90 || rot == 180) local = imageW - 1 - local;
        xm[dx] = local * (quarter ? height : width) / imageW;
    }
    for (int dy = 0; dy < dstH; ++dy) {
        int local = dy - imageY;
        if (local < 0 || local >= imageH) continue;
        if (rot == 270 || rot == 180) local = imageH - 1 - local;
        ym[dy] = local * (quarter ? width : height) / imageH;
    }
    memset(dstY, 0, static_cast<size_t>(pitch) * dstH);
    memset(dstUv, 128, static_cast<size_t>(pitch) * (dstH / 2));
    for (int ty = 0; ty < dstH; ty += 16) {
        for (int tx = 0; tx < dstW; tx += 16) {
            const int bottom = std::min(dstH, ty + 16), right = std::min(dstW, tx + 16);
            for (int dy = ty; dy < bottom; ++dy) {
                if (ym[dy] < 0) continue;
                uint8_t* row = dstY + static_cast<size_t>(dy) * pitch;
                for (int dx = tx; dx < right; ++dx) {
                    if (xm[dx] < 0) continue;
                    const int sx = quarter ? ym[dy] : xm[dx];
                    const int sy = quarter ? xm[dx] : ym[dy];
                    row[dx] = y[static_cast<size_t>(sy) * yStride + sx];
                    if (((dx | dy) & 1) == 0) {
                        uint8_t* dp = dstUv + static_cast<size_t>(dy / 2) * pitch + dx;
                        const uint8_t* sp = uv + static_cast<size_t>(sy / 2) * uvStride + (sx & ~1);
                        dp[0] = sp[0]; dp[1] = sp[1];
                    }
                }
            }
        }
    }
    return true;
}

bool cameraTransitionOk(CameraState from, CameraState to)
{
    if (to == CamReleased) return true;
    if (to == CamStopping) return from != CamReleased;
    if (from == to) {
        return from == CamIdle || from == CamPreviewing || from == CamRecording ||
            from == CamViewingMedia || from == CamError || from == CamReleased;
    }
    switch (from) {
    case CamIdle:
        return to == CamStartingPreview || to == CamViewingMedia || to == CamError;
    case CamStartingPreview:
        return to == CamPreviewing || to == CamError || to == CamIdle;
    case CamPreviewing:
        return to == CamStartingPreview || to == CamStartingRecording || to == CamViewingMedia ||
            to == CamIdle || to == CamError;
    case CamStartingRecording:
        return to == CamRecording || to == CamPreviewing || to == CamIdle || to == CamError;
    case CamRecording:
        return to == CamStoppingRecording || to == CamError;
    case CamStoppingRecording:
        return to == CamPreviewing || to == CamIdle || to == CamError;
    case CamViewingMedia:
        return to == CamIdle || to == CamPreviewing || to == CamStartingPreview || to == CamError;
    case CamStopping:
        return to == CamIdle || to == CamError;
    case CamError:
        return to == CamIdle || to == CamStartingPreview || to == CamPreviewing || to == CamViewingMedia;
    case CamReleased:
        return to == CamIdle || to == CamStartingPreview;
    }
    return false;
}

CropRect aspectCrop(int boundsW, int boundsH, int outW, int outH)
{
    CropRect crop;
    crop.left = 0;
    crop.top = 0;
    crop.width = 0;
    crop.height = 0;
    crop.applied = false;
    if (boundsW < 2 || boundsH < 2 || outW < 2 || outH < 2) return crop;
    int cw = 0;
    int ch = 0;
    if (static_cast<int64_t>(boundsW) * outH > static_cast<int64_t>(boundsH) * outW) {
        ch = evenDown(boundsH);
        cw = evenDown(static_cast<int>(static_cast<int64_t>(ch) * outW / outH));
    } else {
        cw = evenDown(boundsW);
        ch = evenDown(static_cast<int>(static_cast<int64_t>(cw) * outH / outW));
    }
    if (cw > boundsW) cw = evenDown(boundsW);
    if (ch > boundsH) ch = evenDown(boundsH);
    if (cw > boundsW) cw = boundsW & ~1;
    if (ch > boundsH) ch = boundsH & ~1;
    if (cw < 2 || ch < 2) return crop;
    crop.width = cw;
    crop.height = ch;
    crop.left = ((boundsW - cw) / 2) & ~1;
    crop.top = ((boundsH - ch) / 2) & ~1;
    if (crop.left < 0) crop.left = 0;
    if (crop.top < 0) crop.top = 0;
    crop.applied = true;
    return crop;
}

bool resampleRgbaViewport(const uint8_t* src, int width, int height, uint8_t* dst,
    int dstW, int dstH, int imageX, int imageY, int imageW, int imageH, int rotation)
{
    if (!src || !dst || width < 1 || height < 1 || dstW < 1 || dstH < 1 || imageW < 1 || imageH < 1) return false;
    memset(dst, 0, static_cast<size_t>(dstW) * dstH * 4);
    const int rot = (rotation % 360 + 360) % 360;
    const bool quarter = rot == 90 || rot == 270;
    std::vector<int> xs(dstW, -1), ys(dstH, -1);
    for (int x = 0; x < dstW; ++x) {
        int local = x - imageX;
        if (local < 0 || local >= imageW) continue;
        if (rot == 90 || rot == 180) local = imageW - 1 - local;
        xs[x] = static_cast<int>(static_cast<int64_t>(local) * (quarter ? height : width) / imageW);
    }
    for (int y = 0; y < dstH; ++y) {
        int local = y - imageY;
        if (local < 0 || local >= imageH) continue;
        if (rot == 270 || rot == 180) local = imageH - 1 - local;
        ys[y] = static_cast<int>(static_cast<int64_t>(local) * (quarter ? width : height) / imageH);
    }
    for (int ty = 0; ty < dstH; ty += 16) for (int tx = 0; tx < dstW; tx += 16)
        for (int y = ty; y < std::min(dstH, ty + 16); ++y) for (int x = tx; x < std::min(dstW, tx + 16); ++x) {
            if (xs[x] < 0 || ys[y] < 0) continue;
            const int sx = quarter ? ys[y] : xs[x], sy = quarter ? xs[x] : ys[y];
            memcpy(dst + (static_cast<size_t>(y) * dstW + x) * 4, src + (static_cast<size_t>(sy) * width + sx) * 4, 4);
        }
    return true;
}

SensorPoint mapHolePointToSensor(double nx, double ny, int width, int height,
    int direction, int correction, bool cover, double physicalHoleAspect)
{
    const SensorPoint outside = {0, 0, false};
    if (!std::isfinite(nx) || !std::isfinite(ny) || nx < 0 || nx > 1 || ny < 0 || ny > 1) return outside;
    direction = (direction % 360 + 360) % 360;
    double px = nx, py = ny;
    if (direction == 90) { px = 1.0 - ny; py = nx; }
    else if (direction == 180) { px = 1.0 - nx; py = 1.0 - ny; }
    else if (direction == 270) { px = ny; py = 1.0 - nx; }
    const int angle = (direction + correction + 360) % 360;
    return mapUiToSensor(px, py, 0, 0, width, height, angle, false, cover,
        physicalHoleAspect, rotatedAspect(width, height, angle));
}

SensorPoint mapUiToSensor(double nx, double ny, int cropLeft, int cropTop, int cropW, int cropH,
    int rotation, bool mirrorX, bool cover, double holeAspect, double imageAspect)
{
    SensorPoint point;
    point.x = 0;
    point.y = 0;
    point.inside = false;
    if (cropW < 2 || cropH < 2 || !std::isfinite(nx) || !std::isfinite(ny) ||
        nx < 0 || nx > 1 || ny < 0 || ny > 1 ||
        !std::isfinite(holeAspect) || !std::isfinite(imageAspect) || holeAspect <= 0 || imageAspect <= 0) return point;
    double ix = nx;
    double iy = ny;
    if (!cover) {
        if (imageAspect > holeAspect) {
            const double hFrac = holeAspect / imageAspect;
            const double y0 = (1.0 - hFrac) / 2.0;
            if (ny < y0 || ny > y0 + hFrac) return point;
            iy = (ny - y0) / hFrac;
        } else {
            const double wFrac = imageAspect / holeAspect;
            const double x0 = (1.0 - wFrac) / 2.0;
            if (nx < x0 || nx > x0 + wFrac) return point;
            ix = (nx - x0) / wFrac;
        }
    } else if (imageAspect > holeAspect) {
        const double wFrac = holeAspect / imageAspect;
        const double x0 = (1.0 - wFrac) / 2.0;
        ix = x0 + nx * wFrac;
    } else {
        const double hFrac = imageAspect / holeAspect;
        const double y0 = (1.0 - hFrac) / 2.0;
        iy = y0 + ny * hFrac;
    }
    clamp01(&ix);
    clamp01(&iy);
    if (mirrorX) ix = 1.0 - ix;
    double sx = ix;
    double sy = iy;
    const int rot = ((rotation % 360) + 360) % 360;
    if (rot == 90) {
        sx = iy;
        sy = 1.0 - ix;
    } else if (rot == 180) {
        sx = 1.0 - ix;
        sy = 1.0 - iy;
    } else if (rot == 270) {
        sx = 1.0 - iy;
        sy = ix;
    }
    point.x = cropLeft + static_cast<int>(sx * (cropW - 1));
    point.y = cropTop + static_cast<int>(sy * (cropH - 1));
    if (point.x < cropLeft) point.x = cropLeft;
    if (point.y < cropTop) point.y = cropTop;
    if (point.x >= cropLeft + cropW) point.x = cropLeft + cropW - 1;
    if (point.y >= cropTop + cropH) point.y = cropTop + cropH - 1;
    point.inside = true;
    return point;
}

bool commitRecordThread(RecordSession* session, bool threadStarted)
{
    if (!session) return false;
    if (threadStarted) {
        session->recording = true;
        session->threadRunning = true;
        session->code = "";
        return true;
    }
    session->recording = false;
    session->threadRunning = false;
    session->encoderOpen = false;
    session->muxerOpen = false;
    session->partExists = false;
    session->code = "ENCODER_START_FAILED";
    return false;
}

const char* storageCodeForErrno(int err)
{
    if (err == ENOSPC) return "STORAGE_FULL";
    if (err == EACCES || err == EPERM) return "STORAGE_PERMISSION_DENIED";
    if (err == EIO || err == EROFS) return "STORAGE_IO_ERROR";
    return "INTERNAL_ERROR";
}

bool chunkOffsetsNeedCo64(uint64_t maxOffset)
{
    return maxOffset > 0xffffffffull;
}

bool mdatSizeNeeds64(uint64_t dataBytes)
{
    return dataBytes > (0xffffffffull - 8ull);
}

std::string formatUniqueMediaName(const char* prefix, const char* ext, int64_t sec, int ms, int counter)
{
    const char* p = prefix && prefix[0] ? prefix : "IMG";
    const char* e = ext && ext[0] ? ext : "jpg";
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    if (counter < 0) counter = 0;
    const time_t t = static_cast<time_t>(sec);
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    localtime_r(&t, &tm);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s_%04d%02d%02d_%02d%02d%02d_%03d_%04d.%s",
        p, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
        ms, counter % 10000, e);
    return buf;
}

bool mediaFileNameSafe(const std::string& name)
{
    if (name.empty() || name == "." || name == ".." || name.size() > 240) return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    if (name.find("..") != std::string::npos) return false;
    if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".part") == 0) return false;
    return true;
}

bool rotateNv12QuarterTurn(const uint8_t* src, int width, int height, int yStride, int rotate,
    std::vector<uint8_t>* dst, int* outW, int* outH, int* outStride)
{
    if (rotate != 90 && rotate != 270) return false;
    const bool clockwise = rotate == 90;
    if (!src || !dst || width < 2 || height < 2 || (width & 1) || (height & 1)) return false;
    if (yStride < width) return false;
    const size_t need = static_cast<size_t>(yStride) * static_cast<size_t>(height) * 3 / 2;
    (void)need;
    const int dw = height;
    const int dh = width;
    dst->resize(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 3 / 2);
    uint8_t* dy = &(*dst)[0];
    uint8_t* duv = dy + static_cast<size_t>(dw) * static_cast<size_t>(dh);
    const uint8_t* sy = src;
    const uint8_t* suv = src + static_cast<size_t>(yStride) * static_cast<size_t>(height);
    for (int by = 0; by < height; by += 16) {
        for (int bx = 0; bx < width; bx += 16) {
            for (int y = by; y < std::min(by + 16, height); ++y)
                for (int x = bx; x < std::min(bx + 16, width); ++x)
                    dy[static_cast<size_t>(clockwise ? x : width - 1 - x) * dw +
                        (clockwise ? height - 1 - y : y)] = sy[static_cast<size_t>(y) * yStride + x];
        }
    }
    for (int by = 0; by < height / 2; by += 16) {
        for (int bx = 0; bx < width / 2; bx += 16) {
            for (int y = by; y < std::min(by + 16, height / 2); ++y) {
                for (int x = bx; x < std::min(bx + 16, width / 2); ++x) {
                    const uint8_t* sp = suv + static_cast<size_t>(y) * yStride + static_cast<size_t>(x) * 2;
                    uint8_t* dp = duv + static_cast<size_t>(clockwise ? x : width / 2 - 1 - x) * dw +
                        static_cast<size_t>(clockwise ? height / 2 - 1 - y : y) * 2;
                    dp[0] = sp[0];
                    dp[1] = sp[1];
                }
            }
        }
    }
    if (outW) *outW = dw;
    if (outH) *outH = dh;
    if (outStride) *outStride = dw;
    return true;
}

bool rotateNv12CW90(const uint8_t* src, int width, int height, int yStride,
    std::vector<uint8_t>* dst, int* outW, int* outH, int* outStride)
{
    return rotateNv12QuarterTurn(src, width, height, yStride, 90, dst, outW, outH, outStride);
}

bool rotateNv12(const uint8_t* src, int width, int height, int yStride, int rotate,
    std::vector<uint8_t>* dst, int* outW, int* outH, int* outStride)
{
    if (rotate == 90 || rotate == 270)
        return rotateNv12QuarterTurn(src, width, height, yStride, rotate, dst, outW, outH, outStride);
    if (rotate != 180 || !src || !dst || width < 2 || height < 2 ||
        (width & 1) || (height & 1) || yStride < width) return false;
    dst->resize(static_cast<size_t>(width) * height * 3 / 2);
    uint8_t* yOut = dst->data();
    uint8_t* uvOut = yOut + static_cast<size_t>(width) * height;
    const uint8_t* uvIn = src + static_cast<size_t>(yStride) * height;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            yOut[static_cast<size_t>(height - 1 - y) * width + width - 1 - x] = src[static_cast<size_t>(y) * yStride + x];
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            const uint8_t* from = uvIn + static_cast<size_t>(y) * yStride + x * 2;
            uint8_t* to = uvOut + static_cast<size_t>(height / 2 - 1 - y) * width + (width / 2 - 1 - x) * 2;
            to[0] = from[0]; to[1] = from[1]; // Rotate chroma pairs, never exchange U and V.
        }
    }
    if (outW) *outW = width;
    if (outH) *outH = height;
    if (outStride) *outStride = width;
    return true;
}

bool scaleNv12(const uint8_t* src, int srcW, int srcH, int srcStride,
    std::vector<uint8_t>* dst, int dstW, int dstH, int* outStride)
{
    if (!src || !dst || srcW < 2 || srcH < 2 || dstW < 2 || dstH < 2) return false;
    if ((srcW & 1) || (srcH & 1) || (dstW & 1) || (dstH & 1) || srcStride < srcW) return false;
    dst->assign(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 3 / 2, 0);
    uint8_t* dy = &(*dst)[0];
    uint8_t* duv = dy + static_cast<size_t>(dstW) * static_cast<size_t>(dstH);
    const uint8_t* sy = src;
    const uint8_t* suv = src + static_cast<size_t>(srcStride) * static_cast<size_t>(srcH);
    for (int y = 0; y < dstH; ++y) {
        const int syi = y * srcH / dstH;
        for (int x = 0; x < dstW; ++x) {
            const int sxi = x * srcW / dstW;
            dy[static_cast<size_t>(y) * dstW + x] = sy[static_cast<size_t>(syi) * srcStride + sxi];
        }
    }
    for (int y = 0; y < dstH / 2; ++y) {
        const int syi = y * (srcH / 2) / (dstH / 2);
        for (int x = 0; x < dstW / 2; ++x) {
            const int sxi = x * (srcW / 2) / (dstW / 2);
            const uint8_t* sp = suv + static_cast<size_t>(syi) * srcStride + static_cast<size_t>(sxi) * 2;
            uint8_t* dp = duv + static_cast<size_t>(y) * dstW + static_cast<size_t>(x) * 2;
            dp[0] = sp[0];
            dp[1] = sp[1];
        }
    }
    if (outStride) *outStride = dstW;
    return true;
}

bool nv12BytesFit(size_t bytes, int width, int height, int yStride)
{
    if (width < 2 || height < 2 || yStride < width || (width & 1) || (height & 1)) return false;
    const uint64_t need = static_cast<uint64_t>(yStride) * static_cast<uint64_t>(height) * 3ull / 2ull;
    return bytes >= need;
}

bool packNv12Planes(const uint8_t* yPlane, const uint8_t* uvPlane, int width, int height,
    int yStride, int uvStride, std::vector<uint8_t>* packed)
{
    if (!yPlane || !uvPlane || !packed || width < 2 || height < 2 ||
        (width & 1) || (height & 1) || yStride < width || uvStride < width)
        return false;
    const size_t yBytes = static_cast<size_t>(width) * height;
    packed->resize(yBytes + yBytes / 2);
    for (int y = 0; y < height; ++y)
        memcpy(&(*packed)[static_cast<size_t>(y) * width],
            yPlane + static_cast<size_t>(y) * yStride, static_cast<size_t>(width));
    for (int y = 0; y < height / 2; ++y)
        memcpy(&(*packed)[yBytes + static_cast<size_t>(y) * width],
            uvPlane + static_cast<size_t>(y) * uvStride, static_cast<size_t>(width));
    return true;
}

bool finalizePartFile(bool writeOk, const std::string& partPath, const std::string& finalPath, int* errOut)
{
    if (errOut) *errOut = 0;
    if (partPath.empty() || finalPath.empty()) {
        if (errOut) *errOut = EINVAL;
        return false;
    }
    if (!writeOk) {
        if (!partPath.empty()) remove(partPath.c_str());
        if (!finalPath.empty()) remove(finalPath.c_str());
        return false;
    }
    if (rename(partPath.c_str(), finalPath.c_str()) != 0) {
        if (errOut) *errOut = errno;
        remove(partPath.c_str());
        remove(finalPath.c_str());
        return false;
    }
    return true;
}

}  // namespace camera
