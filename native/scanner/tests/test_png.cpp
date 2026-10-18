#include "png.hpp"
#include <cstdlib>
#include <fstream>
#include <png.h>
#include <stdexcept>
#include <unistd.h>
#include <vector>
using namespace penchat_scan;
namespace {
void require(bool okay, const char *reason) {
  if (!okay)
    throw std::runtime_error(reason);
}
} // namespace
int main() {
  char directory[] = "/tmp/penchat-png-XXXXXX";
  require(mkdtemp(directory), "temporary directory");
  const std::string small = std::string(directory) + "/small.png";
  const std::string large = std::string(directory) + "/large.png";
  try {
    writeGrayPng(small, 4, 3, [](int y, uint8_t *row) {
      for (int x = 0; x < 4; ++x)
        row[x] = uint8_t(y * 4 + x);
    });
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    require(png_image_begin_read_from_file(&image, small.c_str()),
            "decode PNG");
    image.format = PNG_FORMAT_GRAY;
    std::vector<uint8_t> decoded(PNG_IMAGE_SIZE(image));
    require(image.width == 4 && image.height == 3, "PNG dimensions");
    require(png_image_finish_read(&image, nullptr, decoded.data(), 0, nullptr),
            "decode pixels");
    png_image_free(&image);
    for (unsigned i = 0; i < decoded.size(); ++i)
      require(decoded[i] == i, "lossless grayscale rows");
    uint32_t random = 123456789;
    bool limited = false;
    try {
      writeGrayPng(large, 4096, 4096, [&](int, uint8_t *row) {
        for (int x = 0; x < 4096; ++x) {
          random ^= random << 13;
          random ^= random >> 17;
          random ^= random << 5;
          row[x] = uint8_t(random);
        }
      });
    } catch (const std::runtime_error &) {
      limited = true;
    }
    require(limited, "16MiB encoded limit");
    require(access(large.c_str(), F_OK) != 0 &&
                access((large + ".part").c_str(), F_OK) != 0,
            "failed export cleanup");
  } catch (...) {
    unlink(small.c_str());
    unlink(large.c_str());
    unlink((large + ".part").c_str());
    rmdir(directory);
    throw;
  }
  unlink(small.c_str());
  rmdir(directory);
}
