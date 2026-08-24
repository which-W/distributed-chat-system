# Xray 备用入口部署与客户端使用方案

## 1. 目标与边界

本方案只新增一个备用入口，不替换现有主入口：

- 主入口继续使用 Nginx：Gate 为 `443/HTTPS`，两个 Chat TLS 入口为 `8443`、`8444`。
- 备用入口新增 `9443/TCP`，协议固定为 `VLESS + XHTTP + REALITY`。
- Xray 收到备用流量后只允许转到本机 Nginx 的 `443/8443/8444`，其它目标全部丢弃。
- Windows 客户端通过 v2rayN/Xray 或 Mihomo 提供的本地 `127.0.0.1:10808` SOCKS5 端口接入。
- Qt Client 默认仍是 Direct。只有用户在“设置 → Network route”中选择 SOCKS5 时才使用备用入口。

这不是 VPN，也不是通用代理。配置特意把可访问范围收窄到本项目的三个本机端口，避免服务器被滥用为开放代理。

## 2. 数据流

```text
主入口：
Qt Client ── HTTPS/TLS ──> Nginx :443/:8443/:8444 ──> Gate/Chat

备用入口：
Qt Client ── SOCKS5 127.0.0.1:10808 ──> Xray/Mihomo Client
          ── VLESS + XHTTP + REALITY :9443 ──> Xray Server
          ── 127.0.0.1:443/:8443/:8444 ──> Nginx ──> Gate/Chat
```

客户端不会自动从 Direct 切到 SOCKS5，以免在本地代理未启动时制造循环故障。选定一种模式后，Chat 连接具有 30 秒心跳、三次丢失判定和 `1/2/4/8/15/30` 秒退避重连；代理切换也会触发重新连接和 token 会话恢复。

## 3. 仓库内文件

| 文件 | 用途 |
|---|---|
| `deploy/xray/server-backup.json.example` | 服务端 Xray 配置模板 |
| `deploy/xray/client-xray.json.example` | 原生 Xray 客户端模板，监听 SOCKS5 `10808` |
| `deploy/xray/client-mihomo.yaml.example` | Mihomo/Clash Meta 客户端模板 |
| `deploy/xray/xray-backup.service` | systemd 服务 |
| `deploy/xray/install-xray-backup.sh` | 安装固定版本二进制、配置和服务 |
| `deploy/xray/verify-xray-backup.sh` | 服务端配置、监听和 Nginx 健康检查 |

真实配置文件名已加入 `.gitignore`。不要把 UUID、REALITY 私钥、password 或 short ID 提交到仓库。

## 4. 前置条件

1. 先按 [公网 TLS 部署文档](public-edge-tls.md) 配好 Nginx，确保服务器本机可访问：

   ```bash
   curl -k https://127.0.0.1/nginx-health
   openssl s_client -connect 127.0.0.1:8443 -servername chat1.chat.example.com </dev/null
   openssl s_client -connect 127.0.0.1:8444 -servername chat2.chat.example.com </dev/null
   ```

2. `config/status.ini` 返回给客户端的 Chat 端口必须仍是 `8443/8444`，传输类型为 TLS，`TLSName` 与证书域名一致。
3. 服务器时间必须通过 NTP 同步。模板的 REALITY `maxTimeDiff` 是 60 秒。
4. 安全组和防火墙新增放行 `9443/TCP`。不要为 Xray 开放其它新端口。

## 5. 固定版本

服务端和原生 Xray 客户端固定为 `26.6.27`。模板的 `version.min/max` 会阻止误用其它版本。Mihomo 官方文档明确说明其 REALITY 实现不兼容 Xray `26.7.11+`，所以使用 Mihomo 时尤其不要擅自升级服务端。

下载官方发布包及对应 `.dgst` 文件并校验 SHA-256：

```bash
mkdir -p /tmp/xray-26.6.27
cd /tmp/xray-26.6.27
curl -fLO https://github.com/XTLS/Xray-core/releases/download/v26.6.27/Xray-linux-64.zip
curl -fLO https://github.com/XTLS/Xray-core/releases/download/v26.6.27/Xray-linux-64.zip.dgst
grep -A1 'SHA2-256' Xray-linux-64.zip.dgst
sha256sum Xray-linux-64.zip
unzip Xray-linux-64.zip xray
chmod 0755 xray
./xray version
```

如果服务器不是 `linux-amd64`，从同一 release 选择对应架构文件，不要用上面的文件名硬装。

## 6. 生成参数

在服务器的临时目录执行：

```bash
./xray uuid
./xray x25519
openssl rand -hex 8
openssl rand -hex 12
```

