// Explicit, test-only readback of one already allocated DRM plane.
// Never takes master, creates/destroys buffers, changes a plane or accesses input.
#include <drm.h>
#include <drm_mode.h>
#include <drm_fourcc.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void need(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
struct Fd {
  int value;
  ~Fd() { if (value >= 0) close(value); }
};
struct Handles {
  int fd;
  uint32_t values[4] = {};
  ~Handles() {
    for (unsigned i = 0; i < 4; ++i) {
      if (!values[i]) continue;
      bool duplicate = false;
      for (unsigned j = 0; j < i; ++j) duplicate |= values[j] == values[i];
      if (!duplicate) {
        drm_gem_close release = {};
        release.handle = values[i];
        if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &release))
          std::cerr << "GEM_CLOSE failed: " << strerror(errno) << '\n';
      }
    }
  }
};
struct Mapping {
  void *data = MAP_FAILED;
  size_t size = 0;
  ~Mapping() { if (data != MAP_FAILED) munmap(data, size); }
};
static drm_mode_get_plane planeState(int fd, uint32_t id) {
  drm_mode_get_plane plane = {};
  plane.plane_id = id;
  need(ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane) == 0, "GETPLANE failed");
  return plane;
}
int main(int argc, char **argv) {
  try {
    need(argc == 4, "usage: drm-readback /dev/dri/cardN PLANE_ID OUTPUT.raw (also writes OUTPUT.raw.json)");
    need(std::strncmp(argv[1], "/dev/dri/card", 13) == 0, "explicit DRM card path required");
    char *end = nullptr;
    errno = 0;
    const unsigned long id = std::strtoul(argv[2], &end, 10);
    need(!errno && end != argv[2] && !*end && argv[2][0] != '-' && id &&
         id <= std::numeric_limits<uint32_t>::max(), "invalid plane ID");
    Fd fd{open(argv[1], O_RDONLY | O_CLOEXEC)};
    need(fd.value >= 0, "cannot open explicit DRM card read-only");
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
      const auto before = planeState(fd.value, static_cast<uint32_t>(id));
      need(before.fb_id && before.crtc_id, "requested plane is not active");
      drm_mode_fb_cmd2 fb = {};
      fb.fb_id = before.fb_id;
      Handles handles{fd.value, {0, 0, 0, 0}};
      const int queried = ioctl(fd.value, DRM_IOCTL_MODE_GETFB2, &fb);
      const int queryError = queried == 0 ? 0 : errno;
      // GETFB2 returns caller-owned GEM handles, including on validation failure.
      for (unsigned i = 0; i < 4; ++i) handles.values[i] = fb.handles[i];
      if (queried != 0)
        throw std::runtime_error("GETFB2 failed: errno=" + std::to_string(queryError) +
                                 " (" + std::strerror(queryError) +
                                 "); no unverified pixel-format fallback");
      need(fb.pixel_format == DRM_FORMAT_XRGB8888 || fb.pixel_format == DRM_FORMAT_ARGB8888,
           "only verified XRGB8888/ARGB8888 framebuffers supported");
      need(fb.width && fb.height && fb.handles[0] && !fb.offsets[0], "invalid framebuffer geometry/handle/offset");
      for (unsigned i = 1; i < 4; ++i)
        need(!fb.handles[i] && !fb.pitches[i] && !fb.offsets[i], "multi-plane framebuffer unsupported");
      for (unsigned i = 0; i < 4; ++i)
        need(!fb.modifier[i], "nonlinear framebuffer unsupported");
      need(uint64_t(fb.width) * 4 <= fb.pitches[0], "framebuffer row exceeds pitch");
      const uint64_t bytes = uint64_t(fb.pitches[0]) * fb.height;
      need(bytes && bytes <= std::numeric_limits<size_t>::max() &&
           bytes <= static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()), "framebuffer size overflow");
      drm_mode_map_dumb map = {};
      map.handle = fb.handles[0];
      need(ioctl(fd.value, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0, "MAP_DUMB failed");
      need(map.offset <= static_cast<uint64_t>(std::numeric_limits<off_t>::max()), "mapping offset overflow");
      Mapping mapping;
      mapping.size = static_cast<size_t>(bytes);
      mapping.data = mmap(nullptr, mapping.size, PROT_READ, MAP_SHARED, fd.value, static_cast<off_t>(map.offset));
      need(mapping.data != MAP_FAILED, "read-only mmap failed");
      std::vector<uint8_t> pixels(mapping.size);
      std::memcpy(pixels.data(), mapping.data, pixels.size());
      const auto after = planeState(fd.value, static_cast<uint32_t>(id));
      if (before.fb_id != after.fb_id || before.crtc_id != after.crtc_id) continue;
      // Output creation occurs only after a stable plane identity was observed.
      std::ofstream raw(argv[3], std::ios::binary | std::ios::trunc);
      need(raw.good(), "cannot create raw output");
      raw.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
      raw.close();
      need(raw.good(), "raw output write failed");
      std::ofstream meta(std::string(argv[3]) + ".json", std::ios::trunc);
      meta << "{\n  \"plane\": " << id << ",\n  \"fb\": " << fb.fb_id
           << ",\n  \"crtc\": " << before.crtc_id << ",\n  \"physical_width\": " << fb.width
           << ",\n  \"physical_height\": " << fb.height << ",\n  \"pitch\": " << fb.pitches[0]
           << ",\n  \"bytes\": " << bytes << ",\n  \"fourcc\": " << fb.pixel_format
           << ",\n  \"storage\": \"little-endian B,G,R," << (fb.pixel_format == DRM_FORMAT_ARGB8888 ? "A" : "X")
           << " with row padding\",\n  \"attempt\": " << attempt
           << ",\n  \"plane_fb_stable\": true,\n  \"pixel_atomicity_guaranteed\": false,\n"
           << "  \"rotation_applied\": false\n}\n";
      meta.close();
      need(meta.good(), "metadata output write failed");
      std::cout << "Captured plane " << id << " FB " << fb.fb_id << " (" << fb.width
                << 'x' << fb.height << ", pitch " << fb.pitches[0] << ")\n";
      return 0;
    }
    throw std::runtime_error("plane framebuffer changed during all three readback attempts");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
