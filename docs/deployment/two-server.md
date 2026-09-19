# 双服务器部署

该方案在保留共享状态服务的基础上增加第二个 Chat 节点，可以提升聊天连接容量。
如果 MySQL、Redis、Gate、Status 和 Varify 没有单独实现高可用，服务器 A 仍然是
系统单点。

以下示例使用两台服务器的内网地址：

- 服务器 A：`10.0.0.10`
- 服务器 B：`10.0.0.20`

| 服务 | 服务器 A（`10.0.0.10`） | 服务器 B（`10.0.0.20`） |
|---|---|---|
| Gate / Status / Varify | 部署 | 不部署 |
| Chat | `chatserver1` | `chatserver2` |
| MySQL / Redis | 部署 | 不部署，连接服务器 A |

两台服务器必须连接同一套 MySQL 和 Redis。数据库凭据通过 `CHAT_MYSQL_*` 和
`CHAT_REDIS_*` 环境变量提供，不要把密码写入 INI 文件或提交到 Git。

## Chat 配置要求

- `SelfServer.Name` 必须全局唯一，并与 `status.ini` 中配置的名称完全一致。
- `SelfServer.ListenHost = 0.0.0.0` 用于监听客户端 TCP 连接。
- `SelfServer.Host = 0.0.0.0` 用于监听 Chat 节点之间的 gRPC 连接。
- 每个 `PeerServer` 的 `Host` 必须填写对端服务器的内网 IP。
- `status.ini` 中的 `PublicHost` 和 `PublicPort` 必须能够被客户端访问，不能填写
  `127.0.0.1`。

## 启动方式

使用 `CHAT_CONFIG_FILE` 为每个进程选择配置文件：

```sh
# 服务器 A
CHAT_CONFIG_FILE=/opt/distributed-chat/config/status.ini ./status_server
CHAT_CONFIG_FILE=/opt/distributed-chat/config/chatserver1.ini ./chat_server
CHAT_CONFIG_FILE=/opt/distributed-chat/config/gate.ini ./gate_server

# 服务器 B
CHAT_CONFIG_FILE=/opt/distributed-chat/config/chatserver2.ini ./chat_server
```

推荐启动顺序：

1. MySQL、Redis
2. VarifyServer
3. StatusServer
4. ChatServer1、ChatServer2
5. GateServer

## 健康检查和路由

每个 Chat 进程启动后会立即向 Redis 写入 `chat_health_<服务器名称>`，随后每五秒
刷新一次。Key 的值为当前 TCP 会话数量，有效期为 15 秒。

Status 会排除以下节点：

- 健康 Key 不存在或已经过期；
- 连接数为负数；
- 连接数格式不合法。

在剩余健康节点中，Status 优先选择连接数最低的节点；连接数相同时，按照节点名称
执行轮询。如果没有任何健康节点，登录请求返回 `RPCFailed`，并且不会签发聊天票据。

## 网络和防火墙

只向公网开放 Gate 的 HTTPS 入口以及两个 Chat 的客户端 TCP 入口。Status、Varify、
Chat 节点间 gRPC、MySQL 和 Redis 必须限制在内网。

| 服务器 | 端口 | 建议来源 |
|---|---:|---|
| A | 443 | 公网客户端 |
| A | 8989 | 公网客户端 |
| B | 8990 | 公网客户端 |
| A | 5000 | 仅 Gate |
| A | 5050 | 仅 A、B |
| A | 50055 | 仅 B |
| B | 50056 | 仅 A |
| A | 3306 | 仅 A、B |
| A | 6379 | 仅 A、B |

内部 gRPC 建议启用 mTLS。当前 Chat TCP 监听器仍是明文协议，生产环境还需要增加
应用层 TLS，或者部署与客户端协议兼容的 TCP TLS 终止代理。

## 高可用边界

上述部署解决了 Chat 节点扩容、健康摘除和负载分配，但服务器 A 宕机后 Gate、
Status、MySQL、Redis 和 Varify 会同时不可用。要实现真正的高可用，还需要：

- 两台服务器都运行 Gate、Status 和 Varify，并通过负载均衡地址访问；
- 使用托管高可用 MySQL、Redis，或者增加第三个仲裁节点；
- 为公网 Gate 和 Chat 入口配置健康检查和自动切换。

仅使用两台服务器无法可靠解决数据库和 Redis 的多数派仲裁及脑裂问题。
