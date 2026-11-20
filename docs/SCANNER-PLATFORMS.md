# 扫描平台与编译期配置

扫描由一套公共会话和 Stream32 引擎驱动，RK、CVI 提供不同的采集及推理后端。
新增机型时，优先组合已有能力；公共流程不按板型或传感器名称选择行为。
本设计覆盖笔尖扫描，不包含主摄像头拍照，也不自动推断未知设备的光学参数。

## 配置入口

`native/scanner/src/profile.hpp` 中的 `scanProfiles()` 是设备注册表。它同时供
C++14 UI／launcher 和 C++17 scanner 使用，修改后重新构建生效。注册项包含
板型匹配、实际传感器匹配和 `CaptureProfile`，未知或冲突身份拒绝启动。
屏幕尺寸、触摸方向和 Falcon 的 `video_direction` 不参与设备或光学配置选择。

| 部分 | 参数与单位 | 作用 |
| --- | --- | --- |
| 身份 | `id`、板型和 sensor 匹配规则 | 稳定诊断／租约身份，与显示布局无关 |
| 采集 | `width/height`、节点、格式 | 原生像素尺寸；RK NV12 灰度平面、CVI 灰度输出 |
| 启动 | `capture.startup` | 选择已实现并测试的驱动调用顺序 |
| 扫描边界 | `capture.boundary` | 硬件门控或按采集时间戳截取落笔区间 |
| 节点 | `capture.nodes/candidates` | 固定节点，或带驱动 card 名称约束的 ISP 候选列表 |
| 光学 | `opticalRegion`、`optics.rotation` | 原生坐标中的纸张矩形；一次光学旋转 |
| 校准 | `optics.calibration/metadata/gain/version` | 空间增益图、锁定常数文件或独立中性校准 |
| 灯 | `light/gpio/ledBrightness/ledOnValue/ledOffValue` | GPIO 编号或驱动属性；数值是驱动定义的模式 |
| 板级配置 | `boardIo` | 固件 JSON 的命名字段、按键身份／键码及 GPIO 所有权策略 |
| 推理 | `inference` | 模型资源、runtime 选择和受限兼容措施 |
| 交接 | `handoff` | OCR 按键转发、媒体交接和阻塞式待机取帧能力 |

固件中的灯和键码值只从配置指定的 section／字段读取，数值与身份经过校验。
不存在生产环境热加载、任意命令脚本或自动降级 runtime 的入口。

## 现有组合

| Profile | 原生尺寸／有效区域 | 启动／结束 | 灯 | 校准 |
| --- | --- | --- | --- | --- |
| orange-sc2356 | 496×360／全幅 | 两次 quick-stream／硬件门控 | GPIO 43 | 原有空间增益图 |
| y02-sc035gs | 640×480／全幅 | 一次 quick-stream＋连续16帧／时间戳 | GPIO 43 | 独立中性 |
| y07-sc2356 | 752×440／全幅 | 两次 quick-stream／硬件门控 | LED class，白光1、关闭0 | Y07 锁定常数配置 |
| melon-ov7251 | 640×480／全幅 | 标准启动优先／时间戳 | 固件配置的 IR GPIO | 独立中性 |
| s6-pag7920 | 320×240／[112,8,160,192] | CVI 管线／时间戳 | 固件配置的白光 GPIO | 独立中性 |

RK 当前光学方向为逆时针90°，S6 为不旋转。S6 始终验证完整原生帧，只有纸张
区域进入匹配和绘制；不能将笔壳纳入，也不能用阈值裁切代替固定光学区域。
同名传感器不表示灯、分辨率、镜头安装或平场可以共用。

启动策略在 `start_policy.hpp`，延时／等待窗口以毫秒计：两次 quick-stream
维持200ms／100ms间隔；单次策略等待50ms并检查连续16帧；标准启动只有完全
零帧才允许补一次 quick-stream，收到不稳定帧直接失败。CVI 先启动 VPSS
消费者，再启用 VI，并检查连续真实帧。CVI启动完全零帧且返回已知空队列码时，
在同一租约内完整关闭并重建一次采集管线；最多两次构造，保持健康模型驻留。
其他控制、格式及连续性失败直接回收并报错。详见`S6-SCAN-RECOVERY.md`。

## 接口与所有权

