# 原单机部署一键迁移到 Resource Server

适用于 Ubuntu/Linux 上按旧版 `compose.production.yaml` 部署的六个服务：MySQL、Redis、Varify、Status、ChatServer1、Gate。保留原账号、数据库、文件卷、密码及文件主密钥。执行期间会暂时停止登录和聊天。

**不要先把新版 compose.production.yaml 或 .env.production.example 覆盖到服务器。** 脚本需要从旧配置及正在运行的旧容器确认项目名、密钥和数据挂载。它会自动生成新 Compose，保留已有的其他配置。自定义多机、多 Chat、已经添加 Resource 的部署不自动处理。

## 1. 在 Windows 电脑生成完整迁移包

要求：当前完整仓库（含资源服务源码和已初始化的子模块）、Docker Desktop 的 Linux 容器模式、足够的构建内存和磁盘。服务器需为 x86_64。

在仓库根目录的 PowerShell 执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy\production\build-resource-migration.ps1
```

输出在 `build/resource-migration-日期时间.tar.gz`，包含新版服务端镜像和迁移工具。不包含密码或生产证书。首次 C++/vcpkg 构建可能较久；构建完成前无需停服。

如果已经构建了对应的新镜像：

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy\production\build-resource-migration.ps1 -SkipBuild -Image deepecho-server:release-resource-1
```

这是完整包的生成方式。单独的源码工具包不包含服务镜像，不能直接当完整包执行。

## 2. 上传到服务器的独立目录

用 FinalShell/SFTP/其他现有 SSH 工具上传压缩包到 `/tmp`。登录服务器，创建一个尚不存在的新目录，然后解压：

```bash
mkdir ~/resource-upgrade
tar -xzf /tmp/resource-migration-实际日期时间.tar.gz -C ~/resource-upgrade
cd ~/resource-upgrade
```

解压位置不要是原项目目录。把压缩包文件名替换成实际名字。

## 3. 找到旧部署目录

```bash
docker compose ls
```

找到原 `compose.production.yaml` 的位置。例如显示 `/opt/distributed-chat-system/compose.production.yaml`，后面的 `--deploy-dir` 就填写 `/opt/distributed-chat-system`。

旧服务此时应该正在运行。保持原 `.env.production`、证书和旧 Compose，**不用手动改镜像标签，不用生成新 PROD_FILE_KEY**。

## 4. 预检查（不停止服务、不导入镜像、不执行 SQL）

```bash
sudo python3 migrate-resource.py --deploy-dir /opt/distributed-chat-system
```

如果 API 域名或 Nginx 配置文件不同，增加参数；正式执行时也使用相同参数：

```bash
sudo python3 migrate-resource.py \
  --deploy-dir /opt/distributed-chat-system \
  --resource-url https://你的API域名/api/resources/v1 \
  --nginx-site /etc/nginx/sites-available/你的站点文件
```

默认站点为 `/etc/nginx/sites-available/deepecho`，默认 API 为 `api.deepecho.top`。程序要求站点文件里有唯一匹配域名的 `listen 443 ssl` server 块；不会覆盖已有资源路由或不认识的自定义配置。

预检查通过后再执行下一步。预检查不验证尚未导入的镜像、实际数据库恢复或公网请求；正式执行会进一步检查镜像和公网 TLS 入口。

## 5. 一条命令迁移

```bash
sudo python3 migrate-resource.py \
  --deploy-dir /opt/distributed-chat-system \
  --image "$(cat image-name.txt)" \
  --image-tar "$PWD/server-image.tar" \
  --apply
```

根据实际情况沿用第 4 步的域名和站点参数。这一步会：

1. 导入并验证新镜像，再进入停服阶段。
2. 备份旧 Compose、环境文件、部署配置、内部证书、Nginx 配置和原镜像标识。
3. 停止原业务服务，保留 MySQL/Redis；备份数据库及附件目录。
4. 用原 CA 补签资源证书；已有完整有效证书则沿用。
5. 检查数据库：未迁移时执行 `002_resources.sql`；已完整存在时跳过；部分迁移时停止。
6. 更新 Compose，将原真实文件卷交给 Resource；MySQL/Redis 的真实卷名也固定为现有卷。
7. 等待 Resource 健康后，向原 Nginx HTTPS 站点添加资源路由；语法检查通过才 reload。
8. 启动新版业务服务，并验证资源公网入口对无凭证请求返回 401。

备份保存在原项目的 `backups/before-resource-日期时间/`。不会删除旧数据卷，不会改 `.env.production`，不会重建内部 CA。
迁移后的 `compose.production.yaml` 采用 JSON 写法（Compose 支持），保留环境变量表达式并设为仅所有者可读写；原文件备份完整保留。日常命令不变：

```bash
cd /opt/distributed-chat-system
sudo docker compose --env-file .env.production -f compose.production.yaml ps
sudo docker compose --env-file .env.production -f compose.production.yaml logs --tail=80 resource1
```

## 6. 迁移后使用

安装与当前仓库匹配的新桌面客户端。旧客户端不支持资源协议，不能继续用于本次升级。
测试原账号登录、双账号聊天、新文件上传下载、头像更新，以及仍在有效期内的历史附件下载和内容校验。
迁移脚本的成功不等于这些业务验收已通过。

将备份复制到服务器之外。脚本检查数据库导出进程是否成功，不会自动启动另一套 MySQL 验证恢复；重要数据建议先用备份副本演练迁移。
当前单机模板容器内存上限合计 1696 MiB，另需系统内存；原来 1 GB 的机器应先评估或扩容。磁盘需要容纳镜像、现有文件及完整备份。

## 出错时

脚本会打印出错阶段与备份路径，状态保存在原目录 `.resource-migration-state.json`。发生错误后不自动重试 SQL、不删除数据、不自动把旧文件清理程序启动回来。不要删除状态文件强行重跑。

把终端错误、状态文件中的 `stage` 和以下日志发给维护者：

```bash
cd /opt/distributed-chat-system
sudo docker compose --env-file .env.production -f compose.production.yaml ps -a
sudo docker compose --env-file .env.production -f compose.production.yaml logs --tail=80
```

不要发送 `.env.production`、密钥、数据库备份或包含密码的完整 Docker inspect/config 输出。不要执行 `down -v`。
回退需要恢复匹配的旧 Compose/配置/客户端并判断数据库及文件兼容性，不能仅改旧镜像标签；必要时从成套备份恢复，先保留升级后产生的数据。

## 工具验证范围

已通过 Python/PowerShell 语法检查及 9 项离线回归测试，覆盖配置生成、卷和密钥保留、预检查、重复执行、部分数据库迁移、备份失败和 Nginx 失败恢复。测试模拟 Docker/MySQL/Nginx，没有访问生产服务器，不代替实际 Linux 容器迁移与数据恢复演练。

开发者可在仓库根目录重跑：

```bash
python -m unittest discover -s deploy/production -p test_migrate_resource.py -v
```
