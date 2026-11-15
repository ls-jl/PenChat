# PenChat 0.3.1 发布准备记录（未发布）

本文件记录本轮已完成验证与未决门槛，不代表已通过完整发布验收。候选版本为 `0.3.1`；
开发基线为 `c33f4a6eef94bb112775a8c5a692c05450380bfa`，所在分支为 `dev`。
审查时 `main` 为 `b6ec3696a56c67ceefde3ec01999e94f720e2186`；
该审查起点的`main..dev`共163个提交（包括合并提交）。最终发布应记录实际提交和安装包SHA-256。
本轮的执行状态以忽略目录 `.work/release-0.3.1/` 下的新日志、清单和截图为准，
旧专题文档中的“通过”只描述当时的提交及范围。

**当前发布门槛仍未闭合：S6已按后续指令完成安装，功能测试仍由用户明确暂缓；视觉疑点须由用户看图决定。**
下方“本次静态审查”描述最初审查者的范围；后续主机与设备执行结果记录在
“本轮候选增量与当前证据”及各自的本轮日志中。生产native、0.3.1 package元数据及主机测试已在本地dev提交`bdd0a17`；
官方IME text cancel/reopen的43/43 JS回归已提交`017e8ed`，单位及键盘文档已提交`ad675a1`。
设备harness已本地提交`4400599`（50 files），竖屏下拉夹具补验已提交`35c4596`；
本记录随本地验收提交保存在dev，未合并main、推送或发布。

## 本轮候选增量与当前证据（2026-11-15）

### S6后续安装（仅安装，测试待用户开始）

用户随后恢复S6安装授权，并明确“先安装，稍后测试”。已从`44d453d`增量构建完整
ARMv7开发包并安装到`JDA0700006552216`，未使用UI-only包或扫描诊断包。
包SHA-256为`f378884837dd67d591c67c25feecafd22fbaaf4e358233fab84b37cf208fbbf7`，
内置native为`93984d62cd14c6a79f86679250db9eace5f2322e42da3379d220324d5d8b23f6`。
包身份、ARM32架构隔离及Web组件资源检查通过；安装后核对了native、scanner、
sensor guard、Web程序、launcher及JS的文件哈希。

安装前PenChat原生进程未运行；已备份其原有私有数据和公共传输目录，安装前后文件
清单一致。MiniApp宿主PID570及boot ID保持不变。未启动PenChat、未运行功能测试、
截图或性能测试。构建、包清单、私有备份和安装回执保存在忽略目录
`.work/release-0.3.1/s6-install-only/`。以下三机验收记录属于此前的非S6阶段，
不代表S6已通过功能验收。

当前最新主机候选为 `penchat-lvgl-candidate-6-back`，SHA-256
`964f426eb61f22fe7134d8e7985c5a499a816263e86fa5f4a0e06305ee6f7fe4`。
对应RK完整候选包为忽略目录中的 `candidate-5-native-back-rk3562.amr`，SHA-256
`a38a94b8ce390e2e59ab2227c84b84c786e12370ecfd89647ffa07429478c028`，
内置native SHA-256为 `cd1ba5d7ec1c8ab2a1269d0d46607829bcfbe8ffebce489a28ad2b86ca75a4a5`。
版本仍为 `0.3.1`，这是本地验收候选。生产及主机验证变更已本地提交`bdd0a17`，
另有`017e8ed`官方IME JS回归和`ad675a1`单位/键盘文档提交；设备harness及竖屏下拉夹具
分别已本地提交`4400599`、`35c4596`。

完整148项主机基线在candidate-4上执行完毕，effective结果为145通过、3项保留：
第24项横屏composer策略、第81项推理展开/收起后的跟随问题、第95项旧Markdown golden。
保留原始结果，不把后续增量改写为同一二进制的148项全通过。
第81项已在candidate-5-focus修复，`disclosure-delta/effective-results.json`记录完整6套增量
6/6通过；第24项仍待确定宽横屏下拖显示与当前实际底部/快捷操作策略的契约冲突。
用户随后明确“聊天页面保留黑底”：第95项已更新为0.3.1合成场景基线，并用精确candidate-6-back
重新渲染验证通过。新图与此前展示的黑底候选图逐字节一致；字体哈希、夹具、尺寸、帧数和
精确PNG比较未变。旧图与定向验证日志保留在`black-approved/`，不回写原148项运行结果。

