# 性能测试与复现

## 聊天业务负载

当前 `chat_loadgen` 使用闭环请求：每连接等待上一批服务端接收回包后再发下一批。P50/P95/P99 表示 `delivery=accepted` 的响应延迟，不是接收方 ACK 或端到端送达延迟。它不能替代开放到达率压测，饱和时仍可能低估排队延迟。

通过 `scripts/benchmark.py` 为每轮创建新登录票据。标准场景预热 30 秒、测量 60 秒、3 轮；`--smoke` 为 3 秒预热、5 秒测量、1 轮。账号文件包含测试凭据，不应提交或作为报告上传。

```bash
python3 scripts/benchmark.py --prepare --accounts build/bench/accounts.json \
  --gate http://127.0.0.1:8080 --mailpit http://127.0.0.1:8025 \
  --loadgen build/linux-server-release/bin/chat_loadgen --label baseline
# 切换到候选服务端，使用相同账号和同一个负载生成器：
python3 scripts/benchmark.py --accounts build/bench/accounts.json \
  --loadgen build/linux-server-release/bin/chat_loadgen --label candidate
python3 scripts/compare-benchmarks.py build/bench/baseline build/bench/candidate
```

脚本拒绝覆盖账号文件或同名报告目录。每轮输出 schema 2 JSON 与资源采样；对比脚本拒绝消息失败、缺失记账、零成功、非法指标及工作负载不一致的结果。环境报告记录负载生成器与 Compose 文件 SHA-256、平台和可读取的 Git 版本/工作树状态；不能读取版本时明确标记 unavailable。Git 版本不能代替服务端镜像摘要，正式验收还应附上实际部署镜像摘要。

Compose 已为 MySQL 固定 1 CPU/1 GiB，两台 Chat 各 1 CPU/512 MiB，Gate、Status、Redis、Varify、Mailpit 各 0.5 CPU/256 MiB，负载生成器 1 CPU/512 MiB。这些是容器上限，并非专用 CPU 核。基线和候选必须使用相同宿主机、镜像依赖、数据库规模和日志级别；容器内没有 Docker CLI 时采样明确记为 unavailable，CI 另从宿主机采样。

当前尚无真实双节点业务性能数字。固定资源配置尚待 Docker 环境验收，不能据本机单元测试宣称吞吐提升。

## 日志微基准

`log_benchmark 200000` 的 schema 2 模式是 `equivalent_serialization_queue_sink`：同步和队列路径构造相同记录、使用相同序列化与最终 flush 策略，并检查实际写入数和丢弃数。它衡量序列化、队列和文件输出成本，不覆盖完整 Logger 调用链；flush 也不等于 fsync。

旧版不等价 flush/序列化路径产生的数据不再作为当前实现的依据。新实现需要在目标环境重复采样后再填写结果。
