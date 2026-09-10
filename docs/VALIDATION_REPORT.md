# 可观测性升级验证记录

最近更新：2026-09-07。候选工作树使用 Windows 11、MSVC 19.41、CMake Release。未在本轮重跑的历史验证保留其原始结果。

| 验证项 | 结果 | 证据 |
|---|---:|---|
| LogSystem Windows/MSVC | PASS | `v1.0.0`：并发、JSON 转义、过滤、溢出、排空、轮转、格式化兼容测试 1/1 通过 |
| 三个 C++ 服务及工具链接 | PASS | `gate_server`、`status_server`、`chat_server`、`chat_loadgen`、`log_benchmark` |
| 聊天仓库测试 | PASS | 2026-09-07 Windows Release CTest 16/16，包括文本消息服务失败注入、附件重复/冲突/截断恢复、投递窗口、会话分片、限速与压测协议回归 |
| Qt 客户端测试 | PASS | 上一轮 CTest 3/3，包含 SQLite 重启恢复、重复投递和 UI smoke；本轮未修改客户端 |
| 压测协议回归 | PASS | `python tests/benchmark_protocol_test.py build/windows-server-release/bin/chat_loadgen.exe`：4/4，覆盖登录/读写超时、拒绝响应、ID 不匹配和未消费帧 |
| Python 脚本语法 | PASS | `scripts/` 与 `tests/e2e/` 共 5 个脚本通过 `ast.parse` |
| Python E2E 客户端语法 | PASS | `python -m py_compile tests/e2e/chat_e2e.py` |
| Node 日志接入语法 | PASS | `node --check` |
| Docker 双节点故障场景 | NOT RUN | 当前验证机未安装 Docker；`scripts/demo.sh` 和 `scripts/fault-test.sh` 应由 Linux CI/开发机生成 JSON、JUnit、Markdown 实测制品 |

未执行项目明确标记为 `NOT RUN`，不以静态检查冒充实测结果。
