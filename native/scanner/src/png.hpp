#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
namespace penchat_scan {
struct PngTiming { uint64_t rowsUs=0, finishUs=0, syncUs=0; };
enum class PngEncoding { Standard, Fast };
class PngSizeLimit : public std::runtime_error {
public:
  PngSizeLimit() : std::runtime_error("scan PNG exceeds 16 MiB") {}
};
void writeGrayPng(const std::string &path, int width, int height,
                  const std::function<void(int, uint8_t *)> &row,
                  PngTiming *timing=nullptr, PngEncoding encoding=PngEncoding::Standard);
}
