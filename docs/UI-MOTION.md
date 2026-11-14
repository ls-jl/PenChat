# Native menu transitions

`ui_motion.hpp` owns every transition through the affected LVGL object's DELETE
callback. Animation state is UI-thread-only. Deleting an object unregisters its
animation; reversal starts from the current painted value. No timer may later
clear a replacement menu's pointers.

Inline public reasoning, search details, balance details and Provider advanced
options use 180ms height and arrow-progress transitions. Reasoning and search
retain their subtrees, hit layouts and unrelated selections; Provider keeps its
workspace and model IDs. A collapsing section clears a selection only if that
selection belongs to the hidden content.

Floating menus use 150ms appearance and 100ms exit. Replacement adopts the old
panel under the new overlay, disables its input and fades it out behind the new
panel. The overlay owns the whole contact sequence, including contacts that
start during exit, preventing clicks through to keys or Send. Full page changes
retain their existing navigation behavior. The native keyboard and history drawer
fade in over 180ms. Closing captures a bounded temporary raster and fades it out
over 100ms; live editor/drawer objects are released immediately. This avoids
keeping field callbacks alive through a page rebuild. Keyboard field transfers
and candidate refreshes do not replay the entrance. Snapshot images retain
logical scaling and are released on DELETE; closing contacts remain blocked.

`finishAll()` runs before lifecycle/appearance/scale teardown and after a close
that creates an outgoing transition. Completion callbacks must finish while
their owning page and editor are still alive. Each root DELETE callback checks
object identity before clearing App fields.

The effort selector uses one rounded track geometry for painted endpoint dots,
knob centres and touch mapping. A single choice is centred. The fixed gauge frame
uses blue for non-default levels, ordered over its 140°–400° arc after removing
API default from that ordering. Default is grey and occupies the second clockwise
position (a minimum of two positions is used when there are fewer levels). The
selector is centered on the logical screen within safe bounds: a caption in the
form “高 · 推理强度” sits above a capsule around the track, with no large outer
card. Drag previews
update only the needle and current caption; release commits, cancellation restores
the resolved preference. API default remains distinct from inherited settings.

Balance popups measure actual provider/status/currency/detail content and retain
scroll position after results change. The entire panel is capped by safe screen
height in landscape and logical screen height / 3 in portrait. Header and Refresh
remain outside the scrolling reader; no real account query is needed by fixtures.

MenuControlsTest uses real LVGL pointer input for endpoint selection and cancel,
checks retained callback counts, reversal/deletion, local 0/1/5-provider fixtures,
multiple currencies, errors, scroll limits and replacement root identity.

SurfaceMotionTest exercises interrupted entry, detached closing snapshots through
page rebuilds, scaled geometry and repeated cleanup using actual LVGL rendering.
Raster/direct opacity rounding may differ by at most one channel value during the
transition. MenuControlsTest also asserts selector centering at multiple scales.
