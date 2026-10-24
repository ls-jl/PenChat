#include "overlay.h"

#include "util.h"
#include "camera_logic.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace camera {
namespace {

#define CAMLOG(fmt, ...) syslog(LOG_INFO, "miniapp-camera: " fmt, ##__VA_ARGS__)

const uint32_t kFormatNv12 = DRM_FORMAT_NV12;
const uint32_t kFormatArgb = DRM_FORMAT_ARGB8888;

int yuv2rgb(int y, int u, int v, int c)
{
    const int cY = y - 16;
    const int cU = u - 128;
    const int cV = v - 128;
    int val = 0;
    if (c == 0) val = (298 * cY + 409 * cV + 128) >> 8;
    else if (c == 1) val = (298 * cY - 100 * cU - 208 * cV + 128) >> 8;
    else val = (298 * cY + 516 * cU + 128) >> 8;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return val;
}

void mapRotatedSrc(int dx, int dy, int dstW, int dstH, int srcW, int srcH, int rotate, int* sx, int* sy)
{
    int x = dx;
    int y = dy;
    if (rotate == 90) {
        x = dy * srcW / dstH;
        y = (dstW - 1 - dx) * srcH / dstW;
    } else if (rotate == 270) {
        x = (dstH - 1 - dy) * srcW / dstH;
        y = dx * srcH / dstW;
    } else if (rotate == 180) {
        x = (dstW - 1 - dx) * srcW / dstW;
        y = (dstH - 1 - dy) * srcH / dstH;
    } else {
        x = dx * srcW / dstW;
        y = dy * srcH / dstH;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= srcW) x = srcW - 1;
    if (y >= srcH) y = srcH - 1;
    if (sx) *sx = x;
    if (sy) *sy = y;
}

}  // namespace

void nv12ToBgra(const uint8_t* yPlane, const uint8_t* uvPlane, int width, int height,
    int yStride, int uvStride, uint8_t* dst, int dstW, int dstH, int rotate)
{
    for (int dy = 0; dy < dstH; ++dy) {
        for (int dx = 0; dx < dstW; ++dx) {
            int sx = 0, sy = 0;
            mapRotatedSrc(dx, dy, dstW, dstH, width, height, rotate, &sx, &sy);
            const int y = yPlane[sy * yStride + sx];
            const int uvIndex = (sy / 2) * uvStride + (sx & ~1);
            const int u = uvPlane[uvIndex];
            const int v = uvPlane[uvIndex + 1];
            uint8_t* p = dst + (dy * dstW + dx) * 4;
            p[0] = static_cast<uint8_t>(yuv2rgb(y, u, v, 2));
            p[1] = static_cast<uint8_t>(yuv2rgb(y, u, v, 1));
            p[2] = static_cast<uint8_t>(yuv2rgb(y, u, v, 0));
            p[3] = 255;
        }
    }
}

void rgbaContain(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH,
    int* outX, int* outY, int* outW, int* outH)
{
    memset(dst, 0, static_cast<size_t>(dstW) * dstH * 4);
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    const float srcA = static_cast<float>(srcW) / srcH;
    const float dstA = static_cast<float>(dstW) / dstH;
    int w = dstW;
    int h = dstH;
    if (srcA > dstA) h = static_cast<int>(dstW / srcA + 0.5f);
    else w = static_cast<int>(dstH * srcA + 0.5f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    const int x = (dstW - w) / 2;
    const int y = (dstH - h) / 2;
    for (int dy = 0; dy < h; ++dy) {
        const int sy = dy * srcH / h;
        for (int dx = 0; dx < w; ++dx) {
            const int sx = dx * srcW / w;
            memcpy(dst + ((y + dy) * dstW + (x + dx)) * 4, src + (sy * srcW + sx) * 4, 4);
        }
    }
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outW) *outW = w;
    if (outH) *outH = h;
}

