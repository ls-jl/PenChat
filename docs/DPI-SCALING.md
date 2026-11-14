# DPI 与全局缩放

本实现位于独立 `feat/dpi-global-scaling` worktree，基线为 `86844f4`，
后续完善从 `ff99040` 继续；未合并、变基或挑选 `dev` 后续提交。
设备运行 density/UI scale 均固定为 100%。没有新增设置、数据库字段、
物理 DPI 推断、根节点变换或运行时倍率切换。

## 单位契约

`UiMetrics` 是 C++14 纯计算层。`dp(n)` 在 64 位整数中合并 density 和 UI
百分比，最后一次四舍五入；负数对称取整，越界报错。App 在启动时持有度量。

| 输入 | 处理方式 |
| --- | --- |
| 设计留白、控件尺寸、圆角、描边、空间触摸容差 | 调用方显式 `dp()`，或取对应指标集合 |
| helper 的 x/y/w/h、LVGL 测量结果、Falcon 逻辑坐标 | 已解析像素，直接传递 |
| `LV_SIZE_CONTENT`、`LV_PCT()`、圆形半径特殊值 | 保持 LVGL 编码 |
| `UiFont` 角色 | 解析最终字号，再交给原有 FreeType 字体缓存 |
| `font(int)`、`label(..., int)` | 最终像素字号，绝不隐式再次缩放 |
| 阅读字号 75%–150% | 与 UI/density 合并后取整一次；保留原设置含义和范围 |
| 原图、相机传感器、裁剪保存像素、PNG tile、缓存字节预算 | 保持其自身单位 |
| 列数、索引、比例、动画时长、超时、token 和字符限制 | 无单位或业务单位，不缩放 |

语义字号保留同号不同角色：正文 21、按钮 20、页面标题 26、字段标题 15、
字段值 19、设置标题 20、说明 16。Composer、表单说明和其他原有字号也有
明确角色。阅读、Markdown 标题/代码/表格使用最终阅读字号及明确的内部间距。

## 覆盖范围与保留边界

| 范围 | 当前处理 |
| --- | --- |
| 公共 box/button/field/pageHeader/pageAction/settingsRow | 内部度量、字号和重排使用同源指标；外部几何保持像素语义 |
| AppLayout、composer | 三类布局、增长、工具行、停靠、返回底部和安全留白统一解析 |
| 设置、Provider、模型与协议表单 | 内部尺寸、字号、间距、字段动作锚点及单列表单重排 |
| 历史、归档、会话、预设、压缩、回复选择 | 行、标题、动作、弹层和阅读区域 |
| 反馈、更新、导入导出、网页、余额 | 页面内部度量、按钮和可读文本 |
| 附件/模型/强度菜单、对话框、阅读选择 | 尺寸、留白、行高、图标、菜单安全定位 |
| Markdown、数学、表格、共享图标 | 最终字号和内部设计尺寸；100% 保留原渲染，齿轮蒙版复用 |
| 相机、裁剪、相册、扫描查看 | UI 控件与工作区布局；采集、图像转换和后端协议不变 |
| 引导 | UI 文本和控件统一解析；非 100% 长文本局部滚动、页脚按测量宽度换行 |
| 键盘、候选、剪贴板、选择/放大镜 | 使用适配后的 KeyboardDesign/EditorLayout 与有效倍率 |

LayoutClass 根据 Falcon 已解释的逻辑几何分类。ScreenSafety 的屏幕圆角
包络仍是逻辑屏幕约束，不随 UI 倍率重复放大。引导演示的 400×150 插图坐标、
640×360 渲染上限和主机图标样本格是独立素材/测试单位。

下拉创建仍按按下时冻结的未遮挡 reader 几何计算，不再乘 UI 倍率。
NativeTouch 的旋转和偏移仍只处理一次。双击/长按时间、500ms quiet guard、
160ms 回弹和图像缩放比不变；普通点击/拖动等设计空间容差由统一指标解析。

## 键盘的完整按键适配

键盘优先保证按键完整。从请求倍率向下按 1% 检查三个内部布局的键帽、文字
行高与边界，选择可容纳完整键盘的最大倍率；下限为 `min(请求倍率,100%)`。
100% 沿用现有几何，不以新行高替换既有高度。选择专用编辑器不执行按键适配。

紧凑横屏既有 960×266 设计画布仍由 EditorLayout 显式适配到真实逻辑屏幕，
它不是屏幕分辨率 fallback。候选、控制、选择和触摸坐标使用同一适配结果；
字母、数字/符号、九键、候选展开、紧凑展开都保留原模式边界。

诊断 `editor.requested_scale/effective_scale` 区分请求和实际倍率。
缓存只保留当前解析结果，键包含宽高、选择模式、inline chat、分区交换、
紧凑展开、密钥模式、圆角能力、官方 IME 占用高度、density 与倍率。
`layout_resolves/layout_cache_hits` 可检查复用情况，不做永久多版本几何缓存。

## 消除重复工作

