#pragma once
#include "capture_stream.hpp"
#include <memory>
namespace penchat_scan {
void verifyCviCaptureRuntime();
std::shared_ptr<CaptureStream> openCviCapture();
}
