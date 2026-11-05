#include "camera_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

namespace camera {
namespace {
int evenDown(int value)
{
    if (value < 2) return 2;
    return value & ~1;
}
} // namespace

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
} // namespace camera