candidate-6-back新增共享BackGeometry，起点边缘至少使用44逻辑像素触摸目标；
slop、travel、时间及失焦取消规则不变。7套真实LVGL检查（back_hit_area、back_navigation、
input_focus、keyboard_rollover、cursor_edges、keyboard、text_selection）与修正后的数学单元均通过。
936宽度的旧43px期望更新为44，960宽度仍为44；不是用旧build/host的单元结果代替。
最新Back定向ASAN检查及ARMv7本地增量编译均exit 0，分别见
`asan-back-final-tests.log`、`build-s6-back-final-local-only.log`。
新增Back测试已加入后续npm队列，后续队列149项不应回写为本次148项完整基线。

- 用户已批准浅色白色实体键帽。修复长草稿重开时再次乘当前高度导致494px的问题，
  并避免原位编辑布局写入0偏移打断键盘滑入。
- 修复空发送按钮失去命中后穿透到输入框，以及菜单退场接触被覆盖层取消后吞掉恢复首击。
- 字体大小现覆盖全应用文字，沿用原`reading_font_percent`键；`sp()`将密度、界面倍率和
  字体倍率组合后一次取整，`dp()`只负责几何。保存采用DB worker确认与前台安全重建。
  设置行、字段、Provider、历史、预设摘要和其他文字容器按实际字体行高容纳内容。
- 拼音组合条按实际键盘视图放置：Wide/CompactExpanded在候选下方，Portrait在候选上方，
  专用Compact保持行内。预留浮条空间，键帽和命中网格保持不动。
- 显示改为保留完整CPU帧缓冲、仅重绘LVGL无效区域，每次刷新只在最后一个区域提交完整帧。
  无缩放0/180度画面使用精确像素复制；完整覆盖DRM行时省去随后会被覆盖的清空。

新字体、预设、键盘字号、浮条、历史字号、输入取消和空发送专项已通过各自的主机矩阵。
预设默认100%三布局的菜单与列表共六张截图，与修复前逐字节一致；
DIRECT与FULL渲染的三布局、深浅两主题、75/100/125/150%共24组比较逐像素一致，
包含318组截图、阅读几何和文字选择结果。审计使用RGBA `tobytes()`严格字节比较，
已纠正仅用ImageChops.getbbox可能遗漏alpha差异的问题；24组均无实际像素或geometry差异。
完整基线及增量分别保留，不能用较早的143项诊断队列代替。

Y07在完整隔离的同一夹具中测得如下native成本（旧包candidate-1 → 优化包candidate-2）：

| 场景 | 平均render成本 | 平均present成本（已包含在render内） |
| --- | --- | --- |
| 16次交替阅读拖动 | 36.635 → 21.149 ms | 17.735 → 5.409 ms |
| 8组键盘打开/收起 | 33.395 → 13.600 ms | 16.745 → 4.937 ms |
| 8组历史抽屉打开/关闭 | 46.071 → 18.315 ms | 16.391 → 4.859 ms |

计数窗口中没有截图、诊断信号或数据库快照；键盘与抽屉各确认8次退出快照。
这些数字不是硬件帧率或稳定60fps保证，present不能再次与render相加。
各轮完整private/public恢复、宿主PID和boot ID均按对应日志核对；三机功能验收继续由设备执行者记录。
旧device helper曾通过固定第三方App ID离开应用，已移除此做法，改用本应用原生返回退出，
每次注入前确认前台。失焦时停止触摸并记录降级退出；不以未知应用作为测试背景。
用户最新再次确认：授权测试中的未发送字符、草稿和preedit无需保留；
已保存DB、设置、资产及private/public数据仍须完整隔离恢复，不能发送待输入内容或重启宿主。

