#pragma once
#include "CSession.h"
#include "MsgNode.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
typedef function<void(std::shared_ptr<CSession>, const short& msg_id, const string& msg_data)>
    FunCallBack;
class LogicWorker {
  public:
    LogicWorker();
    ~LogicWorker();
    bool PostTask(std::shared_ptr<LogicNode> task);
    void RegisterCallBacks();

  private:
    void task_callback(std::shared_ptr<LogicNode>);
    std::thread _work_thread;
    std::queue<std::shared_ptr<LogicNode>> _task_que;
    std::atomic<bool> _b_stop{false};
    static constexpr std::size_t QueueByteLimit = 16 * 1024 * 1024;
    std::size_t _queued_bytes = 0;
    std::mutex _mtx;
    std::condition_variable _cv;
    std::unordered_map<short, FunCallBack> _fun_callbacks;
};
