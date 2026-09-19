# 内部 gRPC TLS/mTLS

[返回文档导航](../README.md)


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

这套配置只保护 gRPC 链路。对公网开放的 Gate HTTP 和客户端到 Chat 的自定义 TCP 协议仍需要单独加密：Gate 建议放在 Nginx/Caddy 后终止 HTTPS；聊天 TCP 的 TLS 配置见 [公网 TLS 部署](public-edge-tls.md)。

