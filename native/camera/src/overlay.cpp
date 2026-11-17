#include "overlay.h"
#include "plane_order.hpp"

#include "camera_logic.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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
#include <chrono>
#include <vector>
#include <set>

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


}  // namespace

void nv12ToBgra(const uint8_t* yPlane, const uint8_t* uvPlane, int width, int height,
    int yStride, int uvStride, uint8_t* dst, int dstW, int dstH, int rotate)
{
    if (width <= 0 || height <= 0 || dstW <= 0 || dstH <= 0) return;
    const bool quarter = rotate == 90 || rotate == 270;
    const bool flipX = rotate == 90 || rotate == 180;
    const bool flipY = rotate == 180 || rotate == 270;
    std::vector<int> columns(dstW), rows(dstH);
    // The same nearest-neighbour mapping, computed once per axis rather than
    // two integer divisions and a rotation branch for every output pixel.
    for (int x = 0; x < dstW; ++x)
        columns[x] = int(int64_t(flipX ? dstW - 1 - x : x) * (quarter ? height : width) / dstW);
    for (int y = 0; y < dstH; ++y)
        rows[y] = int(int64_t(flipY ? dstH - 1 - y : y) * (quarter ? width : height) / dstH);
    // Quarter-turns otherwise walk entire source columns with poor locality.
    // Small tiles keep both the sensor planes and the BGRA destination hot.
    const int tile = quarter ? 16 : std::max(dstW, dstH);
    for (int top = 0; top < dstH; top += tile) {
        for (int left = 0; left < dstW; left += tile) {
            const int endY = std::min(dstH, top + tile), endX = std::min(dstW, left + tile);
            for (int dy = top; dy < endY; ++dy) {
                for (int dx = left; dx < endX; ++dx) {
                    const int sx = quarter ? rows[dy] : columns[dx];
                    const int sy = quarter ? columns[dx] : rows[dy];
                    const int y = yPlane[size_t(sy) * yStride + sx];
                    const size_t uvIndex = size_t(sy / 2) * uvStride + (sx & ~1);
                    const int u = uvPlane[uvIndex], v = uvPlane[uvIndex + 1];
                    uint8_t* p = dst + (size_t(dy) * dstW + dx) * 4;
                    p[0] = static_cast<uint8_t>(yuv2rgb(y, u, v, 2));
                    p[1] = static_cast<uint8_t>(yuv2rgb(y, u, v, 1));
                    p[2] = static_cast<uint8_t>(yuv2rgb(y, u, v, 0));
                    p[3] = 255;
                }
            }
        }
    }
}

