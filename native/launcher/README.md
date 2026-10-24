# Thin launcher / LVGL bridge

Falcon loads the source-built `penchatLauncher` module. `start()` creates an inherited
Unix `SOCK_SEQPACKET` socketpair and starts the independent executable. Only that
socket and stdout/stderr survive `exec`; no stock UI file descriptors are inherited.
`poll()`, `send()`, `status()` and `stop()` are serialized. Foreground recovery checks
child health even if Falcon suspended the polling timer. Both normal child reaping
and a host that already reaped the child are handled. An unexpected child exit shows
the shell's retry page. EOF makes the native main loop clean up and exit.

Every wire message contains `v: 1`, `type`, `requestId` and `generation`; packets are
limited to 64 KiB. The launcher increments the generation for each native start.
Native events and IME completions from older generations/requests are discarded.
Keyboard IDs are opaque strings; ordinary messages use monotonically increasing IDs.

| Direction | Message | Meaning |
|---|---|---|
| shell → native | `init` | generation and clean native initialization |
| shell → native | `display.area` | full logical hole bounds from the native `booted.screen` descriptor |
| native → shell | `display.ready` | accepted dimensions and device rotation/offsets |
| shell → native | `touch` | Falcon pageX/pageY logical coordinates, pressed state and optional `cancelled` flag |
| shell → native | `visibility` | foreground/background; releases pointer and camera when hidden |
| native → shell | `keyboard.open` | request ID, text, placeholder, length and inputType |
| shell → native | `keyboard.result` | matching request ID, confirmed/cancelled, text |
| native → shell | `ocr.start` / `ocr.cancel` | scoped pen-tip OCR registration / release |
| shell → native | `ocr.status` / `ocr.result` | matching request ID, completion or generic error; no logging |
| either | `exit` / `finish` | close native process / request Falcon page finish |
| native → shell | `booted`, `ready`, `fatal` | startup progress and actionable failure |

The native process reads `/etc/miniapp/resources/cfg.json` → `screen` once at startup.
Width and height are required; missing/invalid configuration fails visibly rather
than falling back to a device-specific resolution. `booted.screen` carries those
logical dimensions to the shell, which echoes them in `display.area`. DRM uses the
same configuration for rotation and offsets. Headless tests provide an explicit
`--screen-config` file. The hole/overlay use the full Falcon viewport.
Touch coordinates are already transformed by Falcon. System IME hides the native
plane and releases touch; confirm/cancel restores the view and scroll position.
SQLite is exclusively written by the native process. The shell does not read external Storage. No API key is included in diagnostic output.

`src/launcher/ocr.js` adapts the system Global OCR signals, registers single-line
scanMethod=0, and unregisters on completion, cancellation, timeout or page exit.
It forwards text only; the native page prepares the candidate and opens the English
IME for review. No image stitching code, OCR models, or extra OCR network service
are bundled. The Provider database remains native-owned and changes only on Save.

The v1 touch payload accepts `cancelled: true` on system touch cancellation or
background release. Native resets pointer/gesture state without activating a click
or committing the pull-to-create gesture. Older payloads without this flag retain
normal press/release behavior; the protocol version and socket format are unchanged.
