#pragma once
#include "profile.hpp"
#include <pen_stream/stream.hpp>

namespace penchat_scan {
std::string scanRuntimePath(const CaptureProfile &, const std::string &resources);
std::unique_ptr<pen::stream::Inference> makePlatformInference(const CaptureProfile &,
                                                            const std::string &resources);
} // namespace penchat_scan
