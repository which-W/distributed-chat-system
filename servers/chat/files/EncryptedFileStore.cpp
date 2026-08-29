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

// 共享目录可能同时被多个 ChatServer 使用，原生文件锁用于保护单个密文附件。
class CrossProcessFileLock {
public:
    CrossProcessFileLock(const std::filesystem::path& path, bool shared)
    {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open attachment lock");
        const DWORD flags = shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK;
        if (!LockFileEx(handle_, flags, 0, MAXDWORD, MAXDWORD, &overlapped_)) {
            CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("cannot lock attachment");
        }
#else
        descriptor_ = ::open(path.c_str(), O_RDWR);
        if (descriptor_ < 0 || ::flock(descriptor_, shared ? LOCK_SH : LOCK_EX) != 0) {
            if (descriptor_ >= 0) ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error("cannot lock attachment");
        }
#endif
    }

    ~CrossProcessFileLock()
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped_);
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) { ::flock(descriptor_, LOCK_UN); ::close(descriptor_); }
#endif
    }

    CrossProcessFileLock(const CrossProcessFileLock&) = delete;
    CrossProcessFileLock& operator=(const CrossProcessFileLock&) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_ {};
#else
    int descriptor_ = -1;
#endif
};

std::uint32_t readLength(std::ifstream& input)
{
    unsigned char bytes[4] {};
    input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!input) throw std::runtime_error("encrypted attachment record is truncated");
    return (static_cast<std::uint32_t>(bytes[0]) << 24)
        | (static_cast<std::uint32_t>(bytes[1]) << 16)
        | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
}

void writeLength(std::ofstream& output, std::uint32_t value)
{
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}
} // namespace

EncryptedFileStore::EncryptedFileStore(
    std::filesystem::path root, const std::string& master_key_hex) : root_(std::move(root))
{
    if (sodium_init() < 0 || master_key_hex.size() != master_key_.size() * 2
        || sodium_hex2bin(master_key_.data(), master_key_.size(), master_key_hex.c_str(),
               master_key_hex.size(), nullptr, nullptr, nullptr) != 0) {
        throw std::runtime_error("CHAT_FILE_STORAGE_KEY must be 64 hexadecimal characters");
    }
    std::filesystem::create_directories(root_);
}

std::filesystem::path EncryptedFileStore::pathFor(const std::string& transfer_id) const
{
    // 文件 ID 只能是服务端生成的 UUID，绝不将客户端文件名拼接到磁盘路径。
    if (transfer_id.size() != 36 || transfer_id.find_first_not_of("0123456789abcdef-") != std::string::npos) {
        throw std::invalid_argument("invalid transfer id");
    }
    return root_ / (transfer_id + ".enc");
}

std::array<unsigned char, 32> EncryptedFileStore::transferKey(const std::string& transfer_id) const
{
    std::array<unsigned char, 32> result {};
    crypto_generichash(result.data(), result.size(),
        reinterpret_cast<const unsigned char*>(transfer_id.data()), transfer_id.size(),
        master_key_.data(), master_key_.size());
    return result;
}

std::array<unsigned char, 24> EncryptedFileStore::nonceFor(
    const std::array<unsigned char, 16>& prefix, std::uint64_t chunk_index)
{
    std::array<unsigned char, 24> nonce {};
    std::copy(prefix.begin(), prefix.end(), nonce.begin());
    for (int index = 0; index < 8; ++index) {
        nonce[16 + index] = static_cast<unsigned char>(chunk_index >> (index * 8));
    }
    return nonce;
}

void EncryptedFileStore::create(const std::string& transfer_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto path = pathFor(transfer_id);
    auto lock_path = path; lock_path += ".lock";
    // Windows 的字节范围锁也会阻止同进程另一句柄读写，因此使用独立旁车锁文件。
    std::ofstream lock_seed(lock_path, std::ios::binary | std::ios::app);
    if (!lock_seed) throw std::runtime_error("cannot create attachment lock");
    lock_seed.close();
    CrossProcessFileLock cross_process_guard(lock_path, false);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create encrypted attachment");
    std::array<unsigned char, 16> prefix {};
    randombytes_buf(prefix.data(), prefix.size());
    output.write(Magic, 9);
    output.write(reinterpret_cast<const char*>(prefix.data()), prefix.size());
}

