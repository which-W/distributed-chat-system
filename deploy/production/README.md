# deepecho.top：Ubuntu 24.04 + Docker 单节点部署

适用于现有 Docker Compose v2。以下命令均在 Linux 上执行，并以仓库根目录为当前目录。
这是小规模单机配置，尚未在你的服务器运行验证，不提供高可用保证。
Compose 不在服务器编译，Nginx/Certbot 安装在宿主机；七个业务/存储容器由 Docker 管理。
当前版本的图片、文件和头像由独立的 `resource_server` 处理，Chat 只处理聊天、资源凭证与通知。

已有部署升级请先阅读第 6 节：保留原有数据库、文件卷、文件主密钥及内部 CA，执行数据库迁移后再启动新业务镜像。
原六服务单机部署也可以使用 [一键迁移工具和步骤](MIGRATE_RESOURCE.md)，自动打包、备份及迁移；运行前不要覆盖服务器上的旧 Compose 和环境文件。
本方案只使用 `compose.production.yaml`，不要叠加 `compose.resources.yaml`：后者用于双资源实例/NFS，默认资源入口 8443 还会与本方案的聊天入口冲突。

| 入口 | 转发目标 | 用途 |
|---|---|---|
| `https://api.deepecho.top` | `127.0.0.1:18080` → Gate `8080` | 登录、注册 |
| `https://api.deepecho.top/api/resources/v1/` | `127.0.0.1:18085` → Resource `8085` | 文件、图片、头像 |
| `chat.deepecho.top:8443`（TLS） | `127.0.0.1:18989` → Chat `8989` | 聊天与资源凭证 |

Resource 的 `50065` RPC 仅在 Docker 网络内使用 mTLS；Chat 通过 `resource1:50065` 调用它，Resource 回调 `chatserver1:50055`。
单机使用 `production-files` 持久卷和 `RequireNfs=false`，不要让旧 Chat 文件清理进程继续访问此卷。

## 1. 确认机器和 DNS

```sh
cat /etc/os-release
uname -m
docker compose version
free -h
swapon --show
df -h
```

当前容器内存上限合计为 1696 MiB（包含 Resource 的 768 MiB），还未计入宿主机、Docker 与 Nginx。
这些是上限而非实测常驻用量，但原来 1 GB 的预算无法覆盖它们；可从 2 GB 以上规格开始灰度评估，是否足够以真实负载及 OOM 监测为准。
资源目录可用空间不超过 128 MiB 时 readiness 会失败，应为附件、头像及备份预留额外磁盘空间。

本仓库 Linux 构建 preset 使用 x64，以下镜像命令要求服务器 uname -m 为 x86_64。
如果是 aarch64，先调整构建工具链，不能照用 amd64 镜像。

添加 A 记录：api.deepecho.top 和 chat.deepecho.top 都指向服务器公网 IPv4。
没有可用 IPv6 就不要添加 AAAA。聊天域名需要直接解析到服务器，不能通过仅支持 HTTP 的代理。
公网安全组开放 TCP 80、443、8443，保留当前 SSH 端口。数据库、Redis 和 RPC 不发布公网端口。

## 2. 在另一台有足够内存的 Docker 机器构建 Linux 镜像

先将当前代码同步到构建机器（包括尚未提交的资源服务文件），并初始化子模块。不要在 1 GB 服务器执行 C++/vcpkg 构建。
以下以 `release-resource-1` 为新版本示例，每次发布更换标签，并同步修改 `.env.production` 中的两个镜像名。

```sh
git submodule update --init --recursive
docker build --platform linux/amd64 -f deploy/docker/server.Dockerfile -t deepecho-server:release-resource-1 .
docker build --platform linux/amd64 -f deploy/docker/varify.Dockerfile -t deepecho-varify:release-resource-1 .
docker run --rm --entrypoint test deepecho-server:release-resource-1 -x /app/bin/resource_server
docker save -o deepecho-release-resource-1.tar deepecho-server:release-resource-1 deepecho-varify:release-resource-1
```

将 tar 和仓库部署文件通过现有 SSH 连接复制到服务器，再执行：

```sh
docker load -i deepecho-release-resource-1.tar
```

也可使用自己的私有镜像仓库，将 .env.production 的两个 IMAGE 改成已发布的版本标签或 digest。
示例本地标签不是已发布的公共镜像。每次发布换一个明确版本，保留上一个版本以便回退。

## 3. 配置生产密钥和真实 SMTP

```sh
cp .env.production.example .env.production
chmod 600 .env.production
openssl rand -hex 32
```

用编辑器填写 .env.production。对每一个数据库密码、Redis 密码、RPC token 分别生成独立随机值。
PROD_FILE_KEY 必须是 64 位十六进制，传给 Resource Server；迁移历史文件时填写旧主密钥，不能重新生成。
`PROD_RESOURCE_BASE_URL=https://api.deepecho.top/api/resources/v1` 由 Gate 在登录时下发，末尾不要加 `/`。
它必须是客户端可访问、证书可信的 HTTPS 地址，不能填写容器名、服务器回环地址或 HTTP 地址。
SMTP 使用邮件服务商提供的服务器地址、发送账号和授权凭据，示例默认 465 + TLS。
如果使用 587，则按服务商要求设置 PORT=587 和 SECURE=false，并确认支持 STARTTLS。