`ScanPlatform` 提供 reserve、start、readFrame、keyEvent、light、exposure、
cameraToOcr 和 close。公共租约／按键／灯实现组合 `RkCapture` 或 `CviCapture`。
`hardware.cpp` 保留 UI 可以使用的身份、快照和父进程恢复操作；实际摄像头
及 CVI 媒体实现仅链接进 scanner／设备诊断程序。

每次 reserve 都生成不可变的 `ResolvedDeviceProfile`，固定本次租约的设备、
按键和允许的视频节点。摄像头启动再次检查传感器。健康模型在同一工作进程
中保留，摄像头、灯、按键和 guardian 随页面／后台交接释放。

`FrameView` 是回调有效期内的借用视图：

- `y/bytes`：可访问的灰度数据地址及字节范围；不能在回调返回后持有地址。
- `width/height/stride`：原生像素宽高、每行字节数，允许行尾 padding。
- `sequence`：真实源帧序号；`timestamp` 为采集时的单调时钟微秒。
- `paperFrame()` 只校验和调整视图，不复制、不旋转；`Engine::pushFrame()`
  把有效原始像素复制一次到拥有生命周期的页，之后各阶段共享所有者。

不统一原图分辨率。模型输入缩放、旋转、增益和几何处理沿用原引擎位置。
32／24／8、动态内存准入、丢1—8帧继续／9帧停止、正常结束排空、明确部分
结果以及异步预览均保持原契约。

OCR 的保留按键交接会保留不可变描述及事件时钟属性，释放摄像头后不再控制灯。
Y07 的 host KEY134 过滤仍由 launcher 执行，只处理本进程原有客户端，并恢复
所有原始 mask。CVI 的 SYS／ION／模型是进程生命周期，VB 是采集租约生命周期。

## Runtime 与分批迁移

RK 统一私有文件为 `librknnrt-rk3562.so`，内容仍是原已锁定的 RKNN 1.6.0：

```text
SHA256 9c5e43179e83f82b3ec1f2c2f9b520559af23ab76327aca63500db30d63d6910
```

依赖锁保留来源 commit 和下载 URL；暂存脚本清理原 `librknnrt-y07.so` 名称。
私有库以应用资源绝对路径加载，失败不会尝试系统库。精确哈希的包检查、
scanner 的 `system` 禁执行导出、`ioctl` 导出和动态绑定测试仍然有效。
这不是 seccomp。

ORANGE、Y02、Y07 使用统一私有库。MELON 没有本次授权实机，继续显式选择
系统 RKNN，诊断标为 `pending_validation`。S6 使用原 CVI 模型及设备 runtime。
不能根据“同属 RK3562”将未验证设备标为已通过。

Y02 的 DMA 兼容措施由 RK 推理包装器管理：配置、实际板型与传感器匹配后，
仅后端所有者线程允许启用；每次调用仍核验真实 RKNPU fd 和驱动0.9.1。
其他驱动／未知标志原样透传；保持完整对象、方向和字节范围，不跳过同步。

`capture_strategy`、`runtime_path`、`runtime_migration` 是新增诊断字段。
旧 IPC 命令、profile ID、结果字段和租约字段继续兼容，无数据库迁移。

## 修改示例

新增已有能力的 RK 机型，在注册表中创建独立 profile，组合已有采集、灯、
光学和推理规格，再填写真实板型／传感器匹配规则。例如复用连续采集策略：

```cpp
CaptureProfile p{"new-board-sensor", "actual-sensor", 640, 480};
p.capture.startup = StartupPolicy::SingleQuickStable;
p.capture.boundary = ScanBoundary::TimestampWindow;
p.optics = {OpticalRotation::CounterClockwise90, CalibrationKind::Neutral,
            "", "", "new-board-native-neutral-v1"};
p.light = ScanLight::Gpio;
p.gpio = verifiedGpio;
// 新设备先记录已验证 runtime；完成实机验证后才能选择私有库。
```

单改照明时，只修改该 profile 的灯参数和所有权策略。新增驱动行为时，增加
有明确语义的启动策略和调用顺序测试，不在 Worker／Stream32 中添加机型判断。
新增校准时，保留独立身份、尺寸／旋转校验和资源来源锁，重新做光学验收。
切换 runtime 时修改 `inference`，通过对照回放和采集交接后再提交选择变化。

## 验证与交付记录

