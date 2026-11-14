# UI refresh rate and stable bottom controls

Settings → Application → UI refresh rate exposes 30Hz and 60Hz. The existing
`app_settings.ui_refresh_hz` key stores an integer 30 or 60, without a schema
change. Missing, non-integer or unsupported values use the interpreted Falcon
logical screen orientation: landscape 30, portrait 60. An explicit saved value
wins on subsequent starts, including when the screen orientation changes.
Selecting the currently effective default still saves that explicit choice.

Selection uses the DB worker and request-specific completion. The current rate
stays in effect until a successful save; failure keeps it and offers retry in the
settings row. A successful background save waits for foreground/input ownership
and a released touch. Applying the setting updates timer periods directly and
updates the row without rebuilding the page or restarting the application.
Drafts, attachments, generation, reader position and keyboard state stay intact.

`Display::setRefreshRate` sets the LVGL display, global animation and existing
input timers; subsequently created Input objects inherit the rate. Millisecond
targets are 33ms for 30Hz and 16ms for 60Hz. Timer diagnostics read the real LVGL
periods, with the pinned private timer header confined to platform.cpp. The
main input loop retains its existing maximum 16ms foreground wait.

The S6 surface receives the same target through DisplaySurface, including after
surface recreation. Its worker keeps the existing two output slots, latest-frame
coalescing, focus epochs, visibility and leases. Pacing uses elapsed monotonic
time and preserves the last submission time across switches. Polling remains
bounded at 8ms and can shorten to a pending deadline. No wire protocol, framebuffer
geometry, hardware display mode, rotation or touch mapping is changed. The setting
is an application scheduling target; achieved output still depends on load and
hardware/host availability.

## Bottom controls on every layout

The previous composer reserve changed with visibility, while visibility depended
on the same scroll extent. A small scroll near the end could therefore repeatedly
show/hide both controls. The reader now always reserves the resting composer
space when the keyboard does not already reduce the reader. A visibility change
cannot move the content bottom; following readers also remain anchored when the
composer itself grows.

All layouts share ChatBottomState: enter the visual bottom zone at ≤10dp, leave
it beyond 24dp, and require 120ms continuous confirmation before changing state.
The return button and automatically hidden composer share that visual decision.
Portrait's composer remains always visible and its return button uses the same
buffer. Existing explicit composer-shortcut behavior remains independent.
An explicit jump to bottom initializes immediately. New view construction resolves
its actual geometry before initializing. Touch cancellation, background, focus
loss and overlays pause incomplete confirmation, preventing hidden time from
finishing a transition. No device model or screen-resolution gate exists.

Actual `atLatestBottom()` retains its 10 logical-pixel tolerance for following
and pull-to-create. The new visual buffer does not alter gesture duration,
sensitivity, the 500ms quiet guard, or deliberate/full-progress release rules.

## Targeted checks

RefreshRateTest executes actual LVGL timers and animations. CompositorRefreshTest
uses the real S6 worker, socketpair and sealed shared memory with a test-only
clock wrapper to validate 33/16ms boundaries and ownership. DisplayPendingTest
covers latest-frame retry and handoff. ChatBottomStateTest checks thresholds,
time and pause/reset; BottomStabilityTest runs real App/LVGL geometry and animation
for wide/compact/portrait layouts at 75/100/150%.

`test_refresh_rate.py` covers storage types, orientation defaults, explicit choice,
failed writes, duplicate clicks, background acknowledgement, restart, retained
reader/draft and local streaming. Device acceptance is a separate explicitly
scoped script that isolates/restores the complete private and public directories,
uses only fake local data, exercises real 30/60 selections and scrolling, and
verifies stable button/composer geometry, original data/settings and host PID.
Artifacts and complete development packages live in ignored `.work/refresh-bottom`.
