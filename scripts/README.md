# 脚本导航

以下命令均在仓库根目录执行。

| 用途 | 脚本 |
| --- | --- |
| 全新配置构建 | `configure-fresh.sh` / `configure-fresh.ps1` |
| Windows 服务端构建 | `build-windows-server.cmd` |
| 启动和停止本地服务 | `run-all.sh` / `run-all.ps1`、`stop-all.sh` / `stop-all.ps1`，也可通过 CMake 的 `run_all` / `stop_all` 目标调用 |
| 开发用 gRPC 证书 | `generate-dev-certs.sh` |
| Windows 安装包 | `package-client.ps1`，见 [打包指南](../docs/guides/client-installer.md) |
| 双节点演示 | `demo.sh`，运行 `tests/e2e/chat_e2e.py` 的真实 Gate/Chat 冒烟测试 |
| 故障与性能验证 | `fault-test.*`、`benchmark.*`、`compare-benchmarks.py`；依赖演示环境或相应测试模块，使用前检查脚本参数和依赖 |

脚本路径被 CMake、Compose 和部署流程引用，保持现有位置。Python 缓存和运行报告不属于源码，报告统一输出到 `build/` 下。