void rgbaScaleRotate(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH, int rotate)
{
    for (int dy = 0; dy < dstH; ++dy) {
        for (int dx = 0; dx < dstW; ++dx) {
            int sx = 0, sy = 0;
            mapRotatedSrc(dx, dy, dstW, dstH, srcW, srcH, rotate, &sx, &sy);
            memcpy(dst + (dy * dstW + dx) * 4, src + (sy * srcW + sx) * 4, 4);
        }
    }
}

Overlay::Overlay()
    : fd_(-1)
    , drm_(0)
    , crtcId_(0)
    , connId_(0)
    , planeId_(0)
    , zpos_(0)
    , minZpos_(0)
    , maxZpos_(0)
    , crtcW_(0)
    , crtcH_(0)
    , useNv12_(false)
    , atomic_(false)
    , enabled_(false)
    , cur_(0)
    , propFb_(0)
    , propCrtc_(0)
    , propSrcX_(0)
    , propSrcY_(0)
    , propSrcW_(0)
    , propSrcH_(0)
    , propCrtcX_(0)
    , propCrtcY_(0)
    , propCrtcW_(0)
    , propCrtcH_(0)
    , propZpos_(0)
{
    memset(&hole_, 0, sizeof(hole_));
    memset(bufs_, 0, sizeof(bufs_));
    hole_.rotation = 0;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mu_, &attr);
    pthread_mutexattr_destroy(&attr);
}

Overlay::~Overlay()
{
    close();
    pthread_mutex_destroy(&mu_);
}

bool Overlay::loadDrm()
{
    if (drm_) return true;
    static const char* names[] = {"libdrm.so.2", "libdrm.so", 0};
    for (int i = 0; names[i]; ++i) {
        drm_ = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (drm_) return true;
    }
    error_ = "libdrm missing";
    return false;
}

#define DRM_SYM(name) reinterpret_cast<decltype(&name)>(dlsym(drm_, #name))

bool Overlay::open()
{
    pthread_mutex_lock(&mu_);
    if (ready()) {
        pthread_mutex_unlock(&mu_);
        return true;
    }
    if (!loadDrm()) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    fd_ = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        error_ = "open /dev/dri/card0 failed";
        pthread_mutex_unlock(&mu_);
        return false;
    }
    decltype(&drmSetClientCap) setCap = DRM_SYM(drmSetClientCap);
    if (setCap) {
        setCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        if (setCap(fd_, DRM_CLIENT_CAP_ATOMIC, 1) == 0) atomic_ = true;
    }
    if (!pickResources()) {
        pthread_mutex_unlock(&mu_);
        close();
        return false;
    }
    CAMLOG("operation=overlayOpen plane=%u crtc=%ux%u zpos=%u nv12=%d atomic=%d", planeId_, crtcW_, crtcH_,
        zpos_, useNv12_ ? 1 : 0, atomic_ ? 1 : 0);
    pthread_mutex_unlock(&mu_);
    return true;
}

