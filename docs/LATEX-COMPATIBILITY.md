# 原生 LaTeX 常用语法兼容清单

本批在 dev `da6217d` 后扩展，保持0.3.5。用途是中学至大学常见数学、物理、单位和化学表达，不是完整LaTeX文档编译器。可执行清单为 `tests/fixtures/latex-common.json`；每个有效条目使用真实MicroTeX位图验证，同义命令比较相同布局及像素，非法参数另列。

## 数学与消息上下文

保留分式、根式、积分求和、字体与重音、矩阵、对齐、颜色、删除线、箭头、叠放和左右脚标。新增/接入：

- `overbracket/underbracket`：真实直角方括号，可选线厚、端部高度，支持上下标；不以花括号替代。
- `DeclarePairedDelimiter`：当前公式内定义，普通、星号自动尺寸及big/Big/bigg/Bigg显式尺寸；局部定义优先，不污染后续公式。
- `dcases/dcases*`、`rcases/rcases*`、`drcases/drcases*`、`cases*`；星号第二列按文字处理。
- `multlined`：可选t/c/b基线和显式宽度，按首行左对齐、末行右对齐绘制；内部空行保持有界占位。
- `flalign`：接入消息编号及可用宽度布局；`intertext/shortintertext`为独立说明行，不消耗编号，可包含中文、内联数学和已解析引用。
- `subequations`：父编号与a/b/c子编号、父/子标签、手动标签及取消编号，后续普通编号连续。暂不支持嵌套子编号组、跨消息引用或章节计数器。

完整数学片段在交给Markdown结构解析前使用当前解析调用独有的占位符保护，随后恢复原始公式源文。避免说明中的`$x$`或空行截断外层显示公式；代码区不替换，未闭合内容保持原文路径。原始消息、公式Run源文及复制文本不改写。

## 物理与单位

物理命令支持清单中的常用形式：括号与绝对值、范数、向量及单位向量、微分、普通/偏/泛函导数、高阶与混合导数、取值竖线、期望值、矩阵元、bra/ket/内外积、对易/反对易/泊松括号、梯度/散度/旋度/拉普拉斯、点叉积、阶记号、平式分数，以及常用括号/小矩阵/行列式/单位/零/对角矩阵。长名称别名与短名称使用同一适配入口。

导数星号形式使用平式分数，带参数形式不再仅吞掉星号。保留原来的`div`除号；散度使用`divergence`。不覆盖原有三角函数、实部/虚部语义。`qty{数值}{单位}`按SI解析，定界形式按物理括号解析。显式重定义仅在当前公式内有效。

数值支持正负号、小数点/逗号、e/E科学计数法、紧凑及显式正负不确定度、列表、范围、角度。支持`num/si/unit/SI/qty/pu`以及`numlist/numrange/qtylist/qtyrange/SIlist/SIrange/ang/sisetup`。

支持的配置键为：`output-decimal-marker`、`group-digits`、`group-separator`、`per-mode`、`inter-unit-product`、`separate-uncertainty`、`range-units/list-units`、`round-mode`、`round-precision`、`round-half`、`range-phrase`。选项仅对当前命令或当前公式的sisetup后续命令有效；未知键或未知取值回退原文。

- 默认不舍入、不自动分组，保留正负与输入有效位；精度最多100。
- 显式舍入用十进制字符串计算，不经二进制浮点。中点默认up（远离零），可选even（偶数舍入）；有效数字进位与不确定度同步小数位有专门断言。
- 单位使用现有基本/常用导出单位与SI前缀，支持命令式per/powers和pu隐式正负幂、星号乘法、`//`分式；倒数指数在转为int前验证范围。
- 暂不支持完整siunitx配置、S列数字表格对齐、复杂单位分组/自定义单位、所有本地化格式或多重不确定度。

## 化学语法

支持常用ce元素、系数、离子电荷、同位素、氧化态、变量上下标、自由基点/激发态、聚集态、加成/水合物、分数系数、反应箭头及上下条件、气体/沉淀标记。新增不对称平衡箭头及常用bond单/双/三/虚/点键，键线由原生有界构件绘制。

`Fe3+`与`NH4+`分别解析为单原子电荷与化合物下标加电荷；上下文区分反应运算符和键线。`CuSO4.5H2O`的水合点不再吞进数字下标。明确数学转义和直立文字仍可用于歧义表达。本实现不是完整mhchem/chemfig；复杂晶体缺陷记号、任意结构式和分子图不在范围。

## 资源、安全与依赖修复

原公式8KiB、展开64KiB、嵌套32层、位图4096×2048和公式缓存8MiB/512条限制保留。生成矩阵/格式化数字同样受限。无效或不支持的输入整体走原文回退；不加载外部宏包/文件，不执行TeX I/O。缓存排版版本已更新，身份包括解析后的配置、引用、行类型/边界、主题、字号和宽度。

定向sanitizer发现并留证了MicroTeX小矩阵环境自赋值造成的use-after-free，以及渲染失败时Environment泄漏。通过 `microtex-overlays.json` 固定输入、替换上下文和输出SHA，在独立构建目录生成修复源；不修改缓存依赖目录。依赖锁保存覆盖清单摘要，原C++14准备流程和许可证保持不变，不新增sanitizer豁免。

## 验证与交付

主机覆盖186个有效样例、无效参数和每个样例的未闭合输入，三种布局×四倍率×两主题×三个场景的排版与首/末阅读窗口，另有原文、标签/编号、选区偏移及缓存复用回归。构建与实测体积、开发包和安装收据位于忽略目录 `.work/latex-common/`，最终结果补在本节。

不运行全量回归或全量sanitizer，不自动执行设备触控/截图/性能测试。只覆盖安装完整开发包并核对身份与保存数据；不重启系统宿主、不升版本、不上传替换既有包、不推送或发布。

语法参考：[mathtools](https://docs.mathjax.org/en/latest/input/tex/extensions/mathtools.html)、[physics手册](https://mirrors.ctan.org/macros/latex/contrib/physics/physics.pdf)、[siunitx手册](https://mirrors.ctan.org/macros/latex/contrib/siunitx/siunitx.pdf)、[mhchem手册](https://mhchem.github.io/MathJax-mhchem/)。
