#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace penchat_scan {
void writeGrayPng(const std::string &path, int width, int height,
                  const std::function<void(int, uint8_t *)> &row);
}