bool Overlay::pickResources()
{
    decltype(&drmModeGetResources) getRes = DRM_SYM(drmModeGetResources);
    decltype(&drmModeFreeResources) freeRes = DRM_SYM(drmModeFreeResources);
    decltype(&drmModeGetConnector) getConn = DRM_SYM(drmModeGetConnector);
    decltype(&drmModeFreeConnector) freeConn = DRM_SYM(drmModeFreeConnector);
    decltype(&drmModeGetEncoder) getEnc = DRM_SYM(drmModeGetEncoder);
    decltype(&drmModeFreeEncoder) freeEnc = DRM_SYM(drmModeFreeEncoder);
    decltype(&drmModeGetCrtc) getCrtc = DRM_SYM(drmModeGetCrtc);
    decltype(&drmModeFreeCrtc) freeCrtc = DRM_SYM(drmModeFreeCrtc);
    decltype(&drmModeGetPlaneResources) getPlanes = DRM_SYM(drmModeGetPlaneResources);
    decltype(&drmModeFreePlaneResources) freePlanes = DRM_SYM(drmModeFreePlaneResources);
    decltype(&drmModeGetPlane) getPlane = DRM_SYM(drmModeGetPlane);
    decltype(&drmModeFreePlane) freePlane = DRM_SYM(drmModeFreePlane);
    if (!getRes || !getConn || !getPlanes || !getPlane) {
        error_ = "libdrm symbols missing";
        return false;
    }
    drmModeRes* res = getRes(fd_);
    if (!res) {
        error_ = "drmModeGetResources failed";
        return false;
    }
    drmModeConnector* conn = 0;
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnector* c = getConn(fd_, res->connectors[i]);
        if (!c) continue;
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        freeConn(c);
    }
    if (!conn) {
        freeRes(res);
        error_ = "no connected drm connector";
        return false;
    }
    connId_ = conn->connector_id;
    uint32_t encId = conn->encoder_id;
    if (!encId && conn->count_encoders) encId = conn->encoders[0];
    drmModeEncoder* enc = encId ? getEnc(fd_, encId) : 0;
    if (enc && enc->crtc_id) crtcId_ = enc->crtc_id;
    if (!crtcId_ && res->count_crtcs) crtcId_ = res->crtcs[0];
    drmModeCrtc* crtc = getCrtc(fd_, crtcId_);
    if (crtc && crtc->mode_valid) {
        crtcW_ = crtc->mode.hdisplay;
        crtcH_ = crtc->mode.vdisplay;
    } else if (conn->count_modes) {
        crtcW_ = conn->modes[0].hdisplay;
        crtcH_ = conn->modes[0].vdisplay;
    }
    if (crtc) freeCrtc(crtc);
    if (enc) freeEnc(enc);
    freeConn(conn);

    uint32_t crtcIndex = 0;
    for (int i = 0; i < res->count_crtcs; ++i) {
        if (res->crtcs[i] == crtcId_) crtcIndex = static_cast<uint32_t>(i);
    }
    const uint32_t crtcMask = 1u << crtcIndex;
    drmModePlaneRes* planes = getPlanes(fd_);
    if (!planes) {
        freeRes(res);
        error_ = "no drm planes";
        return false;
    }
    uint32_t overlayNv12 = 0;
    uint32_t overlayArgb = 0;
    uint32_t primaryId = 0;
    uint64_t primaryZpos = 0;
    bool primaryHasZpos = false;
    for (uint32_t i = 0; i < planes->count_planes; ++i) {
        drmModePlane* p = getPlane(fd_, planes->planes[i]);
        if (!p) continue;
        if (!(p->possible_crtcs & crtcMask)) {
            freePlane(p);
            continue;
        }
        uint64_t type = 0;
        findProp(p->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type);
        bool hasNv12 = false;
        bool hasArgb = false;
        for (uint32_t f = 0; f < p->count_formats; ++f) {
            if (p->formats[f] == kFormatNv12) hasNv12 = true;
            if (p->formats[f] == kFormatArgb || p->formats[f] == DRM_FORMAT_XRGB8888) hasArgb = true;
        }
        if (type == DRM_PLANE_TYPE_PRIMARY) {
            primaryId = p->plane_id;
            primaryHasZpos = findProp(primaryId, DRM_MODE_OBJECT_PLANE, "zpos", &primaryZpos) != 0;
        }
        if (type == DRM_PLANE_TYPE_OVERLAY) {
            if (hasNv12 && !overlayNv12) overlayNv12 = p->plane_id;
            if (hasArgb && !overlayArgb) overlayArgb = p->plane_id;
        }
        freePlane(p);
    }
    planeId_ = overlayNv12 ? overlayNv12 : overlayArgb;
    useNv12_ = overlayNv12 != 0;
    if (!planeId_) {
        freePlanes(planes);
        freeRes(res);
        error_ = "no overlay plane";
        return false;
    }
    uint64_t zval = 0;
    propZpos_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "zpos", &zval);
    minZpos_ = 0;
    maxZpos_ = 0;
    zpos_ = static_cast<uint32_t>(zval);
    if (propZpos_) {
        decltype(&drmModeGetProperty) getProp = DRM_SYM(drmModeGetProperty);
        decltype(&drmModeFreeProperty) freeProp = DRM_SYM(drmModeFreeProperty);
        drmModePropertyRes* prop = getProp ? getProp(fd_, propZpos_) : 0;
        if (prop && (prop->flags & DRM_MODE_PROP_RANGE) && prop->count_values >= 2) {
            minZpos_ = static_cast<uint32_t>(prop->values[0]);
            maxZpos_ = static_cast<uint32_t>(prop->values[1]);
            zpos_ = minZpos_;
            if (primaryHasZpos) {
                if (primaryZpos < maxZpos_)
                    zpos_ = static_cast<uint32_t>(std::max<uint64_t>(minZpos_, primaryZpos + 1));
                else
                    CAMLOG("operation=zpos warning=overlay_cannot_above_primary plane=%u primary=%llu max=%u",
                        planeId_, static_cast<unsigned long long>(primaryZpos), maxZpos_);
            } else {
                CAMLOG("operation=zpos warning=primary_zpos_unavailable plane=%u", planeId_);
            }
            if (zpos_ > maxZpos_) zpos_ = maxZpos_;
            CAMLOG("operation=zpos min=%u max=%u primary=%llu use=%u",
                minZpos_, maxZpos_, static_cast<unsigned long long>(primaryHasZpos ? primaryZpos : 0), zpos_);
        }
        if (prop && freeProp) freeProp(prop);
    }
    propFb_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "FB_ID");
    propCrtc_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    propSrcX_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "SRC_X");
    propSrcY_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    propSrcW_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "SRC_W");
    propSrcH_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "SRC_H");
    propCrtcX_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    propCrtcY_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    propCrtcW_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    propCrtcH_ = findProp(planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    freePlanes(planes);
    freeRes(res);
    if (!atomic_ || !propFb_ || !propCrtc_) {
        CAMLOG("%s", "atomic props incomplete, will use drmModeSetPlane");
    }
    return true;
}

