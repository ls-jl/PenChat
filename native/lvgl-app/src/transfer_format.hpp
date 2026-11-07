#pragma once
#include "common.hpp"
namespace penchat {
constexpr int TransferFormatVersion = 1;
struct FormatResource {
  const char *name;
  const unsigned char *bytes;
  size_t size;
};
const std::vector<FormatResource> &transferFormatResources();
Json transferFormatDocument(const std::string &topic);
} // namespace penchat
