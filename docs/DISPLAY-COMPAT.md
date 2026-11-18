# 通用显示能力回退

这套兼容路径适用于所有使用 RK DRM 后端的 PenChat 设备，按实际显示能力选择，
没有 X6、Y01、Y02 型号白名单，也不以型号、横竖屏或分辨率决定是否允许回退。

## 选择规则

1. 优先使用 Y01／Y02 当前采用的底层显示：原生 ARGB 图层位于 Falcon ARGB primary
   下方，由 MiniApp 的 hole 露出原生画面。
2. 底层方式不适用时，在初始化或显式重建显示 surface 时尝试上层 RGB 兼容路径。
   触发情况包括 primary 非 ARGB、GETFB2 不可用、无法建立下层层级关系、底层提交
   能力不满足要求；这些判断对所有 RK 设备一致。ARGB primary 本身不保证支持底层模式。
3. 上层路径仍须找到空闲的 RGB plane，确认它高于 Falcon，并确认缓冲完成／退役能力。
   优先 atomic；legacy SetPlane 必须有相应 CRTC vblank 证明。
4. 暂时未准备好、framebuffer 正在变化或暂时失权时遵循原探测／恢复预算。
   无法证明存在可用路径时保留明确错误，进入现有启动失败／降级流程，不抢占其他图层。

正常提交期间的错误继续遵循当前模式的缓冲退役和所有权检查；不在失败提交后直接
更换模式。显示 surface 显式重建会重新探测，曾经支持底层的同一设备也能在能力变化后
选用兼容路径。

上层模式等待有效前台许可后显示。控制中心、官方输入和后台期间撤回显示客户端，
保留页面、CPU 帧和健康模型；官方输入使用全屏确认／取消。底层模式继续支持局部官方
编辑。屏幕尺寸、旋转与触摸映射始终来自 Falcon。

S6／CVI 保留独立的共享合成后端，不套用 RK 的 plane 选择，不引入旧 CVI VO 后端。
通用显示回退不代表为未知机型复用扫描传感器、校准或媒体库。

## 验证与历史记录

生产入口为 `native/camera/src/overlay.cpp` 的 `Overlay::open/pickResources`，只读取
DRM 资源、格式、属性与完成能力。原生 DRM probe 测试覆盖非 ARGB、查询失败、底层
提交不支持、ARGB 但只有上层可用、显式重建后能力变化、未知层级、占用 plane 及缺少
退役证明。几何 fixture 仅用于验证通用规则，不构成具体机型的实机验收。

本次核对确认合并后的生产代码已经实现上述通用选择，不需要增加机型分支。
补充测试使用独立的 host/native zpos 属性，覆盖五组逻辑 hole 几何，验证上层实际
呈现／撤回不修改 Falcon 层级；同一 surface 关闭后能力变化也能重新选择。
`penchat-drm-probe` 与 `penchat-underlay-order` 两个定向 CTest 目标均通过，记录在
忽略目录 `.work/display-fallback/`。本次未修改生产运行时代码，沿用已校验的合并开发包，
没有再次安装或运行实机测试。

原始分支和合并记录见 [X6 历史记录](X6-DISPLAY-COMPAT.md)。`fix/x6-display-compat`
已合并到 dev 并清理；原 worktree 的完整 `.work` 记录和两个 AMR 已逐项 SHA-256
校验后归档至主工作区 `.work/archive/fix-x6-display-compat-3d73611/`，映射及哈希在
同目录 `manifest.json`。合并时生成的验收记录及开发包继续保留在 `.work/x6-merge/`。
该工作区的构建对象和解包暂存属于可重建缓存，未作为交付物归档；共享 `.deps` 保留。
后续继续在 dev 开发，版本保持 0.3.3，不推送或发布。
