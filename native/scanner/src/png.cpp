#include "png.hpp"
#include <cerrno>
#include <fcntl.h>
#include <png.h>
#include <stdexcept>
#include <unistd.h>
#include <vector>
namespace penchat_scan {
namespace {
struct Output {
  int fd;
  size_t bytes;
};
void output(png_structp png, png_bytep bytes, png_size_t count) {
  auto &o = *static_cast<Output *>(png_get_io_ptr(png));
  if (count > 16 * 1024 * 1024 - o.bytes)
    png_error(png, "PNG exceeds 16 MiB");
  while (count) {
    auto n = ::write(o.fd, bytes, count);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      png_error(png, "PNG write failed");
    bytes += n;
    count -= size_t(n);
    o.bytes += size_t(n);
  }
}
} // namespace
void writeGrayPng(const std::string &path, int w, int h,
                  const std::function<void(int, uint8_t *)> &row) {
  if (w < 1 || h < 1 || w > 1024 * 1024)
    throw std::runtime_error("unsupported scan image dimensions");
  auto part = path + ".part";
  int fd = open(part.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0)
    throw std::runtime_error("cannot create scan image");
  auto png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  auto info = png ? png_create_info_struct(png) : nullptr;
  Output out{fd, 0};
  std::vector<uint8_t> pixels(w);
  bool okay = false;
  if (png && info && !setjmp(png_jmpbuf(png))) {
    png_set_write_fn(png, &out, output, nullptr);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; ++y) {
      bool read = true;
      try {
        row(y, pixels.data());
      } catch (...) {
        read = false;
      }
      if (!read)
        png_error(png, "scan row unavailable");
      png_write_row(png, pixels.data());
    }
    png_write_end(png, info);
    okay = true;
  }
  if (png)
    png_destroy_write_struct(&png, info ? &info : nullptr);
  if (okay && fsync(fd))
    okay = false;
  if (close(fd))
    okay = false;
  if (okay && rename(part.c_str(), path.c_str()))
    okay = false;
  if (!okay) {
    unlink(part.c_str());
    throw std::runtime_error("scan PNG failed or exceeds 16 MiB");
  }
}
} // namespace penchat_scan