不要把 .env.production 当 shell 脚本 source。包含美元符号或 # 等字符的 SMTP 密码按 Compose .env 规则使用单引号。
内部 RPC 证书与公网证书相互独立。仅首次空环境执行：

```sh
sudo sh deploy/production/generate-internal-certs.sh
```

脚本遇到已有证书会退出，防止覆盖；CA 私钥不会挂载到业务容器。
妥善备份 run/production 和 .env.production。内部叶子证书一年有效，需到期前重新签发、重启业务服务；
公网证书自动续期不会更新内部证书。

已有部署只补签资源服务证书（如已有有效的 `resource.crt/key` 则跳过）：

```sh
sudo sh deploy/production/generate-resource-cert.sh
sudo openssl verify -CAfile run/production/certs/ca.crt run/production/certs/resource.crt
sudo openssl x509 -in run/production/certs/resource.crt -noout -ext subjectAltName
```

该脚本使用原 `run/production/ca-private/ca.key`，SAN 包含 `resource1` 和 `resource2`。
若 CA 私钥保存在离线机器，在该机器按同样目录签发，只将 `resource.crt/key` 传回服务器；不要重建 CA 使原证书失效。

## 4. 首次申请公网证书

以下以服务器尚未配置同名 Nginx 站点为前提；如已有站点，先备份并合并，不能覆盖整个 nginx.conf。

```sh
sudo apt-get update
sudo apt-get install -y nginx libnginx-mod-stream certbot
sudo mkdir -p /var/www/certbot
sudo cp deploy/production/nginx-bootstrap.conf /etc/nginx/sites-available/deepecho
sudo ln -s /etc/nginx/sites-available/deepecho /etc/nginx/sites-enabled/deepecho
sudo nginx -t
sudo systemctl reload nginx
sudo certbot certonly --webroot -w /var/www/certbot --cert-name api.deepecho.top -d api.deepecho.top -d chat.deepecho.top
```

首次申请会提示邮箱和服务条款。不要在 DNS 生效前运行，也不要在证书签发前加载正式 TLS 配置。
已存在链接时跳过 ln，不要使用强制覆盖。

签发成功后：

```sh
sudo cp deploy/production/nginx-http.conf /etc/nginx/sites-available/deepecho
sudo cp deploy/production/nginx-stream.conf /etc/nginx/deepecho-stream.conf
```

在 /etc/nginx/nginx.conf **最外层、http {} 外面**添加一次：

```nginx
include /etc/nginx/deepecho-stream.conf;
```

如果已有 stream {}，只合并文件中的 server {}，不要新增第二个 stream 块。
Ubuntu 的 libnginx-mod-stream 通过 modules-enabled 加载模块。

```sh
sudo nginx -t
sudo systemctl reload nginx
sudo install -d /etc/letsencrypt/renewal-hooks/deploy
printf '#!/bin/sh\nnginx -t && systemctl reload nginx\n' | sudo tee /etc/letsencrypt/renewal-hooks/deploy/deepecho-nginx.sh >/dev/null
sudo chmod 755 /etc/letsencrypt/renewal-hooks/deploy/deepecho-nginx.sh
sudo systemctl enable --now certbot.timer
sudo certbot renew --dry-run
```

续期使用 webroot，不需停止 Nginx；签发成功后 hook 重新加载证书。

## 5. 验证并启动

已有数据卷必须先完成第 6 节的停写、备份和迁移；MySQL 初始化脚本只对空卷自动执行。
先填齐所有环境变量，再运行；config -q 不打印展开后的密码：

```sh
docker compose --env-file .env.production -f compose.production.yaml config -q
docker compose --env-file .env.production -f compose.production.yaml up -d --no-build
docker compose --env-file .env.production -f compose.production.yaml ps
docker compose --env-file .env.production -f compose.production.yaml logs --tail=80
docker stats --no-stream
curl --fail http://127.0.0.1:18085/health/ready
# 无凭证应返回 401，而不是 Gate 的响应、404 或 502；不要用 -k 跳过证书验证。
curl -i https://api.deepecho.top/api/resources/v1/users/me/avatar
```

MySQL 首次初始化可能较慢。若出现 unhealthy、OOM 或循环重启，停止验收并查看对应容器日志，
不能靠不断重试上传/登录判断服务正常。Compose restart 不会重新读取环境变量，改配置后运行 up -d。

