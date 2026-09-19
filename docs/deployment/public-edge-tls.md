# 公网 HTTPS 与 Chat TLS 部署方案

生产环境包含两条相互独立的公网加密链路：

```text
Qt 客户端 -- HTTPS --> Nginx --> 127.0.0.1:8080 Gate
Qt 客户端 -- TLS TCP -> Nginx --> 127.0.0.1:8989 Chat
Gate/Status/Chat -- gRPC mTLS --> 内部服务网络
```

Nginx 与对应后端部署在同一台主机，并负责终止公网 TLS。后端的明文 HTTP/TCP 端口因此只能监听回环地址。MySQL、Redis、Status、Verify 以及服务间 gRPC 端口必须留在私有网络中。

## 生产环境端点规则

- Gate 使用 `api.chat.example.com:443`，后端监听 `127.0.0.1:8080`。
- 每台 Chat 主机使用独立的 DNS 名称和公网 TLS 端口 443，后端监听 `127.0.0.1:8989`。
- 单机联调时，Gate 使用 443，Chat 1 使用 8443，Chat 2 使用 8444。HTTP 监听器和 Stream 监听器不能共用同一个地址与端口。
- Status 向客户端返回 `PublicHost`、`PublicPort`、`Transport=tls` 和 `TLSName`。绝不能向互联网客户端返回 `0.0.0.0`、回环地址或私有地址。
- 生产客户端必须设置 `AllowInsecure=false`，TLS 失败时不会自动降级为明文连接。

## 安装并验证 Nginx

将 `deploy/nginx/gate-http.conf.example` 复制到 Gate 主机的 HTTP 配置包含目录。将 `deploy/nginx/chat-stream.conf.example` 复制到每台 Chat 主机，并替换示例域名。Stream 配置是顶层配置块，不能放在 `http {}` 内部。

重新加载 Nginx 前，先检查是否支持 Stream TLS 并验证配置：

```bash
nginx -V 2>&1 | grep stream
sudo nginx -t
sudo systemctl reload nginx
```

编译参数必须包含 `--with-stream` 和 `--with-stream_ssl_module`，或者发行版必须加载功能等价的动态模块。

## 证书

桌面客户端应使用公共 CA 签发的证书。以下是 Certbot 命令示例：

```bash
sudo certbot --nginx -d api.chat.example.com
sudo certbot certonly --standalone -d chat1.chat.example.com
sudo certbot renew --deploy-hook "nginx -t && systemctl reload nginx"
```

不要把私钥或正在使用的证书提交到仓库。本项目已经忽略常见的证书和私钥扩展名。

## 防火墙策略

Gate 只对公网开放 80/443。Chat 只开放其公网 TLS 端口。后端 8080/8989 必须绑定回环地址。5000、5050、50055/50056、3306 和 6379 端口仅允许服务网络访问，并通过主机防火墙或云安全组限制来源。

## 验证方法

```bash
curl --fail --show-error https://api.chat.example.com/
openssl s_client -connect chat1.chat.example.com:443 \
  -servername chat1.chat.example.com -verify_return_error
sudo certbot renew --dry-run
```

还要确认桌面客户端会拒绝域名不匹配、证书过期、证书不受信任以及 Chat 明文端点。

在 Windows 上发布客户端前，运行构建生成的 TLS 运行环境探针：

```powershell
.\build\desktop-qt6-local\bin\chat_tls_probe.exe
.\build\desktop-qt6-local\bin\chat_tls_probe.exe api.chat.example.com 443
```

输出必须包含 `TLS 可用: 是`。Windows 上的 Qt 5.12 需要匹配的 OpenSSL 运行库 DLL，而 `windeployqt` 不会自动提供这些文件。不要通过捆绑已经停止维护的 OpenSSL 版本来解决此问题；生产打包应使用仍受维护的 Qt 6 Windows 套件。