RK候选包身份、架构、来源锁、资源、剥离和完整Web组件检查已通过。
此前非S6验收阶段仅完成最新Back源码的本地ARMv7/C++14编译，未访问S6；后续安装见上节。
完整ASAN/UBSAN脚本在candidate-4上已通过：15项scanner CTest、50项native CTest及后续进程/UI回归完成；
其中FeedbackKeys按设计跳过，原因是主机私有uinput节点不可读取，没有更改设备权限。
最新Back仅完成上述定向sanitizer，不能称为在最新二进制上重跑完整sanitizer。
三机设备操作现已结束、tests冻结；最终汇总各记录36个有passing assertions的独立scoped flows，
具体范围及旧失败关联复验见下节。菜单打开、设置持久化或截图不等于全部门槛通过。
状态清单为`devices/coverage-progress.json`，这是持续更新的快照；本文件不把正在运行或needs-review条目升级为通过。
三机settings-controls已完成12种实改/非默认重启验证，两横屏pinyin-core专项已过；
三机smoke的26行仅证明可达。官方全黑SDK截图仍属capture harness未决，不能据此认定生产IME缺陷。
真实纸面光学、物理圆角、人工动画评估、宽横屏composer契约及S6验收仍pending；聊天黑底已确认。
必需门槛未闭合前继续保留在dev，不合并、推送或发布。

Y07最新候选已在(43,70)起点完成Settings回聊天、根页正常finish、短划/回撤取消、
弹层操作优先及键盘/阅读长按优先专项。SDK最外侧11/25px仍会抢走`$am`焦点，
`panel_visible=false`，而x=34对照不抢焦点；故不能宣称外侧系统返回预览已修复。
A2 hole.stop与B2透明触摸层实验均验证到真实marker，但均无效，未合入主启动器。
实验包须在fake数据下恢复普通a38包并确认小focus序号后，才恢复用户数据。
Y07文档化`$am.moveToBack()`实验验证同PID保留草稿/编辑/脱底阅读，另验证隐藏期间
SSE持续接收、恢复后同PID同请求完成；实验shell已按上述顺序清除。
后台准备使用宽横屏现有双击shortcut仅证明该入口及后台保留，不证明AGENTS下拖显示契约，
第24项仍为`waiting_user_contract`。Y01/Y02的Home会结束native；本轮另用本应用后台实验
完成两横屏同PID后台SSE生成：Y01 PID3761、Y02 PID2805，1894字节接收后后台继续至3788字节，
恢复后保持同PID、单请求并完成。两机均先换回普通a38包并确认小序号5，再恢复用户
private/public数据，宿主和数据恢复核对均为true。
Y07官方IME通过新版UUID与完整PNG校验取得初始图，并确认删1字后length26→25；
删除后当前SDK图的键盘区域为黑色，未点击不可见hide。该scope已完整恢复，
取消后恢复原值仍未完成，不能以删字成功替代取消验收。其余矩阵仍以设备执行者最新记录为准。
最后一次只读C++差异审阅未发现可复现阻断项，这不替代剩余验收。详见[SYSTEM-OVERLAYS](SYSTEM-OVERLAYS.md)。

## 最新设备收敛与中断恢复记录

三台已分别完成真实后台编辑/阅读与后台生成专项，证据在
`devices/<serial>/candidate-5-real-background-validated`及
`candidate-5-background-generation-validated`；实验marker清除、普通a38包、小序号及
原private/public数据和宿主恢复均核对成功。三台原生Back定向通过；这些专项不替代全矩阵。

Y02最新native-touch的host执行exit143中断，真实触发原因尚未证实，该scope不计通过。
`devices/MEA2300009007113/manual-recovery-result.json`为`phase=complete`：保存的原
private/public逐文件hash核对恢复成功，host/boot未变，普通a38包与native cd1…确认，
实验shell marker清除，原应用已恢复。当前fake与nested树rename为`release-aborted-UUID`
留档，没有删除。该恢复记录不应描述为产品数据丢失或native-touch通过。