键盘一次解析后复用；同一状态的 100 次诊断不增加 layout resolves。
竖屏表单先集中赋宽和样式，再统一测量，最后安排行位置；创建局部 row 索引，
去掉每行遍历全部设置控件与逐行强制 layout。`layout_work` 记录批次和行数。
通用按钮复用已经获得的键盘祖先判断；字体继续使用既有缓存。

主机五次独立进程比较（长 Markdown reader、编辑器、100 次诊断及 Provider
表单）结果保存在 `.work/dpi-final/bench-{before,after}.json`：p95 诊断耗时的
中位数 18.178 → 18.336ms，RSS 高水位中位数 52,584 → 52,488KiB。
此结果属于近似不变，不能据此宣称帧率、输入延迟或整体响应速度提升；可以
确认的是重复几何解析被消除，8 行表单共用一次宽度相关测量（另有初始同步）。

## 定向主机验证

基线可执行文件和稳定场景必须来自改造前独立构建，禁止以新结果覆盖基线。
本次在 `.work/dpi-final` 保存基线、几何 JSON、截图、日志和测量数据。
截图用相同字体/资源和确定性编码比较完整 PNG 字节。

```sh
cmake --build build/host --target penchat-lvgl penchat-ui-metrics-tests penchat-ui-controls-tests -j6
ctest --test-dir build/host -R '^penchat-(ui-metrics|platform-layout)$' --output-on-failure
python3 tests/test_ui_controls.py
python3 tests/test_ui_metrics_baseline.py --baseline .work/dpi-final/controls-before
python3 tests/test_form_scale.py --scales 100 --baseline .work/dpi-final/forms-before
python3 tests/test_keyboard_design.py --baseline .work/dpi-final/baseline
python3 tests/test_keyboard_scale.py
python3 tests/test_selection_scale.py
python3 tests/test_onboarding_scale.py
```

度量/组件矩阵覆盖 75/100/125/150%、960×266、640×260、568×1210。
100% 控件和表单几何/像素、8 组键盘设计模式与独立基线一致；非 100% 验证
完整按键、实际倍率、阅读选择菜单和放大镜边界、引导七步控制目标。
其余定向测试覆盖 Markdown/表格、选择、字段切换、rollover、吞吐、停靠取消、
下拉灵敏度、窗口焦点、历史/归档、附件菜单、引导、相机布局/裁剪、反馈、更新、
导入导出格式、上下文压缩和 Anthropic 流程。未运行全量回归、sanitizer 或发布流程。

两项旧基线失败已分别解决：取消事件被阅读选择吞掉的问题修复并保留原断言；
Anthropic/压缩测试按已有的固定应用规则与会话指令契约检查。模型拉取测试
使用显式选择/添加/保存并验证保留原模型 ID。压缩失败测试使用足以进入请求阶段
的输入预算，并断言实际请求发生，防止误把 admission 拒绝当成 HTTP 失败。
另外修复阅读选择转入/退出编辑器时残留的 chat press，避免之后第一次点击丢失。

## 开发包与设备验收

只从本 worktree 构建并暂存 AArch64 与 CV1826/ARMv7 开发 payload；后者使用
GCC 6.3.1 / GNU C++14。包身份保持 package.json 的 0.2.34 / 8001792332371649，
采用包含原生功能、scanner、资源和平台组件的完整开发包，不用 UI-only 包替代。
打包检查身份、ELF 架构、锁定依赖、资源和许可证清单，不升级版本或发布。

`tests/device/dpi_acceptance.py --serial <明确序列号> --package <完整开发包>`
只接受授权的 Y02、Y07、S6、ORANGE。它先检查待处理输入/生成/未保存状态，
保护公共导出目录，再关闭自己的 native 并隔离整个私有数据目录；测试库只用
虚构文本和本地假密钥。数据库、assets、备份和公共 transfer 目录一起恢复并
校验文件哈希；不读其他应用数据，不重启系统 MiniApp 宿主。

验收使用发现的真实 evdev 设备和 Falcon screen 配置反算触点，覆盖历史菜单、
设置、Provider 字段、拼音候选/展开、语言菜单、两触点 rollover、文本选择及
官方输入法打开/返回。设备只验证 100%；非 100% 的视觉检查继续留在主机。
这不是扫描光学质量、所有业务分支或物理圆角校准的完整实机验收。

2026-11-14 的最终开发包验收结果：

| 目标 | Falcon 逻辑几何 | 安装与定向 UI 验收 | 数据恢复 / 宿主未重启 / 运行包哈希 |
| --- | --- | --- | --- |
| Y02 `MEA2300009007113` | 936×280 | 通过 | 通过 |
| Y07 `7G12000010905426` | 568×1210 | 通过 | 通过 |
| S6 `JDA0700006552216` | 640×260 | 通过 | 通过 |
| ORANGE `MEB0400008900794` | 960×266 | 通过 | 通过 |

四台的请求/实际倍率均为 100%。S6 实机覆盖全宽紧凑候选与选择；紧凑分区、
候选展开和模式切换的完整组合另由主机键盘矩阵覆盖。设备结果与虚构 fixture
截图在 `.work/dpi-device-<serial>`，汇总为 `.work/dpi-final/device-results.json`。
设备恢复后通过 `/proc/<pid>/exe` 哈希与相应 AMR 内的原生程序再次比对。
