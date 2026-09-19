#include "EncryptedFileStore.h"

#include <sodium.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {
constexpr char Magic[] = "CHATFILE1";
constexpr std::size_t HeaderBytes = 9 + 16;
constexpr std::size_t TagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;

// 共享目录可能同时被多个资源实例使用，原生文件锁用于保护单个密文附件的记录读写。
class CrossProcessFileLock {
  public:
    CrossProcessFileLock(const std::filesystem::path& path, bool shared) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("cannot open attachment lock");
        const DWORD flags = shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK;
        if (!LockFileEx(handle_, flags, 0, MAXDWORD, MAXDWORD, &overlapped_)) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("cannot lock attachment");
        }
#else
        descriptor_ = ::open(path.c_str(), O_RDWR);
        if (descriptor_ < 0 || ::flock(descriptor_, shared ? LOCK_SH : LOCK_EX) != 0) {
            if (descriptor_ >= 0)
                ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("cannot lock attachment");
        }
#endif
    }

    ~CrossProcessFileLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped_);
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

    CrossProcessFileLock(const CrossProcessFileLock&) = delete;
    CrossProcessFileLock& operator=(const CrossProcessFileLock&) = delete;

  private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_{};
#else
    int descriptor_ = -1;
#endif
};

std::uint32_t readLength(std::ifstream& input) {
    unsigned char bytes[4]{};
    input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!input)
        throw std::runtime_error("encrypted attachment record is truncated");
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
}

void writeLength(std::ofstream& output, std::uint32_t value) {
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

// 关闭 C++ 文件流后、仍持有附件锁时执行，只有持久化成功才能向上层返回确认偏移。
void syncFile(const std::filesystem::path& path) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot open attachment for durable flush");
    const bool flushed = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY);
    if (descriptor < 0)
        throw std::runtime_error("cannot open attachment for durable flush");
    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
#endif
    if (!flushed)
        throw std::runtime_error("attachment durable flush failed");
}

void closeAndSync(std::ofstream& output, const std::filesystem::path& path) {
    output.flush();
    output.close();
    if (!output)
        throw std::runtime_error("attachment write failed");
    syncFile(path);
}
} // namespace

EncryptedFileStore::EncryptedFileStore(std::filesystem::path root,
                                       const std::string& master_key_hex)
    : root_(std::move(root)) {
    if (sodium_init() < 0 || master_key_hex.size() != master_key_.size() * 2 ||
        sodium_hex2bin(master_key_.data(), master_key_.size(), master_key_hex.c_str(),
                       master_key_hex.size(), nullptr, nullptr, nullptr) != 0) {
        throw std::runtime_error("CHAT_FILE_STORAGE_KEY must be 64 hexadecimal characters");
    }
    std::filesystem::create_directories(root_);
}

std::filesystem::path EncryptedFileStore::pathFor(const std::string& transfer_id) const {
    // 文件 ID 只能是服务端生成的 UUID，绝不将客户端文件名拼接到磁盘路径。
    if (transfer_id.size() != 36 ||
        transfer_id.find_first_not_of("0123456789abcdef-") != std::string::npos) {
        throw std::invalid_argument("invalid transfer id");
    }
    return root_ / (transfer_id + ".enc");
}

std::array<unsigned char, 32>
EncryptedFileStore::transferKey(const std::string& transfer_id) const {
    std::array<unsigned char, 32> result{};
    crypto_generichash(result.data(), result.size(),
                       reinterpret_cast<const unsigned char*>(transfer_id.data()),
                       transfer_id.size(), master_key_.data(), master_key_.size());
    return result;
}

std::array<unsigned char, 24>
EncryptedFileStore::nonceFor(const std::array<unsigned char, 16>& prefix,
                             std::uint64_t chunk_index) {
    std::array<unsigned char, 24> nonce{};
    std::copy(prefix.begin(), prefix.end(), nonce.begin());
    for (int index = 0; index < 8; ++index) {
        nonce[16 + index] = static_cast<unsigned char>(chunk_index >> (index * 8));
    }
    return nonce;
}