审阅发现设备测试脚本P1风险：live child超时后恢复可能与子进程重叠，以及重复signal
可能再次中断恢复。reviewer已修补acceptance/matrix，
`tests/device/test_release_supervision.py`五项纯主机检查全部通过；随后设备队列正常执行并恢复，
未在真机主动重复终止信号试验，不能把正常收尾等同于所有信号分支的实机验证。
这些是测试监督/恢复脚本问题，不是已确认的产品缺陷。

Y01最新native-touch-core结果通过并恢复；Y07此前DPI第209行等待`editor.active`超时的scope恢复为true，
后续完整DPI复验已通过。旧超时仍保留为历史证据，不代表当前DPI未过。
三台官方IME取消仍pending：Y07删字后fresh SDK图无可见hide，横屏SDK图显示旧`al`
而非当前nonce，执行者停止并安全恢复。`017e8ed`的43/43 JS回归不能替代这些设备结果。
Y07同一编辑session的长度26→25证明删字已回到原生编辑状态；完整PNG校验只能排除文件损坏，
不能单凭黑色截图判断实体屏幕是否黑屏或所有显示层是否已被捕获。横屏的长度/session检查
也没有直接捕获本轮SDK实际收到的完整文本，故尚不能把旧`al`归因于某个确定的软件层。
最新收敛结果来自`candidate-5-auxiliary-fixed`、`candidate-5-y07-completion`及
`candidate-5-final-hardware`等各机manifest：三机title/compression、feedback观察30s无上传、
update只读检查通过；两横屏scanner核心20循环、OCR和crash重arm通过。Y07拼音、
完整DPI 75/100/125/150及主题重启、HD四姿态、scanner20循环、OCR、后台/crash专项通过。
三机额外真实onHide扫描模型保留delta均已通过，Y07亦已完成；该专项普通a38已还原。
这些是功能断言与恢复通过，不自动批准纸面光学或人工视觉。

60s/4M完整private probe在`.work/scanner-background-probe`，SHA-256
`90cecca30fae1df1d87517a3f602d73f6fb095ccc08a25e218295941a2103576`；
175项资源与普通a38相同，仅JS/manifest变化。实验结束仍须在fake数据下先恢复普通a38和
小marker，再恢复原用户数据。

owned update-self-install首次Y02范围未达到安装动作：未出现fs mounted标记不能证明产品
安装失败，该scope不算install通过。已用SDK Back正常离页，精准卸载本次新建appid
`8004567190154157`，清理自身目录、UUID临时文件及forward35641；host/boot、原private/public
hash、普通a38/native cd1均恢复。生成PageVM的五项Node检查验证独立timer修复，
marker reject/hang不阻止finish；最后仅做诊断截图scope，安装更新桥接自测证据仍未闭合。

self-install父目录兼容性仅再复验一次：预建两个版本父目录，六个event叶子起初缺失，
SDK early仍报告mkdir=false；20s独立watchdog正常返回launcher，仍未到安装动作。
本次owned appid为`8008436238934038`，operation为`96e438fa-7984-4fc2-9b83-b4687303976c`；
owner已确认本次完整恢复：原private/public逐文件hash、host/boot、普通a38/native SHA均核对成功，
自建appid及临时文件已移除，blocker附证据归档后清除。所有设备scope均已结束，不再启动新用例。
最终安装/恢复汇总为`devices/final-install-restore-summary.json`，各机另有
`final-install-restore.json`：三机native cd1及普通a38 JS hash匹配，无probe marker，
原private/public完整manifest恢复，host/boot未变；自建update AppID均不存在，
fake quarantine与临时helpers已退役。Y02最后观察visible/page_visible=false，来源未判定；
只报告已安装/恢复及成功resume检查，不宣称三机当前均在前台。
按既定范围不再继续路径/API或第二台实验；自安装检查保持未完成，不记为产品安装失败。

最终主机收尾检查：launcher及update-template Node共48/48通过（`launcher-final.log`），
supervision 5/5通过（`supervision-final.log`），46份改动设备Python文件py_compile通过，diff check干净。
这些是harness/模板检查，不将未达安装动作的设备scope升级为install通过。

