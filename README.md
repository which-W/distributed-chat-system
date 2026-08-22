# distributed-chat-system

基于 Qt、Boost.Asio、gRPC、Redis 和 MySQL 的分布式即时通信系统。

## 组件

- `gate_server`：HTTP 注册、重置密码和登录网关，默认端口 `8080`。
- `status_server`：聊天节点发现、负载选择和 token 签发，默认 gRPC 端口 `5050`。
- `chat_server`：TCP 聊天节点及节点间 gRPC 服务；同一可执行文件使用不同配置启动多个实例。
- `VarifyServer`：Node.js 邮箱验证码 gRPC 服务，默认端口 `5000`。
- `chat_client`：Qt 桌面客户端，默认不参与服务器构建。

架构与调用关系见 [CODEGRAPH.md](CODEGRAPH.md)。

## 服务端目录结构

三个 C++ 服务统一放在 `servers/` 下：

```text
servers/
├── gate/
│   ├── app/       # 程序入口
│   ├── config/    # INI 配置读取
│   ├── core/      # 常量和通用基类
│   ├── network/   # HTTP 服务、连接和 IO 池
│   ├── rpc/       # Status / Verify gRPC 客户端
│   ├── service/   # 注册、登录等业务编排
│   └── storage/   # MySQL / Redis 访问
├── status/
│   ├── app/       # 程序入口
│   ├── config/    # 配置
│   ├── core/      # 常量和通用基类
│   ├── runtime/   # Asio 运行时
│   ├── rpc/       # Status RPC 实现与 Chat RPC 客户端
│   └── storage/   # 分布式锁、MySQL 和 Redis
└── chat/
    ├── app/       # 程序入口
    ├── config/    # 配置
    ├── core/      # 常量、数据结构和通用基类
    ├── files/     # 文件处理
    ├── network/   # TCP 服务、会话和消息帧
    ├── rpc/       # 节点间 RPC 和 Status RPC 客户端
    ├── runtime/   # IO 线程池
    ├── service/   # 聊天业务、worker 和用户会话管理
    └── storage/   # 分布式锁、MySQL 和 Redis
```

## 前置条件

- CMake 3.24+
- Ninja
- C++17 编译器（Linux 推荐 GCC 11+ 或 Clang 14+）
- vcpkg
- Node.js 18+
- Redis 6+
- MySQL 8+
- 构建桌面客户端时额外安装 Qt 5（推荐）或 Qt 6

依赖由根目录的 `vcpkg.json` 声明，不再使用任何写死的本机库路径。

## Linux 服务器构建

```bash
export VCPKG_ROOT=/opt/vcpkg
cmake --preset linux-server-release
cmake --build --preset linux-server-release -j
npm ci --prefix VarifyServer
```

如果脚本没有执行权限：

```bash
chmod +x scripts/*.sh
```

## Windows CMake 构建

Windows 也不需要打开 Visual Studio，但仍需要安装 MSVC Build Tools、Ninja 和 vcpkg：

```powershell
$env:VCPKG_ROOT = 'C:\tools\vcpkg'
cmake --preset windows-server-release
cmake --build --preset windows-server-release
npm ci --prefix VarifyServer
```

MySQL Connector/C++ 的 JDBC 兼容接口要求静态 vcpkg triplet，因此 Windows 服务器预设使用仓库内的 `x64-windows-static-release`，宿主工具使用 `x64-windows-release`。两者只构建发布版依赖，避免 vcpkg 同时生成体积很大的 Debug/Release 库，并会跳过 libmysql 在 Windows 上可选且可能卡住的 WSL ABI 检查。

## Qt 客户端构建

把 Qt 的 CMake 目录加入 `CMAKE_PREFIX_PATH` 后执行：

```bash
cmake --preset desktop-release -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15/gcc_64
cmake --build --preset desktop-release -j
```

## 配置

仓库中的 `config/*.ini` 只包含安全的本机默认值。生产环境不要把密码写入 Git，复制 `.env.example` 为 `.env` 并填写：

```bash
cp .env.example .env
```

C++ 服务支持以下环境变量覆盖 INI：

- `CHAT_REDIS_HOST`、`CHAT_REDIS_PORT`、`CHAT_REDIS_PASSWORD`、`CHAT_REDIS_USER`
- `CHAT_MYSQL_HOST`、`CHAT_MYSQL_PORT`、`CHAT_MYSQL_PASSWORD`、`CHAT_MYSQL_USER`、`CHAT_MYSQL_SCHEMA`

验证码服务使用 `VARIFY_*` 环境变量，完整列表见 `.env.example`。`.env`、旧 `config.ini` 和 `VarifyServer/config.json` 已被 `.gitignore` 排除。

初始化数据库：

```bash
mysql -u root -p < database/schema.sql
```

## 启动和停止

Linux：

```bash
cmake --build build/linux-server-release --target run_all
cmake --build build/linux-server-release --target stop_all
```

Windows：

```powershell
cmake --build build/windows-server-release --target run_all
cmake --build build/windows-server-release --target stop_all
```

也可以在 Windows 双击 `start_server.bat`；它只是 CMake 目标的薄封装，不再直接启动 VS 产物。日志和 PID 位于对应构建目录的 `logs/` 与 `run/`。

## 单机测试与多机部署

`run_all`/`run-all` 只用于在一台机器上启动整套服务。正式部署时，每台机器只启动自己的进程，并通过 `CHAT_CONFIG_FILE` 指定配置：

