# distributed-chat-system

基于 Qt、Boost.Asio、gRPC、Redis 和 MySQL 的分布式即时通信系统。

项目现在使用 **CMake 作为唯一 C++ 构建入口**。Visual Studio 工程、生成的 protobuf 文件、二进制产物和本机依赖均不再保存在仓库中。

## 组件

- `gate_server`：HTTP 注册、重置密码和登录网关，默认端口 `8080`。
- `status_server`：聊天节点发现、负载选择和 token 签发，默认 gRPC 端口 `5050`。
- `chat_server`：TCP 聊天节点及节点间 gRPC 服务；同一可执行文件使用不同配置启动多个实例。
- `VarifyServer`：Node.js 邮箱验证码 gRPC 服务，默认端口 `5000`。
- `chat_client`：Qt 桌面客户端，默认不参与服务器构建。

架构与调用关系见 [CODEGRAPH.md](CODEGRAPH.md)。

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

MySQL Connector/C++ 的 JDBC 兼容接口要求静态 vcpkg triplet，因此 Windows 服务器预设使用 `x64-windows-static`。

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

## 部署要点

需要开放或映射的默认端口：

| 端口 | 协议 | 服务 |
|---:|---|---|
| 8080 | HTTP | GateServer |
| 5000 | gRPC | VarifyServer |
| 5050 | gRPC | StatusServer |
| 8989 / 8990 | TCP | 两个 ChatServer 客户端入口 |
| 50055 / 50056 | gRPC | 两个 ChatServer 节点间入口 |

多主机部署时，修改 `config/status.ini` 中提供给客户端的聊天节点地址，以及两份 `config/chatserver*.ini` 中的 peer 地址。

## 提交到 GitHub

仓库源码和资源约 1–2MB；原目录超过 1.5GB 的原因是 PDB、ILK、OBJ、DLL、EXE、Qt 运行库和 `node_modules`。它们均已删除并由 `.gitignore` 阻止再次提交。

不要使用 Git LFS 保存构建产物。只有未来确实需要版本控制的大型模型、音视频或测试数据才考虑 Git LFS。

如果曾在其他 Git 仓库中提交过大文件，仅添加 `.gitignore` 不会缩小历史，需要使用 `git filter-repo` 清理历史；本仓库的远端目前只有初始提交，不需要重写历史。

## 安全提示

旧目录中曾存在明文 Redis、MySQL 和邮箱凭据。即使这些文件已经移除，也应在对应服务端立即轮换原凭据。生产环境建议进一步启用 TLS、密码哈希和 gRPC 身份认证。
