# API 核查与兼容模式（0.2.2）

核查基线为 0.2.1 / `98095ee`。本轮覆盖已有 Responses、Chat Completions，
不包含工具执行、实时语音及其他厂商原生协议。官方网页核查日期：2026-10-25。

## 使用

设置 → 模型管理 → 编辑/添加 Provider → API 兼容模式。开关及超时修改先留在
Provider 表单，返回后点击“保存”才写入数据库。不会修改其他 Provider。
旧记录缺少这些字段时使用标准模式。所有请求只执行一次，失败需要手动重试。

兼容模式默认保留流式、用量、公开摘要和加密续传，可分别关闭；可强制 HTTP/1.1。
公开摘要仍须模型本身支持。超时默认600秒，可设30～3600秒，连接超时15秒。
关闭续传仅让之后请求使用历史正文，不删除历史；关闭流式后等待完整回复。
API 默认推理强度始终省略参数，兼容开关不会偷偷改变用户选择的强度。

兼容模式识别同一次请求返回的 SSE 或完整 JSON，允许错误的 Content-Type，
Chat 在正常 EOF 前已有明确 finish_reason=stop 时可接受缺少 [DONE]。
只有正文、没有结束依据的断流仍是错误。标准流式模式不接受完整 JSON 冒充流。
Base URL 可填写 API 根目录或相符的 /responses、/chat/completions 后缀；不会补 /v1，
后缀与所选协议不符时提示修正。当前 Base URL 不支持查询参数或 URL 片段。
TLS 校验始终开启，不自动跟随重定向，不跨站转发认证信息。

兼容性按已知格式实现，无法保证所有非标准服务器可连接。没有真实问题服务器样本，
异常格式均以本地模拟服务验证；不是第三方服务认证或官方 API 在线付费测试。

## 官方要求与验证对应

