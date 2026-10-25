# ORANGE screenshot regression

`markdown.png` is the visually reviewed 960×266 headless rendering of
`tests/markdown-demo.md`, scrolled to the final content by the normal chat UI.
It covers the capsule composer and assistant copy/retry/branch controls,
the table/math tail, CJK code comments,
monospace code, horizontal clipping and link color. Native core tests separately
cover all formula families, list/Markdown parsing and large horizontal tiles.

`manifest.json` pins the device CJK font checksum as well as image checksum.
`python3 tests/test_render.py` rebuilds the scene in a fresh temporary database
and compares the exact PNG. Rendering dependencies are source-locked. A deliberate
font or layout change requires visual review before updating this baseline.
Do not copy private conversations or device screenshots into golden files.

0.1.56 review includes the enlarged attachment target/text inset from 0.1.54 and the corrected retry arrow from 0.1.55; static Markdown content and fonts are unchanged.
