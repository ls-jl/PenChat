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
| shell → native | `visibility` | foreground/background; releases pointer and camera when hidden |
| native → shell | `keyboard.open` | request ID, text, placeholder, length and inputType |
| shell → native | `keyboard.result` | matching request ID, confirmed/cancelled, text |
| native → shell | `ocr.start` / `ocr.cancel` | scoped pen-tip OCR registration / release |
| shell → native | `ocr.status` / `ocr.result` | matching request ID, completion or generic error; no logging |
| either | `exit` / `finish` | close native process / request Falcon page finish |
| native → shell | `booted`, `ready`, `fatal` | startup progress and actionable failure |
| native → shell | `navigation.state` | ordered sequence, native root and image-workspace state |
| shell → native | `navigation.policy` | matching sequence and Falcon setSupportBack result |

The native process reads `/etc/miniapp/resources/cfg.json` → `screen` once at startup.
Width and height are required; missing/invalid configuration fails visibly rather
than falling back to a device-specific resolution. `booted.screen` carries those
logical dimensions to the shell, which echoes them in `display.area`. DRM uses the
same configuration for rotation and offsets. Headless tests provide an explicit
`--screen-config` file. The hole/overlay use the full Falcon viewport.
NativeTouch reads Linux evdev directly and applies configured touch rotation/offsets
once. Falcon page touch callbacks are not registered. System IME hides the native
plane and releases touch; confirm/cancel restores the view and scroll position.
SQLite is exclusively written by the native process. The shell does not read external Storage. No API key is included in diagnostic output.

`src/launcher/ocr.js` adapts the system Global OCR signals, registers single-line
scanMethod=0, and unregisters on completion, cancellation, timeout or page exit.
It forwards text only into the native editor for review. The standalone native
pinyin engine owns composition and learning; the official editor is a manual fallback. No image stitching code, OCR models, or extra OCR network service
are bundled. The Provider database remains native-owned and changes only on Save.

All foreground pages use one read-only, nonexclusive evdev reader. It discovers a
DIRECT type-B device, reconstructs slots at SYN_REPORT and preserves release
coordinates. A new tracking ID is never treated as movement of the old contact.
No EVIOCGRAB, driver settings, raw-resolution scaling or Falcon touch fallback is
used. Headless host tests inject logical packets; production rejects IPC touches.
SYN_DROPPED cancels the active gesture, discards queued frames and resynchronizes
slots before waiting for all fingers to lift. Contacts starting outside the logical
screen cannot activate edge controls; accepted pointers retain their normal drag slop. Startup/resume with a held finger waits for
release. Background, official IME and shutdown close the descriptor.

Falcon side-back is disabled throughout the native foreground, including root chat.
The native logical left edge commits right-swipe back on release after 10% of
logical width. Root chat requests page finish; child pages reuse contextual back.
Short taps, vertical drags, retreat, cancellation and the tap-only drawer retain
their existing rules. There is no backpressed fallback or synthetic page pointer.
Hide, disposal and bridge failure restore the page's system policy.

Images use the same complete multi-touch frames as all other pages. Image workspaces
consume 1→2→1→0 transitions; keyboard and other pages cancel multi-touch until every
finger lifts. Poll waits can wake on the native fd, avoiding a frame sleep on input.

The optional navigation.state imageWorkspace flag temporarily disables the
system top panel during image editing/viewing, so top crop handles remain usable.
Editor and image policies are combined; hide, close, disposal and failure restore
the top panel without changing system settings.

## 0.2.11 native input validation (2026-10-28)

The prior Falcon page callback emitted stale slot-0 coordinates around a slot-1
contact on ORANGE and 113. Read-only kernel capture showed correct coordinates,
and raw page-event instrumentation reproduced the fault before PenChat extraction.
Alternating-slot injection previously committed 15/30 taps at each tested speed.

The native parser test verifies 1,000 exact-position taps across four slots,
release coordinates, contact replacement, multi-touch, dropped-frame cancellation,
bezel starts, held-contact startup and all four rotations. Both devices now commit
400/400 injected taps across middle C/M and edge A/L targets, with press/release
waits of 80, 20, 5, 1 and 0 ms. These are evdev injection regressions, not a claim
that zero-duration physical finger taps can be sensed by hardware.

Both boards pass native edge-back/root exit, drawer tap-only dismissal, cursor
drag/selection, official-editor cancellation and foreground/background recovery.
Both pass two-slot image zoom/pan; ORANGE also passes real camera capture/crop in
four orientations. Private and public test directories are isolated and hash-checked
after restoration; neither system MiniApp host was restarted.

Targeted ASAN/UBSAN covers the parser, edge keyboard, navigation, photo gestures
and exit/EOF/signal cleanup. Application runs use the existing narrowly scoped
MicroTeX symbol-registry leak suppression; the touch parser itself passes without
leak suppression. Screenshots and device data remain ignored local artifacts.

Full `npm test`, 14 host CTests, AArch64 build and production AMR checks pass.
The existing `aiot-cli check` advisory recommends optional cnpm; it is unchanged.
