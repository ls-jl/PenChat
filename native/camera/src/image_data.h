#pragma once
#include <string>

namespace camera {
bool imageDataUrl(const std::string& path, std::string* output, std::string* error);
}
