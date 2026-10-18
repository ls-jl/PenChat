# Native launcher

`arm64-orange/libjsapi_penchat_launcher.so` is built from `native/launcher/` and the
source SDK in `native/vendor/`. It only launches/supervises `assets/bin/penchat-lvgl`
and forwards bounded Unix socket messages. It contains no chat, database,
Markdown, camera, or page-rendering code.

`npm run stage:native` creates a numeric content-hash loader alias in ignored
`libs/`. The distinct alias prevents a pinned older plugin path being reused by
Falcon during a hot update. No legacy camera/OpenAI/scan JSAPI is staged.

The executable statically links its C++ runtime, LVGL, Cairo, Pixman, FreeType,
MicroTeX, MD4C, image decoders and TinyXML2. System libcurl, SQLite, libc and DRM
remain device prerequisites. Rebuild via `npm run build:native`.
