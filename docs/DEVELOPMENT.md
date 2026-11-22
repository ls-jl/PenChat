# PenChat 开发说明

[返回用户指南](../README.md)

本文面向构建、调试和参与开发的读者。版本、AppID 和应用身份以 [`package.json`](../package.json) 为唯一来源；修改前先阅读仓库的 [AGENTS.md](../AGENTS.md)。

## 架构与目录

PenChat 是 AArch64 词典笔上的原生 C++/LVGL 应用。Falcon AMR 是薄启动器，通过全屏 hole 暴露原生页面，负责进程生命周期、系统输入法和 OCR 桥接。业务界面、网络、存储与内容排版在原生进程中执行。

| 目录 | 职责 |
|---|---|
| `native/lvgl-app/` | LVGL UI、MD4C/公式、三种 API、SQLite、图片及相机业务 |
| `native/launcher/`、`src/launcher/` | 原生启动/IPC 桥接与薄 AMR 页面 |
| `native/scanner/` | 独立 Stream32 扫描进程、硬件租约、拼接与 PNG 输出 |
| `native/camera/` | 源码复用的 V4L2、DRM、图像与加速度计原语 |
| `native/pinyin/` | 独立 libpinyinime 适配、词典来源及学习记忆 |
| `native/model-catalog/` | 离线模型能力规则与来源核验 |
| `tests/`、`scripts/` | 主机/设备测试、锁定依赖准备和构建打包 |

LVGL 调用只在 UI 线程执行。网络、数据库、解码和采集使用后台任务与事件队列；异步结果检查会话和请求代次。UI 流式更新最多每100ms一次，数据库每500ms合并提交，终态等待保存确认。应用运行不依赖 shell 求值，详见[运行时命令策略](RUNTIME-COMMANDS.md)。

## 构建与检查

### 环境与资源

