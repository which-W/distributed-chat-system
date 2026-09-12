# deepecho.top：Ubuntu 24.04 + Docker 单节点部署

适用于现有 Docker Compose v2。以下命令均在 Linux 上执行，并以仓库根目录为当前目录。
这是小规模上线配置，尚未在你的服务器运行验证。2 核 1 GB 没有高可用保证，建议先灰度验收。
Compose 不在服务器编译，Nginx/Certbot 安装在宿主机；六个业务/存储容器由 Docker 管理。

## 1. 确认机器和 DNS

```sh
cat /etc/os-release
uname -m
docker compose version
free -h
swapon --show
df -h
```

本仓库 Linux 构建 preset 使用 x64，以下镜像命令要求服务器 uname -m 为 x86_64。
如果是 aarch64，先调整构建工具链，不能照用 amd64 镜像。

添加 A 记录：api.deepecho.top 和 chat.deepecho.top 都指向服务器公网 IPv4。
没有可用 IPv6 就不要添加 AAAA。聊天域名需要直接解析到服务器，不能通过仅支持 HTTP 的代理。
公网安全组开放 TCP 80、443、8443，保留当前 SSH 端口。数据库、Redis 和 RPC 不发布公网端口。

## 2. 在另一台有足够内存的 Docker 机器构建 Linux 镜像

先将当前代码同步到构建机器。不要在 1 GB 服务器执行 C++/vcpkg 构建。

```sh
docker build --platform linux/amd64 -f deploy/docker/server.Dockerfile -t deepecho-server:release-1 .
docker build --platform linux/amd64 -f deploy/docker/varify.Dockerfile -t deepecho-varify:release-1 .
docker save -o deepecho-release-1.tar deepecho-server:release-1 deepecho-varify:release-1
```

将 tar 和仓库部署文件通过现有 SSH 连接复制到服务器，再执行：

```sh
docker load -i deepecho-release-1.tar
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
PROD_FILE_KEY 必须是 64 位十六进制；迁移历史文件时填写旧主密钥，不能重新生成。
SMTP 使用邮件服务商提供的服务器地址、发送账号和授权凭据，示例默认 465 + TLS。
如果使用 587，则按服务商要求设置 PORT=587 和 SECURE=false，并确认支持 STARTTLS。

不要把 .env.production 当 shell 脚本 source。包含美元符号或 # 等字符的 SMTP 密码按 Compose .env 规则使用单引号。
内部 RPC 证书与公网证书相互独立：

```sh
sudo sh deploy/production/generate-internal-certs.sh
```

脚本遇到已有证书会退出，防止覆盖；CA 私钥不会挂载到业务容器。
妥善备份 run/production 和 .env.production。内部叶子证书一年有效，需到期前重新签发、重启业务服务；
公网证书自动续期不会更新内部证书。

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

先填齐所有环境变量，再运行；config -q 不打印展开后的密码：

```sh
docker compose --env-file .env.production -f compose.production.yaml config -q
docker compose --env-file .env.production -f compose.production.yaml up -d --no-build
docker compose --env-file .env.production -f compose.production.yaml ps
docker compose --env-file .env.production -f compose.production.yaml logs --tail=80
docker stats --no-stream
```

MySQL 首次初始化可能较慢。若出现 unhealthy、OOM 或循环重启，停止验收并查看对应容器日志，
不能靠不断重试上传/登录判断服务正常。Compose restart 不会重新读取环境变量，改配置后运行 up -d。

将 deploy/production/client.ini 内容用于新客户端的 config.ini，或者打包时使用：
-DCHAT_CLIENT_CONFIG_FILE=<仓库绝对路径>/deploy/production/client.ini。
不用 SSH 转发，关闭 AllowInsecure；客户端连接 HTTPS API，Status 下发 chat.deepecho.top:8443 + TLS。
生产客户端不要再使用测试的 http://127.0.0.1:8080 配置。

## 6. 迁移已有数据与回退

建议先用空生产库完成双账号验收，再决定是否保留测试账号。
如需迁移，安排停写窗口，停止 demo 的 gate/chat/varify 写入进程，保持其 MySQL 可用于导出。
使用 mysqldump 的一致性导出（single-transaction、routines、triggers、events），导入生产库。
同时复制加密文件卷，并保留原 PROD_FILE_KEY。不要把运行中 MySQL 的数据目录直接复制到新卷。
Redis 中的测试节点/登录状态不复制，让用户重新登录。核对数据库 schema 与当前代码再切换客户端。
旧文件是否可下载仍受数据库记录的到期时间约束。

备份至少包含：数据库逻辑导出、production-files、.env.production、内部 CA/证书。
做一次隔离恢复验证，并将备份保存到服务器之外。
镜像升级前记录旧标签和备份；回退时改回 IMAGE 标签并 up -d。涉及数据库结构变更时，镜像回退不等于数据回退。
任何环境都不要为“重启”执行 down -v，它会删除数据卷。

## 7. 上线验收

两台客户端分别登录两个账号，验证：
- 注册验证码真实送达、错误密码/网络超时有反馈；
- 添加好友，接收方在线和重新登录后都能看到邀请；
- 双向文字、图片和文件，发送方/接收方均可见，下载校验正确；
- 上传失败后能够重新发送，断网/重连后历史文件行为符合预期；
- 重启业务容器和服务器后数据仍在、服务能恢复；
- 整个过程监测内存、swap、磁盘和容器重启次数。

本轮部署配置不代表此前文件传输故障已经完成双机验证。上述验收通过后再公开提供下载。

参考：Docker Compose 服务配置 https://docs.docker.com/reference/compose-file/services/
；Certbot webroot/续期 https://certbot.eff.org/instructions 。
