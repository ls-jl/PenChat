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

构建环境需要 Node.js/npm、aiot-cli、Python 3、CMake、主机 C/C++ 工具链，以及渲染和 HTTP 依赖构建使用的 Meson、Ninja、pkg-config 等工具。AArch64 构建还需要 RK SDK、交叉工具链和扫描资源包。

- `RK_SDK_ROOT`：RK SDK 根目录；`JSAPI_TOOLCHAIN_ROOT`：AArch64 GCC 工具链目录。原生构建脚本中的默认路径属于原开发环境，其他机器需按实际位置配置。
- `PENCHAT_SCAN_BUNDLE`：扫描资源包目录；可使用 `PENCHAT_SCAN_TOOLCHAIN_FILE` 指定扫描交叉工具链文件。SDK/模型及宿主和设备 OpenCV 资源要求见[扫描模块说明](../native/scanner/README.md)。
- 拼音、字体、渲染和网络依赖按源码锁准备，来源及校验以各模块锁文件为准。公开仓库不包含全部设备资源，先核对[第三方许可与未解决授权](../THIRD_PARTY_NOTICES.md)。

在仓库根目录执行：

```bash
# 主机构建与回归
npm run build:host
npm test
npm run test:sanitize

# AArch64 原生程序、桥接与生产 AMR
npm run build:native
npm run check
npm run build:prod
npm run check:package
```

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