保存以下值：

- `UUID`：服务端与客户端相同。
- `PrivateKey`：只写入服务端 `privateKey`，绝不能发给客户端。
- `Password`：`xray x25519` 输出的公开侧参数，写入原生 Xray 客户端 `password`；Mihomo 字段名仍是 `public-key`。
- 第一条 `openssl` 输出：16 个十六进制字符，作为 `shortId`。
- 第二条输出：作为随机 XHTTP path，例如 `/9c33...`，服务端和客户端完全相同。

REALITY target 应选择服务器可直接访问、TLS 1.3 表现稳定、最好与服务器同 ASN 的真实站点，不要选择自己的域名或自己的服务器 IP。先测试：

```bash
./xray tls ping target.example.com
```

如果不得不选择 Cloudflare 等公共 CDN target，未认证流量可能被转发而消耗带宽。仓库模板没有人为添加固定的回落限速特征；优先换成同 ASN、不会造成公共转发滥用的 target。

## 7. 配置并安装服务端

复制模板，不要直接编辑 example：

```bash
cd /opt/distributed-chat-system
cp deploy/xray/server-backup.json.example deploy/xray/server-backup.json
chmod 0600 deploy/xray/server-backup.json
```

替换全部占位符：

```text
REPLACE_WITH_UUID
REPLACE_WITH_SAME_ASN_TARGET
REPLACE_WITH_REALITY_PRIVATE_KEY
REPLACE_WITH_16_HEX_SHORT_ID
REPLACE_WITH_RANDOM_PATH
```

模板按目标端口进行强制重定向：

- 客户端请求目的端口 `443` → `127.0.0.1:443`
- 目的端口 `8443` → `127.0.0.1:8443`
- 目的端口 `8444` → `127.0.0.1:8444`
- 所有其它目的端口 → blackhole

安装并启动：

```bash
sudo bash deploy/xray/install-xray-backup.sh \
  /tmp/xray-26.6.27/xray \
  deploy/xray/server-backup.json
```

脚本会验证二进制版本、创建不可登录的 `xray` 系统用户、安装只读配置、执行 `xray run -test`，然后启用 `xray-backup.service`。

防火墙示例：

```bash
sudo ufw allow 9443/tcp comment 'Xray chat backup'
sudo ufw status numbered
```

云厂商安全组也要放行 `9443/TCP`。如果服务器只有 IPv4，可以只建立 IPv4 规则。

验证：

```bash
sudo bash deploy/xray/verify-xray-backup.sh
sudo journalctl -u xray-backup.service -n 100 --no-pager
sudo ss -lntp | grep ':9443'
```

## 8. Windows 原生 Xray / v2rayN

### 原生 Xray

1. 下载与服务端相同的 Xray `26.6.27` Windows 包并校验 `.dgst`。
2. 复制 `deploy/xray/client-xray.json.example` 为本机 `config.json`。
3. 替换 server、UUID、target、REALITY Password、short ID 和 path。
4. 执行：

   ```powershell
   .\xray.exe run -test -c .\config.json
   .\xray.exe run -c .\config.json
   Test-NetConnection 127.0.0.1 -Port 10808
   ```

### v2rayN

新建 VLESS 节点，字段对应如下：

| v2rayN 字段 | 值 |
|---|---|
| Address | 备用服务器 IP 或域名 |
| Port | `9443` |
| UUID/ID | 生成的 UUID |
| Encryption | `none` |
| Transport | `xhttp` |
| XHTTP mode | `auto` |
| Path | 随机 path，包含开头 `/` |
| Security | `reality` |
| SNI/serverName | REALITY target 域名 |
| Fingerprint | `chrome` |
| Public key/password | `xray x25519` 输出的 Password |
| Short ID | 生成的 16 位十六进制值 |

把 v2rayN 本地 SOCKS 端口固定为 `10808`。不要开启全局 TUN；Qt Client 会显式连接该 SOCKS5 端口。

## 9. Mihomo / Clash Meta

复制 `deploy/xray/client-mihomo.yaml.example`，替换所有占位符后加载。要求使用明确支持 XHTTP 的近期 Mihomo，同时保持服务端 Xray 为 `26.6.27`。

```powershell
.\mihomo.exe -t -f .\client-mihomo.yaml
.\mihomo.exe -f .\client-mihomo.yaml
Test-NetConnection 127.0.0.1 -Port 10808
```

模板只在回环地址暴露 SOCKS 端口（`allow-lan: false`），全局组只包含 `chat-backup`。如果把它合并进已有 Clash 配置，应保留 `socks-port: 10808`，并确保该端口实际选择备用节点。

