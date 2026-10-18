#include "capture.h"

#include "camera_logic.h"
#include "util.h"

#include <cstdlib>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>

namespace camera {
namespace {

#define CAMLOG(fmt, ...) syslog(LOG_INFO, "miniapp-camera: " fmt, ##__VA_ARGS__)

int xioctl(int fd, unsigned long req, void* arg)
{
    int rc;
    do {
        rc = ioctl(fd, req, arg);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

#ifndef V4L2_PIX_FMT_NV12M
#define V4L2_PIX_FMT_NV12M v4l2_fourcc('N', 'M', '1', '2')
#endif

std::string fourccText(uint32_t pix)
{
    char text[5] = {
        static_cast<char>(pix & 0xff),
        static_cast<char>((pix >> 8) & 0xff),
        static_cast<char>((pix >> 16) & 0xff),
        static_cast<char>((pix >> 24) & 0xff),
        0
    };
    return text;
}

bool applyAspectCrop(int fd, int bufType, int outW, int outH)
{
    if (fd < 0 || outW <= 0 || outH <= 0) return false;
    v4l2_selection bounds;
    memset(&bounds, 0, sizeof(bounds));
    bounds.type = bufType;
    bounds.target = V4L2_SEL_TGT_CROP_BOUNDS;
    if (xioctl(fd, VIDIOC_G_SELECTION, &bounds) != 0) return false;
    const int bw = static_cast<int>(bounds.r.width);
    const int bh = static_cast<int>(bounds.r.height);
    if (bw < 16 || bh < 16) return false;
    const CropRect wanted = aspectCrop(bw, bh, outW, outH);
    if (!wanted.applied) return false;
    v4l2_selection crop;
    memset(&crop, 0, sizeof(crop));
    crop.type = bufType;
    crop.target = V4L2_SEL_TGT_CROP;
    crop.r.left = static_cast<__s32>(wanted.left);
    crop.r.top = static_cast<__s32>(wanted.top);
    crop.r.width = static_cast<__u32>(wanted.width);
    crop.r.height = static_cast<__u32>(wanted.height);
    if (xioctl(fd, VIDIOC_S_SELECTION, &crop) != 0) {
        CAMLOG("operation=S_SELECTION errno=%d fallback=full-frame bounds=%dx%d out=%dx%d",
            errno, bw, bh, outW, outH);
        return false;
    }
    CAMLOG("operation=S_SELECTION crop=%dx%d+%d+%d bounds=%dx%d out=%dx%d",
        wanted.width, wanted.height, wanted.left, wanted.top, bw, bh, outW, outH);
    return true;
}

}  // namespace

Capture::Capture()
    : fd_(-1)
    , streaming_(false)
    , mplane_(false)
    , bufType_(V4L2_BUF_TYPE_VIDEO_CAPTURE)
    , nplanes_(1)
    , width_(0)
    , height_(0)
    , yStride_(0)
    , uvStride_(0)
    , focusRegion_(false)
{
}

Capture::~Capture()
{
    close();
}

std::vector<std::string> preferredDevicePaths()
{
    std::vector<std::string> prefer;
    const char* env = getenv("CAMERA_VIDEO_DEVICES");
    if (env && env[0]) {
        std::string text(env);
        size_t at = 0;
        while (at < text.size()) {
            size_t comma = text.find(',', at);
            if (comma == std::string::npos) comma = text.size();
            const std::string item = text.substr(at, comma - at);
            if (!item.empty()) prefer.push_back(item);
            if (comma >= text.size()) break;
            at = comma + 1;
        }
    }
    if (prefer.empty()) {
        prefer.push_back("/dev/video30");
        prefer.push_back("/dev/video29");
    }
    return prefer;
}

std::vector<std::string> Capture::listDevices()
{
    std::vector<std::string> names;
    DIR* dir = opendir("/dev");
    if (!dir) return names;
    while (dirent* ent = readdir(dir)) {
        if (strncmp(ent->d_name, "video", 5) != 0) continue;
        names.push_back(std::string("/dev/") + ent->d_name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    const std::vector<std::string> prefer = preferredDevicePaths();
    std::vector<std::string> ordered;
    for (size_t i = 0; i < prefer.size(); ++i) {
        if (std::find(names.begin(), names.end(), prefer[i]) != names.end() &&
            std::find(ordered.begin(), ordered.end(), prefer[i]) == ordered.end())
            ordered.push_back(prefer[i]);
    }
    for (size_t i = 0; i < names.size(); ++i) {
        if (std::find(ordered.begin(), ordered.end(), names[i]) == ordered.end())
            ordered.push_back(names[i]);
    }
    return ordered;
}

bool Capture::openControls()
{
    close();
    const std::vector<std::string> devices = listDevices();
    for (size_t i = 0; i < devices.size(); ++i) {
        const int fd = ::open(devices[i].c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            ::close(fd);
            continue;
        }
        const unsigned caps = cap.device_caps ? cap.device_caps : cap.capabilities;
        if (!(caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
            ::close(fd);
            continue;
        }
        fd_ = fd;
        device_ = devices[i];
        if (queryControls()) return true;
        close();
    }
    return false;
}

bool Capture::open(const std::string& device, int width, int height)
{
    close();
    std::vector<std::string> candidates;
    if (!device.empty()) candidates.push_back(device);
    const std::vector<std::string> listed = listDevices();
    candidates.insert(candidates.end(), listed.begin(), listed.end());

    const std::vector<std::string> prefer = preferredDevicePaths();
    bool preferredExists = false;
    for (size_t i = 0; i < prefer.size(); ++i) {
        struct stat st;
        if (stat(prefer[i].c_str(), &st) == 0 && S_ISCHR(st.st_mode)) preferredExists = true;
    }
    bool sawBusy = false;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::string& path = candidates[i];
        const bool preferred = std::find(prefer.begin(), prefer.end(), path) != prefer.end();
        if (device.empty() && preferredExists && !preferred) continue;
        const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            ::close(fd);
            continue;
        }
        const unsigned caps = cap.device_caps ? cap.device_caps : cap.capabilities;
        const bool mplane = (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
        const bool capture = mplane || ((caps & V4L2_CAP_VIDEO_CAPTURE) != 0);
        if (!capture) {
            ::close(fd);
            continue;
        }
        const int bufType = mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        const int wantW = width > 0 ? width : 1280;
        const int wantH = height > 0 ? height : 720;

        v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = bufType;
        if (mplane) {
            fmt.fmt.pix_mp.width = wantW;
            fmt.fmt.pix_mp.height = wantH;
            fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
            fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
            fmt.fmt.pix_mp.num_planes = 1;
        } else {
            fmt.fmt.pix.width = wantW;
            fmt.fmt.pix.height = wantH;
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
            fmt.fmt.pix.field = V4L2_FIELD_NONE;
        }
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
            if (errno == EBUSY) sawBusy = true;
            CAMLOG("operation=S_FMT device=%s errno=%d", path.c_str(), errno);
            ::close(fd);
            continue;
        }
        const uint32_t pix = mplane ? fmt.fmt.pix_mp.pixelformat : fmt.fmt.pix.pixelformat;
        const int outW = mplane ? static_cast<int>(fmt.fmt.pix_mp.width) : static_cast<int>(fmt.fmt.pix.width);
        const int outH = mplane ? static_cast<int>(fmt.fmt.pix_mp.height) : static_cast<int>(fmt.fmt.pix.height);
        const int planeCount = mplane ? (fmt.fmt.pix_mp.num_planes ? static_cast<int>(fmt.fmt.pix_mp.num_planes) : 1) : 1;
        const int yStride = mplane
            ? static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline)
            : static_cast<int>(fmt.fmt.pix.bytesperline ? fmt.fmt.pix.bytesperline : fmt.fmt.pix.width);
        const int uvStride = (mplane && planeCount > 1)
            ? static_cast<int>(fmt.fmt.pix_mp.plane_fmt[1].bytesperline)
            : yStride;
        CAMLOG("operation=S_FMT device=%s format=%s %dx%d planes=%d stride=%d uvStride=%d",
            path.c_str(), fourccText(pix).c_str(), outW, outH, planeCount, yStride, uvStride);
        if (pix != V4L2_PIX_FMT_NV12 && pix != V4L2_PIX_FMT_NV12M) {
            CAMLOG("operation=S_FMT device=%s unsupported format", path.c_str());
            ::close(fd);
            continue;
        }
        if (planeCount < 1 || (pix == V4L2_PIX_FMT_NV12M && planeCount < 2) || yStride < outW) {
            ::close(fd);
            continue;
        }
        if (outW * 2 < wantW || outH * 2 < wantH) {
            CAMLOG("operation=S_FMT device=%s rejected size=%dx%d requested=%dx%d",
                path.c_str(), outW, outH, wantW, wantH);
            ::close(fd);
            continue;
        }
        applyAspectCrop(fd, bufType, outW, outH);

        v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 4;
        req.type = bufType;
        req.memory = V4L2_MEMORY_MMAP;
        bool reqOk = false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (xioctl(fd, VIDIOC_REQBUFS, &req) == 0 && req.count >= 2) {
                reqOk = true;
                break;
            }
            if (errno != EBUSY) break;
            usleep(50000);
            req.count = 4;
        }
        if (!reqOk) {
            if (errno == EBUSY) sawBusy = true;
            CAMLOG("operation=REQBUFS device=%s errno=%d", path.c_str(), errno);
            ::close(fd);
            continue;
        }
        buffers_.resize(req.count);
        bool mapOk = true;
        for (unsigned n = 0; n < req.count; ++n) {
            v4l2_buffer buf;
            v4l2_plane planes[VIDEO_MAX_PLANES];
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));
            buf.type = bufType;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = n;
            if (mplane) {
                buf.length = fmt.fmt.pix_mp.num_planes ? fmt.fmt.pix_mp.num_planes : 1;
                buf.m.planes = planes;
            }
            if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
                mapOk = false;
                break;
            }
            const int planesToMap = mplane ? (buf.length > 0 ? static_cast<int>(buf.length) : planeCount) : 1;
            buffers_[n].nplanes = 0;
            memset(buffers_[n].planes, 0, sizeof(buffers_[n].planes));
            for (int p = 0; p < planesToMap && p < VIDEO_MAX_PLANES; ++p) {
                const size_t length = mplane ? planes[p].length : buf.length;
                const off_t offset = mplane ? planes[p].m.mem_offset : buf.m.offset;
                if (!length) {
                    mapOk = false;
                    break;
                }
                void* mapped = mmap(0, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
                if (mapped == MAP_FAILED) {
                    mapOk = false;
                    break;
                }
                buffers_[n].planes[p].start = mapped;
                buffers_[n].planes[p].length = length;
                buffers_[n].nplanes++;
            }
            if (!mapOk) break;
            if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                mapOk = false;
                break;
            }
        }
        if (!mapOk) {
            for (size_t b = 0; b < buffers_.size(); ++b) {
                for (int p = 0; p < buffers_[b].nplanes; ++p) {
                    if (buffers_[b].planes[p].start)
                        munmap(buffers_[b].planes[p].start, buffers_[b].planes[p].length);
                }
            }
            buffers_.clear();
            ::close(fd);
            continue;
        }
        int type = bufType;
        if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            CAMLOG("operation=STREAMON device=%s errno=%d", path.c_str(), errno);
            for (size_t b = 0; b < buffers_.size(); ++b) {
                for (int p = 0; p < buffers_[b].nplanes; ++p) {
                    if (buffers_[b].planes[p].start)
                        munmap(buffers_[b].planes[p].start, buffers_[b].planes[p].length);
                }
            }
            buffers_.clear();
            ::close(fd);
            continue;
        }
        fd_ = fd;
        streaming_ = true;
        mplane_ = mplane;
        bufType_ = bufType;
        nplanes_ = planeCount;
        device_ = path;
        width_ = outW;
        height_ = outH;
        yStride_ = yStride > 0 ? yStride : outW;
        uvStride_ = uvStride > 0 ? uvStride : yStride_;
        queryControls();
        CAMLOG("operation=open device=%s format=%s %dx%d planes=%d stride=%d",
            path.c_str(), fourccText(pix).c_str(), width_, height_, nplanes_, yStride_);
        return true;
    }
    error_ = sawBusy ? "camera device busy" : "no usable v4l2 capture node";
    CAMLOG("operation=openCapture error=%s", error_.c_str());
    return false;
}

