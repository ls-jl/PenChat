# Y01 screenshot regression

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


The 0.3.1 baseline was regenerated on 2026-11-15 after the user explicitly chose
“聊天页面保留黑底” for the previously displayed current image. The fresh real-app
render is byte-identical to that displayed candidate. Migration from the 0.1.56
baseline also records changes already implemented before this decision: Markdown
line spacing, one background per block-code container, and the bottom-left account
balance entry. This baseline update introduces no production UI changes.

Generation used the synthetic fixture, fresh database, pinned font, 960×266 screen
and 250 frames from `test_render.py`, with the exact host candidate
`.work/release-0.3.1/host-tests/penchat-lvgl-candidate-6-back` (SHA-256
`964f426eb61f22fe7134d8e7985c5a499a816263e86fa5f4a0e06305ee6f7fe4`).
The PNG SHA-256 is
`980557c4ec83a9f4c7ddc6e83c7ed211b9c3663f38d5eab55884abefad2e98c3`.
The font checksum, fixture, dimensions, frame count and exact-PNG comparison remain
unchanged. Previous baseline files and regeneration evidence are retained locally
under ignored `.work/release-0.3.1/black-approved/`.
This is a narrow synthetic chat-render regression, not complete 0.3.1 visual,
physical-screen, IME or device acceptance.
