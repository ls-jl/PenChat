# Rich-text typography

The paragraph base remains 21 logical pixels at 100% UI and reading size. Resolve
those two percentages once before layout. H1–H6 scale from 29/27/25/23/22/21.
Paragraph line boxes use 1.6em; heading line boxes use 1.3em; code and table line
boxes use 1.45em. Taller glyphs or formulas expand the line box.

`RichText::typography()` owns block spacing. Paragraphs reserve 0.7em and list
items 0.6em; heading space above is 1.2/1.0/0.9/0.8/0.7/0.6em and below 0.4em.
`blockGap()` takes the maximum adjacent margin, returns zero for the first block
and for consecutive rows of one table. `richMessage()` emits that one gap and
sets flex row padding to zero; callers must not append another fixed spacer.

List groups and list-item IDs distinguish a new item from its continuation.
`prepare()` measures the actual marker column, including multi-digit ordered
markers and task boxes. Continuations and wrapped lines share a text origin;
deep nesting compresses ancestor indentation to preserve about six em of text.
Painting, links, selection/clipboard and reading anchors use the same layout.

Regular body text uses the real HarmonyOS Sans SC Regular face once. Headings
and explicit Markdown strong text use a verified Bold sibling from the same font
family. A controlled synthetic fallback is limited to strong text when no Bold
sibling is available. Normal text is never repainted to simulate weight.

Device fonts stay in the system font directory and are not packaged. The host
fixture caches the corresponding fonts outside Git: Regular SHA-256
`297b088424be212207df2ce8b98e335468b782aa6b96832af0b8b773d711e2b1`, Bold
`43a424b85e47fb53a17b3b32026a71801f86f8e022ca6798d186b47d39fa5f01`.

Dark body/heading colours are #D5D7DE/#F4F6FA; light uses #30343B/#151922.
Links, quotations, public reasoning, code and borders have separate palette roles.
Formula bitmaps include the theme, foreground and typography version in their
cache key, retaining explicit LaTeX colours.

The chat reader's bottom padding has one owner, `syncChatBottomPadding()`: stable
composer/keyboard occupancy plus a 16dp tail. Pull gestures and transient keyboard
translations do not change the scroll extent.

RichTypographyTest compares glyph alpha with actual Regular/Bold Cairo faces,
checks spacing, hanging indents and formula colours. UiControlsTest exercises the
real message tree, collapsed margins, selection/copy and retained disclosure
objects. MarkdownWrapTest, MarkdownTableTest and TextSelectionTest cover existing
line breaking, tables and hit/selection behavior.