uint32_t Overlay::findProp(uint32_t objId, uint32_t objType, const char* name, uint64_t* value)
{
    decltype(&drmModeObjectGetProperties) getProps = DRM_SYM(drmModeObjectGetProperties);
    decltype(&drmModeFreeObjectProperties) freeProps = DRM_SYM(drmModeFreeObjectProperties);
    decltype(&drmModeGetProperty) getProp = DRM_SYM(drmModeGetProperty);
    decltype(&drmModeFreeProperty) freeProp = DRM_SYM(drmModeFreeProperty);
    if (!getProps || !getProp) return 0;
    drmModeObjectProperties* props = getProps(fd_, objId, objType);
    if (!props) return 0;
    uint32_t found = 0;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyRes* p = getProp(fd_, props->props[i]);
        if (!p) continue;
        if (strcmp(p->name, name) == 0) {
            found = p->prop_id;
            if (value) *value = props->prop_values[i];
        }
        freeProp(p);
        if (found) break;
    }
    freeProps(props);
    return found;
}

bool Overlay::allocBuf(DrmBuf* buf, int width, int height, uint32_t format)
{
    freeBuf(buf);
    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    if (format == kFormatNv12) {
        creq.bpp = 8;
        creq.width = static_cast<uint32_t>(width);
        creq.height = static_cast<uint32_t>(height) * 3 / 2;
    } else {
        creq.bpp = 32;
        creq.width = static_cast<uint32_t>(width);
        creq.height = static_cast<uint32_t>(height);
    }
    if (ioctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
        error_ = "CREATE_DUMB failed";
        return false;
    }
    buf->handle = creq.handle;
    buf->pitch = creq.pitch;
    buf->height = static_cast<uint32_t>(height);
    buf->size = creq.size;
    buf->format = format;
    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = creq.handle;
    if (ioctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
        error_ = "MAP_DUMB failed";
        return false;
    }
    buf->map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, mreq.offset);
    if (buf->map == MAP_FAILED) {
        buf->map = 0;
        error_ = "mmap dumb failed";
        return false;
    }
    memset(buf->map, 0, buf->size);
    uint32_t handles[4] = {creq.handle, 0, 0, 0};
    uint32_t pitches[4] = {creq.pitch, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};
    if (format == kFormatNv12) {
        handles[1] = creq.handle;
        pitches[1] = creq.pitch;
        offsets[1] = creq.pitch * static_cast<uint32_t>(height);
    }
    decltype(&drmModeAddFB2) addFb = DRM_SYM(drmModeAddFB2);
    if (!addFb || addFb(fd_, width, height, format, handles, pitches, offsets, &buf->fbId, 0) != 0) {
        error_ = "AddFB2 failed";
        return false;
    }
    return true;
}

