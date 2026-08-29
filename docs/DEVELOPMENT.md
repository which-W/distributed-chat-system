# 开发基线

## 全新构建

项目支持的配置命令统一使用 CMake 的 `--fresh` 模式。该模式会丢弃已有的
`CMakeCache.txt`，避免复用其他机器或工具链留下的绝对路径缓存。

在 Windows 开发者命令提示符中构建服务端：

```powershell
scripts\build-windows-server.cmd
```

使用其他预设：

```powershell
pwsh scripts/configure-fresh.ps1 -Preset desktop-release
```

```sh
sh scripts/configure-fresh.sh linux-server-release
```

## 测试

CTest 包含 C++ 密码哈希测试；系统安装 Node 后，还会运行 VarifyServer 测试套件：

```sh
ctest --test-dir build/linux-server-release --output-on-failure
cd VarifyServer
npm ci
npm run deps:check
npm run check
npm test
```

## 本地开发用 MySQL 和 Redis

Compose 只把数据库端口绑定到回环地址，并要求显式设置本地凭据。复制示例后，
请把所有 `replace-with-...` 占位值替换为各自独立的随机值；不要在共享环境或生产环境复用。

```sh
cp .env.compose.example .env
docker compose up -d
docker compose ps
```

MySQL 会通过 `database/schema.sql` 初始化。Redis 已启用 AOF 持久化和密码认证。
如需从空数据卷重新初始化数据库，必须先确认本地开发数据可以删除，再执行：

```sh
docker compose down -v
```

## 静态分析和编译警告

常规构建在 MSVC 上启用 `/W4 /permissive-`，在 GCC/Clang 上启用
`-Wall -Wextra -Wpedantic -Wshadow`。清理完存量警告后，可以启用以下严格选项：

```sh
cmake --fresh --preset linux-server-release \
  -DCHAT_ENABLE_CLANG_TIDY=ON \
  -DCHAT_WARNINGS_AS_ERRORS=ON
```

格式化和静态分析规则分别位于 `.clang-format` 和 `.clang-tidy`。

## 安全行为

- 新密码使用 libsodium Argon2id 哈希存储。Gate 启动时通过条件更新迁移历史明文
  密码，用户登录时还会进行兜底升级。确认数据库中不再存在明文密码后，应删除兼容
  迁移路径。
- 验证码使用密码学安全随机数生成六位数字，有效期五分钟；发送冷却一分钟，并限制
  每小时发送次数。验证码会被原子消费，连续输错五次后立即失效。
- Status 签发随机、绑定目标 Chat 节点、有效期 60 秒的一次性票据。Chat 会原子消费
  票据，防止重复使用。
- 已认证的聊天处理器从服务端 Session 获取操作人 UID，请求中的 `uid` 和
  `fromuid` 不参与授权判断。

CI 覆盖 Linux Server Release、Windows Client Release、CTest、Node 依赖检查、
Node 语法与测试以及 C++ 格式检查。

双机拓扑、健康心跳、防火墙边界和进程配置选择请参见
`docs/TWO_SERVER_DEPLOYMENT.md`。