void Capture::close()
{
    if (fd_ >= 0 && streaming_) {
        int type = bufType_;
        xioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    for (size_t i = 0; i < buffers_.size(); ++i) {
        for (int p = 0; p < buffers_[i].nplanes; ++p) {
            if (buffers_[i].planes[p].start)
                munmap(buffers_[i].planes[p].start, buffers_[i].planes[p].length);
        }
    }
    buffers_.clear();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    controls_.clear();
}

bool Capture::grab(CaptureFrame* frame, int timeoutMs)
{
    if (fd_ < 0) return false;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    const int r = select(fd_ + 1, &fds, 0, 0, &tv);
    if (r <= 0) return false;
    v4l2_buffer buf;
    v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = bufType_;
    buf.memory = V4L2_MEMORY_MMAP;
    if (mplane_) {
        buf.length = nplanes_ > 0 ? nplanes_ : 1;
        buf.m.planes = planes;
    }
    if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN) {
            error_ = "DQBUF failed";
            CAMLOG("operation=DQBUF device=%s errno=%d", device_.c_str(), errno);
        }
        return false;
    }
    if (buf.index >= buffers_.size() || !buffers_[buf.index].planes[0].start) {
        xioctl(fd_, VIDIOC_QBUF, &buf);
        return false;
    }
    const Buffer& slot = buffers_[buf.index];
    size_t used = mplane_ ? planes[0].bytesused : buf.bytesused;
    if (used == 0 || used > slot.planes[0].length) used = slot.planes[0].length;
    frame->data = static_cast<const uint8_t*>(slot.planes[0].start);
    frame->size = used;
    frame->width = width_;
    frame->height = height_;
    frame->yStride = yStride_;
    frame->uvStride = uvStride_ > 0 ? uvStride_ : yStride_;
    frame->nplanes = slot.nplanes;
    frame->uv = slot.nplanes > 1 ? static_cast<const uint8_t*>(slot.planes[1].start) : 0;
    if (slot.nplanes > 1) {
        size_t uvUsed = mplane_ ? planes[1].bytesused : 0;
        if (uvUsed == 0 || uvUsed > slot.planes[1].length) uvUsed = slot.planes[1].length;
        frame->uvSize = uvUsed;
    } else {
        frame->uvSize = 0;
    }
    frame->index = static_cast<int>(buf.index);
    return true;
}

