# Native appearance

PenChat owns the theme on the LVGL UI thread. `ThemePalette` in `theme.hpp`
provides semantic colours to native pages, controls, keyboard design, icons and
rich text. Dark is the default; light uses white pages, pale cards and dark text.
The image, photo, scanner and system IME pixels are not recoloured.

`app_settings.theme_mode` stores `dark` or `light`. Missing, non-string or unknown
values resolve to dark. Settings → Application → Theme offers two choices; a
click submits the existing DB worker request and disables the list while pending.
Only its matching acknowledgement changes appearance. A failed write retains the
old theme and allows retry. A completed write in the background waits until the
UI is foreground, owns input, and has no live touch before rebuilding Settings.
No schema, version or API protocol changes are involved.

A theme rebuild finishes object-owned transitions, cancels old pointer state,
clears layout/formula rasters, drops the keyboard-layout cache and rebuilds the
settings reader at its former scroll position. It keeps font faces, the bounded
FreeType cache, MicroTeX, conversations, drafts, attachments and background model
requests. Reading size and UI scale remain independent preferences. Formula cache
identity includes appearance and typography; explicit formula colours survive.

Native sends an `appearance` bridge message with resolved logical dimensions and
semantic colours. The thin launcher applies those values to the startup/failure
surface and its own official-editor connection field. Native field coordinates
are passed through unchanged. Before the handshake the launcher uses named 100%
startup design metrics. The vendor input-method panel retains its own appearance.

Validation is targeted: ThemePaletteTest checks normal-text contrast ≥ 4.5:1 and
per-theme keyboard styles; launcher tests cover appearance and coordinate units;
`test_theme_settings.py` exercises real LVGL settings and asynchronous persistence.
Runtime acceptance artifacts belong in `.work/ui-unification`, never in Git.
