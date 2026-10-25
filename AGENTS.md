# Repository Guidelines

PenChat 0.2.1 is a native LVGL app for ORANGE (RK3562/AArch64, 960×266).
The Falcon AMR is a thin launcher with a full-screen hole, system IME, touch and
lifecycle forwarding. Do not put business UI or networking back into Vue.
Scanning uses the source-pinned Stream32 backend in native/scanner, with a
separate C++17 process and ORANGE-only capture lease. Keep 32/24/8 windowing,
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
plane on lifecycle transitions. Never dereference freed LVGL object pointers;
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
130px fill distance (142px total). Pulls may start in the composer text entry,
but never its attachment or send controls. Scale spatial
thresholds, slow-motion speed and stretch by the unobstructed logical reader
height relative to 208px. Freeze geometry at pointer-down, excluding resting
composer space, clipping and viewport translations. Falcon already maps rotation
and touch offsets; never scale a second time using physical/raw touch dimensions.
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

## Builds and checks

Read README.md for toolchain prerequisites. `npm run build:host` builds host tests;
`npm run build:native` cross-builds the executable and launcher; `npm test` runs
bridge, native and actual-process integration tests. Run `npm run check` and
`npm run build:prod` before delivery. `check` currently reports optional cnpm.
For runtime/camera/display changes test on ORANGE serial MEB0400008900794.
Device test helpers use real touchscreen events; do not target other devices.
Do not restart the system MiniApp host or overwrite user data to bypass failures.
Device chat acceptance snapshots/restores the DB and uses only fake local keys.

Keep credentials, data, screenshots, research, downloads, toolchains and build
artifacts out of Git. `.deps`, `build`, `.work`, `libs`, `.falcon_`, `dist`,
`assets/bin`, `assets/microtex`, `assets/licenses` are generated/ignored. Record
source hashes in the dependency lock and preserve license notices. Runtime native
changes must regenerate/stage their binaries; never hand-edit generated files.
HaaS UI shell/IME reference: /persistent/home/ls-jl/dictpen/haasui-docs.

PenChat is an independent application. Never read external application databases or Storage. The album may read the shared system photo directory /userdisk/Pictures; never modify its originals. Identity comes from package.json. New installs start without providers or messages.

Every delivered fix increments the package patch version (0.1.0 → 0.1.1 → 0.1.2). Keep package-lock root metadata in sync; build identity and AMR filename derive from package.json.

Commit every completed change to Git after validation. Each delivered fix also increments the patch version; never claim uncommitted work is committed.

The user authorizes future ORANGE package installation and device testing without asking again. Preserve user data and pending input where possible; never restart the system MiniApp host.

Camera captures stay in camera-tmp until crop confirmation. Confirm attaches without auto-send. Persist saveCameraToAlbum; only explicit opt-in creates a new PenChat photo in /userdisk/Pictures. Never alter existing system photos; cancellation and exit discard temporary captures.

Scanner ingress uses a lossless, ordered FrameQueue: bounded RAM inbox/prefetch, at most 640 pending frames and an unlinked disk ring below 110 MiB. Only its worker performs disk I/O. Feed the engine below 128 resident frames; seal only after queued input drains. Only an isolated one-frame forward gap within 35ms and after 16 continuous frames may recover, with visible loss accounting. Keep larger/repeated hardware discontinuity, memory and storage-error guards explicit; never silently drop queued frames.

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

Export persistence defaults to enabled. When disabled, normal native shutdown
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
message supplies the same dimensions to the AMR; Falcon already maps input coordinates.
Use EditorLayout for keyboard controls, decorations and touch bounds. Test fixtures may
specify explicit resolutions; camera sensor capture sizes are independent of display sizes.
`tests/device/display_config.py --serial <explicit serial>` covers the user-authorized Y02
and ORANGE display targets using their config and discovered input device. Keep ORANGE
scanner calibration guards even when display/input support expands to another board.

The native editor owns libpinyinime on one background worker. Never reintroduce the Falcon
shared decoder, system pinyin imports, symbol borrowing or a fallback bridge for candidates.
The official text editor remains a manually selected fallback; OCR is independent. Keep
26-key Chinese, English and nine-key main layouts separate from number/symbol pages.
Ordinary globe taps toggle preferred Chinese/English; rapid unedited taps under 2s cycle
Chinese26/English/Chinese9. Only actual Chinese typing changes the persisted preference.
Park unfinished Chinese compositions separately; Enter emits original letters/digits, while
finishing discards inactive compositions. Learning commits only after confirmed editing,
never from secret fields/OCR/paste/cancel. Base dictionaries are read-only; private user
deltas use bounded 10000-record/2MiB atomic files, never logs or transfer exports.