bool Capture::release(int index)
{
    if (fd_ < 0 || index < 0 || static_cast<size_t>(index) >= buffers_.size()) return false;
    v4l2_buffer buf;
    v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = bufType_;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = static_cast<unsigned>(index);
    if (mplane_) {
        buf.length = buffers_[static_cast<size_t>(index)].nplanes;
        buf.m.planes = planes;
        for (int p = 0; p < static_cast<int>(buf.length) && p < VIDEO_MAX_PLANES; ++p)
            planes[p].length = static_cast<__u32>(buffers_[static_cast<size_t>(index)].planes[p].length);
    }
    return xioctl(fd_, VIDIOC_QBUF, &buf) == 0;
}

bool Capture::setControl(int id, int value)
{
    if (fd_ < 0) return false;
    v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = static_cast<unsigned>(id);
    ctrl.value = value;
    if (xioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) return false;
    for (size_t i = 0; i < controls_.size(); ++i) {
        if (controls_[i].id == id) controls_[i].value = ctrl.value;
    }
    return true;
}

bool Capture::getControl(int id, int* value)
{
    if (fd_ < 0) return false;
    v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = static_cast<unsigned>(id);
    if (xioctl(fd_, VIDIOC_G_CTRL, &ctrl) < 0) return false;
    if (value) *value = ctrl.value;
    return true;
}

