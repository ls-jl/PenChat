# X6 display compatibility development fix

Branch: `fix/x6-display-compat`, based on committed `dev` at `d865dfe`.
Application version stays **0.3.3**. This is a development package, not a release
or an assertion of X6 hardware acceptance. The primary worktree's uncommitted
power-detection changes are not included.

## Behavior

- Query the active primary/CRTC/framebuffer, actual GETFB2 result and errno,
  native RGB plane availability, zpos ranges and submission/completion facilities.
  Prefer the existing ARGB underlay. A failed query is not an assertion that
  the primary has an incompatible format.
- Recheck not-ready/changing resources every 50ms within a shared 1.5-second
  budget, including the legacy completion proof. A framebuffer need only remain
  valid across one probe; its ID need not stay constant throughout animations.
- If underlay cannot be established, select only an idle, verifiably higher RGB
  overlay. Do not change Falcon's zpos in this mode. Prefer atomic; a legacy
  SetPlane path requires a corresponding CRTC vblank acknowledgement. Never
  overwrite the queued or previous scanout buffer before retirement.
- TEST_ONLY rejection for an unsupported configuration is distinct from busy,
  temporary permission loss or an inactive CRTC. Inactive display state waits for
  wakeup; it must not make an otherwise supported device fail initialization.
- Mode is selected at construction or explicit surface reconstruction, not as
  an opportunistic fallback after a failed visible frame submission.
- Upper overlays wait for fresh, known foreground permission before their first
  upload. System panels, official input and background transitions withdraw the
  display client and native touch while retaining the CPU frame and business
  state. Official input uses the full-screen confirm/cancel path; native-under-
  Falcon retains partial official input. S6 keeps its separate shared compositor.
- Preserve the first startup failure and its structured display probe. An
  unknown wait status, including ECHILD, is -1 rather than successful exit 0.

See [system overlay rules](SYSTEM-OVERLAYS.md) and the kernel explanation of
[DRM flip completion](https://docs.kernel.org/gpu/drm-kms-helpers.html).

## Targeted checks

The following were run on the host; no full release regression or sanitizer run:

| Check | Result |
|---|---|
| DRM initialization/retirement, surface capability flags, order, focus, runtime ownership and recovery | 7 CTest targets passed |
| Relevant launcher startup, stale owner, timeout and official-input cases | 14 passed; 65 unrelated cases skipped |
| Actual non-headless app with injected DRM/evdev, 960×266 and 568×1210 | Passed: upper-mode cold bootstrap, panel loss/return and full-screen official confirmation/cancellation |
| Existing actual-process focus regression | Passed |
| Existing retained official-input regression, 568×1210 and 600×1280 | Passed |
| Existing keyboard handoff/normal dismissal/failure recovery and scrolled composer, three layouts | Passed |
| AArch64 and ARMv7 development builds | Passed |
| Both AMR identity/architecture, native payload, dependency/resource and web-component checks | Passed |

The initialization mock includes ARGB and non-ARGB primaries, GETFB2 failure,
delayed readiness, changing FB IDs, occupied planes, unknown ordering, atomic
rejection, inactive CRTC, temporary EBUSY/EAGAIN/EACCES/EPERM, legacy vblank
availability, delayed completion, safe cancellation and foreign ownership.

## Development installation, 2026-11-18

| Device | Selected backend | Passive installation check |
|---|---|---|
| Y02 / 113 | `underlay`, atomic page-flip events | Running payload hash and saved business rows verified; display asleep, visible first-frame/manual interaction pending |
| Y07 | `underlay`, atomic page-flip events | Running payload hash and saved business rows verified; display asleep, visible first-frame/manual interaction pending |
| S6 | `shared` compositor | Running payload hash, submitted first frame and saved business rows verified; manual interaction pending |

MiniApp host PID and device boot identity remained unchanged on all three.
No model requests, scanning, control-center or keyboard device acceptance flows
were injected. Private data snapshots and runtime diagnostics remain outside Git.

Package SHA-256:

```text
rk3562    491d1e6c9e1419aad07991778ac57d0e4752e07bcf83fc01aa41078297198788
cv1826-s  8ace378bf9c7b5907c1dbe34c9522f52a8aaa278162f9d161da611bbea3401c7
```

X6 has no authorized serial available: its real cold start, control center,
official keyboard and framebuffer behavior remain **pending user verification**.
Host injection is not X6 hardware evidence. No scanner algorithm, model, database
schema, app identity or network protocol was changed.

## 2026-11-18：合并到 dev

以上为该分支最初的独立记录。本次把 `3d73611` 合入 `dev` 的 `b9f255d`，同时保留
随后加入 dev 的熄屏门控、双 ABI 降级 bootstrap、执行前 ELF 复核和自动精简诊断。
仍保持 0.3.3，不推送或发布，原分支及其独立 worktree 保留。

冲突处理包括：上层模式的有效前台显示租约仍受熄屏许可约束；首个 fatal 文案、显示
探测和降级诊断分类一起保留，后续清理错误不覆盖它们；显式重试清除旧显示探测。
新增实际非 headless 上层模式的熄屏／恢复组合检查，确认编辑会话与拼音不变。

合并后的定向检查通过：134 项 JS、9 个原生 CTest 目标、两个布局的实际上层模式
启动／控制中心／熄屏／全屏官方输入，既有焦点、保留页面的官方输入、三布局键盘交接、
20 项降级 worker 和启动失败恢复。AArch64 与 S6 ARMv7/C++14 编译、完整开发包的
身份／资源／架构／依赖／无 shell 检查通过，降级 SO 的标准双 ABI 布局保持。
没有运行全量发布回归或全量 sanitizer。

Y01、Y02、Y07、S6 均完成被动安装：在线 SQLite backup 的逻辑摘要、资产、IME 和
公共文件一致，系统宿主 PID 与 boot ID 不变。未注入触摸、截图、唤醒或启动验收流程。
Y02 安装后保留旧 native；Y01、Y07、S6 安装后未检测到 native，没有自动拉起。
四台新运行时均待重新打开 PenChat 激活；Y02 先正常退出再打开，不需要重启系统宿主。
X6 仍没有可用的授权实机，真实硬件验收范围不因合并而扩大。

构建、定向检查、包 SHA 和安装记录保存在忽略目录 `.work/x6-merge/`。
