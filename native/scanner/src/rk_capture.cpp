#include "scan_platform.hpp"
#include "hardware_ops.hpp"
#include "start_policy.hpp"
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <unistd.h>
namespace penchat_scan {
using namespace hardware_detail;
namespace {
class RkCapture final : public CaptureStream {
  struct Buffer {void *bytes=nullptr;std::size_t size=0;};
  const CaptureProfile profile_;
  int video_=-1,sensor_=-1,stride_=0;
  bool stream_=false;
  std::vector<Buffer> buffers_;
  void open(const ResolvedDeviceProfile &resolved) {
    // Revalidate identity before changing the capture format or sensor controls.
    auto sensorName = hardware_detail::read("/sys/class/video4linux/v4l-subdev2/name");
    require(sensorName.find(profile_.sensor) != std::string::npos,
            "unsupported pen sensor");
    v4l2_format f{};
    const auto &candidates=resolved.videoNodes;
    for(const auto &candidate:candidates) {
      const auto &path=candidate;
      video_=::open(path.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(video_<0)continue;
      f={};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      f.fmt.pix_mp.width=profile_.width;f.fmt.pix_mp.height=profile_.height;
      f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;f.fmt.pix_mp.num_planes=1;
      if(io(video_,VIDIOC_S_FMT,&f)==0 && f.fmt.pix_mp.width==unsigned(profile_.width) &&
         f.fmt.pix_mp.height==unsigned(profile_.height) && f.fmt.pix_mp.num_planes==1 &&
         f.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12) {break;}
      ::close(video_);video_=-1;
    }
    require(video_>=0,"unsupported pen NV12 profile");
    stride_ = f.fmt.pix_mp.plane_fmt[0].bytesperline;
    require(stride_ >= profile_.width, "invalid pen stride");
    v4l2_requestbuffers r{};
    // Absorb short scheduler stalls without dropping real sensor frames.
    r.count = 12;
    r.type = f.type;
    r.memory = V4L2_MEMORY_MMAP;
    require(io(video_, VIDIOC_REQBUFS, &r) == 0 && r.count >= 2 && r.count <= 16,
            "pen buffers unavailable");
    buffers_.resize(r.count);
    for (unsigned n = 0; n < r.count; ++n) {
      v4l2_buffer b{};
      v4l2_plane p[VIDEO_MAX_PLANES]{};
      b.type = f.type;
      b.memory = V4L2_MEMORY_MMAP;
      b.index = n;
      b.length = VIDEO_MAX_PLANES;
      b.m.planes = p;
      require(io(video_, VIDIOC_QUERYBUF, &b) == 0, "pen QUERYBUF failed");
      buffers_[n].size = p[0].length;
      auto ptr = mmap(nullptr, p[0].length, PROT_READ | PROT_WRITE, MAP_SHARED,
                      video_, p[0].m.mem_offset);
      require(ptr != MAP_FAILED, "pen mmap failed");
      buffers_[n].bytes = ptr;
      require(io(video_, VIDIOC_QBUF, &b) == 0, "pen QBUF failed");
    }
    sensor_ = ::open(profile_.sensorNode.c_str(), O_RDWR | O_CLOEXEC);
    require(sensor_ >= 0, "pen sensor unavailable");
    startRkCapture(profile_.capture.startup,[&](StreamControl control) {
      int one=1;
      if (control==StreamControl::StreamOn) {
        int type=f.type;
        require(io(video_,VIDIOC_STREAMON,&type)==0,"pen STREAMON failed");
        stream_=true;
      } else {
        const unsigned long request=control==StreamControl::QuickOnly?0x400456ea:0x400456ca;
        require(io(sensor_,request,&one)==0,"pen quick stream control failed");
      }
    },[](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));},
      [&](unsigned ms) {
        bool any=false;unsigned consecutive=0;std::uint32_t previous=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
        while (consecutive<16 && std::chrono::steady_clock::now()<deadline) {
          if (!read(FrameConsumer([&](const FrameView &frame) {
            const auto current=std::uint32_t(frame.sequence);
            consecutive=any && current==previous+1?consecutive+1:1;
            any=true;previous=current;
          }))) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return consecutive>=16?StartupFrames::Stable:any?StartupFrames::Unstable:StartupFrames::None;
      });
  }
  void close() noexcept {
    if (sensor_ >= 0 && profile_.capture.startup == StartupPolicy::DoubleQuick) {
      int zero = 0;
      io(sensor_, 0x400456ca, &zero);
      io(sensor_, 0x400456ea, &zero);
    }
    if (video_ >= 0 && stream_) {
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      io(video_, VIDIOC_STREAMOFF, &type);
    }
    stream_ = false;
    for (auto &b : buffers_)
      if (b.bytes)
        munmap(b.bytes, b.size);
    buffers_.clear();
    if (video_ >= 0)
      ::close(video_);
    if (sensor_ >= 0)
      ::close(sensor_);
    video_=sensor_=-1;
  }
public:
  explicit RkCapture(const ResolvedDeviceProfile &resolved):profile_(resolved.profile) {
    try {open(resolved);} catch (...) {close();throw;}
  }
  ~RkCapture() override {close();}
  void exposure(bool enabled) override {
    if (!profile_.gatesExposure()) return;
    int value=enabled?1:0;
    require(sensor_>=0 && io(sensor_,0x400456ca,&value)==0,"cannot change pen exposure");
  }
  bool read(const FrameConsumer &consume) override {
    v4l2_buffer b{};
    v4l2_plane p[VIDEO_MAX_PLANES]{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    b.memory = V4L2_MEMORY_MMAP;
    b.length = VIDEO_MAX_PLANES;
    b.m.planes = p;
    if (io(video_, VIDIOC_DQBUF, &b) < 0) {
      if (errno == EAGAIN || errno == EINTR)
        return false;
      throw std::runtime_error("pen DQBUF failed");
    }
    try {
      require(b.index < buffers_.size(), "invalid pen buffer index");
      auto &v = buffers_[b.index];
      size_t used = p[0].bytesused, off = p[0].data_offset;
      require(off <= used && used <= v.size &&
                  used - off >= size_t(stride_) * profile_.height * 3 / 2,
              "invalid pen NV12 payload");
      require(!(b.flags & V4L2_BUF_FLAG_ERROR), "pen frame error");
      // These verified RK capture profiles timestamp buffers in CLOCK_MONOTONIC.
      require(b.timestamp.tv_sec>=0 && b.timestamp.tv_sec<=INT64_MAX/1000000-1 &&
              b.timestamp.tv_usec>=0 && b.timestamp.tv_usec<1000000,"invalid pen timestamp");
      const FrameView frame{static_cast<const uint8_t *>(v.bytes)+off,used-off,
        profile_.width,profile_.height,stride_,b.sequence,
        int64_t(b.timestamp.tv_sec)*1000000+b.timestamp.tv_usec};
      validateFrame(frame);consume(frame);
    } catch (...) {
      io(video_, VIDIOC_QBUF, &b);
      throw;
    }
    require(io(video_, VIDIOC_QBUF, &b) == 0, "pen requeue failed");
    return true;
  }
};
}
std::shared_ptr<CaptureStream> openRkCapture(const ResolvedDeviceProfile &p) {
  return std::make_shared<RkCapture>(p);
}
} // namespace penchat_scan