bool Capture::queryControls()
{
    controls_.clear();
    if (fd_ < 0) return false;
    v4l2_queryctrl q;
    memset(&q, 0, sizeof(q));
    q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    while (xioctl(fd_, VIDIOC_QUERYCTRL, &q) == 0) {
        if (!(q.flags & V4L2_CTRL_FLAG_DISABLED) &&
            (q.type == V4L2_CTRL_TYPE_INTEGER || q.type == V4L2_CTRL_TYPE_BOOLEAN ||
                q.type == V4L2_CTRL_TYPE_MENU)) {
            V4l2Control c;
            c.id = static_cast<int>(q.id);
            c.name = reinterpret_cast<char*>(q.name);
            c.type = q.type == V4L2_CTRL_TYPE_BOOLEAN ? "bool" :
                     (q.type == V4L2_CTRL_TYPE_MENU ? "menu" : "int");
            c.min = q.minimum;
            c.max = q.maximum;
            c.step = q.step;
            c.def = q.default_value;
            c.value = q.default_value;
            getControl(c.id, &c.value);
            controls_.push_back(c);
        }
        q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    return !controls_.empty();
}

bool Capture::setFocusPoint(float nx, float ny)
{
    (void)nx;
    (void)ny;
    focusRegion_ = false;
    CAMLOG("%s", "operation=setFocusPoint result=unsupported no-region-control");
    return false;
}

}  // namespace camera
