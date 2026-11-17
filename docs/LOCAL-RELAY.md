# 服务器文件与反馈日志的本地中转

本文保存用户于 2026-11-18 提供的本地中转约定。后续管理服务器上的应用文件、
更新／降级包、同步结果和反馈日志时，优先使用此入口。

## 入口与认证

- 基础地址：`http://localhost:17865/`。
- 服务只监听 `127.0.0.1:17865`，不是设备端或局域网访问地址。
- 本地调用无需传 SSH 密码、共享密钥或 `Authorization`。
- 后端连接凭据由中转服务保管，不复制到 PenChat 源码、文档、命令行或安装包。

该中转属于开发主机的运维工具，不改变 PenChat 设备端的请求地址、协议或认证方式。

## 路由与服务器目录

| 本地路径 | 对应内容 | 支持操作 |
| --- | --- | --- |
| `/app/` | 主机 `/srv/files/app/` | 列目录、读文件、上传或覆盖文件、创建目录、删除文件和空目录 |
| `/logs/primary/` | 主机 `/srv/app-logs/` | 列目录、读日志、上传新日志、删除日志 |
| `/logs/hk/` | 香港机 `/srv/app-logs/` | 列目录、读日志、上传新日志、删除日志 |
| `/standby/app/` | 香港机 `/srv/files/app/` | 只读，检查异步同步结果 |
| `/health` | 本地服务状态 | 健康检查 |

目录地址返回 JSON 文件列表；在目录地址后追加文件名，读取原始文件内容。
目录请求保留末尾 `/`。

## 主从与日志规则

应用文件以主机的 `/app/` 为准。变更主机目录后，中转会触发到香港机的异步同步；
`/standby/app/` 仅用于读回检查。主机写入成功不等于香港机已经同步完成，报告同步
成功前应核对香港机对应文件的内容或 SHA-256。

两台服务器的日志分别保存在各自节点，不按应用目录的主从规则处理。上传新日志使用
目录地址上的 POST，继续经过原有日志接收服务，由服务生成文件名，并保留原有容量
与循环清理规则。

上传、覆盖、建目录和删除直接作用于服务器文件，按当前任务已授权的范围执行。
中转可用不代表已经授权推送、发布、覆盖线上版本或清理历史日志；继续遵守仓库的
开发与发布流程。`DELETE` 支持文件和空目录，不是递归目录删除接口。

下载的原始日志、安装包、备份及校验产物继续保存在忽略目录，不加入 Git。

## 调用示例

以下命令仅为操作示例，实际路径和本地文件须与当前任务对应。

```bash
# 健康检查
curl --fail --show-error http://localhost:17865/health

# 分别列出两台服务器的日志
curl --fail --show-error http://localhost:17865/logs/primary/
curl --fail --show-error http://localhost:17865/logs/hk/

# 查看主机降级目录
curl --fail --show-error http://localhost:17865/app/downgrade/

# 读取指定日志或文件：在目录 URL 后追加列表返回的文件名
curl --fail --show-error \
  http://localhost:17865/logs/primary/example.log

# 上传或覆盖主机上的降级包（PUT）
curl --fail --show-error --upload-file ./package.amr \
  http://localhost:17865/app/downgrade/penchat-rk3562/package.amr

# 读回香港机对应文件，检查同步结果
curl --fail --show-error --output ./standby-package.amr \
  http://localhost:17865/standby/app/downgrade/penchat-rk3562/package.amr
sha256sum ./package.amr ./standby-package.amr

# 创建目录
curl --fail --show-error -X MKCOL \
  http://localhost:17865/app/example/

# 向主机上传一份新日志；文件名由日志服务生成
curl --fail --show-error --data-binary @./feedback.json \
  http://localhost:17865/logs/primary/

# 删除指定文件；须属于当前任务授权范围
curl --fail --show-error -X DELETE \
  http://localhost:17865/app/downgrade/penchat-rk3562/package.amr
```

## 本地服务管理与实现

用户提供的配置已设置为随本地用户登录自动启动。当前是否运行，以 `/health` 或
以下状态命令的结果为准：

```bash
systemctl --user status vps-local-relay.service
systemctl --user restart vps-local-relay.service
systemctl --user stop vps-local-relay.service
```

实现位于开发主机的
[local-relay 目录](/home/ls-jl/Documents/ChatGPT/土豆器配置喵/local-relay/)，
验证记录见 [checks.json](/home/ls-jl/Documents/ChatGPT/土豆器配置喵/local-relay/checks.json)。
按用户提供的记录，服务已通过 7 项隔离测试，以及真实读写、香港同步和服务重启验证；
测试文件及测试日志已清理，并已验证局域网地址无法连接、外部网页跨站请求被拦截。
这些是已有记录，本次文档整理没有重新执行服务器读写或服务重启。
