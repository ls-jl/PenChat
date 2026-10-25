# Anthropic Messages · PenChat 0.2.4

原生网络层支持三个独立协议：Responses、Chat Completions、Anthropic Messages。
聊天、重试、命名和上下文压缩共用传输与取消机制，不跨协议重发或自动降档。

## 配置入口

设置 → 模型管理 → 添加/编辑 Provider → 接口 → Anthropic Messages。

- 官方地址默认 `https://api.anthropic.com/v1`，也接受完整 `/messages` 地址。
  官方裸域名会规范为 `/v1`；自定义中转的根路径按填写值使用，不猜测 `/v1`。
- “Anthropic 连接设置”选择 x-api-key（默认）或 Bearer，以及
  `2023-06-01`（默认）或 `2026-10-21`。两种鉴权不同时发送。
- 自定义请求头为 JSON 对象，可填写 Workspace 等参数；请求头、密钥不会进入诊断或导出。
  自定义版本头必须与界面选择一致。返回后仍需显式保存 Provider。
- 自动获取模型支持分页，最多100页/10000项；失败可手动填写，不隐式重试。
- 兼容模式可显式选择非流式、HTTP/1.1、思考请求、续传和超时；不显示 OpenAI 的
  stream_options 开关。TLS 校验始终开启，不跟随携带凭据的重定向。

## 模型与思考

模型设置保存最大输出 token（默认16384；已知模型按官方上限约束），包含思考与正文。
思考模式分为不声明、自适应强度、固定预算。已知模型有模板，未知服务名不猜测能力；
手动配置不会被模板覆盖。更换思考模式会初始化档位映射，模型行和历史 ID 保持不变。

固定预算默认仅启用中、高；可选默认映射如下，可在“参数映射”中修改：

| 档位 | 思考 token 预算 |
|---|---:|
| 低 | 1024 |
| 中 | 4096 |
| 高 | 8192 |
| 极高 | 12288 |
| 最高 | 14336 |

预算必须至少1024且小于最大输出；保存时检查所有启用档位，不自动降低数值。
自适应使用 thinking/adaptive 与 output_config.effort；固定预算使用 enabled/budget_tokens。
“即时”只在明确支持关闭思考时启用，发送 disabled，不发送 OpenAI 的 none 值。
“API 默认”省略强度/预算，也不擅自开启思考；会话、进程记忆、Provider 的优先级保持不变。
请求可读思考时只在配置支持 display 的模型上发送 summarized；否则不发送这个扩展。

可读文本进入已有折叠思考区；只有签名或脱敏块时仅显示耗时。
SSE 以明确内容块阶段计时，完整 JSON 使用请求至首个正文的估算，并标记为估算。
签名及脱敏数据保持原样，只在完整成功回复后存入私有消息元数据。
续传要求 Provider、规范地址、模型、协议、API版本和凭据代次匹配；更换密钥或请求头使旧代次失效。
压缩覆盖、编辑截断、重试和导入不会重新携带失效/未完成的签名；导出仅包含公开内容。

## 响应、用量与边界

Messages 独立构建 system、messages、图片和 max_tokens，不附加 OpenAI 参数。
命名和压缩有独立提示词且使用 API 默认强度；压缩素材以含原角色/状态标记的用户参考数据发送，
避免把末尾助手历史当作 Claude 的预填输出，也不会将历史提升为系统指令。

流式以索引累积内容块和签名；用量增量是累计值，覆盖更新而非反复相加。
标准流要求 message_stop；兼容模式仅在内容块全部关闭、有有效结束原因、连接正常结束时，
接受缺少 message_stop。SSE/JSON 格式识别发生在同一次请求中，不发第二次请求。
截断、上下文超限、拒答、工具调用、不明结束和断流分别处理，保留已收到的文字与思考。
本轮不执行工具、不发送 beta 扩展、不使用服务端压缩或自动缓存写入。

