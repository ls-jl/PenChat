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
    info_=DisplayInfo{};info_.mode=CompositionMode::Unselected;info_.stage="libdrm_unavailable";
    if (!loadDrm()) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    fd_ = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        info_.stage="device_open_failed";info_.queryError=errno;
        error_ = "open /dev/dri/card0 failed: "+std::to_string(errno);
        pthread_mutex_unlock(&mu_);
        return false;
    }
    atomic_=false;
    decltype(&drmSetClientCap) setCap = DRM_SYM(drmSetClientCap);
    if (setCap) {
        setCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        if (setCap(fd_, DRM_CLIENT_CAP_ATOMIC, 1) == 0) atomic_ = true;
    }
    info_=DisplayInfo{};info_.mode=CompositionMode::Unselected;allowUpper_=false;
    probeDeadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
    const auto deadline=probeDeadline_-std::chrono::milliseconds(100); // Reserve completion proof time.
    bool selected=false;
    do {
        ++info_.attempts;
        if(pickResources()){selected=true;break;}
        if(completionFailed_)break;
        if(std::chrono::steady_clock::now()>=deadline)break;
        const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(deadline-std::chrono::steady_clock::now()).count();
        if(remaining>0)usleep(static_cast<useconds_t>(std::min<int64_t>(50000,remaining)));
    } while(true);
    if(!selected&&!completionFailed_) {allowUpper_=true;++info_.attempts;selected=pickResources();}
    if (!selected) {
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
    error_.clear();
    crtcId_=0;crtcW_=crtcH_=0;info_.stage="resources";
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
    if (!getRes || !freeRes || !getConn || !freeConn || !getEnc || !freeEnc ||
        !getCrtc || !freeCrtc || !getPlanes || !freePlanes || !getPlane || !freePlane) {
        error_ = "libdrm symbols missing";
        return false;
    }
    drmModeRes* res = getRes(fd_);
    if (!res) {
        info_.queryError=errno;info_.stage="resources_unavailable";
        error_ = "drmModeGetResources failed: "+std::to_string(errno);
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
    struct Candidate {uint32_t id,format;};
    std::vector<Candidate> candidates;
    uint32_t primaryId=0,primaryFb=0;
    uint64_t primaryZpos=0;
    for(uint32_t i=0;i<planes->count_planes;++i) {
        drmModePlane *plane=getPlane(fd_,planes->planes[i]);
        if(!plane)continue;
        if(plane->possible_crtcs&crtcMask) {
            uint64_t type=0;findProp(plane->plane_id,DRM_MODE_OBJECT_PLANE,"type",&type);
            if(type==DRM_PLANE_TYPE_PRIMARY&&plane->fb_id&&plane->crtc_id==crtcId_) {
                primaryId=plane->plane_id;primaryFb=plane->fb_id;
                findProp(primaryId,DRM_MODE_OBJECT_PLANE,"zpos",&primaryZpos);
            }
            if(type==DRM_PLANE_TYPE_OVERLAY&&!plane->fb_id) {
                uint32_t format=0;
                for(uint32_t f=0;f<plane->count_formats;++f) {
                    if(plane->formats[f]==DRM_FORMAT_ARGB8888){format=DRM_FORMAT_ARGB8888;break;}
                    if(plane->formats[f]==DRM_FORMAT_XRGB8888)format=DRM_FORMAT_XRGB8888;
                }
                if(format)candidates.push_back({plane->plane_id,format});
            }
        }
        freePlane(plane);
    }
    freePlanes(planes);freeRes(res);
    info_.primary=primaryId;info_.crtc=crtcId_;info_.framebuffer=primaryFb;
    info_.format=0;info_.queryResult=-1;info_.queryError=0;
    auto fail=[&](const char *stage,const std::string &reason) {
        info_.stage=stage;error_=std::string("DRM display initialization [")+stage+"]: "+reason;
        return false;
    };
    if(!primaryId||!primaryFb)return fail("primary_not_ready","Falcon primary framebuffer is not ready");
    drm_mode_fb_cmd2 hostFrame{};hostFrame.fb_id=primaryFb;
    errno=0;info_.queryResult=ioctl(fd_,DRM_IOCTL_MODE_GETFB2,&hostFrame);
    info_.queryError=info_.queryResult<0?errno:0;
    if(info_.queryResult==0)info_.format=hostFrame.pixel_format;
    std::set<uint32_t> handles;
    for(auto handle:hostFrame.handles)if(handle&&handles.insert(handle).second) {
        drm_gem_close release{};release.handle=handle;ioctl(fd_,DRM_IOCTL_GEM_CLOSE,&release);
    }
    drm_mode_get_plane current{};current.plane_id=primaryId;
    if(ioctl(fd_,DRM_IOCTL_MODE_GETPLANE,&current)!=0||current.fb_id!=primaryFb||current.crtc_id!=crtcId_)
        return fail("primary_changed","Falcon framebuffer changed during display probe");
    const bool alpha=info_.queryResult==0&&info_.format==DRM_FORMAT_ARGB8888;
    info_.reason=alpha?"underlay_capabilities":info_.queryResult<0?
        "framebuffer_query_failed errno="+std::to_string(info_.queryError):
        "primary_format="+std::to_string(info_.format);
    if(candidates.empty())return fail("no_free_rgb_plane","No idle RGB overlay is available");
    hostPlane_=primaryId;hostZprop_=findProp(hostPlane_,DRM_MODE_OBJECT_PLANE,"zpos",&hostOriginalZ_);
    propOutFence_=findProp(crtcId_,DRM_MODE_OBJECT_CRTC,"OUT_FENCE_PTR");
    crtcIndex_=crtcIndex;
    auto range=[&](uint32_t property,uint64_t currentZ,PlaneZRange &out) {
        auto get=DRM_SYM(drmModeGetProperty);auto release=DRM_SYM(drmModeFreeProperty);
        auto p=get&&property?get(fd_,property):nullptr;
        const bool valid=p&&(p->flags&DRM_MODE_PROP_RANGE)&&p->count_values>=2;
        if(valid)out={p->values[0],p->values[1],currentZ,!(p->flags&DRM_MODE_PROP_IMMUTABLE)};
        if(p&&release)release(p);
        return valid;
    };
    PlaneZRange host{};
    if(!range(hostZprop_,hostOriginalZ_,host))return fail("host_order_unknown","Falcon plane order cannot be verified");
    info_.hostRange={{host.low,host.high,host.current}};
    std::string completionFailure;
    const bool atomicClient=atomic_;
    for(int upper=0;upper<=(allowUpper_?1:0);++upper) {
        if(!upper&&!alpha)continue;
        for(const auto &candidate:candidates) {
            uint64_t currentZ=0;
            const auto zprop=findProp(candidate.id,DRM_MODE_OBJECT_PLANE,"zpos",&currentZ);
            PlaneZRange native{};UnderlayOrder order{};
            if(!range(zprop,currentZ,native))continue;
            if(!(upper?upperOverlayOrder(native,host,order):underlayOrder(native,host,order))||order.native>UINT32_MAX)continue;
            planeId_=candidate.id;propZpos_=zprop;nativeOriginalZ_=currentZ;
            nativeFormat_=candidate.format;zpos_=uint32_t(order.native);hostDesiredZ_=order.host;
            minZpos_=uint32_t(native.low);maxZpos_=uint32_t(native.high);useNv12_=false;
            propFb_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"FB_ID");
            propCrtc_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"CRTC_ID");
            propSrcX_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"SRC_X");
            propSrcY_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"SRC_Y");
            propSrcW_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"SRC_W");
            propSrcH_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"SRC_H");
            propCrtcX_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"CRTC_X");
            propCrtcY_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"CRTC_Y");
            propCrtcW_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"CRTC_W");
            propCrtcH_=findProp(planeId_,DRM_MODE_OBJECT_PLANE,"CRTC_H");
            atomic_=atomicClient&&propOutFence_&&propFb_&&propCrtc_&&propSrcX_&&propSrcY_&&propSrcW_&&propSrcH_&&
                propCrtcX_&&propCrtcY_&&propCrtcW_&&propCrtcH_&&DRM_SYM(drmModeAtomicAlloc)&&DRM_SYM(drmModeAtomicFree)&&
                DRM_SYM(drmModeAtomicAddProperty)&&DRM_SYM(drmModeAtomicCommit)&&DRM_SYM(drmHandleEvent);
            info_.mode=upper?CompositionMode::UpperOverlay:CompositionMode::Underlay;
            uint64_t active=1;
            const bool inactive=findProp(crtcId_,DRM_MODE_OBJECT_CRTC,"ACTIVE",&active)&&!active;
            // A sleeping CRTC cannot validate a visible plane. Keep the proven
            // property/event path selected and let present() wait for wakeup.
            if(atomic_&&!inactive) {
                DrmBuf probe{};int x=0,y=0,w=0,h=0;
                mapToCrtc(&x,&y,&w,&h);
                const bool allocated=allocBuf(&probe,w,h);
                errno=0;
                const bool accepted=allocated&&commit(probe.fbId,x,y,w,h,w,h,true);
                const int testError=errno;
                const bool deferred=allocated&&(temporarilyUnavailable_||testError==EBUSY||testError==EAGAIN);
                if(!accepted&&!deferred&&error_.empty())error_="Atomic probe rejected: "+std::to_string(testError);
                freeBuf(&probe);
                if(!accepted&&!deferred) {
                    atomic_=false;info_.reason="atomic_test_rejected: "+error_;
                }
            }
            if(!atomic_) {
                if(!upper){info_.mode=CompositionMode::Unselected;continue;}
                if(!DRM_SYM(drmModeSetPlane)||!DRM_SYM(drmModeObjectSetProperty)||!DRM_SYM(drmHandleEvent))continue;
                error_.clear();
                const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(probeDeadline_-std::chrono::steady_clock::now()).count();
                if(remaining<=0){completionFailure="Display probe deadline expired before vblank proof";break;}
                if(!queueVblank()||!finishPendingFlip(true,static_cast<int>(std::min<int64_t>(100,remaining)))) {
                    completionFailure=error_;
                    if(flipPending_){completionFailed_=true;return fail("completion_unavailable","Cannot confirm legacy vblank completion");}
                    continue;
                }
            }
            info_.mode=upper?CompositionMode::UpperOverlay:CompositionMode::Underlay;
            info_.plane=planeId_;info_.nativeFormat=nativeFormat_;info_.atomic=atomic_;
            info_.completion=atomic_?"page_flip_event":"crtc_vblank";
            info_.nativeRange={{native.low,native.high,native.current}};
            info_.stage="ready";error_.clear();
            return true;
        }
    }
    info_.mode=CompositionMode::Unselected;
    atomic_=atomicClient;planeId_=0;
    if(!completionFailure.empty())return fail("completion_unavailable",completionFailure);
    if(!alpha&&!allowUpper_)return fail(info_.queryResult<0?"framebuffer_query_failed":"primary_format_incompatible",info_.reason);
    return fail("order_or_completion_unsupported","No safe overlay order and completion path is available; "+info_.reason);
}