Overlay::Overlay()
    : fd_(-1)
    , drm_(0)
    , crtcId_(0)
    , planeId_(0)
    , zpos_(0)
    , minZpos_(0)
    , maxZpos_(0)
    , crtcW_(0)
    , crtcH_(0)
    , useNv12_(false)
    , atomic_(false)
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
    hole_ = {};
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
    if (drm_) dlclose(drm_);
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
    uint32_t primaryId = 0, primaryFb = 0;
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
        if (type == DRM_PLANE_TYPE_PRIMARY && p->fb_id && p->crtc_id == crtcId_) {
            primaryId = p->plane_id;primaryFb=p->fb_id;
            primaryHasZpos = findProp(primaryId, DRM_MODE_OBJECT_PLANE, "zpos", &primaryZpos) != 0;
        }
        if (type == DRM_PLANE_TYPE_OVERLAY && !p->fb_id) {
            if (hasNv12 && !overlayNv12) overlayNv12 = p->plane_id;
            if (hasArgb && !overlayArgb) overlayArgb = p->plane_id;
        }
        freePlane(p);
    }
    drm_mode_fb_cmd2 hostFrame{};hostFrame.fb_id=primaryFb;
    const bool alphaPrimary=primaryFb&&ioctl(fd_,DRM_IOCTL_MODE_GETFB2,&hostFrame)==0&&hostFrame.pixel_format==DRM_FORMAT_ARGB8888;
    std::set<uint32_t> handles;
    for(auto handle:hostFrame.handles)if(handle&&handles.insert(handle).second) {
        drm_gem_close closeHandle{};closeHandle.handle=handle;ioctl(fd_,DRM_IOCTL_GEM_CLOSE,&closeHandle);
    }
    if(!alphaPrimary){freePlanes(planes);freeRes(res);error_="Falcon primary is not an ARGB hole surface";return false;}
    planeId_ = overlayArgb;
    useNv12_ = false;
    if (!planeId_) {
        freePlanes(planes);
        freeRes(res);
        error_ = "no overlay plane";
        return false;
    }
    // The Falcon primary owns the UI above this plane. A hole exposes us;
    // system panels/IME must occlude us without retiring our framebuffer.
    hostPlane_=primaryId;
    hostZprop_=findProp(hostPlane_,DRM_MODE_OBJECT_PLANE,"zpos",&hostOriginalZ_);
    uint64_t nativeCurrent=0;
    propZpos_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"zpos",&nativeCurrent);
    auto range=[&](uint32_t property,uint64_t &low,uint64_t &high,bool &writable) {
        auto get=DRM_SYM(drmModeGetProperty);auto release=DRM_SYM(drmModeFreeProperty);
        auto p=get&&property?get(fd_,property):nullptr;
        bool ok=p&&(p->flags&DRM_MODE_PROP_RANGE)&&p->count_values>=2;
        if(ok){low=p->values[0];high=p->values[1];writable=!(p->flags&DRM_MODE_PROP_IMMUTABLE);}
        if(p&&release)release(p);return ok;
    };
    uint64_t low=0,high=0,hostLow=0,hostHigh=0;bool writable=false,hostWritable=false;
    UnderlayOrder order{};
    bool valid=primaryId&&primaryHasZpos&&atomic_&&
        range(propZpos_,low,high,writable)&&range(hostZprop_,hostLow,hostHigh,hostWritable)&&
        underlayOrder({low,high,nativeCurrent,writable},{hostLow,hostHigh,hostOriginalZ_,hostWritable},order);
    if(!valid||order.native>UINT32_MAX) {
        freePlanes(planes);freeRes(res);error_="Cannot establish Falcon-above-native DRM order";return false;
    }
    hostDesiredZ_=order.host;zpos_=static_cast<uint32_t>(order.native);
    minZpos_=static_cast<uint32_t>(low);maxZpos_=static_cast<uint32_t>(high);
    CAMLOG("operation=underlay native=%u zpos=%u falcon=%u original=%llu target=%llu",planeId_,zpos_,hostPlane_,
           static_cast<unsigned long long>(hostOriginalZ_),static_cast<unsigned long long>(hostDesiredZ_));
    propOutFence_ = findProp(crtcId_, DRM_MODE_OBJECT_CRTC, "OUT_FENCE_PTR");
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
        error_="Atomic DRM underlay properties unavailable";return false;
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

bool Overlay::allocBuf(DrmBuf* buf, int width, int height)
{
    freeBuf(buf);
    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    creq.bpp = 32;
    creq.width = static_cast<uint32_t>(width);
    creq.height = static_cast<uint32_t>(height);
    if (ioctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
        error_ = "CREATE_DUMB failed";
        return false;
    }
    buf->handle = creq.handle;
    buf->pitch = creq.pitch;
    buf->height = static_cast<uint32_t>(height);
    buf->size = creq.size;
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
    decltype(&drmModeAddFB2) addFb = DRM_SYM(drmModeAddFB2);
    if (!addFb || addFb(fd_, width, height, kFormatArgb, handles, pitches, offsets, &buf->fbId, 0) != 0) {
        error_ = "AddFB2 failed";
        return false;
    }
    return true;
}