正式产品源码均已本地提交且产品diff为空；普通候选仍为a38/native cd1，纯tests/docs不重复构建。
尚未闭合：宽横屏composer契约、官方IME设备取消、真实纸面/物理圆角/主观动画、
暂缓的S6功能验收和既有再分发许可。此前三机设备操作已经结束；功能覆盖与对应证据见
`devices/coverage-progress.json`，安装和原数据恢复另见上述最终汇总。
用户随后明确“更新api先不管”：更新API及安装桥接自测暂缓，不再作为当前推进项；保留未验证
状态与既有失败证据，不改写为通过，也不继续设备实验。

最终覆盖补审发现的Y07两项缺口已闭合，证据为
`matrix/candidate-5-y07-two-gap-delta/7G12000010905426/manifest.json`：onboarding七步、
21帧、pause/replay、配置返回、完成重启共144s通过；portrait pull共94s通过，冻结reader968、
extent282、baseline452，覆盖取消、保存-10/重启/复位、差1px不创建、满452只创建一次及空会话guard。
仅调整`tests/device/pull_sensitivity_flow.py`夹具，本地提交`35c4596`，py_compile/diff check通过，
生产无变。最终coverage记录三机各36个独立scoped-flow passing assertions，旧失败/中断均保留
并关联后续复验；smoke26行仍仅为可达，不能解释为所有功能、视觉或发布门槛通过。

两项补验后已刷新Y07最终恢复记录：native PID2784、host PID2305，原boot不变，普通a38
native/JS匹配，原private/public全树恢复，无probe/saved/nested残留。Y01/Y02复用此前最终
只读核对，Y02最后背景观察原样保留。所有设备操作结束、tests冻结，不再起新scope。

## 既有累计变化

- 原生界面统一逻辑尺寸、语义字体和深浅色调色板，支持持久化界面缩放与安全重建。
  设置、Provider、菜单、图文排版、输入法及三类屏幕布局使用共享单位和安全边距。
- 优化Markdown正文、表格、代码、公式和中英文换行；将重复布局、公式像素和字形度量复用，
  避免无变化的文本框与发送按钮样式写入。
- 增加30/60Hz应用刷新调度选择；稳定输入区底部保留空间与回到底部控件，
  通过距离滞回和持续时间确认减少临界位置反复显隐。
- 键盘从底部、历史抽屉从左侧滑入滑出；退出使用独立快照并及时释放原编辑对象。
  字段切换保留键盘，过渡期间阻止移动命中区域的输入。推理强度选择居中显示标题与滑块胶囊，
  API默认档使用独立灰色表盘位置；支持等级和请求映射仍由模型能力定义。
- 完善竖屏、宽横屏和紧凑横屏键盘、拼音组合、候选展开、选区手柄、官方输入法/OCR交接，
  优化Provider与模型独立管理、账户余额信息和可操作的空状态。
- 上下文快照按100条物理消息批量读取图片部件，减少查询；单模型读取复用无凭证目录缓存。
  回复备选在下一次成功提交消息前保留，继续发送、压缩、分支和导出使用有效消息快照。
- 共用扫描会话下隔离RK/CVI采集与推理后端，保留各板型独立身份、尺寸、灯和校准配置。
  Y01、Y02、Y07统一锁定私有RKNN 1.6.0；MELON仍待实机验证，使用既定系统runtime策略。
  S6新增共享内存Falcon合成后端和独立ARMv7载荷；相机预览、照片方向及扫描结果查看交接得到调整。
- 增加独立联网搜索进程、私有网络传输及锁定Mozilla证书资源；完善启动、覆盖层、
  数据恢复和导出清理的生命周期边界。首次安装仍不携带Provider、消息或用户数据。

上述内容来自累计提交，不等于所有功能已经在0.3.1候选包上重新验收。

## 本次静态审查