std::uint64_t EncryptedFileStore::append(const std::string& transfer_id, std::uint64_t offset,
    const std::vector<unsigned char>& plaintext)
{
	if (plaintext.empty() || plaintext.size() > chat::files::PlainChunkBytes
		|| offset % chat::files::PlainChunkBytes != 0) throw std::invalid_argument("invalid chunk");
	std::lock_guard<std::mutex> lock(mutex_);
	const auto path = pathFor(transfer_id);
	auto lock_path = path; lock_path += ".lock";
	// 多个 ChatServer 共享目录时，用操作系统文件锁串行化同一附件的读写。
	CrossProcessFileLock cross_process_guard(lock_path, false);
	std::ifstream header(path, std::ios::binary);
    std::array<char, 9> magic {};
    std::array<unsigned char, 16> prefix {};
    header.read(magic.data(), magic.size());
    header.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
    if (!header || std::memcmp(magic.data(), Magic, magic.size()) != 0) {
        throw std::runtime_error("invalid encrypted attachment header");
    }
    const auto chunk_index = offset / chat::files::PlainChunkBytes;
    // 数据已落盘但元数据 CAS 尚未提交时可能崩溃；续传前截掉未确认的尾部记录。
    for (std::uint64_t index = 0; index < chunk_index; ++index) {
        const auto confirmed_size = readLength(header);
        if (confirmed_size == 0 || confirmed_size > chat::files::PlainChunkBytes) {
            throw std::runtime_error("invalid confirmed attachment record");
        }
        header.seekg(static_cast<std::streamoff>(confirmed_size + TagBytes), std::ios::cur);
        if (!header) throw std::runtime_error("confirmed attachment data is missing");
    }
    const auto confirmed_end = static_cast<std::uintmax_t>(header.tellg());
    header.close();
	std::filesystem::resize_file(path, confirmed_end);
    const auto key = transferKey(transfer_id);
    const auto nonce = nonceFor(prefix, chunk_index);
    std::vector<unsigned char> ciphertext(plaintext.size() + TagBytes);
    unsigned long long ciphertext_size = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext.data(), &ciphertext_size,
            plaintext.data(), plaintext.size(), nullptr, 0, nullptr, nonce.data(), key.data()) != 0) {
        throw std::runtime_error("attachment encryption failed");
    }
	std::ofstream output(path, std::ios::binary | std::ios::app);
    writeLength(output, static_cast<std::uint32_t>(plaintext.size()));
    output.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext_size);
    if (!output) throw std::runtime_error("attachment write failed");
    return offset + plaintext.size();
}

std::vector<unsigned char> EncryptedFileStore::read(
    const std::string& transfer_id, std::uint64_t offset, std::size_t maximum_bytes) const
{
    if (offset % chat::files::PlainChunkBytes != 0 || maximum_bytes == 0) {
        throw std::invalid_argument("invalid download offset");
	}
	std::lock_guard<std::mutex> lock(mutex_);
	const auto path = pathFor(transfer_id);
	auto lock_path = path; lock_path += ".lock";
	CrossProcessFileLock cross_process_guard(lock_path, true);
	std::ifstream input(path, std::ios::binary);
    std::array<char, 9> magic {};
    std::array<unsigned char, 16> prefix {};
    input.read(magic.data(), magic.size());
    input.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
    if (!input || std::memcmp(magic.data(), Magic, magic.size()) != 0) throw std::runtime_error("invalid attachment");
    const auto target = offset / chat::files::PlainChunkBytes;
    for (std::uint64_t index = 0;; ++index) {
        const auto plain_size = readLength(input);
        if (plain_size == 0 || plain_size > chat::files::PlainChunkBytes) throw std::runtime_error("invalid record size");
        std::vector<unsigned char> ciphertext(plain_size + TagBytes);
        input.read(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());
        if (!input) throw std::runtime_error("truncated encrypted attachment");
        if (index != target) continue;
        const auto key = transferKey(transfer_id);
        const auto nonce = nonceFor(prefix, index);
        std::vector<unsigned char> plaintext(plain_size);
        unsigned long long actual_size = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &actual_size, nullptr,
                ciphertext.data(), ciphertext.size(), nullptr, 0, nonce.data(), key.data()) != 0) {
            throw std::runtime_error("attachment authentication failed");
        }
		// 禁止静默截断分片，否则客户端会以错误偏移继续并破坏续传状态。
		if (actual_size > maximum_bytes) throw std::runtime_error("download buffer is too small");
		plaintext.resize(static_cast<std::size_t>(actual_size));
        return plaintext;
    }
}

std::string EncryptedFileStore::sha256(const std::string& transfer_id, std::uint64_t total_size) const
{
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    for (std::uint64_t offset = 0; offset < total_size; offset += chat::files::PlainChunkBytes) {
        const auto chunk = read(transfer_id, offset, chat::files::PlainChunkBytes);
        crypto_hash_sha256_update(&state, chunk.data(), chunk.size());
    }
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest {};
    crypto_hash_sha256_final(&state, digest.data());
    std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex {};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return hex.data();
}

void EncryptedFileStore::remove(const std::string& transfer_id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto path = pathFor(transfer_id);
	auto lock_path = path; lock_path += ".lock";
	std::error_code ignored;
	if (!std::filesystem::exists(lock_path, ignored)) {
		std::filesystem::remove(path, ignored);
		return;
	}
	{
		CrossProcessFileLock cross_process_guard(lock_path, false);
		std::filesystem::remove(path, ignored);
	}
	// Windows 需要先关闭锁文件句柄，才能立即删除旁车文件。
	std::filesystem::remove(lock_path, ignored);
}
