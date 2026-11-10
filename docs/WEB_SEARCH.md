# 联网搜索与图片引用（dev）

加号中的“联网搜索”默认开启，模型自行决定是否检索。设置里的“新聊天联网搜索”
可选默认开启、默认关闭或使用最近一次手动选择。历史菜单新建按该设置，上拉新建
继承上一会话。生成中修改开关只影响下一次提问。

搜索、阅读和图片获取显示在同一条 AI 回复里；点按可展开来源，再进入原生阅读页。
长网页分段阅读，不执行网页脚本。保存的是公开来源片段；需要更多内容可重新获取。
网页加载失败可手动重试，不会重新提交聊天请求。

AI 可以把实际获取的图片插在回答正文中，点击放大，双指缩放、单指移动、双击恢复。
图片只保存在 PenChat 私有资产中，随聊天备份，不自动加入系统相册。纯文字模型也能
引用有来源说明的图片；只有明确读图且支持图片的模型才收到像素。

## 协议与组件边界

- Responses：function call/output，保留必要的有序输出和同身份思考状态。
- Chat Completions：assistant tool_calls + tool 消息；视觉资料使用明确标记的用户
  图片资料消息，仅在 API 上存在，不创建聊天气泡或新问题。
- Anthropic Messages：tool_use/input_json_delta + 对应 tool_result，保持内容块顺序。
- 三者共用原生工具注册与执行、取消、额度、来源及图片资产机制。
- 编辑、重试、分支、归档、删除、压缩和导入导出复用现有数据库工作线程与引用保护。

官方协议依据：[OpenAI function calling](https://developers.openai.com/api/docs/guides/function-calling)、
[Chat create](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)、
[Anthropic tool calls](https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls)。

## 开发验证记录

本次保持 0.2.34，不发布。实施中的主机、RK3562/AArch64、CV1826-S/ARMv7 编译及
开发包静态校验分别记录在本地构建日志；最终交付时说明实际结果。

新增可选回归入口 `python3 tests/test_web_tools.py`，使用本地模拟 API 和隔离假 worker，
覆盖三协议、逐字节 SSE/完整 JSON、不调用工具、用量累计、图片引用和 Base64 不落库。
原生 `penchat-web-tools-tests` 还覆盖开关继承、注册表拒绝未知/错误参数、三协议请求构建及导出白名单。
本轮仅编译这些用例，不自动执行这些请求用例，不进行主机全回归、ASAN、性能或设备触摸验收。

待用户/后续定向验收：三类布局、真实搜索与网页风控表现、视觉结果、长网页/图文滚动、
折叠来源、取消/断连、损坏图片与原图重试、分支/归档/备份完整往返，以及共享图片删除。
Edge Search 原项目的 x86 历史测试不代表词典笔已通过以上验收。

组件源码、依赖、构建与 IPC 说明见 [native/web-search](../native/web-search/README.md)。
