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
| native → shell | `navigation.state` | ordered sequence and root-only system side-exit permission |
| shell → native | `navigation.policy` | matching sequence and Falcon setSupportBack result |
| shell → native | `navigation.back` | firmware backpressed completion with page sequence and physical gesture ID |

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
It forwards text only into the native editor for review. The standalone native
pinyin engine owns composition and learning; the official editor is a manual fallback. No image stitching code, OCR models, or extra OCR network service
are bundled. The Provider database remains native-owned and changes only on Save.

The v1 touch payload accepts `cancelled: true` on system touch cancellation or
background release. Native resets pointer/gesture state without activating a click
or committing the pull-to-create gesture. Older payloads without this flag retain
normal press/release behavior; the protocol version and socket format are unchanged.

Only unobstructed root chat enables Falcon's `setSupportBack(true)`. Native child
pages, image viewing, the editor, menus and modals disable the shell's exit gesture.
When raw touch remains available, the native logical left edge owns right-swipe
back: commit on release after 10% of logical width, cancel on retreat/interruption,
and otherwise replay taps or
vertical scrolling. Interior image pan, text selection and horizontal tables keep
their existing handlers. Back reuses each page's contextual return action; the
history drawer retains its tap-only dismissal. Firmware may cancel raw touches
before emitting `backpressed`; the shell forwards that completion, and native
validates the frozen edge target and deduplicates it against the raw release by
physical gesture ID. Hide, disposal and bridge
failure restore Falcon policy; resume reapplies native state. Sequence and process
generation checks prevent stale permissions from re-enabling exit over a child page.


Image workspaces also accept optional `points: [{id,x,y}]` in v1 touch packets.
The launcher preserves one physical gesture through 1→2→1→0 fingers and forwards
logical coordinates without another scale/rotation. Other pages cancel multi-touch
until all contacts lift. On ORANGE and Y02 the firmware page callback only reports
one contact despite hardware MT slots. In those image workspaces alone the native
app opens a nonexclusive, read-only evdev descriptor, reconstructs complete MT
frames, and maps raw coordinates once using Falcon's tp_direction/tp_xoffset/
tp_yoffset. Duplicate page touches are suppressed while that source owns the image
workspace; background/exit closes it. It never grabs the input device or changes
its configuration. See docs/IMAGE-GESTURES.md.
The optional navigation.state imageWorkspace flag temporarily disables the
system top panel during image editing/viewing, so top crop handles remain usable.
Editor and image policies are combined; hide, close, disposal and failure restore
the top panel without changing system settings.