主机和设备的工具链、下载来源及构建命令见[自行编译](../README.md#自行编译)。
公开依赖按各模块锁文件准备；设备私有资源的许可范围见[第三方声明](../THIRD_PARTY_NOTICES.md)。

在仓库根目录执行：

```bash
# 主机 LVGL 构建
bash scripts/build-lvgl.sh host

# 同时构建主机扫描器，再运行完整回归
npm run build:host
npm test
npm run test:sanitize

# AArch64 原生程序、桥接与生产 AMR
npm run build:native
npm run check
npm run build:prod
npm run check:package
```

### 设备构建变量与缓存

在仓库根目录配置以下变量，将示例路径替换为实际位置。两种设备 AMR 都需要两套 GCC 6 工具链来生成急救桥。

```bash
export JSAPI_TOOLCHAIN_ROOT=/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu
export PENCHAT_CV1826_TOOLCHAIN=/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf

# RK 扫描
export RK_SDK_ROOT=/opt/vendor/RK3562_Android13.0_SDK
export PENCHAT_SCAN_BUNDLE=/opt/penchat-inputs/stream32_complete_20261018
export PENCHAT_SCAN_CROSS_ROOT=/opt/penchat-inputs/arm-cross
export PENCHAT_SCAN_BOARD_LIBS=/opt/penchat-inputs/rk-board-libs
export PENCHAT_EDGE_CXX_HEADERS=/usr/aarch64-linux-gnu/include/c++/12

# S6 扫描与联网搜索用的 ARM C++ 头，运行 S6 构建前设置
export PENCHAT_CVI_SCAN_BUNDLE=/opt/penchat-inputs/cv1826_int8_complete_20261018
export PENCHAT_CV1826_OPENCV_ARCHIVE=/opt/penchat-inputs/opencv-3.4.3.tar.gz
export PENCHAT_CV1826_PNG_INCLUDE="$RK_SDK_ROOT/external/libpng"
export PENCHAT_EDGE_CXX_HEADERS=/usr/arm-linux-gnueabihf/include/c++/12
```

上面两行 `PENCHAT_EDGE_CXX_HEADERS` 分别用于各自的构建，取当前目标对应的值。OpenCV 归档可用 `sha256sum` 对照 [`cvi-media.lock.json`](../native/scanner/cvi-media.lock.json)；使用该锁定归档，源目录会生成到 `.deps/opencv-cv1826-s-source/`。

- RK 扫描默认用 GCC 12 编译 C++17，再用 GCC 6 和板端 ABI 链接。将 Debian 的 amd64 交叉编译器和匹配的 GCC/C++ 开发包用 `dpkg-deb -x <包文件> "$PENCHAT_SCAN_CROSS_ROOT/root"` 解到同一个根目录。包装器读取 `root/usr/bin/aarch64-linux-gnu-g++-12`、`root/usr/aarch64-linux-gnu/include/c++/12` 和 `root/usr/lib/gcc-cross/aarch64-linux-gnu/12/include`。开发机仍需提供对应的宿主共享库和交叉汇编器。目录结构与自定义工具链接口见 [`scanner-cxx.py`](../scripts/scanner-cxx.py)。
- RK 的 `PENCHAT_SCAN_BUNDLE` 中，主机扫描器读取 `third_party/opencv-host/`，设备扫描器读取 `third_party/opencv-arm/`。后者的 OpenCV 3.4.3 二进制指纹与板端库原始文件名见 [`dependencies.lock.json`](../native/scanner/dependencies.lock.json)。相同版本号的通用 OpenCV 包不保证这些指纹一致。
- 检查资源包配置可运行 `python3 scripts/prepare-scanner.py` 和 `python3 scripts/prepare-scanner-cvi.py`。这两条命令读取对应环境变量；带 `--bundle <目录>` 时以参数为准。RK 用 `--cross` 进一步检验 PNG SDK 资源和板端链接库。联网搜索源码由脚本下载校验，缓存默认在 `.deps/edge-source-cache/`，可用 `PENCHAT_EDGE_SOURCE_CACHE` 指向已有缓存。
- 私有反馈配置可选，普通自编译使用默认未配置状态。第三方申请或设备提取的私有材料不进入 Git。

冷缓存交叉构建会通过 ADB 从用户自己的匹配设备读取系统库和字体。先用 `adb devices` 获取设备序列，再设置 `PENCHAT_ADB_SERIAL`。当前 RK SQLite 自动准备有 ORANGE 板型检查，CVI 媒体自动准备有 S6 固件检查。已有匹配固件导出的链接文件时，可按下表备好本地缓存，再运行构建。把 `<目标>` 替换为 `rk3562` 或 `cv1826-s`。

| 本地路径 | 来源 |
| --- | --- |
| `.deps/<目标>-libs/libsqlite3.so.0` | 对应固件的 `/usr/lib/libsqlite3.so.0`。SQLite 开发头从主机 `/usr/include` 读取，也可指定 `SQLITE_INCLUDE_DIR`。 |
| `.deps/http-system-<目标>/lib/` | 对应固件 `/usr/lib/` 中的 `libssl.so.1.1`、`libcrypto.so.1.1`、`libz.so.1`。 |
| `.deps/chinese.ttf` | 自有设备的 `/etc/miniapp/resources/fonts/HarmonyOS_Sans_SC_Regular.ttf`，或用 `PENCHAT_TEST_FONT` 给设备构建准备一个自有、可使用的替代测试字体。仅做主机 LVGL 字体验收时，可直接将测试字体复制到这个缓存路径。 |
| `.deps/cv1826-s-media/` 和 `.deps/cv1826-s-png/` | S6 完整扫描器的媒体链接参考、PNG 头和 ARM32 运行库。文件名、固件路径和哈希见 [`cvi-media.lock.json`](../native/scanner/cvi-media.lock.json)。 |

`npm run check` 目前会提示可选 cnpm 工具。生产包位于 `dist/`，文件名由配置中的 AppID 和版本生成。原生源码变更后必须重新构建、暂存生成资源，再打包；不要手工修改二进制。

生产构建对暂存的 AArch64 程序和动态库执行 `strip --strip-unneeded`，保留必要动态符号；构建目录保留原始调试文件。AMR 检查会核对应用身份、桥接导出、依赖/资源哈希、许可及 ELF 调试段。

缓存、依赖下载、构建目录、设备数据、截图和安装包保持 Git 忽略。不要提交密钥、私有构建配置或原始研究数据。反馈上传的私有配置要求见[反馈服务说明](FEEDBACK.md)。

## 设备与硬件

### 支持范围

| 设备配置 | 已授权测试设备 | 笔头采集 | 界面与相机 |
|---|---|---|---|
| ORANGE | `MEB0400008900794` | SC2356，496×360 | 横屏；已有正向纸面样例验收 |
| Y02 | `MEA2300009007113` | SC035GS，640×480 | 横屏；使用独立采集和中性平场配置 |
| Y07 | `7G12000010905426` | SC2356，752×440 | 竖屏；960×720预览、3264×2448拍照路径 |

这些是明确匹配的板型/传感器配置，不通过分辨率或相同传感器推断未知机型可用。中性平场不等于白纸实测校准，收到原帧不等于完成光学质量验收。Y07 的适配与剩余验收范围见[Y07说明](Y07-0.2.25.md)；各版本真实样例与限制保留在扫描文档及专题记录中。

设备测试必须显式指定已授权设备，隔离测试数据后恢复用户数据库、图片和公共目录。不重启系统 MiniApp 宿主，不覆盖其他应用数据。测试入口和具体恢复约束见 [AGENTS.md](../AGENTS.md) 与 `tests/device/`。

### 显示与触摸

原生启动读取 Falcon 的 `/etc/miniapp/resources/cfg.json` 中 `screen` 配置。宽高必填，旋转和偏移用于独立 DRM plane；启动器使用原生返回的相同逻辑几何，不提供设备分辨率回退。

前台输入由原生 evdev 按完整多点帧处理，触摸旋转和偏移仅映射一次；后台、官方键盘和退出时关闭输入。竖屏使用共享布局与 EditorLayout，聊天全宽、表单单列、编辑区在键盘上方。相机光学方向与屏幕、触摸方向分别处理。

主机离屏运行需显式传入 `--screen-config <JSON路径>`；测试分辨率是夹具，不是运行时默认值。详见[显示适配](FALCON-DISPLAY.md)、[原生输入约定](../native/launcher/README.md)及[图片手势](IMAGE-GESTURES.md)。

### 扫描与相机

扫描的编译期设备配置、RK／CVI 平台边界、帧所有权和 runtime 分批迁移见
[扫描平台与编译期配置](SCANNER-PLATFORMS.md)。新增已支持能力的机型优先组合
配置；不要在公共扫描会话或 Stream32 中添加板型分支。

扫描采用全内存流水线：原始Y平面复制一次，后台逐帧筛选，选中帧有序进入32帧首批、24帧步长、8帧重叠的模型窗口，绘制提交后按引用释放。保留原始尺寸和逐帧光照估计，临时帧不写盘；每100ms根据整机余量授予动态预算，不设置固定240MiB RSS限制。正常松开排空已接收工作，取消可丢弃排队任务，资源不足返回明确部分结果。

模型在后台预热成功后才启动采集，健康模型和扫描进程常驻至应用退出；离页或后台仍需释放采集、灯、按键及系统服务租约。Y02 的驱动缓存同步仅用于已核实的驱动和调用范围。绘制前检查几何及图像一致性，不可信尾部不会覆盖此前画布。

Y07 通过驱动 LED 类控制白灯，使用厂商模式值1，不套用 ORANGE 的 GPIO 控制；其锁定私有 RKNN 运行库与无执行 `system` 阻断符号必须保留。板型、V4L2 节点、丢帧规则、退出恢复及资源锁见[扫描模块说明](../native/scanner/README.md)。

相机预览合成到整页 LVGL，不独占额外显示层。拍照冻结方向并只旋转一次真实像素；裁剪使用原图逆映射，界面留白不进入输出。照片确认前位于临时目录；只有用户选择加入系统相册才创建新的系统照片。源码来源和加速度计租约见[相机模块说明](../native/camera/README.md)。

## 数据与请求约定

应用使用自己的 `penchat.db`、照片与资产目录，不读取旧应用数据库或 Storage。系统相册 `/userdisk/Pictures` 仅供读取；发送时复制为应用资产，不删除系统原图。未发送的空会话保持内存状态，首条消息与活动选择原子落库。

数据库业务事务在单一工作线程执行，使用版本化迁移、升级前备份、SQL 游标分页和事务化资产回收。UI 更新与持久化分开合并，退出排空保存队列；迁移冲突不能通过删除历史自动修复。详见[存储与图标说明](STORAGE-AND-ICONS.md)。

三种 API 共用原生网络服务，聊天、重试、命名和压缩冻结各自的参数快照。强度统一解析，API 默认省略相应可选参数；公开思考与不可展示的续传状态分开保存，后者仅按匹配的请求身份续传，不导出或写入日志。失败不自动重发、降档或换协议。

自动压缩默认在包含本次输入的估算上下文达到模型本地预算75%时触发，全局阈值可调。成功时完整归档与摘要更新原子提交；原文与原图保留，不静默截断历史。会话信息中的估算和用量统计不代表服务商账单。

## 专题文档与验证记录

| 主题 | 文档 |
|---|---|
| 输入、学习与纠错 | [内置编辑器](NATIVE-EDITOR.md)、[拼音验收](PINYIN-ACCEPTANCE.md)、[纠错评价](PINYIN-CORRECTION-0.2.9.md)、[边缘触摸](KEYBOARD-EDGES-0.2.10.md) |
| API 与网络 | [API兼容设置](API-COMPATIBILITY.md)、[Anthropic Messages](ANTHROPIC-MESSAGES.md)、[连接复用](HTTP-CONNECTIONS.md)、[模型能力匹配库](MODEL-CATALOG.md) |
| 用户流程 | [首次引导](ONBOARDING.md)、[上拉灵敏度](PULL-SENSITIVITY.md)、[字体大小](FONT-SIZE.md)、[图片手势](IMAGE-GESTURES.md) |
| 会话与数据 | [导入导出](TRANSFER.md)、[格式规范源](transfer-format/使用说明.md)、[上下文压缩](CONTEXT-COMPRESSION.md)、[会话信息](SESSION-INFO.md)、[存储](STORAGE-AND-ICONS.md) |
| 更新与反馈 | [更新和发布](UPDATING.md)、[反馈与私有配置](FEEDBACK.md)、[按键采集](KEY-CAPTURE-0.2.22.md)、[启动与IPC恢复](STARTUP-RECOVERY.md) |
| 扫描优化 | [全内存流水线](SCAN-MEMORY-0.2.29.md)、[Y02连续性](SCAN-CONTINUITY-0.2.30.md)、[尾部几何保护](SCAN-GEOMETRY-0.2.31.md) |
| 性能与清理 | [输入法与退出检查](PERFORMANCE-0.2.7.md)、[0.2.32代码检查](CODE-AUDIT-0.2.32.md) |
| 早期验收与布局 | [LVGL验收](LVGL-ACCEPTANCE.md)、[API验收](API-ACCEPTANCE.md)、[UI检查](UI-REVIEW.md)、[Y07适配](Y07-0.2.25.md) |

历史验收记录只表示当时的测试范围。新的硬件/运行时修改应重跑相关测试并查看真实结果，人工纸面划扫与光学质量确认单独记录。

## 图标与许可

原创图标源为 [`assets/penchat-icon.svg`](../assets/penchat-icon.svg)。安装图标PNG使用 `icon-<SHA256前8位>.png` 内容命名，配置引用来自 `package.json`；更换图像时同步名称，避免宿主缓存旧图。

自有代码、文档、测试和原创图形采用 [Apache-2.0](../LICENSE)。第三方代码、字体、模型、SDK与运行库按各自许可处理。RKNN SDK/运行库、扫描模型及附带校准资源未获授权；部分 Falcon/JQ SDK 的许可也尚未确认。公开源码构建成功不表示相关二进制可公开再分发。

第三方来源、许可原文、源码和再分发义务以[第三方声明](../THIRD_PARTY_NOTICES.md)、[NOTICE](../NOTICE)及各依赖锁为准。

开发整合中的公共组件、三类布局及 Rockchip/S6 后端边界见 [共享平台说明](SHARED-PLATFORMS.md)。S6 的采集反复重开仍为独立待办。

### 联网搜索组件（dev）

公共工具注册、三协议调用续答和网页图片引用的接入说明见
[WEB_SEARCH.md](WEB_SEARCH.md)，独立构建/更新入口见
[web-search/README.md](../native/web-search/README.md)。修改搜索组件后分别执行
`bash scripts/build-web-search.sh rk3562` 与 `bash scripts/build-web-search.sh cv1826-s`，
主应用仍使用原有构建脚本。不要把另一目标的 `assets/web-search` 放进安装包。
开发编译不执行搜索、不调用付费模型；`tests/test_web_tools.py` 是单独的模拟请求回归入口。

### Markdown 标点换行（dev）

三类布局共用 `RichText::layout` 的软换行规则。常见中英文收尾标点与前文
一起换行，左括号、左引号随其后的文字；加粗、链接及其他行内样式的边界
不产生额外断点。表格单元格复用相同规则，行内公式也保留收尾标点。
超长英文单词仍可按字符拆行；极窄区域中，连一个完整标点组合都放不下时
保留该组合并允许横向阅读。主动换行与代码块不套用禁则，不改写原文和复制偏移。
字宽预读与落点共用单次布局内、最多256项的缓存。

定向检查：构建 `penchat-markdown-wrap-tests` 和 `penchat-text-selection-tests`，
再执行 `ctest --test-dir build/host -R '^penchat-(markdown-wrap|text-selection)$' --output-on-failure`。
新增用例覆盖多字号、窄栏、样式边界、公式、表格、长单词及显式换行。
`penchat-markdown-wrap-tests <字体> <MicroTeX资源目录> <已有输出目录>`
可额外生成几种阅读宽度的排版截图；截图不入库。

### DPI 与全局缩放基础（实验分支）

当前“字体大小”已覆盖全应用文字，沿用`reading_font_percent`旧键，与独立的
界面缩放组合后一次取整。设计几何走`dp()`，设计字号走`sp()`或语义字体角色；
字体预览、异步保存与失败处理见[字体大小](FONT-SIZE.md)。

以下保留各阶段的实施记录；当前完整范围、单位边界、键盘适配和验收命令见
[DPI 与全局缩放](DPI-SCALING.md)。设备可通过正式设置调整界面倍率，仍不开放主机测试命令行参数。

`ui_metrics.hpp` 的 `UiMetrics` 统一设计尺寸和语义字号，默认 density/UI scale
均为 100%。两者先组合，再统一四舍五入到 Falcon 逻辑像素；不根据分辨率推测
物理 DPI。生产设备固定 100%，本阶段没有用户设置或数据库迁移。

- `dp()` 只接受设计尺寸。坐标、已计算的宽高、图片像素、LVGL 百分比和
  `LV_SIZE_CONTENT` 直接传递，不能再次转换。普通 helper 的外部 x/y/w/h 均为
  已解析逻辑像素，例如调用方先计算 `metrics.dp(44)`，按钮内部不再缩放高度。
- `label(..., UiFont)` 使用语义字号；原 `label(..., int)` 和 `font(int)` 继续接受
  最终像素字号。阅读字体百分比、Markdown 和键盘的既有字号路径保持独立。
- 基础控件已迁移通用按钮、字段、页面标题/动作、设置行内部度量及 box 默认
  字体；第二阶段接入 AppLayout 和 composer，详见下文。键盘复用 helper 时仍
  使用 100% 兼容指标，不能把局部试验当作全应用缩放验收。
- 主机程序仅在 `--headless` 下接受 `--ui-scale-percent 75..150` 整数参数；设备
  构建拒绝该参数。App 度量启动时确定，不支持运行中更改。

定向检查（先完成 host 构建）：

```bash
cmake --build build/host --target penchat-ui-metrics-tests penchat-ui-controls-tests -j6
ctest --test-dir build/host -R '^penchat-ui-metrics$' --output-on-failure
python3 tests/test_ui_controls.py
# 基线必须由改造前的独立程序生成，不能用当前结果更新后再比较。
python3 tests/test_ui_metrics_baseline.py --binary /absolute/path/to/baseline \
  --output .work/ui-metrics/baseline
python3 tests/test_ui_metrics_baseline.py --baseline .work/ui-metrics/baseline
```

控件检查在临时数据目录中运行真实 LVGL，每个进程只创建一个 App，遵循
MicroTeX 的进程级生命周期；覆盖三种布局与四档倍率。基线比较使用相同字体、
资源和确定性的 PNG 编码，比较稳定界面的几何及完整图片字节。截图和基线
可执行文件保留在忽略目录，不提交到 Git。


### AppLayout 与 composer 度量接入

AppLayout 接收显式 UiMetrics，转换导航/页面/消息区/弹层及各工作区的固定设计
尺寸；显示宽高、布局类别的宽高比、比例分栏及 ScreenSafety 的物理圆角包络
保持 Falcon 逻辑坐标。相机/裁剪只转换工作区控件尺寸，不改变采集、图片像素
或旋转坐标。EditorLayout 默认使用原有指标，键盘键帽和候选布局尚未迁移。

composer 的控件直径、内外留白、文字区域、工具行、键盘间隔及返回底部按钮
由同一组指标驱动；创建、草稿增长和停靠路径共用这些度量。Composer 字体角色
默认 20px，测量、预览和竖屏内嵌 textarea 使用同一个最终字号，不再借用键盘
字号隐式决定正文布局。聊天阅读字号设置仍独立，键帽不会跟随缩放。

保持单行圆角、横屏两倍/竖屏既有增长策略、局部滚动、composer 显隐策略，以及
160ms 回弹和所有手势时间规则。下拉创建只根据转换完成后的未遮挡 reader
计算空间阈值，按下时冻结；不再乘 UI 倍率，也不改变 NativeTouch 映射。

主机验证包括四档倍率、三类布局、空白/短/长草稿、有无推理强度按钮及停靠编辑；
100% 与上一阶段的独立二进制比较几何及 PNG 字节。非 100% 仍为主机实验，其他
页面的内部内容、图标图形及键盘整体缩放未完成，不开放设备设置。

```bash
python3 tests/test_composer_scale.py
# 在修改前记录一次；此文件夹不能由新程序覆盖。
python3 tests/test_composer_scale.py --binary /absolute/path/to/baseline \
  --scales 100 --output .work/composer-before
python3 tests/test_composer_scale.py --scales 100 --baseline .work/composer-before
ctest --test-dir build/host -R '^penchat-(ui-metrics|platform-layout)$' --output-on-failure
```

此阶段曾发现 `test_docked_dismiss.py` 取消触摸后的 offset 归零超时，独立基线
`1e36122` 同样失败。最终阶段已修复：先把取消事件交还已拥有该手势的键盘
下推处理器，避免被阅读选择处理器吞掉；原有取消与归零断言现已通过。

### 设置页与 Provider / 模型表单度量接入

第三阶段把设置分组的起点、列间距、行距和组间距接入 Form 指标；Provider
列表、连接表单、模型列表、身份字段、能力填充区、API 兼容与 Anthropic 表单
的内部尺寸和字号显式解析到逻辑像素。字号预览页、连接保留时间和键盘设置页
也使用同一套页面度量；阅读字号百分比的预览继续使用阅读字体，不乘 UI 倍率。

- 100% 保留原有单列/双列结构、字段高度与标题动作位置；与上一阶段二进制
  比较设置及 Provider/模型页面的几何和 PNG 字节。
- 非 100% 字段使用实际字体行框确定标题、值和高度；Provider 连接区及添加
  模型页根据解析后的字段高度推进，避免相邻行重叠。所有尺寸只换算一次，
  API 超时、token 上限、分页数量、字符限制与业务默认值不属于布局单位。
- 单列表单重排使用统一留白，能力填充按钮保持绑定对应字段。竖屏模型标题栏
  在放大后容不下两个动作时，把模式切换放入滚动内容；100% 不走这项调整。
- 竖屏内嵌字段编辑框沿用表单的字号和文本起点；键盘根节点、候选区和键帽
  几何保持原样。字段切换仍只提交到 Provider 编辑工作区，必须显式保存才写库。
- 非 100% 仍仅供主机测试。此阶段不迁移全部业务页面、通用弹层和键盘整体布局。

```bash
python3 tests/test_form_scale.py
python3 tests/test_form_editor_scale.py
# 使用修改前的独立二进制记录；不要用新程序覆盖旧基线。
python3 tests/test_form_scale.py --binary /absolute/path/to/baseline \
  --scales 100 --output .work/forms-before
python3 tests/test_form_scale.py --scales 100 --baseline .work/forms-before
```

表单测试使用临时数据库和虚构本地 Provider，不请求真实模型；通过实际控件
边界定位入口，避免把旧测试固定坐标在放大后的失效当成产品回归。

第三阶段曾发现 `test_anthropic_ui.py` 的旧 system 等值断言与已存在的应用规则
不一致，独立基线 `1423410` 同样失败。最终阶段按当前请求契约检查应用规则
只出现一次、先于会话指令、会话指令只出现一次且位于末尾；完整测试已通过。
请求生产逻辑未因此改变，模型同步测试也改为实际的选择、添加和显式保存流程。

### 思考期间的联网状态

工具轮次结束不代表整个思考结束。`web_pending` 仅标识本地搜索/等待续答阶段，
不进入模型请求协议；最终回答、取消和失败会结束该状态。“思考完毕后收起”
在搜索期间保持展开，明确的手动展开/收起选择优先。正在搜索及已完成的查询
在思考详情里作为独立只读文字显示，不拼入 reasoning_text 或模型上下文。
每条查询按Unicode字符截断到160字，最多显示最近24条工具记录；错误或取消
不显示为成功。完整检索记录与来源仍在回答阶段可访问。

`test_reasoning_search_ui.py` 使用本地HTTP模型与隔离假搜索子进程，验证运行中、
搜索完成等待续答、手动收起、最终回答和保存内容；不访问搜索引擎或真实账户。
相关工具协议、既有推理UI、字体和键盘像素定向检查的日志分别位于忽略目录
`.work/reasoning-search/`、`.work/footer-scan-font/` 和 `.work/composer-lower-corners/`。