uint32_t Overlay::findProp(uint32_t objId, uint32_t objType, const char* name, uint64_t* value)
{
    decltype(&drmModeObjectGetProperties) getProps = DRM_SYM(drmModeObjectGetProperties);
    decltype(&drmModeFreeObjectProperties) freeProps = DRM_SYM(drmModeFreeObjectProperties);
    decltype(&drmModeGetProperty) getProp = DRM_SYM(drmModeGetProperty);
    decltype(&drmModeFreeProperty) freeProp = DRM_SYM(drmModeFreeProperty);
    if (!getProps || !freeProps || !getProp || !freeProp) return 0;
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
    if (!addFb || addFb(fd_, width, height, nativeFormat_?nativeFormat_:kFormatArgb, handles, pitches, offsets, &buf->fbId, 0) != 0) {
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
        if(flipPending_&&!finishPendingFlip(true)) {::close(fd_);fd_=-1;}
    }
    if(upperOverlay()&&fd_>=0) {
        drm_mode_get_plane plane{};plane.plane_id=planeId_;uint64_t z=0;
        auto set=DRM_SYM(drmModeObjectSetProperty);
        if(set&&ioctl(fd_,DRM_IOCTL_MODE_GETPLANE,&plane)==0&&!plane.fb_id&&
           findProp(planeId_,DRM_MODE_OBJECT_PLANE,"zpos",&z)==propZpos_&&z==zpos_)
            set(fd_,planeId_,DRM_MODE_OBJECT_PLANE,propZpos_,nativeOriginalZ_);
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

bool Overlay::finishPendingFlip(bool wait, int waitMs)
{
    if (completionFailed_) return false;
    if (!flipPending_) return true;
    // An accepted NONBLOCK commit still owns both buffers: the new one is
    // queued and the old one may still be scanned. EBUSY at the next commit is
    // too late to protect an upload into that old buffer.
    struct pollfd event = {fd_, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    int result;
    int timeout = wait ? waitMs : 0;
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
    context.vblank_handler = &Overlay::flipComplete;
    if (handle(fd_, &context) != 0) {
        error_ = "DRM completion read failed";
        completionFailed_ = true;
    }
    return !flipPending_ && error_.empty();
}

bool Overlay::queueVblank()
{
    drm_wait_vblank request{};
    request.request.type=static_cast<drm_vblank_seq_type>(_DRM_VBLANK_RELATIVE|_DRM_VBLANK_EVENT|
        (crtcIndex_<<_DRM_VBLANK_HIGH_CRTC_SHIFT));
    request.request.sequence=1;request.request.signal=reinterpret_cast<unsigned long>(this);
    if(ioctl(fd_,DRM_IOCTL_WAIT_VBLANK,&request)!=0) {
        error_="Legacy vblank request failed: "+std::to_string(errno);return false;
    }
    flipPending_=true;return true;
}

bool Overlay::commit(uint32_t fbId, int x, int y, int w, int h, int srcW, int srcH, bool testOnly)
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

    if(upperOverlay()&&!atomic_) {
        auto set=DRM_SYM(drmModeSetPlane);auto property=DRM_SYM(drmModeObjectSetProperty);
        if(!set||!property){error_="Legacy DRM submission unavailable";return false;}
        if(fbId) {
            uint64_t currentZ=0;
            if(findProp(planeId_,DRM_MODE_OBJECT_PLANE,"zpos",&currentZ)!=propZpos_||
               (currentZ!=nativeOriginalZ_&&currentZ!=zpos_)) {error_="DRM overlay order changed";return false;}
            if(currentZ!=zpos_&&property(fd_,planeId_,DRM_MODE_OBJECT_PLANE,propZpos_,zpos_)!=0) {
                error_="Legacy overlay order rejected: "+std::to_string(errno);return false;
            }
        }
        const int rc=set(fd_,planeId_,crtcId_,fbId,0,x,y,w,h,0,0,uint32_t(srcW)<<16,uint32_t(srcH)<<16);
        if(rc) {
            if(errno==EBUSY||errno==EAGAIN)return false;
            if(errno==EACCES||errno==EPERM){temporarilyUnavailable_=true;return false;}
            error_="Legacy plane submission failed: "+std::to_string(errno);return false;
        }
        presented_=fbId!=0;
        if(!queueVblank()){completionFailed_=true;return false;}
        return true;
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
            (!fbId || upperOverlay() || addProp(req,hostPlane_,hostZprop_,hostDesiredZ_));
        if (!propsOk) {
            error_ = "DRM atomic property setup failed";
            CAMLOG("operation=atomicAddProperty failed plane=%u", planeId_);
            freeReq(req);
            return false;
        }
        // Disabling is synchronous: lifecycle teardown must retire scanout
        // before unmapping buffers. Regular frames stay nonblocking.
        const uint32_t flags = testOnly?DRM_MODE_ATOMIC_TEST_ONLY:fbId ? DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT : 0;
        const int rc = commitFn(fd_, req, flags, !testOnly&&fbId ? this : nullptr);
        const int commitError = errno;
        freeReq(req);
        if (rc == 0) {
            if(testOnly)return true;
            if(fbId&&!upperOverlay()&&hostDesiredZ_!=hostOriginalZ_)hostAdjusted_=true;
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
    if (ownsCurrentPlane()) {commit(0, 0, 0, 0, 0, 1, 1);finishPendingFlip(true);}
    else presented_ = false;
    pthread_mutex_unlock(&mu_);
}

}  // namespace camera