| 官方依据 | 实现 | 验证入口 |
|---|---|---|
| [Chat 创建请求](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)：按角色构造消息，reasoning_effort | 纯文本与图文分开；支持模型 instruction_role，现代推理模型使用 developer；API 默认省略强度 | ApiTest、test_api_protocol、test_context |
| [Responses 创建请求](https://developers.openai.com/api/reference/resources/responses/methods/create)：instructions、store、reasoning | 聊天提示词独立注入；store:false；摘要与强度独立；include 显式请求加密内容，兼容旧实现 | ApiTest、test_api_protocol |
| [推理指南](https://developers.openai.com/api/docs/guides/reasoning)：摘要须显式请求，完成条目用于续传 | 公开文本和密文分开；保存有序完成输出，保留 assistant phase；摘要不控制是否续传密文 | ApiTest、CoreTest、test_reasoning_ui |
| 同一推理上下文，保留消息阶段和原顺序 | 续传同时核对 Provider ID、模型 ID/名称、原始端点和协议；不匹配退回历史原文 | ApiTest；contextInput |
| [Responses 流式事件](https://developers.openai.com/api/reference/resources/responses/streaming-events)：failed/incomplete/refusal | 显示未完成原因和拒答正文；增量及最终条目按索引覆盖，不重复追加 | test_api_protocol、ApiTest |
| [Chat 流式事件](https://developers.openai.com/api/reference/resources/chat/subresources/completions/streaming-events)：空 choices 用量块、finish_reason | 不在 finish_reason 后提前丢弃用量；stop 才是正常完成，截断/过滤/工具调用有明确提示 | test_api_protocol、test_session_info |
| [错误说明](https://developers.openai.com/api/docs/guides/error-codes)：HTTP、连接和超时错误 | 状态码先于业务解析；错误 UTF-8 修复和凭据遮盖；保存有限 HTTP 状态、耗时和 request ID | test_http_retry、test_api_protocol |
| [GPT-5.1](https://developers.openai.com/api/docs/models/gpt-5.1)、[GPT-5.4](https://developers.openai.com/api/docs/models/gpt-5.4) 能力 | 保留已有正确模板及用户自定义；补充 o3/o4-mini 的公开摘要模板，不为未知模型猜测最高档 | CoreTest、test_reasoning_ui |

模型列表与聊天、命名、压缩共用 apiRequest，使用相同请求头、连接策略和超时。
命名清除聊天 system_prompt，压缩使用自身指令，不混用聊天提示词。
不从正文标签猜测思考，不把加密内容显示或写进诊断、聊天导出。

## 存储与兼容

Provider 的 `metadata_json.compatibility` 保存 enabled、streaming、usage、summary、replay、
http1、timeout。请求开始冻结这些值；`request_json` 保存无凭据配置快照。
消息元数据增加 `replay_identity`、`response_output_items` 和有限的 `http` 诊断。
原有 reasoning 元数据仍可读取，不改变表结构。旧记录没有端点身份时不续传旧密文，
仍保留并发送历史正文；新请求会建立新的可核验状态。

SSE 行/事件上限4MiB、响应累计上限32MiB；超限明确失败，不截断成成功回复。
网络生产者最多每100ms合成一次完整状态，以50ms有界轮询刷新停顿前的尾部增量，结束/取消/错误立即提交最终快照；
LVGL 绘制与500ms数据库保存调度保持原规则。

## 代码检查与清理

检查原生 UI/网络/存储/图片/输入、相机及启动器共45个翻译单元，结合 -Wall/-Wextra、
调用搜索和实际进程回归核实。不是对第三方所有算法进行形式化验证。

- 删除没有控件、桥接生产者或测试依赖的旧键盘 tools/方向动作及旧 delta/reasoning 消费分支。
- 删除只注册 CLICK 回调中的不可达 DELETE 分支，保留专门的 DELETE 清理回调。
- 修正 HoleRect 的非平凡对象 memset、废弃 LVGL 标志查询、重复 include、
  符号类型比较、误导性多语句缩进和图标透明度枚举类型。
- 修复 Provider 兼容子页原本会触发父页表单清空的导航边界。
- 修复 main 参数解析在异常处理范围外的问题，缺少参数时返回可读错误而非 abort。
- 图片解码等待队列限制64项；成功/失败缓存合计128项，像素缓存32MiB；失败记录也参与回收，
  单张预览最多400万像素；远程图片超时独立限制60秒。
- SSE 逐字符扫描替代反复擦除字符串前缀，并限制跨多行累积事件的大小。

保留扫描32/24/8算法、硬件租约、迁移与文件回收事务、生命周期等待和第三方来源。
第三方 MicroTeX/SDK 自身警告没有通过删除代码或更改来源校验来掩盖。

## 性能对比

同一主机、相同链接依赖，隔离比较0.2.1与0.2.2网络生产者。各运行三次；模拟服务
每4ms发送512字节正文，共300段，最终正文153600字节，均逐字节一致。
旧网络代码从98095ee提取，仅加入快照计数；基线源码和产物在忽略目录。

| 指标 | 0.2.1 | 0.2.2 |
|---|---:|---:|
| 完整状态快照次数 | 302 | 14 |
| RSS峰值范围 | 11320～11528 KiB | 11060～11348 KiB |
| 用户+内核CPU时间均值 | 0.0420秒 | 0.0286秒 |
| 墙钟时间范围 | 1.250～1.254秒 | 1.253～1.257秒 |

快照减少95.4%，没有更改服务器生成速度。此数据不是设备绘制帧率或全应用内存结论。
UI并发验证仍使用数据库写锁期间打开编辑器：本轮耗时0.281秒（验收上限1.5秒），
该请求7次事务，两处消息正文一致。

## 验证入口

- `ctest --test-dir build/host --output-on-failure`
- `python3 tests/test_api_protocol.py`：两协议、JSON/SSE、错误Content-Type、BOM、字节分包、拒答、截断、非法字段、超大事件、HTTP错误、超时、取消、命名；每次操作断言一次请求。
- `python3 tests/test_api_ui.py`：两种逻辑尺寸、双列控件、滚动、超时输入、显式保存。
- 消毒器使用 build/asan 的 penchat-api-tests、penchat-network-probe 和 penchat-lvgl。
- `python3 tests/device/display_config.py --serial <授权设备序列号> --api-compatibility`：
  全私有/公共数据目录隔离，真实触摸、图像输入、JSON兼容、统计、401和手动重试、Provider控件；结束逐文件核对恢复，不重启宿主。

最终执行结果、安装包校验及ORANGE延后项见 [0.2.2验收记录](API-ACCEPTANCE.md)。
