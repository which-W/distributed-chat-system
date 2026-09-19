# Resource Server 本地回归测试报告

日期：2026-09-14

## 本轮修改

- 下载取消同时中止活动 HTTP 请求、等待凭证的请求和延迟重试；保留远端附件、部分下载文件与重试入口。
- 退出先更新请求代次，再清空身份、凭证、传输状态和头像内存缓存，阻止旧回调影响新账号。
- 资源凭证响应匹配真实会话 UID 与本轮 request_id，拒绝旧账号和旧续领响应；客户端与 ChatServer 需同步升级。
- 补充上述状态转换、缓存隔离、锁作用范围、密文持久化与测试场景的中文注释。
- 登录测试在认证断言结束后才初始化聊天工作区，消除不必要的工作区访问。

## 执行结果

环境：Windows、MSVC 2022、Qt 6.6.3，本地文件系统。网络边界测试替换 QNetworkAccessManager 的传输层，使用真实请求管理、传输与缓存逻辑，不访问线上服务。

| 测试 | 结果 | 覆盖范围 |
| --- | --- | --- |
| client_resource_boundaries | 通过，24 项断言 | 取消、续传、排队取消、退出隔离、凭证响应关联、延迟重试取消、校验后确认 |
| resource_store_recovery | 通过 | 持久化重试、内容冲突、空文件、末尾短片、锁、残缺记录、篡改、独立进程退出与并发重放 |
| client_auth | 通过 | 注册响应、登录失败后的重试状态 |
| message_store | 通过 | 既有本地消息存储回归 |

客户端三项 CTest 总耗时约 2.45 秒；密文恢复约 0.21 秒。chat_client、chat_workspace_test、chat_server、resource_server、resource_store_test 已构建成功。服务端曾因过期对象仍引用旧的非虚函数符号而链接失败，重编相关对象后通过。

修复前，资源边界测试复现四项失败：取消未中止 HTTP、取消后重试入口丢失、等待凭证的已取消请求仍被发送、退出未清空身份和端点。修复后这四项及新增断言全部通过。

## 重现命令

在已配置 MSVC、Qt 与依赖的开发终端执行：

```powershell
cmake --build build/desktop-release --target chat_workspace_test chat_client -j 4
ctest --test-dir build/desktop-release -R "client_auth|message_store|client_resource_boundaries" --output-on-failure
cmake --build build/windows-server-release --target resource_store_test chat_server resource_server -j 4
ctest --test-dir build/windows-server-release -R resource_store_recovery --output-on-failure
```

日志：`build/resource-boundary-before.log`、`build/resource-boundary-after.log`、`build/resource-recovery-after.log`。CTest 的详细断言见对应构建目录的 `Testing/Temporary/LastTest.log`；后续测试会覆盖此日志。

## 验证边界与待办

- 独立进程在密文 append 持久化后直接退出，不执行析构；两个后续进程重放同一偏移，并验证冲突内容拒绝、长度不增长和原内容可解密。
- 此测试未接入 MySQL，不能证明真实数据库 CAS、事务提交和响应窗口的端到端恢复。
- 未进行两台 Linux NFS 客户端的互斥、持久化、NFS 重启恢复测试，也未运行线上吞吐与聊天 p95 压测。
- 本轮未运行完整聊天工作区与 UI smoke 套件；结果只覆盖上表测试，不代表所有部署验收项完成。
- 执行中 C 盘再次耗尽，后续构建临时目录改到 F 盘 `build/task-temp`。继续使用默认沙箱前仍需释放 C 盘空间。