重点阅读了最近主题/缩放、刷新调度、底部控件、键盘及抽屉滑动、推理强度、
DB查询优化和生命周期代码。最初静态审查后，用户追加了浅色键帽、Y07动画流畅度及
输入框异常放大的修复要求；以下问题纳入本轮发布，不能沿用修复前的测试结果。

- **P2，已有草稿再次打开可突破输入框3倍高度上限。**
  `app_keyboard.cpp`的`edit()`用当前`chatControls_`实际高度乘1.75作为开场最小高度，
  当前对象可能已经因草稿多行展开；`app_input_target.cpp`的`chatComposerHeight()`又用
  `max(minimum,base*3)`作为上界。因此100%缩放下282px的长草稿再次打开会得到494px的
  最小高度。复现场景为竖屏编辑到多行上限、隐藏键盘保留草稿、再次点开。
  应用单行语义基础高度计算开场最小值，并独立约束最终3倍上限；修复后补充重复重开回归。
- **待运行验证的动画竞争。** `slideSurfaceIn()`逐帧写键盘根的`translate_y`，
  `editorViewport()`也会直接写该属性为当前下推偏移（通常为0）。首次文本区几何变动、
  同步文本或候选结果可能触发后者，存在入场中途归零再被下一动画帧拉回的路径。
  应统一入场与下推的位移所有权，检查真实帧序列，不能只看最终截图。
- **用户要求调整的视觉问题。** 浅色普通键帽使用`#eef1f5`和153/255背景透明度，
  叠在`#e6e9ef`底板上，两者分离很弱。应提供更明亮且状态可辨的浅色键帽，
  用户已于2026-11-15批准第二版白色实体键帽；深色外观保持既有设计。

输入框问题已用复制保存的修复前host二进制动态复现：
`tests/test_portrait_input.py --composer-reopen-only`在第一轮得到494px高度、25个字符，
测试按预期失败，原图和指标位于`.work/release-0.3.1/review/before/`。
源码修复已将开场高度改为单行语义基础、最终高度独立限制到3倍；浅色普通/功能键帽改为
白色不透明，保留深色外观；编辑布局对入场位移的覆盖也加入防护。
修复后使用独立`build/release-host`二进制运行定向回归：深浅主题各连续重开3次，
均保持282px高度、25字符及未发送草稿，没有新增消息；`penchat-keyboard-design`
实际LVGL样式测试通过。两张568×1210深色静态键盘图完整逐像素一致，浅色键盘上方
像素也一致；浅色键帽测试点由RGBA`[234,237,242,255]`变为`[255,255,255,255]`。
对照原图、源码/二进制来源和指标保存在`.work/release-0.3.1/review/`的
`index.html`、`review-manifest.json`及`after/results.json`。
这些只证明本节定向范围。用户已批准第二版白色实体键帽；Y07真实动画流畅度与其他视觉疑点仍待验收。
其余审查范围未确认新增P1/P2，不是全仓库无缺陷声明。

在本次工作树中直接计算源码SHA-256：`native/scanner/dependencies.lock.json`
列出的56份本地源码、`native/compositor/source-lock.json`列出的11份adapter均匹配，
没有缺失项。该检查未覆盖生成二进制、模型、运行库和AMR，不能替代打包校验。

## 发布门槛与证据