输入总计按可用的未缓存输入、缓存读取、缓存写入计算；部分缓存数据缺失时不伪造总计。
会话信息保留未缓存输入和缓存读写，计算 token 命中占比；未知指标显示未提供。
SSE 单事件4MiB、响应32MiB、JSON深度64及节点20万上限；现有上下文/图片本地上限继续生效。
UI 最多100ms更新，持久化500ms合并，结束状态最终提交。

## 验证入口

- CTest 的 penchat-anthropic：请求配置、预算、协议隔离、签名、计时、用量及凭据代次。
- tests/test_anthropic_protocol.py：真实 curl 对本地服务的47类协议/兼容测试（模型分页除外，每次操作只发一次请求）。
- tests/test_anthropic_ui.py：两种逻辑尺寸的连接/模型设置与完整聊天、图片、统计、重试、分支和压缩。
- tests/test_anthropic_transfer.py：聊天与预设往返、准确模型匹配、公开内容保留和私密字段排除。
- tests/device/display_config.py --serial MEA2300009007113 --anthropic：读取 Falcon 配置，隔离用户目录，真实触摸对本地模拟服务验收。

本轮没有使用真实官方账号或商业中转凭据；协议兼容结论以模拟验证为限。
ORANGE 真机验收按用户要求暂缓，不能把主机的960×266截图记为 ORANGE 真机验证。

## 官方依据（2026-10-25查阅）

- [Messages](https://platform.claude.com/docs/en/api/messages/create)
- [认证](https://platform.claude.com/docs/en/manage-claude/authentication)
- [版本迁移](https://platform.claude.com/docs/en/api/migration-2026-10-21)
- [流式事件](https://platform.claude.com/docs/en/build-with-claude/streaming)
- [思考与能力表](https://platform.claude.com/docs/en/build-with-claude/thinking)
- [旧式固定预算](https://platform.claude.com/docs/en/build-with-claude/extended-thinking)
- [强度](https://platform.claude.com/docs/en/build-with-claude/effort)
- [图片](https://platform.claude.com/docs/en/build-with-claude/vision)
- [模型列表](https://platform.claude.com/docs/en/api/models/list)

## 本轮验收结果

- 完整 npm test 主机回归通过（11项 CTest、三协议、全部 UI/业务与扫描测试）。
- 最终凭据竞态保护增量后，重新执行11项主机 CTest、47项 Messages 协议、两尺寸
  Messages UI、慢流/模型冻结/草稿/取消/隐藏思考、模型分页、自动命名、聊天/预设往返，
  以及原两接口的上下文压缩、回复重试与分支回归，全部通过。
- ASAN/UBSAN：11项 CTest、47项原 OpenAI 协议及 Messages/界面/导出检查通过；
  最终增量后再次执行11项 CTest、47项 Messages 协议、完整 Messages UI与往返检查，均通过。
- AArch64、生产 AMR内容/身份、资源校验、41份许可材料完整性、10个 ELF strip 检查通过。
  aiot-cli check 返回0，仍提示可选 cnpm 推荐项；没有为此更改环境。
- 113（MEA2300009007113）最终包：Falcon配置936×280、270°，四角映射通过。
  本地模拟服务7次请求，覆盖图片/流式/签名续传/35%缓存命中统计、压缩归档、
  x-api-key与Bearer、两种API版本、HTTP/1.1、JSON兼容与401后的手动重试。
  未自动重发，用户私有和公开目录均恢复，系统宿主未重启。
- 安装包：dist/8001792332371649.0_2_4.amr，18,186,511字节；SHA256：
  3f84f9b6a7837a5afb2d1e1dd481a0d34463e8b9bb4540dcdc78b95b910b718c。
- 最终设备原生程序与本次构建一致，SHA256：
  b3d006357211da58a3b2288950b59758493ba07779e7b0f385618d86b5eb74ec。
- 已查看主机 .work/anthropic-ui/ 及113真机
  .work/device-display-MEA2300009007113/anthropic-*.png；截图和测试数据不进入Git。

真实官方/商业中转联调未执行；ORANGE真机验收仍暂缓。
