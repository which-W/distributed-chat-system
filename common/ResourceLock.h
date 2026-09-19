#pragma once
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace chat::resources {
inline bool validId(const std::string& id) {
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != '-') return false; }
        else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    }
    return true;
}
// 业务操作锁覆盖状态读取、密文落盘和数据库偏移提交；与存储层的记录锁分离。
// 在线清理不得删除该锁文件；Linux NFS 部署还须保证远程锁生效，并实际跨主机验证。
class ResourceLock {
public:
    ResourceLock(const std::filesystem::path& root, const std::string& id) {
        if (!validId(id)) throw std::invalid_argument("invalid resource id");
        auto path = root / (id + ".operation.lock");
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("resource lock unavailable");
#else
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd_ < 0) throw std::runtime_error("resource lock unavailable");
#endif
        // 限制锁竞争的重试时间；此期限不能中断底层文件系统本身发生的阻塞。
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
#ifdef _WIN32
            if (LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap_)) return;
            if (GetLastError() != ERROR_LOCK_VIOLATION) break;
#else
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) return;
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) break;
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < end);
        close();
        throw std::runtime_error("resource lock timeout");
    }
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
    ~ResourceLock() { close(); }
private:
    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
    }
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlap_{};
#else
    int fd_ = -1;
#endif
};
}
