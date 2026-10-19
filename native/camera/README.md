# Camera / display primitives

These source files were imported from `/home/ls-jl/dictpen/miniapp-projects/Camera`
on 2026-10-18. The application directly links the V4L2 capture, DRM plane,
geometry, STB image encoding, and binary image reader into `penchat-lvgl`.

Falcon bindings, the former camera engine, recording, MPP/OpenH264 dependencies,
and their prebuilt camera libraries have been removed. The camera preview is
converted into an LVGL image; the same DRM plane presents the whole application.
`native/lvgl-app/src/images.cpp` owns capture, frame validation and photo saving.

STB headers retain their license notices. `npm run build:native` rebuilds the
executable; no separate camera JSAPI is packaged. `ImageDataTest.cpp` covers
bounded, binary-safe image reads for chat attachments.

The 2026-10-20 orientation update reuses Camera's source-only accelerometer lease
and sensor guard (`accelerometer.cpp/.h`, `sensor_guard.cpp`). Source snapshot
hashes are recorded in `orientation-source.json`; no prebuilt helper is copied.
The helper is rebuilt as `penchat-sensor-guard`, shares the original cooperative
sensor lock, and restores only reporting it enabled. It never changes wake,
calibration or sampling-frequency settings. The projected-gravity classifier,
smoothing and 600 ms orientation hysteresis are ported into
`native/lvgl-app/src/photo_orientation.hpp`. Camera/edit UI rotation is separate
from the fixed system display rotation; photo RGB pixels rotate once at capture.
