# Windows 离线安装包

第一版通过 EXE 安装包分发，不依赖更新服务器，不下载或执行远程升级脚本。
用户收到安装包后双击安装；使用聊天功能仍然需要联网访问聊天服务。

## 用户行为

- 安装到当前用户的 `%LOCALAPPDATA%\Programs\NebulaChat`，不需要管理员权限。
- 创建开始菜单入口，可选择创建桌面快捷方式，支持 Windows 应用列表卸载。
- 首次安装内置 `https://api.deepecho.top`，严格验证 HTTPS/TLS。
- `chat.deepecho.top` 及其端口、TLS server name 由登录响应下发，安装器不重复配置。
  服务端必须返回用户可访问的地址、正确端口，以及有效的 TLS 证书。
- 安装和卸载前提示关闭正在运行的客户端，不强制结束文件传输或数据库写入。
- 新版安装包沿用固定 AppId，覆盖升级；允许同版本重装，拒绝旧版本覆盖新版。
- 升级保留安装目录内已有的 `config.ini`；卸载也保留它。修改发布默认网关不会覆盖
  已有用户的配置，需要明确迁移。安装时选择的目录应专用于 NebulaChat。
- 消息数据库在 Qt `AppLocalDataLocation` 下，主题和代理在用户 QSettings 中。
  安装器不删除这些用户数据。重新安装可能继续使用此前保留的设置。

## 开发机准备

使用现有 Qt 6.6.3、MSVC x64、CMake 和 Ninja，另安装
[Inno Setup 6.3 或更高的 6.x 版本](https://jrsoftware.org/isdl.php)。
无需在用户电脑安装 Qt、CMake、Inno Setup 或 Visual Studio。
打包使用 app-local MSVC x64 CRT DLL，Windows 10/11 用户无需另外运行提权运行库安装器。

在 **x64 Native Tools Command Prompt / Developer PowerShell for VS 2022** 中运行。
首次构建先按本机 Qt 路径配置，例如：

```powershell
cmake -S . -B build/client-package -G Ninja `
  -DCHAT_BUILD_CLIENT=ON -DCHAT_BUILD_SERVERS=OFF `
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=G:/QT/6.6.3/msvc2022_64

.\scripts\package-client.ps1 -BuildDir build/client-package
```

已有配置好的 `build/desktop-release` 时：

```powershell
.\scripts\package-client.ps1
```

可显式指定配置和工具路径（带空格的路径需要引号）：

```powershell
.\scripts\package-client.ps1 `
  -BuildDir build/desktop-release `
  -ConfigFile config/client.release.ini `
  -IsccPath 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' `
  -VcRuntimeDir 'D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC\14.40.33807\x64\Microsoft.VC143.CRT'
```

`-StageOnly` 仅构建、收集并校验运行目录，不要求安装 Inno Setup。
脚本默认用仓库内 `config/client.release.ini`，不会打包构建目录中的本地联调配置。
它拒绝示例域名、回环地址、明文网关和 `AllowInsecure=true`。
这只检查配置格式，不代表线上接口、证书或用户登录已验收。

## 产物和版本

版本唯一来源为根 `CMakeLists.txt` 的 `project(ChatProject VERSION 1.0.0 ...)`。
修改版本后重新打包，CMake 会生成客户端版本资源和打包元数据，脚本检查 EXE 版本匹配。
第一版沿用工程版本，不另维护容易不同步的版本文件。

每次生成独立目录，避免误打包历史文件：

```text
build/packages/1.0.0/<时间戳-随机后缀>/
  app/                                  独立运行目录
  package-manifest.json                  文件列表、版本、哈希、网关
  NebulaChat-1.0.0-windows-x64-setup.exe   发给用户的离线安装包
  NebulaChat-1.0.0-windows-x64-setup.exe.sha256
```

打包只从 CMake 指定的客户端/Ela 目标和资源目录收集文件，不复制测试程序、日志、数据库、
截图或整个开发输出目录。运行目录包括 Qt 插件、SQLite 驱动、Windows Schannel TLS 后端
及 MSVC CRT。安装包自包含，不会在安装过程中联网拉取运行依赖。

第一版安装包未进行发布者代码签名，Windows 可能显示未知发布者或 SmartScreen 提示。
SHA-256 用于比对文件完整性，不证明发布者身份。生产分发可在生成 EXE 后使用受控证书签名，
并重新生成最终文件的 SHA-256。安装脚本不提供绕过系统安全检测的设置。

## 发布前验收

1. 在没有 Qt、Visual Studio 的 Windows 10/11 x64 测试机上安装并启动。
2. 验证注册/登录，确认服务端返回 `chat.deepecho.top` 及可访问端口，并验证聊天和文件传输。
3. 修改代理/主题、产生消息后，关闭客户端，运行同版本及更高版本安装包，确认设置和消息保留。
4. 客户端运行时安装/卸载应提示关闭；旧版安装包覆盖新版应被拒绝。
5. 卸载后程序和快捷方式消失，配置和聊天数据保留。

这里不包含自动更新、增量包、数据库 schema 迁移或自动回滚。未来修改消息库结构时必须
单独设计迁移；不能通过卸载新版再安装旧版绕过数据兼容性要求。
