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
| `/files/` | 主机 `/srv/files/` | 同上，下载站总目录 |
| `/penchat-rk/` | 主机 `/srv/files/penchat-rk/` | 普通 RK 更新源直达入口；读写子目录与文件，禁止修改映射根目录本身 |
| `/logs/primary/` | 主机 `/srv/app-logs/` | 列目录、读日志、上传新日志、删除日志 |
| `/logs/hk/` | 香港机 `/srv/app-logs/` | 列目录、读日志、上传新日志、删除日志 |
| `/standby/app/` | 香港机 `/srv/files/app/` | 只读，检查异步同步结果 |
| `/standby/files/` | 香港机 `/srv/files/` | 只读，检查下载站同步结果 |
| `/standby/penchat-rk/` | 香港机 `/srv/files/penchat-rk/` | 只读，检查普通 RK 更新源同步结果 |
| `/health` | 本地服务状态 | 健康检查 |

目录地址返回 JSON 文件列表；在目录地址后追加文件名，读取原始文件内容。
目录请求保留末尾 `/`。

## 主从与日志规则

下载文件以主机为准，`/app/`、`/files/` 和 `/penchat-rk/` 的写操作均作用于主机。
变更后中转会触发到香港机的异步同步；`/standby/` 下对应入口仅用于读回检查。
主机写入成功不等于香港机已经同步完成，报告同步
成功前应核对香港机对应文件的内容或 SHA-256。

普通更新的公网地址是 `https://cdn.ls-jl.cn/penchat-rk/`，对应 `/srv/files/penchat-rk/`，
应使用 `/penchat-rk/` 或 `/files/penchat-rk/` 管理；`/app/penchat-rk/` 是另一条路径，
不对应客户端使用的更新源。降级目录继续使用 `/app/downgrade/`。

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

# 查看普通 RK 更新源及香港镜像
curl --fail --show-error http://localhost:17865/penchat-rk/
curl --fail --show-error http://localhost:17865/standby/penchat-rk/

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
以上是最初接入时引用的已有记录；其后的新增验证另见下节。

## 2026-11-18：普通 RK 更新源专用映射

在已有 `/files/` 后端能力上增加 `/penchat-rk/` 和 `/standby/penchat-rk/` 直达映射。
保留原通道、认证和主机校验；仅重载本地中转服务，没有修改服务器端 helper 或设备宿主。

- 主机可 GET／HEAD、PUT、MKCOL，以及删除文件和空子目录；映射根目录本身不可覆盖或删除。
- 香港入口仅允许 GET／HEAD；写操作返回 409，主机根目录修改返回 403。
- 保留路径穿越、符号链接、跨站／异常 Host、非递归删除、容量与中断写入保护。
- 13 项隔离测试通过；实际创建非版本临时目录，完成写入、覆盖、读回、HEAD、香港同步及
  拒绝操作检查，随后删除临时文件和目录。原更新目录条目逐项核对不变。

验证记录在 PenChat 主工作区忽略目录 `.work/relay-penchat-rk/`。
该次映射验证按当时指令仅确认权限，未上传 0.3.4；三件套保存在 `.work/update-0.3.4/`。
随后用户单独明确“上传”，已完成普通更新源 0.3.4 的临时上线，结果见
[UPDATING.md](UPDATING.md)。版本保留到用户更新完成并明确要求删除，不自动清理；
既有降级目录不在这次临时更新的清理范围。