主机增加了配置组合、帧范围／stride／区域、光学资源和启动顺序测试，以及
`tests/test_scanner_profiles.py --baseline <原工作进程>` 的五配置、三批次逐图
哈希对照。设备专项入口为 `tests/device/scanner_runtime.py` 和
`tests/device/scanner_unification.py`，必须显式指定已授权 serial。

本轮原始证据位于忽略目录 `.work/scanner-unification/`。runtime 对照使用
主机生成的固定纹理帧，真实执行 NPU 预热和推理；不把输入落到设备文件系统。
采集交接测试隔离／恢复 PenChat 自身完整数据目录和公共传输目录，只丢弃
测试扫描结果，不发送消息，不重启系统 MiniApp 宿主。

纸面亮度、文字清晰度、手持运动和中性校准的光学质量仍需用户实际验收。
自动回放、成功取帧和开发包安装不代表这些项目已通过。

### 2026-11-12 定向结果

- 基线与重构后的扫描主机定向套件通过；原生测试从14项增加为15项。
  五种配置各80帧／三个推理批次，计数、原图和预览 PNG 哈希与基线完全一致。
- RKNN 系统／私有运行库对照使用 ORANGE、Y02 的原选择及 Y07 的原1.6.0文件。
  三台均完成预热和80帧／三个批次，画布、mask、计数相同，灰度最大差异为0。
- AArch64、ARMv7 的 UI、launcher、scanner 交叉构建通过；S6 诊断探针也通过
  新接口编译。两种开发包的身份、架构、来源锁、必要导出和平台隔离检查通过。
- 命令策略检查补齐了既有 `tools.cpp` 固定路径 web-search 子进程的精确调用
  记录；未增加 shell 执行能力，也未放宽私有 runtime 的哈希或符号例外。

| 设备 | runtime 结果 | 实机采集／交接 | 尚未完成 |
| --- | --- | --- | --- |
| ORANGE | 私有1.6.0对照通过 | 5轮采集、3轮OCR、后台、子进程异常恢复通过 | 新的手持纸面质量验收 |
| Y02 | 私有1.6.0对照通过 | 同上；首次短扫报告3帧丢失并按原规则继续 | 新的手持纸面质量验收 |
| Y07 | 同内容1.6.0统一路径通过 | 5轮采集、3轮OCR、后台、子进程异常恢复通过 | 中性校准及手持纸面质量验收 |
| S6 | CVI模型／runtime保持不变 | 同日零帧恢复修复后，5轮采集、3轮OCR、后台、子进程异常恢复通过 | 手持纸面质量验收；供应商首次零帧的内部原因仍未完全定位 |
| MELON | 保留系统RKNN | 主机配置／过程验证 | 缺少授权实机，runtime与设备验收待完成 |

S6 超时为 `CVI sensor startup produced no continuous frames`。使用基线提交
`3d8d239910be75ac8b7036fde978bc1cc53c3c71` 构建原扫描工作进程，在相同 UI／系统
环境中临时替换进行对照：原工作进程完成三轮OCR交接后，后台恢复也出现相同
错误。对照后已恢复安装的工作进程；公私数据目录哈希、宿主 PID、boot ID 均
保持。此证据证明故障也能在原工作进程出现。后续零帧恢复修复及其定向验收
见[S6修复记录](S6-SCAN-RECOVERY.md#2026-11-12零帧启动的受限恢复)。

设备脚本在 OCR 返回后等待输入焦点稳定；其测试数据库在原生进程退出后检查，
不复制在线数据库，也不依赖 S6 缺少的 sqlite3 命令。三台 RK 验证没有保存会话
或发送消息。详细的各轮数据、对照错误及恢复结果保存在忽略目录，设备安装
与纸面质量验收状态分别报告。

本轮平台重构开发包（S6零帧恢复修复前）已安装到四台在线授权设备，原生程序及 scanner 的设备
哈希与包内文件一致，安装后均进入 ready。三台 RK 的进程映射确认实际加载
统一私有库，设备库 SHA256 与锁定值一致。安装未重启系统宿主；S6 本次启动
成功不撤销上述重开验收失败。最终包 SHA256：

```text
rk3562   5baf377b8ecd0344ddcfa5c655d72ab5b38d8d0e3b92165a1e3c452f2b9a5efd
cv1826-s f70a22ae54d8400cfa51d0eb9f55525677079ad7f262bf5fed3df0799c46d870
```
