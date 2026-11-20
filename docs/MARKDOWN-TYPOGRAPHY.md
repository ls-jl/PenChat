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

## 原生化学式输入（2026-11-19）

MicroTeX 之前没有注册 `\ce`，未知命令会直接露出命令名。现在由原生
`chemistry.hpp` 将常用 mhchem 风格输入转换为现有 MicroTeX 可排版的数学表达式，
不在 Vue 中运行化学式解析，也不额外引入 Web 渲染器。输入语法参考
[mhchem 官方手册](https://mhchem.github.io/MathJax-mhchem/)。

覆盖元素直立字形、自动数字下标、显式上下标与电荷、圆／方括号、同位素、系数、
水合物点号、常用键线，以及 `->`／`<-`／`<->`／`<-->`／`<=>` 和上下标注。
例如 `\ce{H2O}`、`\ce{SO4^2-}`、`\ce{Ca(OH)2}`、`\ce{^{14}_{6}C}`、
`\ce{2 H2 + O2 -> 2 H2O}` 和 `\ce{A ->[\Delta][H2O] B}`。
这是原生常用语法适配，不是完整移植 mhchem；复杂扩展宏不在此次化学式适配范围。后续有界扩展另补充了常用 `\pu` 数值/单位语法，见下文。

`$...$`、`$$...$$` 和既有 `\(...\)`／`\[...\]` 数学环境内都使用这条路径。
完整的裸 `\ce{...}` 会识别为行内公式；代码围栏、行内代码以及尚未闭合的流式内容
保持原有文字处理。公式源文仍用于复制、选择和缓存键，排版替换只发生在栅格化入口。
主题前景色、字体尺度和已有公式缓存上限保持不变；输入、嵌套及展开长度分别受界限保护。

`penchat-chemistry` 覆盖 16 组实际 MicroTeX 位图、深浅两种前景色、普通数学式不变、
源文／复制语义、代码隔离、无效输入及裸公式。三种主机布局的深浅截图保存在
`.work/feedback-fixes-20261119/image-chemistry-stream-gauge/visual/`。

## 消息内公式编号与有界扩展（2026-11-20）

Document在排版前两遍解析标签和引用，测量、绘制、选择使用同一份公式行信息。
支持tag/tag*、label、ref/eqref及常用AMS自动编号，正文、思考和摘要上下文互相隔离。
前向引用可随流式内容补全，重复标签首次定义优先，原始消息与复制源文不变。
窄屏编号放不下时另起一行靠右，保留公式横向滚动。

数学、物理、SI单位及局部宏的实际清单、32层/64KiB限制和回退规则见
[S6-LATEX-MOTION.md](S6-LATEX-MOTION.md)。无效/未知命令保留源文回退；不加载外部
TeX包，不宣称完整LaTeX兼容。缓存身份包含引用解析结果、排版版本、宽度、主题及最终字号。

## 常用数学、物理、单位与化学扩展

后续有界语法、参数形式、子编号与公式间说明行的实际兼容清单见
[LATEX-COMPATIBILITY.md](LATEX-COMPATIBILITY.md)。原文、复制、主题及用户字号规则保留；
新增语法不代表任意LaTeX宏包或完整文档编译兼容。
