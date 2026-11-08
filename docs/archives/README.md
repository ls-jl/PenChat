# 本地原型归档

这里保存开发原型的本地交付快照。ZIP 包含构建产物和演示截图，按仓库约定不入 Git；
源码、说明及校验文件保留在 Git。切换机器或仅克隆仓库时，ZIP 不会自动出现。

## S6 共享内存合成，第2步

- 日期：2026-11-08。
- 文件：[s6-compositor-step2-20261108-6beebeb.zip](s6-compositor-step2-20261108-6beebeb.zip)。
- 大小：3,370,380 字节，约 3.21 MiB。
- SHA-256：`88425aceea092828a7c8edf62ae6bde6f1a4b92d28b3e7daf749f0d4ab0e3ca7`。
- 校验文件：[ZIP SHA-256](s6-compositor-step2-20261108-6beebeb.zip.sha256)。
- 源码提交：`6beebeb`，分支 `s6-armv7`。

内容包括合成原型源码及测试、相关共享输入代码和构建配置、必要许可、独立 ARM32
测试 AMR、演示截图、使用说明和逐文件 SHA-256 清单。它是定点源码及测试包快照，
不包含完整第三方依赖或工具链；重新构建需要对应提交的完整仓库及依赖。

仅适用于已核验固件的 S6；安装包不是正式 PenChat 更新包。它仍是单个 LVGL 示例
生产者的合成原型，未接入正式聊天显示后端或多客户端仲裁。使用前阅读包内 README
及[原型完整说明](../../tests/device/falcon_submit_probe/README.md)。
