# Repository Guidelines

PenChat 0.1.0 is a native LVGL app for ORANGE (RK3562/AArch64, 960×266).
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
- `native/camera/`: reused V4L2/DRM/image primitives; no opaque camera libraries.
- `src/launcher/`: thin AMR page; `assets/images/`: product icons.
- `native/vendor/`: SDK source for the launch bridge.

Use two-space indentation; format C++ consistently. Keep LVGL calls on its UI
thread. Use the existing queues for background work and discard stale generation
and input request IDs. Release child processes, camera, pointers and the display
plane on lifecycle transitions. Never dereference freed LVGL object pointers;
register image cleanup on LV_EVENT_DELETE (9.6's old is_valid alias is unsafe).
Section pages use a fixed 48px header and a separate scrolling body. Fixed-height
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
117px fill distance (129px total, 85% of the former 152px). Scale spatial
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
