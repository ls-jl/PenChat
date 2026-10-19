#pragma once

#include <string>

namespace camera {

struct Acceleration {
    bool ok = false;
    int session = 0;
    int x = 0, y = 0, z = 0;
    std::string error;
};

bool parseAcceleration(const std::string& text, Acceleration* result);
Acceleration startAccelerometer(const std::string& workspace);
Acceleration readAccelerometer(int session);
bool stopAccelerometer(int session = 0);
bool accelerometerRunning();

}  // namespace camera
