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
and the history drawer retain their existing navigation behavior.

`finishAll()` runs before lifecycle/appearance/scale teardown and after a close
that creates an outgoing transition. Completion callbacks must finish while
their owning page and editor are still alive. Each root DELETE callback checks
object identity before clearing App fields.

The effort selector uses one rounded track geometry for painted endpoint dots,
knob centres and touch mapping. A single choice is centred. The fixed gauge frame
maps the full option index to a moving needle on its 140°–400° arc. Drag previews
update only the needle and current caption; release commits, cancellation restores
the resolved preference. API default remains distinct from inherited settings.

Balance popups measure actual provider/status/currency/detail content and retain
scroll position after results change. The entire panel is capped by safe screen
height in landscape and logical screen height / 3 in portrait. Header and Refresh
remain outside the scrolling reader; no real account query is needed by fixtures.

MenuControlsTest uses real LVGL pointer input for endpoint selection and cancel,
checks retained callback counts, reversal/deletion, local 0/1/5-provider fixtures,
multiple currencies, errors, scroll limits and replacement root identity.
