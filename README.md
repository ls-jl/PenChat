<p align="center">
  <img src="assets/penchat-icon.svg" width="112" height="112" alt="PenChat 蜡笔与聊天气泡图标">
</p>

# PenChat

**让词典笔可以扫描、拍照，与 AI 聊天。**

扫一道题、拍一张图，或者直接输入问题，把想问的内容交给你选择的模型。

- **扫描与拍照**：扫描纸面内容、拍照裁剪，也能从相册选图。
- **边生成边阅读**：支持 Markdown、表格和数学公式，可以随时上翻查看历史。
- **按自己的习惯使用**：中英文与9键拼音键盘、会话预设、模型切换、聊天备份。

## 支持哪些设备？

| 设备 | 界面 | 适配说明 |
|---|---|---|
| Y01 | 横屏 | 已适配聊天、输入、扫描与拍照 |
| Y02 | 横屏 | 独立适配扫描与相机 |
| Y07 | 竖屏 | 竖屏聊天、上编辑下键盘，独立适配扫描与相机 |

其他机型尚未验证。不同固件的硬件行为可能不同；具体适配和验收范围见[开发说明](docs/DEVELOPMENT.md#设备与硬件)。

## 第一次使用

### 获取与安装

本仓库提供 PenChat 源码，构建后得到 `.amr` 安装包。首次安装需要使用设备支持的 MiniApp 安装工具；自行构建请看[下方的构建与依赖说明](#自行编译)。

已经安装 PenChat 的用户，可以在 **设置 → 检查更新** 查看可用新版，阅读更新说明后选择更新或稍后处理。0.2.20 及更早版本需要先手动安装支持在线更新的版本。

### 配好模型，开始提问

PenChat 需要你自行准备模型服务的 **API Key**，调用费用由对应服务商计算。首次打开没有预置密钥或模型。

1. 点击聊天页左上角的**两条横线**，在展开菜单的右下角进入**设置**。
2. 打开 **模型管理 → 添加 Provider**。Provider 就是你使用的模型服务商。
3. 填写名称、**Base URL（服务地址）**和 **API Key**，选择服务商提供的接口类型：Responses、Chat Completions 或 Anthropic Messages。
4. 在同页下方的**模型 → 添加模型**获取列表并勾选，或手动填写准确的模型 ID，点击**加入草稿**；最后点击本页**保存**，连接和模型一起生效。
5. 按会话预设或聊天中的选择使用模型。之后点击对应 Provider，可在同一页修改连接、补充模型、编辑参数或标记删除，最后统一保存；模型管理页不设置默认模型。
6. 返回聊天，点输入框写下问题，完成输入后点击发送箭头。

不确定地址、接口类型或模型 ID 时，以你所用服务商提供的接入说明为准。要问图片内容，记得选择支持图片输入的模型。连接和模型在同页统一保存，详见[设置规则](docs/PROVIDER-SETTINGS.md)。

首次打开还有动画引导；之后可以在 **设置 → 关于 PenChat → 使用引导** 重看。

## 同样是扫描，两种用法喵

| 你在哪里扫描？ | 得到什么？ | 适合做什么？ |
|---|---|---|
| **聊天页面，未打开键盘** | 扫描拼接后的**图片** | 把题目、公式、表格交给支持看图的模型 |
| **打开输入框，等键盘就绪后** | OCR 识别出的**可编辑文字** | 输入问题、摘录文字，也可以填写密钥等字段 |

**直接扫描图片**：笔头贴住纸面，沿文字从左向右平稳划扫。结束后检查结果，再确认使用；图片自动发送可在设置中调整。

**键盘内扫描文字**：打开输入框后划扫，识别结果会进入编辑区，可以继续修改。底部按钮可切换单行、多行识别，完成输入后再发送问题或保存表单。

也可以点击聊天输入框左侧的 **＋ → 拍照 / 相册**：拍照后支持裁剪，双指缩放图片、拖动选框边缘调整范围。勾选“加入系统相册”会额外保存一份；不勾选则只用于聊天。待发送图片可长按删除。

## 常用操作速查

| 想做什么 | 怎么操作 |
|---|---|
| 找回旧聊天 | 点击左上角两条横线，打开历史列表 |
| 新建聊天 | 历史菜单左下角“新聊天”；有内容的聊天也可在底部继续上拉，进度满后松手 |
| 管理历史 | 长按聊天标题，可重命名、置顶、归档、删除或导出 |
| 查看归档 | 设置 → 归档会话；可浏览，也可还原后继续聊天 |
| 换模型、调整思考强度 | 点击输入框左侧 ＋，选择“模型”或“推理强度”；“API 默认”让服务端决定强度 |
| 联网查资料与图片 | ＋ → 联网搜索，开启后由模型按需搜索；回复中的来源可阅读，图片可点击放大 |
| 使用预设 | 设置 → 会话预设，开启入口并配置；之后从 ＋ → 预设切换 |
| 查询账户余额 | 在服务商连接设置选择“余额查询厂商”并保存，再点横屏聊天左下的钱包图标，竖屏仍在历史菜单底部；支持 DeepSeek、Kimi、硅基流动及302.AI，悬浮面板内统一刷新 |
| 改自己的问题 | 长按已发送消息 → 编辑消息；重发会覆盖后文，可先归档，发送前可点叉号取消 |
| 复制、重试或创建分支 | 使用 AI 回复下方图标；复制到应用内剪贴板，重试会询问是否归档，分支会保留原聊天 |
| 放大看图 | 点击聊天图片，双指缩放、单指拖动，双击放大或恢复全图 |
| 输出时看前文 | 直接上翻；点击向下箭头回到最新处，继续跟随输出 |
| 横屏阅读时唤出输入框 | 默认在最新消息底部显示；双击加号左上侧、钱包右上方的空白可临时唤出，单击同处收回。设置 → 横屏输入框快捷唤出可调整或关闭 |
| 调整文字大小 | 设置 → 字体大小，支持 75%～150% |
| 查看用量、整理长对话 | ＋ → 会话信息 / 压缩上下文；每次成功压缩都会保留一份“压缩前副本”归档 |
| 备份或导入 | 设置 → 导入与导出，支持聊天、图片和会话预设 |

小提示：从历史菜单新建使用默认预设，上拉新建沿用上一会话的配置。空聊天不会因反复上拉而创建历史记录。

### 输入与选词

内置键盘支持 **26键拼音、英语、9键拼音**，长按地球图标可直接选择布局。

- **空格**选词，**回车**输入尚未上屏的原始字母或数字。
- 有拼音时，第一次点击“完成”先选词，再点一次结束编辑。
- 点按文字定位光标；双击或静止长按选择文字，长按后拖动可精细移动光标。拖动选区两端调整范围，浮动菜单提供复制、粘贴、剪切和撤销。
- AI 回复、思考与阅读页面可直接双击或长按选字；自己的已发送消息仍通过长按菜单进入“选择文本”。[查看文本交互说明](docs/TEXT-INTERACTIONS.md)。
- 默认长按小地球选择“有道输入法”。在 **设置 → 键盘与输入 → 快捷切换官方键盘** 开启快捷入口后，横屏在编辑区底部“取消”与单行／多行按钮之间显示“官方”，竖屏在键盘上方功能区显示“官方”，短横屏在顶部功能区显示系统键盘图标；同时从小地球菜单移除该入口。快捷切换默认关闭，自动保存。
- **默认使用官方键盘** 是独立开关，默认关闭；开启后新输入直接打开有道输入法，收起后保留文字并结束编辑，打开失败时回到内置键盘。拼音学习和纠错也在“键盘与输入”页调整。

应用内剪贴板可跨输入框使用，退出应用后清空。

### 备份放在哪里？

版本回退前创建的完整备份可从“设置 → 还原备份”增量还原，已有聊天或配置逐项选择替换／跳过；详见[还原说明](docs/BACKUP-RESTORE.md)。聊天导出文件仍使用下面的导入导出目录。

聊天与预设文件位于设备的 `/userdisk/Favorite/PenChat/`：

```text
PenChat/
├── 导出/聊天/    ← 导出的聊天包
├── 导出/预设/    ← 导出的预设
├── 导出/格式规范/ ← 格式说明与可导入示例 ZIP
├── 导入/聊天/    ← 放入要导入的聊天包或 Markdown
└── 导入/预设/    ← 放入要导入的预设
```

想自己制作导入文件？在同一页面点击 **查看格式规范**，或用 **导出格式规范** 保存说明与可导入示例 ZIP。先解压规范包，再按其中的“使用说明.md”复制示例。

聊天备份附带图片和可读 Markdown，预设单独保存。**API Key 不随聊天或预设导出**，换设备后需要重新配置模型服务。

“持久化导出目录”默认关闭，正常退出应用会清理导出文件；需要保留备份时请先开启，或将文件复制到其他位置。[查看导入导出说明](docs/TRANSFER.md)。

## 遇到问题？

| 情况 | 可以先检查 |
|---|---|
| 连接失败、域名解析失败 | 检查设备网络与 Base URL；域名解析失败通常发生在连接模型前 |
| 提示密钥或权限错误 | 核对完整 API Key、所用服务地址和接口类型，确认服务账户可用 |
| 兼容服务无法正常回复 | 在 Provider 编辑页查看“API 兼容模式”，根据服务商要求调整并保存 |
| 模型看不到图片 | 确认模型支持图片输入，检查图片是否已加入待发送区 |
| 扫描结尾歪斜、重叠或不清晰 | 尽量贴平纸面、匀速正向扫描，结束时避免转动笔头；反向和深色屏幕扫描尚未验证 |
| 回复中断 | 检查网络后手动重试；重试会覆盖该回复及后文，请留意归档提示 |

可以从 **设置 → 问题反馈** 描述问题、预览诊断后提交，也可以在 [GitHub Issues](https://github.com/ls-jl/PenChat/issues) 反馈。请写明设备型号、复现步骤和看到的错误，避免附上 API Key 等凭据。

愿意帮助适配其他机型时，可在反馈页勾选“帮助开发者适配更多机型”，按向导采集按键信息。

## 自行编译

GitHub 仓库已有 [Falcon/JQ 启动桥所需的头文件和源码](native/vendor/iot-miniapp-sdk)。安装已发布的 AMR 无需准备开发 SDK。自行编译时，**Linux 主机版和词典笔安装包所需的依赖不同**。

### Linux 主机版

验收环境为 x86_64 Linux。准备 C/C++ 编译器、Git、Python 3.12、CMake 3.x（设备包用 3.20 或以上）、Meson、Ninja、pkg-config、Make，以及 SQLite、OpenSSL、zlib 开发包。以下命令使用 Debian/Ubuntu 的包名。

```bash
sudo apt install build-essential git cmake meson ninja-build pkg-config \
  libsqlite3-dev libssl-dev zlib1g-dev
python3 --version
bash scripts/build-lvgl.sh host
```

用 `python3` 运行完整构建脚本时，确保系统提供 Python 3.12。上面的命令构建主机版 LVGL 和相关原生测试。LVGL、渲染库和 [libdrm 2.4.124](https://dri.freedesktop.org/libdrm/) 等公开源由脚本按 [依赖锁](native/lvgl-app/dependencies.lock.json) 下载并校验，放入 `.deps/`。需要自选 DRM 头文件时，可在首次配置前把 `LIBDRM_DIR` 指向包含 `xf86drm.h` 和 `include/drm/drm.h` 的 libdrm 源码树。完整的 `npm run build:host` 还会构建扫描器，因此也需要下表中的 RK 扫描资源包和其中的主机 OpenCV。

### RK3562 与 S6/CVI 设备包

在主机工具之外，准备 Node.js/npm（验收使用 Node.js 20）、[ADB Platform-Tools](https://developer.android.com/tools/releases/platform-tools)、Perl 和 patch。打包 CLI 的 npm 包名为 [`aiot-vue-cli`](https://registry.npmjs.org/aiot-vue-cli/1.0.32)。

```bash
npm ci
npm install -g aiot-vue-cli@1.0.32
aiot-cli check
```

| 依赖 | 获取与配置 |
| --- | --- |
| **RK 和 CVI 基础工具链** | 两个平台的 AMR 都构建双架构急救桥，因此需要 Linaro GCC **6.3.1 / 2017.05** 的两套 Linux 工具链及各自随附的 sysroot。可在 [Arm 旧版工具链档案](https://developer.arm.com/Downloads/-/Legacy%20Linaro%20GNU%20Toolchains) 按版本查找。旧 `releases.linaro.org` 下载链接已重定向到联系页，可向板卡厂商获取配套 BSP，或使用 Firefly 的同版本 [AArch64](https://gitlab.com/firefly-linux/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu) 和 [ARM hard-float](https://gitlab.com/firefly-linux/prebuilts/gcc/linux-x86/arm/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf) 工具链库。分别配置 `JSAPI_TOOLCHAIN_ROOT`、`PENCHAT_CV1826_TOOLCHAIN`，路径指向包含 `bin/` 及对应目标 `libc/` 的工具链根目录。 |
| **联网搜索编译** | 使用 [Clang 18](https://packages.ubuntu.com/noble/clang-18)（脚本查找 `/usr/lib/llvm-18/bin/clang++`）和 GCC 12 的目标 C++ 头文件。按目标从 Debian 获取 [arm64](https://packages.debian.org/bookworm/libstdc++-12-dev-arm64-cross) 和 [armhf](https://packages.debian.org/bookworm/libstdc++-12-dev-armhf-cross) 开发包。脚本识别系统的 `/usr/<目标三元组>/include/c++/12`；解压在其他位置时，分别设置 `PENCHAT_EDGE_CXX_HEADERS_RK3562`、`PENCHAT_EDGE_CXX_HEADERS_CV1826_S`。 |
| **公开扫描依赖** | RK 和 S6 的 PNG／zlib 开发头文件可从公开 AOSP 获取，脚本按[扫描依赖锁](native/scanner/dependencies.lock.json)自动下载并校验。已有厂商 SDK 时，RK 可用 `RK_SDK_ROOT` 指定其中的 `external/libpng`、`external/zlib`，S6 可用 `PENCHAT_CV1826_PNG_INCLUDE` 指定 PNG 头文件目录。S6 的 [OpenCV 3.4.3](https://github.com/opencv/opencv/releases/tag/3.4.3) 也会自动下载校验；离线可用 `PENCHAT_CV1826_OPENCV_ARCHIVE` 指向相同归档。 |
| **RK 扫描器工具链** | 用 [Debian GCC 12 AArch64 交叉包](https://packages.debian.org/bookworm/g++-12-aarch64-linux-gnu)及其配套开发包准备扫描编译器，按[目录示例](docs/DEVELOPMENT.md#设备构建变量与缓存)配置 `PENCHAT_SCAN_CROSS_ROOT`；也可用 `PENCHAT_SCAN_TOOLCHAIN_FILE` 指定兼容的 CMake 工具链。两套 GCC 6 工具链已单独配置时，编译 PenChat 无需准备完整 RK Android SDK 源码。 |
| **RK 私有扫描资源** | `PENCHAT_SCAN_BUNDLE` 指向含锁定模型、平场校准和 OpenCV 的 `stream32_complete_20261018` 资源包，`PENCHAT_SCAN_BOARD_LIBS` 指向含锁定板端 PNG、zlib、libstdc++ 的目录。模型和校准没有公开下载，需从资源提供方取得授权副本。构建会按[依赖锁](native/scanner/dependencies.lock.json)从 [RKNN 上游](https://github.com/airockchip/rknn-toolkit2/tree/b25dadacc24b88eb7dfcaa47c9c525ecca89b319)获取并校验 RKNN 1.6.0 运行库。 |
| **S6/CVI 完整扫描器** | 使用 Clang 18 和 ARM hard-float sysroot，必要时用 `PENCHAT_CV1826_CLANG` 指定编译器。若仅取得授权的锁定模型文件，用 `PENCHAT_CVI_SCAN_MODEL` 指向该文件；若取得完整资源包，用 `PENCHAT_CVI_SCAN_BUNDLE` 指向 `cv1826_int8_complete_20261018`。模型和资源包没有公开下载。编译用的固件库由脚本从用户自己的匹配 S6 通过 ADB 准备并校验，来源见 [CVI 模型锁](native/scanner/cvi-dependencies.lock.json)和 [CVI 媒体锁](native/scanner/cvi-media.lock.json)。 |
| **设备系统链接库与字体** | `PENCHAT_ADB_SERIAL` 指定自己的匹配设备，或按[缓存目录说明](docs/DEVELOPMENT.md#设备构建变量与缓存)准备来自同目标固件的 SQLite、OpenSSL 1.1、zlib 链接库与字体。RK 与 CVI 的库不可混用。 |

配置好对应依赖后，在仓库根目录选择一组命令。只想编译 LVGL 主程序和启动桥，可以先执行各组第一行；完整 AMR 需要该平台的扫描资源。

```bash
# RK3562，输出 dist/*-rk3562.amr
bash scripts/build-lvgl.sh rk3562
bash scripts/build-web-search.sh rk3562
npm run build:prod

# S6 / CV1826，输出 build/package-cv1826-s/*-cv1826-s.amr
bash scripts/build-lvgl.sh cv1826-s
bash scripts/build-web-search.sh cv1826-s
bash scripts/build-scanner.sh cv1826-s
python3 scripts/package-cv1826-s-dev.py --no-build
```

工具链、设备库、模型和生成物留在本地。完整设备包仍依赖相应的授权扫描资源。请通过资源或设备提供方获取可使用的副本，脚本会核对锁文件中的精确大小或 SHA-256。已有通用 SDK、同版本 OpenCV 或相同芯片，不代表其设备二进制与项目锁定输入一致。只有公开源码和公共工具链时，可以先构建 Linux 主机版。私有文件清单、变量示例、缓存布局及校验方法见[开发说明](docs/DEVELOPMENT.md#设备构建变量与缓存)。

### RKNN 运行库和 pintu 模型

RK3562 构建使用 [Rockchip RKNN Toolkit2 v1.6.0](https://github.com/airockchip/rknn-toolkit2/releases/tag/v1.6.0) 的 **Linux AArch64** 运行库。在线构建会从[锁定的上游文件](https://github.com/airockchip/rknn-toolkit2/blob/b25dadacc24b88eb7dfcaa47c9c525ecca89b319/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so)下载并验证。离线时，可以提前下载[运行库和原版许可归档](https://github.com/ls-jl/PenChat/releases/download/sdk-rknn-runtime-v1.6.0/rknn-runtime-v1.6.0-linux-aarch64.tar.gz)，在仓库根目录执行以下命令。

```bash
mkdir -p .deps/scanner-rknn/v1.6.0
tar -xzf /path/to/rknn-runtime-v1.6.0-linux-aarch64.tar.gz -C .deps/scanner-rknn/v1.6.0
(cd .deps/scanner-rknn/v1.6.0 && sha256sum -c SHA256SUMS)
```

该归档只提供锁定的运行库。SDK 的 C 接口位置见 [Rockchip v1.6.0 SDK](https://github.com/airockchip/rknn-toolkit2/tree/b25dadacc24b88eb7dfcaa47c9c525ecca89b319/rknpu2/runtime/Linux/librknn_api)，需完整 SDK 或 SDK 文件授权时请联系 Rockchip。PenChat 构建会使用仓库中现有的 C 接口头文件。

项目使用的 `pintu_32.rknn` 没有获授权的公开下载。向模型权利人取得可使用的资源副本后，将模型放在 `$PENCHAT_SCAN_BUNDLE/models/rknn/pintu_32.rknn`，并按[私有文件清单](docs/DEVELOPMENT.md#完整扫描包所需的私有文件)准备配套校准和其他构建文件。运行 `python3 scripts/prepare-scanner.py --cross` 可依锁文件验证。S6 使用独立的 CVI 模型和设备自带运行库。

### GitHub 开发构建

`main` 或 `dev` 有推送时，[PenChat Dev Build](https://github.com/ls-jl/PenChat/actions/workflows/dev-build.yml) 用 GitHub 的 Ubuntu runner 编译 Linux x86_64 无窗口版本。通过定向测试后，在该次运行的 **Artifacts** 下载压缩包、渲染示例和 SHA-256 校验文件，保留 14 天。压缩包中的 `SOURCE_COMMIT` 标识对应源码。

要保留一份可公开下载的预发布，从 Actions 手动运行该工作流，选择 `main`，并勾选 **Publish the verified Linux build as a GitHub pre-release**。每次发布使用独立的 `dev-linux-*` 标签，正式更新源与现有 AMR 保持原样。此工作流的产物用于 Linux 主机调试。RK/CVI 安装包仍需上文所列的设备 SDK、固件库与扫描资源。

### 从本机发布词典笔 Dev 包

在已经配置两套设备构建环境的可信 Linux 主机上，提交当前改动，再执行以下命令。

```bash
# 编译并校验两套完整 AMR，结果保存在 .work/dev-amr/
bash scripts/publish-dev-amr.sh

# 同一流程完成后，用本机已登录的 gh 发布 GitHub Dev Pre-release
bash scripts/publish-dev-amr.sh --publish
```

发布使用单独的 `dev-amr-<版本>-<源码提交>` 标签，附件按 RK3562 和 S6/CVI 标明目标。流程在草稿阶段回读核对 SHA-256，校验通过后公开为 Pre-release。应用内版本沿用 `package.json`，Dev 包用于手动安装；普通更新源、回退目录和已有正式附件不参与这条流程。公开 Dev 构建禁用本机的私有反馈配置。

## 开发与许可

想参与开发或了解实现？请看[开发说明](docs/DEVELOPMENT.md)，其中整理了构建、架构、设备边界与专题文档。

PenChat 自有代码采用 [Apache-2.0](LICENSE)，第三方材料保留原许可。

**Fork 和商业使用代码不等于获得 PenChat 名称及官方图标的使用许可。** 非官方发布版本必须使用可区分的应用名称和图标，不得冒充“PenChat 原版”“PenChat 官方版”“PenChat 最新版”等；正常注明“基于 PenChat 开发”不受影响。详见[名称与图标使用政策](TRADEMARKS.md)。
**RKNN SDK 头文件、扫描模型及随附校准资源的使用／再分发授权仍未取得；部分 Falcon/JQ SDK 文件的授权也尚未确认。** 锁定的 RKNN 1.6.0 Linux AArch64 运行库随附其上游 BSD 许可。完整说明见[第三方声明](THIRD_PARTY_NOTICES.md)和 [NOTICE](NOTICE)。

作者：**ls-jl** · AI 协作：**Codex（OpenAI）**

项目地址：[github.com/ls-jl/PenChat](https://github.com/ls-jl/PenChat)。软件内的版本、署名与项目地址也可在 **设置 → 关于 PenChat** 查看。

启动失败页的“版本回退”、双架构 bootstrap、历史版本目录与数据备份约定见 [docs/DOWNGRADE.md](docs/DOWNGRADE.md)。