void Overlay::freeBuf(DrmBuf* buf)
{
    if (!buf) return;
    decltype(&drmModeRmFB) rmFb = DRM_SYM(drmModeRmFB);
    if (buf->fbId && rmFb) rmFb(fd_, buf->fbId);
    if (buf->map && buf->size) munmap(buf->map, buf->size);
    if (buf->handle) {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = buf->handle;
        ioctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
    memset(buf, 0, sizeof(*buf));
}

void Overlay::close()
{
    pthread_mutex_lock(&mu_);
    if (fd_ >= 0 && planeId_) {
        commit(0, 0, 0, 0, 0, 1, 1);
        enabled_ = false;
    }
    freeBuf(&bufs_[0]);
    freeBuf(&bufs_[1]);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    planeId_ = 0;
    pthread_mutex_unlock(&mu_);
}

void Overlay::setHole(const HoleRect& hole)
{
    pthread_mutex_lock(&mu_);
    hole_ = hole;
    pthread_mutex_unlock(&mu_);
}

void Overlay::mapToCrtc(int* dx, int* dy, int* dw, int* dh) const
{
    mapRectToCrtc(hole_.x, hole_.y, hole_.w, hole_.h, dx, dy, dw, dh, true);
}

int Overlay::imageRotation() const
{
    int rotate = hole_.rotation + hole_.turn;
    rotate %= 360;
    if (rotate < 0) rotate += 360;
    return rotate;
}

void Overlay::mapRectToCrtc(int x, int y, int w, int h, int* dx, int* dy, int* dw, int* dh, bool clamp) const
{
    if (w <= 0 || h <= 0) {
        x = 0;
        y = 0;
        w = crtcW_;
        h = crtcH_;
    }
    const int rot = hole_.rotation % 360;
    // The framework draws a configured viewport into the full framebuffer;
    // screen offsets are in the pre-rotation framebuffer coordinate system.
    // Do not stretch an inset Falcon viewport to the entire scanout.
    const int canvasW = rot == 90 || rot == 270 ? crtcH_ : crtcW_;
    const int canvasH = rot == 90 || rot == 270 ? crtcW_ : crtcH_;
    const CropRect mapped = mapPanelRect(x + hole_.offsetX, y + hole_.offsetY, w, h,
        canvasW, canvasH, crtcW_, crtcH_, rot);
    int rx = mapped.left, ry = mapped.top, rw = mapped.width, rh = mapped.height;
    if (clamp) {
        const int right = std::min(crtcW_, rx + rw), bottom = std::min(crtcH_, ry + rh);
        rx = std::max(0, rx); ry = std::max(0, ry);
        rw = right - rx; rh = bottom - ry;
    }
    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;
    if (dx) *dx = rx;
    if (dy) *dy = ry;
    if (dw) *dw = rw;
    if (dh) *dh = rh;
}

void Overlay::setViewport(double zoom, double centerX, double centerY)
{
    pthread_mutex_lock(&mu_);
    viewZoom_ = zoom; viewCenterX_ = centerX; viewCenterY_ = centerY;
    pthread_mutex_unlock(&mu_);
}

void Overlay::fitSourceToCrtc(int srcW, int srcH, int* dx, int* dy, int* dw, int* dh) const
{
    int x = 0, y = 0, w = 0, h = 0;
    mapToCrtc(&x, &y, &w, &h);
    const CropRect hole = {x, y, w, h, true};
    const CropRect content = fitPhysicalViewport(hole, srcW, srcH, imageRotation(),
        hole_.fit != 0, viewZoom_, viewCenterX_, viewCenterY_);
    *dx = content.left; *dy = content.top; *dw = content.width; *dh = content.height;
}

bool Overlay::addProp(void* req, uint32_t objId, uint32_t propId, uint64_t value)
{
    if (!req || !propId) return false;
    decltype(&drmModeAtomicAddProperty) add = DRM_SYM(drmModeAtomicAddProperty);
    if (!add) return false;
    return add(static_cast<drmModeAtomicReq*>(req), objId, propId, value) >= 0;
}

bool Overlay::commit(uint32_t fbId, int x, int y, int w, int h, int srcW, int srcH)
{
    if (atomic_ && propFb_ && propCrtc_) {
        decltype(&drmModeAtomicAlloc) alloc = DRM_SYM(drmModeAtomicAlloc);
        decltype(&drmModeAtomicFree) freeReq = DRM_SYM(drmModeAtomicFree);
        decltype(&drmModeAtomicCommit) commitFn = DRM_SYM(drmModeAtomicCommit);
        drmModeAtomicReq* req = alloc();
        if (!req) return false;
        const bool propsOk =
            addProp(req, planeId_, propFb_, fbId) &&
            addProp(req, planeId_, propCrtc_, fbId ? crtcId_ : 0) &&
            addProp(req, planeId_, propSrcX_, 0) &&
            addProp(req, planeId_, propSrcY_, 0) &&
            addProp(req, planeId_, propSrcW_, static_cast<uint64_t>(srcW) << 16) &&
            addProp(req, planeId_, propSrcH_, static_cast<uint64_t>(srcH) << 16) &&
            addProp(req, planeId_, propCrtcX_, static_cast<uint64_t>(x)) &&
            addProp(req, planeId_, propCrtcY_, static_cast<uint64_t>(y)) &&
            addProp(req, planeId_, propCrtcW_, static_cast<uint64_t>(w)) &&
            addProp(req, planeId_, propCrtcH_, static_cast<uint64_t>(h)) &&
            (!propZpos_ || addProp(req, planeId_, propZpos_, zpos_));
        if (!propsOk) {
            CAMLOG("operation=atomicAddProperty failed plane=%u", planeId_);
            freeReq(req);
            return false;
        }
        const int rc = commitFn(fd_, req, DRM_MODE_ATOMIC_NONBLOCK, 0);
        const int commitError = errno;
        freeReq(req);
        if (rc == 0) {
            enabled_ = fbId != 0;
            return true;
        }
        if (commitError == EBUSY || commitError == EAGAIN) return false;
        CAMLOG("atomic commit rc=%d errno=%d, retry without zpos", rc, errno);
        req = alloc();
        if (!req) return false;
        addProp(req, planeId_, propFb_, fbId);
        addProp(req, planeId_, propCrtc_, fbId ? crtcId_ : 0);
        addProp(req, planeId_, propSrcX_, 0);
        addProp(req, planeId_, propSrcY_, 0);
        addProp(req, planeId_, propSrcW_, static_cast<uint64_t>(srcW) << 16);
        addProp(req, planeId_, propSrcH_, static_cast<uint64_t>(srcH) << 16);
        addProp(req, planeId_, propCrtcX_, static_cast<uint64_t>(x));
        addProp(req, planeId_, propCrtcY_, static_cast<uint64_t>(y));
        addProp(req, planeId_, propCrtcW_, static_cast<uint64_t>(w));
        addProp(req, planeId_, propCrtcH_, static_cast<uint64_t>(h));
        const int rc2 = commitFn(fd_, req, DRM_MODE_ATOMIC_NONBLOCK, 0);
        const int retryError = errno;
        freeReq(req);
        if (rc2 == 0) {
            enabled_ = fbId != 0;
            return true;
        }
        if (retryError == EBUSY || retryError == EAGAIN) return false;
        CAMLOG("atomic commit failed rc=%d errno=%d", rc2, errno);
    }
    decltype(&drmModeSetPlane) setPlane = DRM_SYM(drmModeSetPlane);
    if (!setPlane) return false;
    const int rc = setPlane(fd_, planeId_, crtcId_, fbId, 0, x, y, w, h, 0, 0, srcW << 16, srcH << 16);
    if (rc == 0) enabled_ = fbId != 0;
    return rc == 0;
}

bool Overlay::commitPlacedBgra(const uint8_t* bgra, int cw, int ch, int cx, int cy)
{
    if (!bgra || cw < 1 || ch < 1) return false;
    int hx = 0, hy = 0, hw = 0, hh = 0;
    mapToCrtc(&hx, &hy, &hw, &hh);
    DrmBuf* buf = &bufs_[cur_];
    if (!buf->map || static_cast<int>(buf->height) != hh || static_cast<int>(buf->pitch / 4) < hw ||
        buf->format != kFormatArgb) {
        if (!allocBuf(buf, hw, hh, kFormatArgb)) return false;
    }
    uint8_t* dst = static_cast<uint8_t*>(buf->map);
    memset(dst, 0, buf->size);
    const int x0 = std::max(hx, cx);
    const int y0 = std::max(hy, cy);
    const int x1 = std::min(hx + hw, cx + cw);
    const int y1 = std::min(hy + hh, cy + ch);
    const int pitch = static_cast<int>(buf->pitch);
    for (int y = y0; y < y1; ++y) {
        const int rowW = x1 - x0;
        if (rowW <= 0) continue;
        memcpy(dst + (y - hy) * pitch + (x0 - hx) * 4,
            bgra + (static_cast<size_t>(y - cy) * cw + (x0 - cx)) * 4,
            static_cast<size_t>(rowW) * 4);
    }
    if (!commit(buf->fbId, hx, hy, hw, hh, hw, hh)) return false;
    cur_ = 1 - cur_;
    return true;
}

bool Overlay::presentNv12(const uint8_t* y, int width, int height, int yStride, int uvStride,
    const uint8_t* uvPlane)
{
    pthread_mutex_lock(&mu_);
    if (!y || width < 2 || height < 2 || yStride < width) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    if (fd_ < 0 || !planeId_) {
        pthread_mutex_unlock(&mu_);
        if (!open()) return false;
        pthread_mutex_lock(&mu_);
    }
    const uint8_t* uv = uvPlane ? uvPlane : y + static_cast<size_t>(yStride) * static_cast<size_t>(height);
    const int rot = imageRotation();
    int cx = 0, cy = 0, cw = 0, ch = 0;
    fitSourceToCrtc(width, height, &cx, &cy, &cw, &ch);
    int hx = 0, hy = 0, hw = 0, hh = 0;
    mapToCrtc(&hx, &hy, &hw, &hh);
    hw &= ~1;
    hh &= ~1;
    cw &= ~1;
    ch &= ~1;
    if (hw < 2 || hh < 2 || cw < 2 || ch < 2) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    bool ok = false;
    if (useNv12_) {
        DrmBuf* buf = &bufs_[cur_];
        if (!buf->map || buf->format != kFormatNv12 || static_cast<int>(buf->height) != hh ||
            static_cast<int>(buf->pitch) < hw) {
            if (!allocBuf(buf, hw, hh, kFormatNv12)) {
                pthread_mutex_unlock(&mu_);
                return false;
            }
        }
        const int pitch = static_cast<int>(buf->pitch);
        const size_t bytes = static_cast<size_t>(pitch) * hh * 3 / 2;
        if (scratch_.size() < bytes) scratch_.resize(bytes);
        // Scale/transpose in cached RAM, then upload sequentially. Pixel-wise writes to
        // the DRM DMA mapping were throttling portrait video even with hardware encoding.
        uint8_t* dstY = scratch_.data();
        uint8_t* dstUv = dstY + static_cast<size_t>(pitch) * static_cast<size_t>(hh);
        const int uvStep = uvStride > 0 ? uvStride : yStride;
        // A portrait panel made the old per-pixel divisions dominate the capture loop.
        // Precompute separable coordinates, then tile the transpose to retain source cache locality.
        if (!resampleNv12Viewport(y, uv, width, height, yStride, uvStep,
            dstY, dstUv, hw, hh, pitch, cx - hx, cy - hy, cw, ch, rot)) {
            pthread_mutex_unlock(&mu_);
            return false;
        }
        memcpy(buf->map, scratch_.data(), bytes);
        ok = commit(buf->fbId, hx, hy, hw, hh, hw, hh);
        if (ok) cur_ = 1 - cur_;
    } else {
        const size_t need = static_cast<size_t>(hw) * static_cast<size_t>(hh) * 4;
        if (scratch_.size() < need) scratch_.resize(need);
        const int fallbackStride = uvStride > 0 ? uvStride : yStride;
        std::vector<uint8_t> viewport(static_cast<size_t>(hw) * hh * 3 / 2);
        ok = resampleNv12Viewport(y, uv, width, height, yStride, fallbackStride,
            viewport.data(), viewport.data() + hw * hh, hw, hh, hw, cx - hx, cy - hy, cw, ch, rot);
        if (ok) {
            nv12ToBgra(viewport.data(), viewport.data() + hw * hh, hw, hh, hw, hw, scratch_.data(), hw, hh, 0);
            ok = commitPlacedBgra(scratch_.data(), hw, hh, hx, hy);
        }
    }
    pthread_mutex_unlock(&mu_);
    return ok;
}

bool Overlay::presentRgba(const uint8_t* rgba, int width, int height)
{
    pthread_mutex_lock(&mu_);
    if (!rgba || width < 1 || height < 1) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    if (fd_ < 0 || !planeId_) {
        pthread_mutex_unlock(&mu_);
        if (!open()) return false;
        pthread_mutex_lock(&mu_);
    }
    int cx = 0, cy = 0, cw = 0, ch = 0;
    fitSourceToCrtc(width, height, &cx, &cy, &cw, &ch);
    int hx = 0, hy = 0, hw = 0, hh = 0;
    mapToCrtc(&hx, &hy, &hw, &hh);
    const size_t need = static_cast<size_t>(hw) * static_cast<size_t>(hh) * 4;
    if (scratch_.size() < need) scratch_.resize(need);
    resampleRgbaViewport(rgba, width, height, scratch_.data(), hw, hh, cx - hx, cy - hy, cw, ch, imageRotation());
    // ARGB8888 is stored as B,G,R,A bytes. stb's RGBA must not be copied unchanged.
    rgbaToBgra(&scratch_[0], &scratch_[0], static_cast<size_t>(hw) * hh);
    const bool ok = commitPlacedBgra(&scratch_[0], hw, hh, hx, hy);
    pthread_mutex_unlock(&mu_);
    return ok;
}

void Overlay::disable()
{
    pthread_mutex_lock(&mu_);
    if (fd_ < 0 || !planeId_) {
        pthread_mutex_unlock(&mu_);
        return;
    }
    commit(0, 0, 0, 0, 0, 1, 1);
    enabled_ = false;
    pthread_mutex_unlock(&mu_);
}

}  // namespace camera