| 门槛 | 必须记录的证据 | 本草案状态 |
| --- | --- | --- |
| 版本与身份 | package与lock根版本一致；两类载荷身份、AMR名、About与实际版本一致 | package与lock根均0.3.1；最新RK候选身份已核对；未发布 |
| 完整回归 | 本轮`npm test`、sanitizer及新增专项测试的命令、退出状态、日志 | 148基线145通过+3保留；81已由host5增量6/6修复；95按用户黑底决定更新后定向通过，24仍待确定；完整sanitizer在candidate-4通过，最新Back定向通过 |
| 原生构建与生产包 | host、RK原生、生产AMR；适用的S6 ARMv7构建/包校验；`check`结果如实记录可选cnpm提示 | host、AArch64和完整RK候选构建/包检查通过；ARMv7/C++14及S6完整开发包检查通过；check仅提示可选cnpm，S6功能验收待进行 |
| 来源与资源 | 锁文件、字体/词典/公式/模型/运行库/证书和许可材料与实际包一致 | 上述67份本地源码匹配，RK完整包锁定资源检查通过；再分发许可事项仍未解决 |
| 程序ABI与隔离 | AArch64和ARMv7 hard-float隔离；launcher导出；scanner的`system`禁执行和`ioctl`导出；stripping后仍可绑定 | RK及S6完整候选包身份/架构/资源检查通过；包检查不代替S6功能验收 |
| 更新与恢复 | 受限更新URL、版本/SHA/ZIP验证、安装实际版本核对、未知结果不重投；恢复草稿、图片和阅读位置 | 三机只读更新流程、持久化和数据恢复检查通过；隔离安装自测尚未达到安装动作；用户已暂缓更新API与桥接自测，保持未验证 |
| 用户数据恢复 | 在线快照用SQLite backup；设备测试隔离并恢复DB、资产、备份和公共传输目录；宿主PID不变 | 三机原private/public完整文件manifest恢复核对一致，host/boot不变；详见final-install-restore-summary.json |
| Y01/Y02/Y07 | 明确serial、候选包SHA、测试范围及安装/恢复结果；不把取帧或回放等同纸面光学验收 | 三机普通a38包、native cd1与启动器JS均已核对；功能范围见最终coverage，视觉与纸面光学仍单列 |
| S6 | 按用户后续指令先安装，功能测试稍后进行，保留扫描重开历史风险和恢复证据 | **完整0.3.1开发包已安装，原数据保留；未启动应用或运行功能测试** |
| 视觉变化 | 新截图来源可追溯；疑点有原图、布局、主题、缩放、复现步骤和用户决定 | **待用户看图决定** |
| 再分发许可 | 明确扫描资源、RKNN、Falcon/JQ材料及对应源码义务的处理 | 存在下述已记录未解决事项 |
| 合并与发布 | 必要门槛完成后才将dev合入main并推送；更新目录原子发布且不覆盖旧版 | **未执行** |

`tests/check_amr.py`覆盖RK生产包身份、资源哈希、锁文件、运行时命令策略和ELF调试段；
`tests/check_cv1826_s_amr.py`覆盖S6开发包的身份、ARM hard-float、模型、宿主库排除与后端隔离。
两者的范围不同，不能用其中一个通过代替另一个，也不能用开发包校验代替生产包校验。

[第三方声明](../THIRD_PARTY_NOTICES.md)明确记录RKNN SDK/运行库、扫描模型与校准资源
尚未取得使用/再分发授权，部分Falcon/JQ SDK许可仍未确认，板端libstdc++的精确来源和
对应源码也有未解决项。该声明要求取得授权或替换/移除，且说明含这些材料的AMR
不应作为已获授权的公开安装包发布。哈希、附带许可文本和技术构建检查不会补授权利。
公开二进制发布前须按既有[开发说明](DEVELOPMENT.md)和第三方声明处理这些事项，
不得在发布说明中将其改写为已经解决。

S6曾记录ISP反复重开导致内核异常，参见[S6扫描恢复](S6-SCAN-RECOVERY.md)。
S6此次仅获安装授权，不得为补验收证据自动运行设备测试。Y07中性平场与其他板型的纸面文字、
手持运动质量仍须按各自原有限制描述，不扩大宣称的支持范围。

## 视觉证据清单与判定规则

本轮独立清单模板位于`.work/release-0.3.1/review/review-manifest.template.json`。
每张新截图应记录生成命令、UTC时间、源码提交、工作树差异指纹、实际二进制SHA-256、
逻辑屏幕配置、主题、界面缩放、字体设置、夹具和图片SHA-256。
设备截图还要记录明确serial与安装包SHA-256；不得使用真实会话或凭证作为夹具。
截图生成与人工视觉决定必须分开记录，新增项目一律为`pending_user_review`。

