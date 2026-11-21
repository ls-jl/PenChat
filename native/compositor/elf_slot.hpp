#pragma once
#include <cstdint>
#include <string>

namespace penchat_compositor {
// A single already-resolved PLT relocation. Never patches instructions or files.
struct Slot {
  std::uintptr_t* address = nullptr;
  std::uintptr_t original = 0;
  int protection = 0;
  std::string profile,firmwareSha;
  bool replace(std::uintptr_t expected, std::uintptr_t replacement, std::string& error) const;
};
Slot locate(const char* module, const char* symbol, const char* sha256 = nullptr);
// Resolve a pinned CVI Falcon ABI by the loaded image identity; no hook installed.
Slot locateCviDisplay();
}