```bash
# 网关机
CHAT_CONFIG_FILE=/etc/distributed-chat/gate.ini /opt/distributed-chat/bin/gate_server

# 状态服务器
CHAT_CONFIG_FILE=/etc/distributed-chat/status.ini /opt/distributed-chat/bin/status_server

# 聊天节点 1 / 2
CHAT_CONFIG_FILE=/etc/distributed-chat/chatserver1.ini /opt/distributed-chat/bin/chat_server
CHAT_CONFIG_FILE=/etc/distributed-chat/chatserver2.ini /opt/distributed-chat/bin/chat_server
```

多机配置规则：

- `config/client.ini` 的 Gate `Host` 是客户端能够访问的网关公网 IP 或域名。
- `config/gate.ini` 中 `VarifyServer.Host`、`StatusServer.Host` 是网关能够访问的服务内网 IP 或 DNS 名。
- `config/status.ini` 中各 `chatserver*.Host/Port` 会原样返回客户端，必须是客户端能够访问的地址；外网客户端不能使用服务器内网 IP，除非通过 VPN 或专网接入。
- `config/chatserver*.ini` 的 peer `Host/Port` 是聊天节点之间的 gRPC 内网地址和 RPC 端口（默认 `50055/50056`）。
- 所有服务必须访问同一套 Redis 和 MySQL。它们应只开放在内网，不要把 `6379/3306` 暴露到公网。
- `chatserver1`、`chatserver2` 等节点名必须在 `[chatservers]`、`[SelfServer]`、`[PeerServer]` 和 peer section 中完全一致，因为 Redis 路由保存的是节点名。

推荐网络拓扑是：公网只开放 Gate `8080` 和客户端需要直连的 Chat TCP `8989/8990`；`5000`、`5050`、`50055/50056`、`6379`、`3306` 仅允许服务所在的内网或安全组访问。

## gRPC TLS/mTLS

项目支持三种模式：

- `insecure`：明文，只适合本机开发，且是仓库配置的默认值。
- `tls`：客户端验证服务器证书，链路加密。
- `mtls`：双方都验证证书，既加密又认证服务身份；生产环境推荐。

C++ 服务通过每个 INI 的 `[GrpcTLS]` 配置，也可以由环境变量覆盖：

```ini
[GrpcTLS]
Mode = mtls
CACert = /etc/distributed-chat/tls/ca.crt
Cert = /etc/distributed-chat/tls/service.crt
Key = /etc/distributed-chat/tls/service.key
```

```bash
export CHAT_GRPC_TLS_MODE=mtls
export CHAT_GRPC_CA_CERT=/etc/distributed-chat/tls/ca.crt
export CHAT_GRPC_CERT=/etc/distributed-chat/tls/status.crt
export CHAT_GRPC_KEY=/etc/distributed-chat/tls/status.key
```

每台机器应使用自己的叶子证书和私钥，所有证书由同一个内部 CA 签发。证书必须同时具有 `serverAuth` 和 `clientAuth` 扩展。私钥权限建议设为 `0600`，CA 私钥不要复制到业务服务器。

当 `Host` 使用 IP，而服务器证书使用 DNS 名时，在对应远端 section 设置 `TLSName`：

```ini
[StatusServer]
Host = 10.0.1.20
Port = 5050
TLSName = status
```

`TLSName` 必须出现在服务器证书的 SAN 中。生产环境更推荐让 `Host` 直接使用内部 DNS 名，并让证书 SAN 与该名称一致。不要用 `TLSName` 绕过错误证书；它只用于指定预期身份，证书仍会由 CA 校验。

本地联调可以用 OpenSSL 脚本生成一套开发证书：

```bash
sh scripts/generate-dev-certs.sh certs
```

该脚本生成 `gate`、`status`、`chatserver1`、`chatserver2`、`varify` 证书，SAN 包含服务名、`localhost` 和 `127.0.0.1`。`certs/` 和私钥已被 Git 忽略。生产环境应使用企业 CA、Vault PKI、step-ca 或云厂商私有 CA，不要使用开发 CA。

VarifyServer 使用单独的 Node.js 环境变量：

```bash
export VARIFY_GRPC_TLS_MODE=mtls
export VARIFY_GRPC_CA_CERT=/etc/distributed-chat/tls/ca.crt
export VARIFY_GRPC_CERT=/etc/distributed-chat/tls/varify.crt
export VARIFY_GRPC_KEY=/etc/distributed-chat/tls/varify.key
node VarifyServer/server.js
```

启用 mTLS 时，Gate、Status、Chat 和 Varify 必须同时切换；混用 `insecure` 与 `mtls` 的两端无法建立连接。当前 mTLS 验证“证书是否由内部 CA 签发”以及服务器 SAN；如需限制某个 RPC 只能由特定服务调用，还应增加基于证书身份的授权策略。

这套配置只保护 gRPC 链路。对公网开放的 Gate HTTP 和客户端到 Chat 的自定义 TCP 协议仍需要单独加密：Gate 建议放在 Nginx/Caddy 后终止 HTTPS；聊天 TCP 应增加 TLS（或先限制在可信 VPN/专网内）。

## 部署端口

需要开放或映射的默认端口：

| 端口 | 协议 | 服务 |
|---:|---|---|
| 8080 | HTTP | GateServer |
| 5000 | gRPC | VarifyServer |
| 5050 | gRPC | StatusServer |
| 8989 / 8990 | TCP | 两个 ChatServer 客户端入口 |
| 50055 / 50056 | gRPC | 两个 ChatServer 节点间入口 |

多主机部署时，修改 `config/status.ini` 中提供给客户端的聊天节点地址，以及两份 `config/chatserver*.ini` 中的 peer 地址。

