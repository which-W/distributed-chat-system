# 从源码构建与运行

[返回文档导航](../README.md)

命令均在仓库根目录执行。需要 CMake 3.25+、Ninja、C++17 编译器、vcpkg 和 Node.js（CI 使用 22）。构建前运行：

```sh
git submodule update --init --recursive
```

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

初始化 ElaWidgetTools 子模块，并把 Qt 6.6.3 的 CMake 目录加入
`CMAKE_PREFIX_PATH` 后执行：

```bash
git submodule update --init --recursive
cmake --preset desktop-release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.6.3/gcc_64
cmake --build --preset desktop-release -j
```

客户端使用 ElaWidgetTools 的 Qt Widgets 私有接口，建议与 CI 一致使用 Qt 6.6.3。Windows 请在 MSVC Developer PowerShell 中构建，把路径替换为自己的安装位置：

```powershell
$env:VCPKG_ROOT = 'C:\tools\vcpkg'
cmake --fresh --preset desktop-release -DCMAKE_PREFIX_PATH=C:/Qt/6.6.3/msvc2019_64
cmake --build --preset desktop-release
& '.\build\desktop-release\bin\chat_tls_probe.exe'
```

本地明文联调时，在配置命令后添加 `-DCHAT_CLIENT_CONFIG_FILE=config/client.local.ini`。默认使用 `config/client.ini`，运行前检查网关地址。Windows 构建会运行 `windeployqt` 部署 Qt 运行库和 TLS 插件。`CMakeUserPresets.json` 只保存个人路径，不应提交，也不是新克隆仓库的构建前提。
## 配置

服务配置位于 `config/`。复制环境变量模板后，填写数据库、Redis、SMTP、内部 RPC 凭据、文件存储密钥并检查 TLS 证书路径：

```bash
cp .env.example .env
```

C++ 服务支持以下环境变量覆盖 INI：

- `CHAT_REDIS_HOST`、`CHAT_REDIS_PORT`、`CHAT_REDIS_PASSWORD`、`CHAT_REDIS_USER`
- `CHAT_MYSQL_HOST`、`CHAT_MYSQL_PORT`、`CHAT_MYSQL_PASSWORD`、`CHAT_MYSQL_USER`、`CHAT_MYSQL_SCHEMA`
- `CHAT_LOG_LEVEL`、`CHAT_LOG_DIR`、`CHAT_LOG_CONSOLE`、`CHAT_LOG_FILE_ENABLED`
- `CHAT_LOG_MAX_FILE_MB`、`CHAT_LOG_MAX_FILES`、`CHAT_LOG_QUEUE_CAPACITY`

三个 C++ 服务使用固定在 `v1.0.0` 的 [LogSystem](https://github.com/which-W/LogSystem) 子模块输出 JSON Lines；Node 验证码服务输出相同核心字段。禁止记录密码、验证码、登录票据、RPC token、邮件/聊天正文及文件内容，邮箱只使用不可逆短摘要。验证入口和当前缺失项见 [开发指南](../DEVELOPMENT.md)。

验证码服务使用 `VARIFY_*` 环境变量，完整列表见 `.env.example`。`.env`、旧 `config.ini` 和 `VarifyServer/config.json` 已被 `.gitignore` 排除。

本地数据库容器使用 `.env.compose.example`，步骤见 [开发指南](../DEVELOPMENT.md)。C++ 与 Node 服务的 Redis 凭据必须一致，并需补齐 SMTP 配置。`run_all` 会读取根目录 `.env`；直接运行服务时需自行导出环境变量。`.env.example` 默认启用 mTLS，需要提供证书。

使用已有 MySQL 时初始化数据库（在 Bash 或 cmd 中执行）：

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

