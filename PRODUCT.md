# PenChat

<!-- impeccable:product-schema 1 -->

## Platform

Embedded Linux, native LVGL with a thin Falcon MiniApp launcher. This is not a web interface.

## Users

词典笔用户，包括第一次使用 AI、尚不熟悉 API 服务与扫描模式的用户。

## Product Purpose

将纸面扫描图片、OCR文字、拍照和打字带入可持续的 AI 会话，保留用户对发送与配置的控制。

## Operating Context

横屏词典笔，显示几何和旋转由 Falcon 配置提供；原生触摸、笔头扫描、独立拼音键盘以及用户自行配置的 API 服务。

## Capabilities and Constraints

原生聊天支持 Responses、Chat Completions 和 Anthropic Messages；保留会话、图片、预设、压缩归档及导入导出。引导前三步必须依次阅读，但不要求真实扫描、密钥或网络请求；之后可结束。已有用户也展示一次。

不重启系统宿主，不改变现有用户数据。演示使用虚构内容，不启用真实硬件或调用 API。正式配置须由用户主动进入现有表单。

## Brand Commitments

名称 PenChat，沿用现有 ChatGPT 深色风格及共享原生线条图标。

## Product Principles

- 先演示聊天页扫描成图片，再说明键盘扫描成文字，最后解释 API 配置。
- 发送前保留检查与修改；不把演示当作实际操作。
- 保持已有会话、草稿、图片与阅读位置。
- 高级功能渐进介绍，不阻碍用户完成首次提问。