void EncryptedFileStore::create(const std::string& transfer_id) {
    std::lock_guard<std::mutex> lock(mutexes_[std::hash<std::string>{}(transfer_id) % mutexes_.size()]);
    const auto path = pathFor(transfer_id);
    auto lock_path = path;
    lock_path += ".lock";
    // Windows 的字节范围锁也会阻止同进程另一句柄读写，因此使用独立旁车锁文件。
    std::ofstream lock_seed(lock_path, std::ios::binary | std::ios::app);
    if (!lock_seed)
        throw std::runtime_error("cannot create attachment lock");
    lock_seed.close();
    CrossProcessFileLock cross_process_guard(lock_path, false);
    if (std::filesystem::exists(path))
        throw std::runtime_error("attachment already exists");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create encrypted attachment");
    std::array<unsigned char, 16> prefix{};
    randombytes_buf(prefix.data(), prefix.size());
    output.write(Magic, 9);
    output.write(reinterpret_cast<const char*>(prefix.data()), prefix.size());
    closeAndSync(output, path);
#ifndef _WIN32
    // 数据库发布新上传前，先持久化目录项，避免仅文件内容落盘而文件名在故障后丢失。
    const int directory = ::open(root_.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0)
        throw std::runtime_error("cannot open attachment directory");
    const bool synced = ::fsync(directory) == 0;
    ::close(directory);
    if (!synced)
        throw std::runtime_error("attachment directory flush failed");
#endif
}

std::uint64_t EncryptedFileStore::append(const std::string& transfer_id, std::uint64_t offset,
                                         const std::vector<unsigned char>& plaintext) {
    if (plaintext.empty() || plaintext.size() > chat::files::PlainChunkBytes ||
        offset % chat::files::PlainChunkBytes != 0)
        throw std::invalid_argument("invalid chunk");
    std::lock_guard<std::mutex> lock(mutexes_[std::hash<std::string>{}(transfer_id) % mutexes_.size()]);
    const auto path = pathFor(transfer_id);
    auto lock_path = path;
    lock_path += ".lock";
    // 多个资源实例共享目录时，用操作系统文件锁串行化同一附件的读写。
    CrossProcessFileLock cross_process_guard(lock_path, false);
    std::ifstream header(path, std::ios::binary);
    std::array<char, 9> magic{};
    std::array<unsigned char, 16> prefix{};
    header.read(magic.data(), magic.size());
    header.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
    if (!header || std::memcmp(magic.data(), Magic, magic.size()) != 0) {
        throw std::runtime_error("invalid encrypted attachment header");
    }
    const auto chunk_index = offset / chat::files::PlainChunkBytes;
    const auto file_size = std::filesystem::file_size(path);
    // 已确认记录必须完整存在；仅 seek 越过文件末尾并不报错，因此还需检查实际文件长度。
    for (std::uint64_t index = 0; index < chunk_index; ++index) {
        const auto confirmed_size = readLength(header);
        if (confirmed_size != chat::files::PlainChunkBytes ||
            static_cast<std::uintmax_t>(header.tellg()) + confirmed_size + TagBytes > file_size) {
            throw std::runtime_error("invalid confirmed attachment record");
        }
        header.seekg(static_cast<std::streamoff>(confirmed_size + TagBytes), std::ios::cur);
        if (!header)
            throw std::runtime_error("confirmed attachment data is missing");
    }
    const auto confirmed_end = static_cast<std::uintmax_t>(header.tellg());
    const auto key = transferKey(transfer_id);
    const auto nonce = nonceFor(prefix, chunk_index);
    std::vector<unsigned char> ciphertext(plaintext.size() + TagBytes);
    unsigned long long ciphertext_size = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext.data(), &ciphertext_size,
                                                   plaintext.data(), plaintext.size(), nullptr, 0,
                                                   nullptr, nonce.data(), key.data()) != 0) {
        throw std::runtime_error("attachment encryption failed");
    }
    if (confirmed_end < file_size) {
        // 密文可能已落盘而数据库偏移尚未提交，也可能已被其他实例提交。
        // 只接受完全相同的记录重放；不得复用同一 nonce 覆写内容或截断后续记录。
        const auto existing_size = readLength(header);
        if (existing_size != plaintext.size() || file_size - confirmed_end < 4 + ciphertext_size)
            throw std::runtime_error(
                "conflicting or incomplete attachment chunk; start a new upload");
        std::vector<unsigned char> existing(ciphertext_size);
        header.read(reinterpret_cast<char*>(existing.data()), existing.size());
        if (!header || sodium_memcmp(existing.data(), ciphertext.data(), existing.size()) != 0)
            throw std::runtime_error("attachment chunk conflict; start a new upload");
        header.close();
        syncFile(path);
        return offset + plaintext.size();
    }
    header.close();
    std::ofstream output(path, std::ios::binary | std::ios::app);
    writeLength(output, static_cast<std::uint32_t>(plaintext.size()));
    output.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext_size);
    closeAndSync(output, path);
    return offset + plaintext.size();
}