void Overlay::freeBuf(DrmBuf* buf)
{
    if (!buf) return;
    decltype(&drmModeRmFB) rmFb = DRM_SYM(drmModeRmFB);
    if (fd_ >= 0 && buf->fbId && rmFb) rmFb(fd_, buf->fbId);
    if (buf->map && buf->size) munmap(buf->map, buf->size);
    if (fd_ >= 0 && buf->handle) {
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
    const bool retired = finishPendingFlip(true);
    if (!retired && fd_ >= 0) {
        // Abandon this DRM client instead of recycling uncertain scanout.
        // Kernel framebuffer references pin any still-scanned storage after
        // userspace mappings/handles are released; no further upload is allowed.
        ::close(fd_);
        fd_ = -1;
    }
    if (retired && presented_ && ownsCurrentPlane()) {
        commit(0, 0, 0, 0, 0, 1, 1);
    }
    restoreHostOrder();
    freeBuf(&bufs_[0]);
    freeBuf(&bufs_[1]);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    planeId_ = 0;
    presented_ = false;
    flipPending_ = false;
    completionFailed_ = false;
    pthread_mutex_unlock(&mu_);
}

std::array<uint64_t,4> Overlay::stacking() {
    uint64_t native=0,host=0;
    findProp(planeId_,DRM_MODE_OBJECT_PLANE,"zpos",&native);
    findProp(hostPlane_,DRM_MODE_OBJECT_PLANE,"zpos",&host);
    return {{planeId_,hostPlane_,native,host}};
}

bool Overlay::restoreHostOrder()
{
    if(!hostAdjusted_||fd_<0)return !hostAdjusted_;
    uint64_t current=0;
    auto set=DRM_SYM(drmModeObjectSetProperty);
    // Never overwrite a subsequent owner's different order.
    bool same=findProp(hostPlane_,DRM_MODE_OBJECT_PLANE,"zpos",&current)==hostZprop_&&current==hostDesiredZ_;
    if(same&&(!set||set(fd_,hostPlane_,DRM_MODE_OBJECT_PLANE,hostZprop_,hostOriginalZ_)!=0)) {
        CAMLOG("operation=restoreFalconOrder failed errno=%d",errno);return false;
    }
    hostAdjusted_=false;return true;
}

bool Overlay::ownsCurrentPlane() const
{
    if (fd_ < 0 || !planeId_) return false;
    struct drm_mode_get_plane plane = {};
    plane.plane_id = planeId_;
    if (ioctl(fd_, DRM_IOCTL_MODE_GETPLANE, &plane) != 0) return false;
    return plane.fb_id && (plane.fb_id == bufs_[0].fbId || plane.fb_id == bufs_[1].fbId);
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

void Overlay::fitSourceToCrtc(int srcW, int srcH, int* dx, int* dy, int* dw, int* dh) const
{
    int x = 0, y = 0, w = 0, h = 0;
    mapToCrtc(&x, &y, &w, &h);
    const CropRect hole = {x, y, w, h, true};
    const CropRect content = fitPhysicalViewport(hole, srcW, srcH, imageRotation(),
        hole_.fit != 0, 1, .5, .5);
    *dx = content.left; *dy = content.top; *dw = content.width; *dh = content.height;
}

bool Overlay::addProp(void* req, uint32_t objId, uint32_t propId, uint64_t value)
{
    if (!req || !propId) return false;
    decltype(&drmModeAtomicAddProperty) add = DRM_SYM(drmModeAtomicAddProperty);
    if (!add) return false;
    return add(static_cast<drmModeAtomicReq*>(req), objId, propId, value) >= 0;
}

void Overlay::flipComplete(int, unsigned, unsigned, unsigned, void* context)
{
    static_cast<Overlay*>(context)->flipPending_ = false;
}

bool Overlay::finishPendingFlip(bool wait)
{
    if (completionFailed_) return false;
    if (!flipPending_) return true;
    // An accepted NONBLOCK commit still owns both buffers: the new one is
    // queued and the old one may still be scanned. EBUSY at the next commit is
    // too late to protect an upload into that old buffer.
    struct pollfd event = {fd_, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    int result;
    int timeout = wait ? 100 : 0;
    for (;;) {
        result = poll(&event, 1, timeout);
        if (result >= 0 || errno != EINTR) break;
        if (!wait) return false;
        // Signals do not retire the queued buffers. Retry lifecycle waits,
        // charging every interruption against the same monotonic deadline.
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            result = 0;
            break;
        }
        timeout = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        // Round down so repeated signals can never extend the 100ms budget.
    }
    if (result < 0 || (event.revents & (POLLERR | POLLHUP | POLLNVAL)) ) {
        error_ = result == 0 ? "DRM flip completion timed out" : "DRM completion poll failed";
        completionFailed_ = true;
        return false;
    }
    if (result == 0 || !(event.revents & POLLIN)) return false;
    decltype(&drmHandleEvent) handle = DRM_SYM(drmHandleEvent);
    if (!handle) { error_ = "DRM event handling unavailable"; completionFailed_ = true; return false; }
    drmEventContext context = {};
    context.version = 2;
    context.page_flip_handler = &Overlay::flipComplete;
    if (handle(fd_, &context) != 0) {
        error_ = "DRM completion read failed";
        completionFailed_ = true;
    }
    return !flipPending_ && error_.empty();
}

bool Overlay::commit(uint32_t fbId, int x, int y, int w, int h, int srcW, int srcH)
{
    temporarilyUnavailable_ = false;
    uint64_t active=1;
    if(fbId&&findProp(crtcId_,DRM_MODE_OBJECT_CRTC,"ACTIVE",&active)&&!active) {
        temporarilyUnavailable_=true;return false;
    }
    if(fbId) {
        drm_mode_get_plane owner{};owner.plane_id=planeId_;
        uint64_t hostZ=0;
        if(ioctl(fd_,DRM_IOCTL_MODE_GETPLANE,&owner)!=0||
           (owner.fb_id&&owner.fb_id!=bufs_[0].fbId&&owner.fb_id!=bufs_[1].fbId)||
           findProp(hostPlane_,DRM_MODE_OBJECT_PLANE,"zpos",&hostZ)!=hostZprop_||
           (hostZ!=hostOriginalZ_&&hostZ!=hostDesiredZ_)) {
            error_="DRM underlay ownership/order changed";return false;
        }
    }

    if (atomic_ && propFb_ && propCrtc_) {
        decltype(&drmModeAtomicAlloc) alloc = DRM_SYM(drmModeAtomicAlloc);
        decltype(&drmModeAtomicFree) freeReq = DRM_SYM(drmModeAtomicFree);
        decltype(&drmModeAtomicCommit) commitFn = DRM_SYM(drmModeAtomicCommit);
        // A plane already attached to this CRTC need not add CRTC state.
        // PAGE_FLIP_EVENT then fails before atomic_check can add that state.
        // A null OUT_FENCE_PTR explicitly includes the CRTC without changing
        // its ACTIVE/mode or asking the kernel to create an output fence.
        if (fbId && !propOutFence_) {
            error_ = "DRM CRTC event binding unavailable";
            return false;
        }
        drmModeAtomicReq* req = alloc();
        if (!req) { error_="DRM atomic allocation failed"; return false; }
        const bool propsOk =
            (!fbId || addProp(req, crtcId_, propOutFence_, 0)) &&
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
            addProp(req, planeId_, propZpos_, zpos_) &&
            (!fbId || addProp(req,hostPlane_,hostZprop_,hostDesiredZ_));
        if (!propsOk) {
            error_ = "DRM atomic property setup failed";
            CAMLOG("operation=atomicAddProperty failed plane=%u", planeId_);
            freeReq(req);
            return false;
        }
        // Disabling is synchronous: lifecycle teardown must retire scanout
        // before unmapping buffers. Regular frames stay nonblocking.
        const uint32_t flags = fbId ? DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT : 0;
        const int rc = commitFn(fd_, req, flags, fbId ? this : nullptr);
        const int commitError = errno;
        freeReq(req);
        if (rc == 0) {
            if(fbId&&hostDesiredZ_!=hostOriginalZ_)hostAdjusted_=true;
            presented_ = fbId != 0;
            flipPending_ = fbId != 0;
            return true;
        }
        if (commitError == EBUSY || commitError == EAGAIN) return false;
        if(commitError==EACCES||commitError==EPERM) {temporarilyUnavailable_=true;return false;}
        // Retrying without zpos would silently place us above Falcon again.
        const int retryError=commitError;
        // Atomic submission failures must not switch to a legacy path whose
        // buffer retirement/completion contract has not been established.
        error_ = "DRM atomic submission failed: " + std::to_string(retryError);
        return false;
    }
    error_="Atomic DRM underlay submission unavailable";return false;
}

bool Overlay::commitPlacedBgra(const uint8_t* bgra, int cw, int ch, int cx, int cy)
{
    if (!bgra || cw < 1 || ch < 1) return false;
    int hx = 0, hy = 0, hw = 0, hh = 0;
    mapToCrtc(&hx, &hy, &hw, &hh);
    if (!finishPendingFlip()) return false;
    DrmBuf* buf = &bufs_[cur_];
    if (!buf->map || static_cast<int>(buf->height) != hh || static_cast<int>(buf->pitch / 4) < hw) {
        if (!allocBuf(buf, hw, hh)) return false;
    }
    uint8_t* dst = static_cast<uint8_t*>(buf->map);
    // The native full-hole upload covers every visible pixel. Partial images
    // and padded rows still clear their uncovered bytes before submission.
    if (cx != hx || cy != hy || cw != hw || ch != hh || buf->pitch != unsigned(hw) * 4)
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

bool Overlay::presentBgra(const uint8_t* bgra, int width, int height)
{
    pthread_mutex_lock(&mu_);
    if (completionFailed_) { pthread_mutex_unlock(&mu_); return false; }
    error_.clear();
    if (!bgra || width < 1 || height < 1) {
        error_="Invalid DRM frame geometry";
        pthread_mutex_unlock(&mu_);
        return false;
    }
    if (fd_ < 0 || !planeId_) {
        pthread_mutex_unlock(&mu_);
        if (!open()) return false;
        pthread_mutex_lock(&mu_);
    }
    if (!finishPendingFlip()) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    int cx = 0, cy = 0, cw = 0, ch = 0;
    fitSourceToCrtc(width, height, &cx, &cy, &cw, &ch);
    int hx = 0, hy = 0, hw = 0, hh = 0;
    mapToCrtc(&hx, &hy, &hw, &hh);
    const size_t need = static_cast<size_t>(hw) * static_cast<size_t>(hh) * 4;
    if (scratch_.size() < need) scratch_.resize(need);
    resampleRgbaViewport(bgra, width, height, scratch_.data(), hw, hh, cx - hx, cy - hy, cw, ch, imageRotation());
    const bool ok = commitPlacedBgra(&scratch_[0], hw, hh, hx, hy);
    pthread_mutex_unlock(&mu_);
    return ok;
}

void Overlay::disable()
{
    pthread_mutex_lock(&mu_);
    if (!presented_) {
        pthread_mutex_unlock(&mu_);
        return;
    }
    if (!finishPendingFlip(true)) {
        pthread_mutex_unlock(&mu_);
        return;
    }
    if (ownsCurrentPlane()) commit(0, 0, 0, 0, 0, 1, 1);
    else presented_ = false;
    pthread_mutex_unlock(&mu_);
}

}  // namespace camera
