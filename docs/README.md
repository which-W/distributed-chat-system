# 文档导航

[返回项目首页](../README.md)

## 入门与开发

- [从源码构建与运行](guides/build.md)：Linux、Windows、Qt、环境变量和启停方式。
- [开发指南](DEVELOPMENT.md)：本地依赖、验证命令与静态分析。
- [系统架构](ARCHITECTURE.md)：节点协作、登录与消息投递。
- [优化实施报告](OPTIMIZATION_REPORT.md)：修改点、预期效果与已完成验证。
- [贡献指南](../CONTRIBUTING.md)：反馈问题和提交修改。

## 部署与分发

- [单机生产部署](../deploy/production/README.md)：安装、升级、资源服务和数据库迁移。
- [双机部署](deployment/two-server.md)：拓扑、节点地址与防火墙边界。
- [会话恢复与部署](SESSION_RECOVERY.md)：新接口、资源租约与故障处理。
- [公网 HTTPS 与 Chat TLS](deployment/public-edge-tls.md)。
- [内部 gRPC TLS/mTLS](deployment/grpc-tls.md)。
- [Windows 客户端安装包](guides/client-installer.md)。
- [Xray 备用入口](deployment/xray-backup-entry.md)：按需使用的高级部署方案。

## 配置入口

以下路径均相对仓库根目录。Compose 文件保留在根目录，以保持现有脚本和默认路径解析一致。

| 文件 | 用途 |
| --- | --- |
| `compose.yaml` / `.env.compose.example` | 本地开发依赖，具体服务以 Compose 配置为准 |
| `compose.production.yaml` / `.env.production.example` | 生产部署 |
| `compose.resources.yaml` | 资源服务相关容器配置 |
| `compose.demo.yaml` | 双节点演示环境，通过 `tests/e2e/chat_e2e.py` 验证会话恢复 |
| `.env.example` | 原生服务环境变量参考，包含 mTLS 配置 |
| `config/` | 服务与客户端 INI 配置 |
| `deploy/` | Dockerfile、反向代理、证书脚本和部署模板 |

## 工具与文档维护

常用脚本见 [脚本导航](../scripts/README.md)。性能入口位于 `scripts/benchmark.sh`，执行前检查依赖和参数，不将工具存在等同于性能结果已经验证。

日常说明放在 `guides/`，部署专题放在 `deployment/`。临时日志、截图和测试输出放在 `build/` 下；需要长期保留的测试报告应写明日期、版本和运行条件，并与操作指南分开。新增或移动文档时，同步更新本页与引用链接。
