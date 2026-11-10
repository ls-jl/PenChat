# Repository Guidelines

PenChat shares native LVGL UI/business components across explicit Rockchip/AArch64
and S6/CV1826/ARMv7 profiles. Hardware backends and payloads are target-isolated.
Read display geometry/rotation from Falcon, never a device-specific screen fallback.
The Falcon AMR is a thin launcher with a full-screen hole, system IME and
lifecycle forwarding. Do not put business UI or networking back into Vue.
Scanning uses the source-pinned Stream32 backend in native/scanner, with a
separate C++17 process and explicit ORANGE/Y02/Y07/MELON capture leases. Keep 32/24/8 windowing,
per-frame photometric estimates and bounded memory. Always review scan results
before appending messages. API Key entry still uses the system pen-tip OCR bridge.
Do not import legacy stitching engines or research data. The first release supports
forward paper scans only; reverse motion and dark-screen scans are not validated.
Canvas preview reads run outside the capture loop; keep only bounded latest
previews and join/cancel readers before releasing scan results.

## Structure

- `native/lvgl-app/`: native UI, Markdown/math, HTTP/SSE, SQLite, images/camera.
- `native/scanner/`: Stream32 source, isolated worker, hardware lease and row PNG encoding.
- `native/launcher/`: source-built Falcon launch/IPC bridge.
- `native/compositor/`: S6 verified Falcon shared-memory/alpha backend.
- `native/pinyin/`: locked libpinyinime source overlay and resource provenance.
- `native/camera/`: reused V4L2/DRM/image primitives; no opaque camera libraries.
- `src/launcher/`: thin AMR page; `assets/images/`: product icons.
- `native/vendor/`: SDK source for the launch bridge.

