#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace penchat_scan {
struct PngTiming { uint64_t rowsUs=0, finishUs=0, syncUs=0; };
void writeGrayPng(const std::string &path, int width, int height,
                  const std::function<void(int, uint8_t *)> &row,
                  PngTiming *timing=nullptr);
}