std::vector<unsigned char> EncryptedFileStore::read(const std::string& transfer_id,
                                                    std::uint64_t offset,
                                                    std::size_t maximum_bytes) const {
    if (offset % chat::files::PlainChunkBytes != 0 || maximum_bytes == 0) {
        throw std::invalid_argument("invalid download offset");
    }
    std::lock_guard<std::mutex> lock(mutexes_[std::hash<std::string>{}(transfer_id) % mutexes_.size()]);
    const auto path = pathFor(transfer_id);
    auto lock_path = path;
    lock_path += ".lock";
    CrossProcessFileLock cross_process_guard(lock_path, true);
    std::ifstream input(path, std::ios::binary);
    std::array<char, 9> magic{};
    std::array<unsigned char, 16> prefix{};
    input.read(magic.data(), magic.size());
    input.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
    if (!input || std::memcmp(magic.data(), Magic, magic.size()) != 0)
        throw std::runtime_error("invalid attachment");
    const auto target = offset / chat::files::PlainChunkBytes;
    for (std::uint64_t index = 0;; ++index) {
        const auto plain_size = readLength(input);
        if (plain_size == 0 || plain_size > chat::files::PlainChunkBytes)
            throw std::runtime_error("invalid record size");
        std::vector<unsigned char> ciphertext(plain_size + TagBytes);
        input.read(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());
        if (!input)
            throw std::runtime_error("truncated encrypted attachment");
        if (index != target)
            continue;
        const auto key = transferKey(transfer_id);
        const auto nonce = nonceFor(prefix, index);
        std::vector<unsigned char> plaintext(plain_size);
        unsigned long long actual_size = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &actual_size, nullptr,
                                                       ciphertext.data(), ciphertext.size(),
                                                       nullptr, 0, nonce.data(), key.data()) != 0) {
            throw std::runtime_error("attachment authentication failed");
        }
        // 禁止静默截断分片，否则客户端会以错误偏移继续并破坏续传状态。
        if (actual_size > maximum_bytes)
            throw std::runtime_error("download buffer is too small");
        plaintext.resize(static_cast<std::size_t>(actual_size));
        return plaintext;
    }
}

std::string EncryptedFileStore::sha256(const std::string& transfer_id,
                                       std::uint64_t total_size) const {
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    for (std::uint64_t offset = 0; offset < total_size; offset += chat::files::PlainChunkBytes) {
        const auto chunk = read(transfer_id, offset, chat::files::PlainChunkBytes);
        crypto_hash_sha256_update(&state, chunk.data(), chunk.size());
    }
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256_final(&state, digest.data());
    std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex{};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return hex.data();
}

void EncryptedFileStore::remove(const std::string& transfer_id) {
    std::lock_guard<std::mutex> lock(mutexes_[std::hash<std::string>{}(transfer_id) % mutexes_.size()]);
    const auto path = pathFor(transfer_id);
    auto lock_path = path;
    lock_path += ".lock";
    std::error_code ignored;
    if (!std::filesystem::exists(lock_path, ignored)) {
        std::filesystem::remove(path);
        return;
    }
    {
        CrossProcessFileLock cross_process_guard(lock_path, false);
        std::filesystem::remove(path);
    }
    // 在线清理保留旁车锁文件，避免重建后出现不同 inode，使新旧持锁者失去互斥。
}
