#pragma once

#include "FileTransferTypes.h"

#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class EncryptedFileStore {
public:
    EncryptedFileStore(std::filesystem::path root, const std::string& master_key_hex);

    void create(const std::string& transfer_id);
    std::uint64_t append(const std::string& transfer_id, std::uint64_t offset,
        const std::vector<unsigned char>& plaintext);
    std::vector<unsigned char> read(const std::string& transfer_id,
        std::uint64_t offset, std::size_t maximum_bytes) const;
    std::string sha256(const std::string& transfer_id, std::uint64_t total_size) const;
    void remove(const std::string& transfer_id);

private:
    std::filesystem::path pathFor(const std::string& transfer_id) const;
    std::array<unsigned char, 32> transferKey(const std::string& transfer_id) const;
    static std::array<unsigned char, 24> nonceFor(
        const std::array<unsigned char, 16>& prefix, std::uint64_t chunk_index);

    std::filesystem::path root_;
    std::array<unsigned char, 32> master_key_ {};
    mutable std::mutex mutex_;
};