Use two-space indentation; format C++ consistently. Keep LVGL calls on its UI
thread. Use the existing queues for background work and discard stale generation
and input request IDs. With GCC 6/C++14, read JSON defaults into a local before
assigning through `operator[]` on that object; assignment operands are unordered
and `[]` may insert null before the read.
Release child processes, camera, pointers and the display
plane on lifecycle transitions. The scanner child and healthy model are an
exception: retain them until native app exit, while releasing camera/key/light,
guardian leases and per-scan results on page/background handoff. Never dereference freed LVGL object pointers;
register image cleanup on LV_EVENT_DELETE (9.6's old is_valid alias is unsafe).
Section page titles and their actions belong to the full-height scrolling reader,
using pageHeader/pageContent/pageAction. Never pin a title/action strip over menu
content. Camera/crop remain full workspaces; scan omits its redundant page title.
Fixed-height
buttons and list captions must bound labels to one line with ellipsis; never center
a wrapped label that can grow beyond its control.
Use the shared `theme.hpp` palette for all pages and native rich-text rendering.
The centered return-to-bottom control sits 12px above the composer and becomes
translucent over message content. Pull-to-create commits only on a full-progress
release; cancellation, keyboard and lifecycle changes must never commit it.
Only the root chat shows the two-line menu button; child pages show a contextual
back arrow. The drawer spans two thirds of the screen, has no menu toggle inside,
with no header or inline delete control. Rows use long-press actions for rename,
pin, archive and delete; archived chats are managed in Settings. It
closes through a right-side backdrop tap (maximum movement 12px), never a swipe or cancelled touch. Its footer has New chat on the left
and Settings on the right. Closing that drawer
must preserve generation, drafts and reading position. Pulling to create translates
the reading viewport upward; cancellation restores its transform before later
gestures. Do not translate the message container: that changes the scroll extent
and makes LVGL readjust its position during a pull.
After scrolling or jumping back to the bottom, require 500ms of quiet before
allowing a rapid pull-to-create; repeated touches and scroll inertia restart that
interval. Slow deliberate pulls bypass it. Once the progress ring appears, its
release/cancellation does not rearm the wait. Guarded pulls still translate and
retract the viewport, with smooth handoff into a 160ms return. Disable LVGL elastic
overscroll so it cannot compete with that controlled visual movement.
Freshly entered conversations with content bypass this guard. Empty conversations
and unsent drafts never enter pull-to-create. The ORANGE pull uses 12px slop plus
155px fill distance (167px total), with 0% sensitivity offset. The new baseline
is 85% of the prior sensitivity; app-global offsets range from -25% to +25%,
positive means more responsive. Freeze the offset with geometry at pointer-down. Pulls may start in the composer text entry,
but never its attachment or send controls. Scale spatial
thresholds, slow-motion speed and stretch by the unobstructed logical reader
height relative to 208px. Freeze geometry at pointer-down, excluding resting
composer space, clipping and viewport translations. Native evdev frames apply tp_direction and touch offsets exactly once; never scale
spatial gesture thresholds a second time using physical/raw touch dimensions.
Blank new chats are transient; persist the conversation and active selection
atomically with the first message. Do not add history rows for unsent drafts.
Chat model selection is per session and also updates the global new-chat default.
Resolve effort centrally: session override, process-only memory, Provider default,
then omit the API parameter. Skip unsupported levels, never silently approximate.
Model capability levels and wire mappings live in capabilities_json.reasoning;
Provider defaults and session preferences use existing metadata fields. Persist
empty-chat preferences only with its first message. Freeze request parameters at
send time. Preserve public reasoning separately from opaque Responses items;
opaque items may only replay to the same Provider/model, never appear in UI or
logs. Reasoning headers precede answers, with in-place public-summary expansion.
No returned reasoning means no header; legacy text without timing stays untimed.
Explicit api_default effort is a separate slider stop after none; it bypasses
all inherited defaults and never sends an effort field. Empty effort still means
inherit settings. Auto-title requests run once after the first assistant completes,
using its model unless Settings specifies one. Bound titles by both character count
and 80% of the measured row width. Keep auxiliary work off the UI thread and
coalesce its stream snapshots. Apply results only to the original session/token;
manual rename and deletion invalidate them. Do not backfill old history or retry
unknown-outcome title requests. Never call Input::cancel/read recursively inside
an LVGL long-press callback: enqueue an event and handle it on the next UI tick.
Provider edits must update the existing row and retain model IDs/history links.
OCR results require user review and explicit Provider save; never log raw keys.
Unregister system OCR on cancel, timeout, background, bridge failure and unload.
Y07 docked editing returns camera/light/guardian ownership to system OCR and
keeps a nonexclusive scanner pen-key reader. Before releasing its exclusive grab,
the launcher snapshots and filters only KEY134 on its own led_control evdev
clients using EVIOCGMASK/EVIOCSMASK; never mask CaptureFrame, Home or other keys.
CaptureFrame must still receive real pen edges to drive lamp and frame acquisition.
Forward matched edges to the current OCR session through named, already loaded
engine entry points, then reassert text mode (the vendor press forces image mode).
Restore every original host-client mask and close retained fds on editor close,
official IME, background, IPC EOF or native exit. Unsupported masking must reject
handoff rather than expose the home image-preview handler. Do not poll an idle
released scanner at keyboard frequency.

## Builds and checks

Read README.md for toolchain prerequisites. `npm run build:host` builds host tests;
`npm run build:native` cross-builds the executable and launcher; `npm test` runs
bridge, native and actual-process integration tests. During development, run only
checks relevant to the change; do not run the full regression/sanitizer suite or
production packaging for every request. Run `npm run check`, full release checks
and `npm run build:prod` when the user explicitly requests a new release.
`check` currently reports optional cnpm. For runtime/camera/display changes,
use targeted ORANGE verification when necessary; do not automatically test and
install on every device for each change.
Device test helpers use real touchscreen events. Authorized targets are ORANGE
MEB0400008900794, Y02 MEA2300009007113, Y07 7G12000010905426 and S6
JDA0700006552216. Do not target unlisted devices.
The user has temporarily forbidden all Y02/113 device operations; do not access
MEA2300009007113 until the user explicitly resumes it. This restriction overrides
the allowlist.
Do not restart the system MiniApp host or overwrite user data to bypass failures.
Device chat acceptance snapshots/restores the DB and uses only fake local keys.

Keep credentials, data, screenshots, research, downloads, toolchains and build
artifacts out of Git. `.deps`, `build`, `.work`, `libs`, `.falcon_`, `dist`,
`assets/bin`, `assets/microtex`, `assets/licenses` are generated/ignored. Record
source hashes in the dependency lock and preserve license notices. Runtime native
changes must regenerate/stage their binaries before packaging or installation;
source-only development does not require rebuilding all binaries. Never hand-edit generated files.
HaaS UI shell/IME reference: /persistent/home/ls-jl/dictpen/haasui-docs.

PenChat is an independent application. Never read external application databases or Storage. The album may read the shared system photo directory /userdisk/Pictures; never modify its originals. Identity comes from package.json. New installs start without providers or messages.

## Development and release workflow

- Keep ongoing and temporary changes on the local `dev` branch. `main` contains
  released work; do not merge development into it for each user request.
- Ordinary development device acceptance is performed by the user. Do not automatically
  run device test flows, screenshots or performance benchmarks unless the user asks
  for testing or a Release. Necessary compilation/package identity checks and already
  authorized installation remain allowed; never describe pending manual checks as passed.
- Commit completed logical changes locally on `dev` after appropriate targeted
  verification. Do not automatically push commits, create releases, bump the app
  version or run the full test suite for ordinary development requests.
- Only when the user explicitly requests a new release: increment the version once
  for the accumulated release (or use the user-specified version), keep package-lock
  root metadata in sync, run full regression/sanitizer/build/package checks and the
  applicable authorized-device acceptance, then merge `dev` into `main` and push.
  Do not merge or publish a release with unresolved required checks.
- Return to `dev` for subsequent development. A release request is not standing
  authorization to publish each later change. An explicit separate push/test
  request applies only to its stated scope.
- Build identity and AMR filename continue to derive from package.json. Report
  targeted validation and remaining release checks accurately; never claim an
  uncommitted change is committed or an unpushed commit is pushed.

The user authorizes future ORANGE package installation and device testing without asking again. Preserve user data and pending input where possible; never restart the system MiniApp host.

Camera captures stay in camera-tmp until crop confirmation. Confirm attaches without auto-send. Persist saveCameraToAlbum; only explicit opt-in creates a new PenChat photo in /userdisk/Pictures. Never alter existing system photos; cancellation and exit discard temporary captures.

Scanner ingress is RAM-only: copy a sensor Y plane once into releasable pages,
select asynchronously, queue prepared owners without waiting for NPU, then use
32/24/8 windows sharing those owners through rendering. Never reintroduce raw-frame
spill files or duplicate full-resolution buffers. Release rejected frames promptly;
committed originals live only as long as overlapping windows need them. Normal
seal drains all accepted work; explicit cancellation may discard queued work.
There are no fixed RSS/frame/canvas byte caps. A serialized 100ms system-memory
sample grants dynamic allocation headroom above max(32MiB, MemTotal/20); allocations
since the sample spend that headroom. Invalid samples expire after one second.
Keep allocation/geometry/overflow checks and explicit partial results on pressure.
Reclaim allocator pages only once processing is quiescent; retain the healthy model.
All supported boards continue through 1–8 consecutive missing frames and stop at
9 or more (sequence distance >=10); cumulative losses are not this threshold.
Keep timestamp reset, capture failure and stall guards explicit. Count every
lost frame and show a wrapping review warning, preserving the real stop reason;
never synthesize pixels. MELON requires RK3562 MELON + OV7251 identity, configured
IR GPIO and discovered led_control key, native 640x480 NV12 via ISP self/main path.
Use its standard-stream-first startup; only zero frames permits one quick-stream
retry. Never apply SC2356 quick-only/repeated controls or another board's gain map.
Scan-result viewing prepares/caches original row-PNG asynchronously and retains
that result; it never confirms, sends, or disarms through the ordinary viewer path.
Join/cancel the reader before releasing results on background/disconnect/exit.


Reserve the scan guardian/key lease before preparing a cold model, but start
V4L2 only after asynchronous model open plus one fixed32 warmup has succeeded.
Warmup creates no scan, event counts or canvas; cancellation must never rearm
capture later. Discard key events accumulated during preparation and require
release if the tip is still down. Keep the prepared model across disarm/rearm.
Only the Y02 backend owner thread may enable the bounded 2MiB DMA-sync adapter,
after verifying an RKNPU DRM fd and driver 0.9.1. Preserve the complete byte range,
object and direction; no retries, cancellation or skipped synchronization within
a vendor call. Unknown drivers/flags and other boards must pass through unchanged.
Retain the scanner's tested ioctl export when stripping/packaging.

Validate model-space geometry before painting. The bounded eight-thumbnail
history belongs to the render owner and dynamic budget, never full sensor-frame
copies. A failed geometric/image-consistency check stops with an explicit partial
result and retains unlocated originals; do not clamp angles, fabricate pixels or
silently drop a tail. Stable image-supported rotation must remain usable.

Normalize bounded HTTP error text to valid UTF-8 before saving or displaying it; byte truncation can split a multibyte character. Manual retry reuses the latest failed/cancelled/interrupted assistant row, excludes its partial output and reasoning from input, and resets its reasoning metadata. Never retry automatically or duplicate user/image rows.

Camera/crop surfaces may be quarter-turned without changing the 960×266 bridge/display coordinates. Map custom crop touches through photoPoint exactly once; freeze pose at the shutter and rotate saved RGB pixels once. Crop zoom (1–16×) only changes the view, never the saved pixel geometry; keep preview decoding bounded. The source-built penchat-sensor-guard shares Camera's sensor lock and restores only reporting it enabled, including parent EOF; never modify wake/calibration/frequency settings.

Schema v2 migrations must validate ownership/uniqueness before any data change and
publish a durable SQLite backup before the migration transaction. Never repair
conflicts by deleting records. Repository business transactions run on the single
DB worker; UI streaming uses versioned/coalesced 500ms snapshots and final-save
acknowledgements. Do not reintroduce UI waits for per-delta writes. Render live
reasoning from the current UI page snapshot. `models()` is credential-free except
for the endpoint needed by capability templates; resolve API keys/headers with
`requestModel()` only when a request starts. Keep both text representations atomic.
Private asset deletion is queued transactionally, then collected after commit;
respect shared references and never follow symlinks or delete system album originals.
Device tests that swap databases must isolate assets/backup files as well and use
SQLite backup snapshots for online reads. Shared icon geometry lives in icons.cpp;
use it for keyboard symbols too. History long-press actions occupy one horizontal
row with 50px touch height. Nonempty chat content has 8px top padding.

Transfer jobs run outside LVGL. Production transfer root is /userdisk/Favorite/PenChat;
host tests use a private data directory (PENCHAT_TRANSFER_ROOT is host-only).
Never export the SQLite snapshot, provider credentials/headers or opaque reasoning.
Pin assets for the lifetime of an export snapshot. Prepare imports before the single
DB-worker transaction; regenerate IDs and preserve user/global preferences. Validate
archive paths, actual size/CRC, duplicates and symlinks before preview/commit. Default
preset replacement requires a separate confirmation, both in UI and service. Device
transfer tests must isolate/restore the public transfer folder as well as DB/assets.

Export persistence defaults to disabled. Preserve explicit saved user choices. When disabled, normal native shutdown
retires only the public 导出 subtree into private transfer staging before removal;
keep 导入 and instructions. Background/IME transitions never trigger cleanup. Do not
restart or access the device while the user says it is unavailable.

Context compression is an auxiliary background job. Freeze both model configurations and pending
input; never truncate history silently. Use ordinary historical reference summaries, never promote
them to system instructions or replay covered opaque reasoning. Commit a full pre-compression
archive and checkpoint atomically on the DB worker after all batches succeed; retain shared image
references and cancel stale jobs on session replacement/deletion/shutdown. Preset compression model
empty means follow the chat model, not inherit a previous preset override. New chats never inherit
summary coverage or archive links. Device access remains forbidden while unavailable.

Screen geometry must come from Falcon's `/etc/miniapp/resources/cfg.json` screen section.
Never add a device-specific width/height fallback or resolution gate. The native booted.screen
message supplies the same dimensions to the AMR. NativeTouch owns all foreground input,
commits complete type-B SYN_REPORT frames and maps touch rotation/offsets once.
Do not reintroduce Falcon page touch forwarding, system backpressed completion or
a touch fallback. Use read-only, nonexclusive fds; close them on background, official
IME and shutdown. Native left-edge back handles child pages and root exit.
Y07's system panel can open without page onHide or loss of window focus. Observe
both $am focus and the existing Falcon panel's exported read-only visibility;
yield the DRM plane and native touch fd through window.focus, without pretending
the page has exited. Preserve drawers, composition, reading position and owned
scan results. Only native touch messages enter top-edge gesture arbitration;
interleaved panel/lifecycle/exit packets must always pass. See docs/SYSTEM-OVERLAYS.md.
Use EditorLayout for keyboard controls, decorations and touch bounds. Test fixtures may
specify explicit resolutions; camera sensor capture sizes are independent of display sizes.
Caret hit testing resolves the nearest grapheme insertion boundary, including after
the rightmost glyph. Captured horizontal drags may leave the field. Clamp only the
magnifier bubble to visible editor sides and screen safe bounds; never use its
clamped coordinates to restrict caret movement.
At a soft wrap, retain the visual upstream/downstream affinity of the finger's
row. A horizontal drag beyond a full line must stay drawn at that line end,
including after release; moving vertically selects another visual row. Keep the
rendered caret, magnifier and selection handles on that same visual boundary.
`tests/device/display_config.py --serial <explicit serial>` covers the user-authorized Y02
and ORANGE display targets using their config and discovered input device. Keep ORANGE
scanner calibration guards even when display/input support expands to another board.
Y02 is an explicit SC035GS 640×480 profile, not an ORANGE alias. Its neutral
flat-field is separately identified; never substitute the SC2356 gain map. Restore
the previous V4L2 format before resuming the system capture guardian. Use the shared dynamic system-memory admission for the native-size frame pages;
no temporary frame spill files are permitted.

Native keyboard key caps accept two-contact rollover by tracking ID, preserving
press order while each pointer releases or cancels independently. Keep text,
candidate/control mixed touches and three-contact cancellation separate; never
route keyboard rollover into LVGL's single-pointer or page-back handling. Preserve
key objects during typing/results and active touches; repaint only changed text
and candidate subtrees. Pending space/Done selection must retain subsequent keys
in bounded order, not reject them because a dictionary query is still pending.
The native editor owns libpinyinime on one background worker. Never reintroduce the Falcon
shared decoder, system pinyin imports, symbol borrowing or a fallback bridge for candidates.
The official text editor remains a manually selected fallback named 有道输入法 inside the globe long-press menu; ordinary globe taps never cycle into it. OCR is independent. Keep
26-key Chinese, English and nine-key main layouts separate from number/symbol pages.
Ordinary globe taps toggle preferred Chinese/English; rapid unedited taps under 2s cycle
Chinese26/English/Chinese9. Only actual Chinese typing changes the persisted preference.
Park unfinished Chinese compositions separately; Enter emits original letters/digits, while
finishing discards inactive compositions. Learning commits only after confirmed editing,
never from secret fields/OCR/paste/cancel. Base dictionaries are read-only; private user
deltas use bounded 10000-record/2MiB atomic files, never logs or transfer exports.

Provider compatibility options live in metadata_json and are frozen per request. Never
automatically resend, downgrade effort, switch protocol, disable TLS verification or
follow credential-bearing redirects. Use apiRequest for models/chat/title/compression.
Responses output replay requires matching provider/model/endpoint/protocol identity;
retain completed output order and assistant phase. Unknown counts stay unknown.
Coalesced stream states must also flush while the peer is idle, not only on new bytes.

Anthropic Messages is a separate protocol, never an OpenAI payload fallback. Keep signed/redacted blocks private and replay only completed same-identity messages; include API version and credential revision in that identity. Preserve cumulative usage without double counting and account for uncached/cache-read/cache-write input separately. Auxiliary compaction material is user reference data with original role/status labels, never assistant prefill or elevated system instructions. Unknown model thinking capabilities must remain opt-in.

Y07 portrait uses shared AppLayout and EditorLayout: full-width reader and a
docked bottom keyboard retaining the original page. Apply their shared rounded-
corner safe insets to composer and edge navigation. Portrait letter keys use the
full width with 4px cap margins; bottom globe/hide controls keep safe insets. Landscape
geometry stays unchanged. Portrait editing attaches the native textarea directly
inside the focused original control; never add a second bottom editor. Chat raises
its existing composer above composition/candidates/actions; form fields retain
their original location, with long multiline fields expanding in the form. The
function row stays directly above the keys; globe and hide/finish sit in the lower
utility row. Chat has only hide (retains draft), while form Cancel still restores
the original field. The empty portrait composer with keyboard hidden is one 94px row. With text or
an open keyboard, give text its own full-width region above attachment/gauge/send;
grow upward to 3x the original height, then scroll. Expanded portrait composers use a
12px inner inset and bottom-aligned tool row; landscape keeps text between its
controls, one row for short drafts and at most 2x height with local text scrolling.
Its corner radius stays at the single-row radius when growing. Wide-landscape upward reader
drags hide the composer, downward drags reveal it without changing follow state.
Compact-landscape (currently S6) shows the composer only at the actual latest
message bottom, with the existing 10px tolerance; direction reversal alone must
not reveal it. All composer placement paths share this policy. Portrait keeps
the composer visible. A page-origin downward drag that crosses
the docked keyboard panel may push it down; release beyond the frozen threshold
closes editing without sending, otherwise return over 160ms. Key/caret/selection
drags never dismiss. Lifecycle and multi-touch cancel the push and reset transforms.
The portrait attachment menu expands upwards from the composer, capped at five
44px rows plus padding; remaining items scroll locally, never stretch to the top. The collapsed portrait
pill alone keeps 25px around the 44px send circle. Chinese
composition uses the same row as the official/OCR/clipboard actions; never add
a separate candidate row in portrait. The borderless globe is 1.5x, below and
slightly left of comma, centered 3px above the midpoint between the comma key bottom edge and screen bottom.
Keep the existing grid: latitude ends overlap the outer stroke centre and
meridians reach its poles without extending beyond the circle.
The portrait finish and globe pixel centres mirror around the screen centre and
share a vertical baseline. Portrait finish uses a compact
borderless 90-degree downward chevron. Docked form content height follows the
visible reader instead of retaining a full-screen minimum spacer. Switching fields commits the prior
field (including raw unfinished preedit) once before focusing the next. Retain the
keyboard root, viewport and top-panel policy during that transfer; compatible
fields reuse key caps and layout. Replace field-scoped OCR registration without
releasing scanner ownership; new field IDs reject old OCR/pinyin results. Undo and
secret visibility stay isolated per field. A non-input action must close a retained
transfer and clear all pointers/actions into the keyboard it deletes. Cancel
restores the original value; Provider edits still require explicit Save.
Portrait candidate expansion temporarily replaces the main key area; keep the expand/collapse button at the same candidate-row position and leave the original page visible. Other layouts expand over the reader; never move the keys. Portrait clipboard
replaces only the keyboard panel; landscape clipboard stays in the right editor. Ordinary
page scrolling and field focus remain available above the keyboard. Rebind focused
controls by stable page/field identity after a rebuild; cancel a missing target on
the next UI tick, never dereference a deleted LVGL control. Portrait pull distance is based on
min(unobstructed reader height, reader width/2), frozen at pointer-down.
Preserve existing landscape geometry. Model fill buttons stay anchored to their
field; album grids/archive readers keep their own layout rather than form reflow.
Y07 SC2356 is an independent 752x440 profile using the driver LED class, never
GPIO export/unbind. The vendor brightness attribute is a mode: white=1, IR=127,
off=0 (stock len_onoff.sh); never use generic LED_FULL=255. Its locked neutral flat field is provisional until white-paper
and forward-text optical validation. Raw transport alone does not count as scan
quality acceptance. The Y07 GC08A8 main path captures 3264x2448, separately from
960x720 preview. A shutter during preview conversion remains pending for a true HD
frame; never silently save preview pixels. Camera generation IDs reject late results.
Do not treat Falcon video_direction as optical camera mounting calibration.

Y07's system RKNN 1.4.0 aborts on the model MaxPool dilations; use only its locked
private 1.6.0 runtime. Other boards keep system RKNN. Keep the scanner's exported
no-execution system blocker and dynamic-binding test: the vendor import must never
reach libc's command launcher. Do not remove the blocker or broaden package policy
beyond the exact runtime hash and the scanner-defined stub. Do not claim seccomp:
this Y07 kernel does not support the required filter.

Globe holds open the selector for three internal layouts plus the explicit 有道输入法 fallback at 500ms while the finger is
still down. Cancel the opening key before rebuilding and absorb its release;
never defer the menu to pointer-up or turn that release into a short tap. Keep
450ms caret/delete timing unchanged. The list floats just above the globe, with
one highlighted current layout; never add unavailable reference-image features.

Portrait official IME uses a real Falcon input connection aligned to the existing
LVGL field, with only the native plane's upper logical region presented while the
system keyboard occupies the bottom. Mirror reviewed text/caret by request ID;
keep an edit snapshot for cancellation, never persist previews. Do not hide an IME
window from inside its own completion callback; defer completion to the next turn.
Only dismiss the system panel while our app owns the foreground. Do not restore
system pinyin borrowing or copy its dictionary.
S6 adaptation is integrated on dev. S6
JDA0700006552216 is an explicit CV1826/ARMv7 target; reuse the accepted CVI INT8
bundle and public runtime in place, never load RKNN or copy device libraries into
the system. Follow docs/S6-ARMV7-DEV.md. Keep the independent s6-armv7 worktree unchanged. Y02/113 remains forbidden.

S6 uses the same LVGL pages and keyboard state machine, scaled as a whole on its
narrow landscape screen. Physical panel dimensions and rotation come from Falcon;
S6 uses native/compositor shared-memory output through Falcon's verified original
post_fb. Never initialize the old CVI VO display backend or fall back to it on
an unknown firmware. Rotation occurs once in Falcon; never use touch offsets to
shift display pixels. The main UI process holds no CVI display descriptors/VMAs;
scanner mediaHandoff must retain this shared surface. Keep the exact ELF hash and
slot-ownership gate, bounded ready/release slots, focus epochs and 300ms expiry.
On app/IPC exit restore the original function and release frames/threads; retain
only the pinned relay for in-flight calls until the host naturally exits.
Retain the public-API 4KiB process ION anchor until final exit: the verified libsys
otherwise frees its last-entry hashmap without resetting its open ION fd.
S6 capture must start the VPSS consumer before enabling VI output; use full native
320x240 PAG7920 frames, never the stock downscaled/cropped outputs. CVI media DSOs
remain on the device, with hash-locked ABI references; do not redistribute them.
S6's fixed optical paper region is [112,8,160,192] inside that native frame.
Exclude its stationary white housing and lower dark aperture from BOTH model
matching and canvas painting. The hardware still captures/validates 320x240;
copy native pixels from this region without downsampling or colour-based trimming.
Do not restore whole-frame matching/painting: it registers stationary housing and
overwrites previously stitched text with white. Other boards keep their full field.
Use `docs/S6-ARMV7-DEV.md` for target builds and current acceptance limits.

Shared UI uses LayoutClass (portrait, >=3:1 wide landscape, compact landscape). Classify only interpreted Falcon logical geometry; never select hardware by aspect ratio. Compact editor design scaling is isolated in EditorLayout. DisplaySurface false means temporary busy/retry; preserve latest pixels. DRM topRegion retains portrait official IME clipping. S6 SYS, ION anchor and NN stay process-scoped; VB config/init/exit is capture-lease-scoped and exits after VI/ISP/VPSS before stock handback. Failed VB cleanup poisons reuse before pausing the guardian. Keep evidence and remaining paper-quality acceptance in docs/S6-SCAN-RECOVERY.md; merge or ready alone is not proof of recovery.