将 deploy/production/client.ini 内容用于新客户端的 config.ini，或者打包时使用：
-DCHAT_CLIENT_CONFIG_FILE=<仓库绝对路径>/deploy/production/client.ini。
不用 SSH 转发，关闭 AllowInsecure；客户端连接 HTTPS API，Status 下发 chat.deepecho.top:8443 + TLS。
生产客户端不要再使用测试的 http://127.0.0.1:8080 配置。
必须发布与本次服务端匹配的新客户端：Chat 登录要求 `resource_protocol_version=1`，旧客户端会被拒绝。
资源请求强制 HTTPS；聊天的 `AllowInsecure` 设置不能让资源请求降级为明文。

## 6. 迁移已有数据与回退

升级原单机部署时，保留 `name: deepecho-production` 和原项目名；若原先使用 `-p`，后续所有命令也使用同一个项目名，否则会创建另一套空数据卷。
先在原部署仍完整时记录镜像标签、实际卷名及挂载点，备份 `.env.production`、部署配置、Nginx 配置与 `run/production`，再安排停写窗口。

```sh
docker compose --env-file .env.production -f compose.production.yaml stop gate chatserver1 status varify
# 若此前已运行资源服务，还要停止 resource1，以及其他旧文件写入/清理实例。
mkdir -p backups
chmod 700 backups
umask 077
docker compose --env-file .env.production -f compose.production.yaml exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysqldump -uroot --single-transaction --routines --triggers --events --no-tablespaces --set-gtid-purged=OFF --databases wgt' > backups/wgt-before-resources.sql
test -s backups/wgt-before-resources.sql
```

备份命令应成功退出，且需在隔离库验证可恢复；同时对停止写入后的原文件卷做完整备份。确认新 Resource 的 `production-files` 实际指向原附件卷。
如原来是绑定目录或不同卷名，应修改 Resource 的卷映射或停写后复制文件，不能只启动一个空卷。保持原 `PROD_FILE_KEY`，并抽样核对历史附件的解密和 SHA-256。
不要直接复制运行中的 MySQL 数据目录。若从 demo 迁移，应先逻辑导入到目标库；不要复制测试 Redis 的节点和登录状态，让用户重新登录。

已有库先检查是否已完成资源迁移：

```sh
docker compose --env-file .env.production -f compose.production.yaml exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot wgt' <<'SQL'
SHOW COLUMNS FROM file_transfer LIKE 'idempotency_key';
SHOW INDEX FROM file_transfer WHERE Key_name = 'uk_file_sender_idempotency';
SHOW COLUMNS FROM user LIKE 'avatar%';
SHOW TABLES LIKE 'resource_%';
SQL
```

若均未存在，且备份已验证，在所有旧写入与清理进程停止后执行一次：

```sh
docker compose --env-file .env.production -f compose.production.yaml exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot wgt' < database/migrations/002_resources.sql
```

该 SQL **不是幂等脚本**，不要重复执行。若已经有部分列或表，说明版本不同或迁移中断，需逐项核对迁移文件再修复；MySQL DDL 失败不会自动回滚先前的 DDL。
全新空卷会按顺序自动执行 `schema.sql` 和 `002_resources.sql`，无需再次手动迁移。

然后补签资源证书、更新镜像标签/资源 URL、合并新版 Nginx 资源路由，执行 `nginx -t` 后 reload，再按第 5 节启动并验收。
旧文件是否可下载仍受数据库记录的到期时间约束；切换前先在备份副本验证历史文件兼容性。

备份至少包含：数据库逻辑导出、production-files、.env.production、内部 CA/证书。
做一次隔离恢复验证，并将备份保存到服务器之外。
镜像升级前记录旧标签和备份。本次跨资源协议升级不能只改回 IMAGE 标签：先停止新 Gate/Chat/Resource，恢复成套的旧 Compose、INI、Nginx 配置及匹配的客户端，确认旧 Chat 的文件卷与主密钥挂载也恢复后再启动。
数据库或文件格式不兼容时，按恢复演练结果恢复升级前成套备份；升级后的新增数据需单独保留并评估，不能默认镜像回退等于数据回退。不要让新 Resource 与旧 Chat 文件清理进程同时运行。
任何环境都不要为“重启”执行 down -v，它会删除数据卷。

## 7. 上线验收

两台客户端分别登录两个账号，验证：

- 注册验证码真实送达、错误密码/网络超时有反馈；
- 添加好友，接收方在线和重新登录后都能看到邀请；
- 双向文字、图片和文件，发送方/接收方均可见，下载校验正确；
- 上传、更换头像，另一账号能看到新头像；资源请求通过 443，聊天仍通过 8443；
- 上传失败后能够重新发送，断网/重连后历史文件行为符合预期；
- 重启业务容器和服务器后数据仍在、服务能恢复；
- 单独重启 resource1 后，重新登录/重试可恢复资源操作，历史附件及头像仍可读取；
- 整个过程监测内存、swap、磁盘和容器重启次数。

本轮部署配置不代表此前文件传输故障已经完成双机验证。上述验收通过后再公开提供下载。

参考：Docker Compose 服务配置 https://docs.docker.com/reference/compose-file/services/
；Certbot webroot/续期 https://certbot.eff.org/instructions 。