最少应覆盖三类布局的深浅主题、75/100/150%缩放，重点检查：聊天空白/正文/长草稿、
底部控件显隐、键盘入场中途与退场中途、字段切换、历史抽屉与长按菜单、
API默认/none/多等级强度胶囊、设置及Provider表单、Markdown表格/公式/代码、
候选展开、覆盖层/后台恢复与缩放后重建。真机证据只来自本轮允许访问的设备。
不适用组合应给出原因；不能简单以截图张数代表覆盖率。

仓库中的`tests/golden/README.md`记录其960×266 Markdown图由0.1.56旧基线迁移到用户确认的
0.3.1黑底候选；`tests/golden/manifest.json`锁定字体/图片哈希和帧数。它是窄范围像素回归，
不是0.3.1完整主题、缩放、输入法或设备视觉验收。

`scripts/ui-gallery.py`从通用`.work/chatgpt-style`、`reasoning-ui`、`scanner-ui`、
`device-acceptance`及已有`ui-audit/after/extra`等路径复制图片；标题使用当前
`package.json`版本。脚本不会验证来源提交、二进制哈希或截图时间，且部分图沿用已存在文件。
因此仅运行该脚本不能产生可信的“0.3.1全部已审查”结论，其打印的`reviewed UI states`
也不表示用户已审查。旧目录图片可用于历史对照，必须标为历史基线并单独记录来源。

视觉疑点必须保留原图路径和触发步骤，请用户判断；本审查不会擅自批准新的视觉变化。

## Y07 官方键盘定向修复（2026-11-15）

继续 dev，版本保持 0.3.1，不发布。新增默认官方键盘的独立布尔设置；竖屏透明
代理输入框固定初始 value，后续仅镜像到 LVGL，稳定控件及样式身份，拒绝旧会话
事件。系统确认仍通过一次性的 UUID 结果注册，配置不回写代理文本。详细契约见
[Y07 官方输入法](Y07-OFFICIAL-IME.md)。Provider 保留单个保存、DRM flip-buffer
所有权修复已分别提交，均包含在本轮完整开发包。

修正早先证据解释：完整原始图与键盘区域图有键盘像素，长图的部分预览缺失不能作为
“删字后 surface 全黑”的结论。过程候选 6 确实暴露了仅依赖 input confirm 不足的问题，
候选 7 恢复 vendor UUID 确认通道；并非通过把隐藏一律当作确认来绕过。

候选 7 的 Y07 隔离流程已通过：连续三次退格 length26→23、取消恢复26、重开后
删字确认保留25且结束编辑、Provider 单行字段18→17并确认回原表单、丢弃未保存
Provider 草稿、默认官方入口开关两向保存与重启恢复。没有发送消息或写入真实
Provider。主机 launcher 48项、隔离恢复 supervision 8项、三个布局的默认设置契约
以及两个竖屏的 retained-page 检查通过。没有执行全量回归或 sanitizer。

AArch64 完整包 SHA256：
`db40b222c5c57e3e33bb9f4cf8bb0c76303c359cbb11c60548bb7004b54ec428`。
S6 ARMv7 完整包 SHA256：
`a782959f6c6889c60f22112faabd5670710ef9b1e71722e5e6528e716172bb7c`。
复用已经针对相同 native 源码构建并核对哈希的 C++14 payload，重新编译 Launcher JS
并生成完整包；架构、包身份、原生组件与资源检查通过。

Y07、Y02 均安装候选7，核对恢复后的可执行文件；Y02 本轮没有功能测试。
S6 仅安装与校验完整 payload、私有/公共文件哈希，没有启动 PenChat 或运行功能测试。
三机宿主与 boot ID 未改变，已保存用户数据及设置保留。S6 首次安装回执后紧接读取
manifest 遇到暂未落盘的空文件，随后只读复核完整包与数据通过，未重复安装或启动。
证据、原始截图及前述失败尝试留在忽略目录 `.work/y07-display-fix/candidate-7/`。
功能断言通过不等于用户已批准实体 LCD 观感；更新 API、S6 功能验收与其他既有发布
缺口仍维持原状态。本批不推送、不发布。