## 10. Qt Client 使用

构建与运行方式不变。生产 `config.ini` 新增默认值：

```ini
[Proxy]
Mode = direct
Host = 127.0.0.1
Port = 10808
```

客户端设置写入用户级 `QSettings`，不会修改安装目录中的 `config.ini`。

使用主入口：

1. 进入“设置 → Network route”。
2. 选择 `Direct (normal entry)`。
3. 点击 `Apply network route`。

使用备用入口：

1. 先启动 v2rayN、Xray 或 Mihomo，并确认 `127.0.0.1:10808` 在监听。
2. 进入“设置 → Network route”。
3. 选择 `SOCKS5 (Xray backup)`，Host 填 `127.0.0.1`，Port 填 `10808`。
4. 点击应用。Client 会关闭旧 Chat 连接，通过新路由重连并自动恢复会话。
5. 如果登录页阶段主入口已不可用，可先启动本地代理，再把 `config.ini` 的 `Proxy/Mode` 改成 `socks5`；首次启动会读取它。应用成功运行一次后，用户级设置优先于该默认值。

`System proxy` 只适合已经正确配置 Windows 系统代理的场景。v2rayN 只开本地端口但未设置系统代理时，应选择 SOCKS5，而不是 System。

## 11. 验收清单

- Direct 模式下注册、验证码、登录和 Chat 收发与改造前一致。
- SOCKS5 模式下 Gate HTTPS 登录成功，Status 返回的两个 Chat TLS 入口均可连接。
- 服务端 `journalctl` 能看到合法连接，`ss` 显示 `9443` 监听。
- 停止本地代理后 Client 显示断线提示并退避重连；恢复代理后自动重新认证，不重复创建 ChatWindow。
- 切回 Direct 后连接恢复，重启 Client 后记住最后选择。
- 从备用节点请求 80、22 或任意非 `443/8443/8444` 目标时无法转发。
- 重启服务器后 Xray 与 Nginx 都能自动启动。

## 12. 故障排查

### `xray run -test` 报 unknown field

先确认 `xray version` 是 `26.6.27`。新旧配置字段不可混用：本模板使用当前的 `settings.users`、`streamSettings.method` 和 REALITY 客户端 `password` 字段。

### REALITY 握手失败

依次检查服务器时间、target 可达性、SNI、Password、short ID、UUID、path 以及两端版本。`PrivateKey` 只在服务端，客户端必须使用它对应的 Password。

### 本地 SOCKS 端口通，但登录失败

确认 Client 选择的是 SOCKS5；检查 Gate URL 仍为 `https://api...` 且端口为 443；检查 Xray 服务端 `443` 出站能访问本机 Nginx。证书名称必须与 Client 请求域名一致，不能把 Gate URL 改成 `https://127.0.0.1`。

### 登录成功但 Chat 连接失败

检查 Status 返回的端口必须是 `8443/8444`，而不是后端明文端口 `8989/8990`；检查 `TLSName` 和证书 SAN；查看 `/var/log/nginx/chat-error.log`。

### 频繁断线

先分别测试 Direct 与 SOCKS5，区分业务服务故障和备用链路故障。查看客户端的心跳/重连提示、Xray journal 与 Nginx stream 日志。不要通过关闭 TLS 校验解决问题。

## 13. 密钥轮换与回滚

轮换时生成新 UUID、REALITY keypair、short ID 和 path，先更新服务端并通过一台客户端验证，再分发客户端配置。只有单用户模板时，切换会中断旧客户端；需要平滑轮换可临时在 `users` 和 `shortIds` 中同时保留新旧值，完成迁移后删除旧值。

停用备用入口：

```bash
sudo systemctl disable --now xray-backup.service
sudo ufw delete allow 9443/tcp
```

然后让 Client 切回 Direct。以上操作不会修改或停止 Nginx、Gate、Status、Chat、Redis、MySQL，主入口仍可独立运行。

## 14. 参考

- [Xray StreamSettings 与 XHTTP](https://xtls.github.io/en/config/transport.html)
- [Xray REALITY](https://xtls.github.io/en/config/transports/reality.html)
- [Xray VLESS 入站](https://xtls.github.io/en/config/inbounds/vless.html)
- [Xray Freedom redirect/finalRules](https://xtls.github.io/en/config/outbounds/freedom.html)
- [Mihomo VLESS/XHTTP](https://wiki.metacubex.one/en/config/proxies/transport/)
- [Xray-core v26.6.27 release](https://github.com/XTLS/Xray-core/releases/tag/v26.6.27)
