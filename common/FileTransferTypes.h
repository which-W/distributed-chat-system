#pragma once

#include <cstdint>
#include <string>

namespace chat::files {

// 普通聊天帧仍保持小上限；附件由固定大小分片独立传输，单文件最多 100 MB。
inline constexpr std::uint64_t MaxFileBytes = 100ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t PlainChunkBytes = 32ULL * 1024ULL;

enum class TransferStatus {
    Uploading,
    Available,
    Downloaded,
    Cancelled,
    Expired,
};

struct TransferRecord {
    // id 由服务端生成，original_name 只用于展示，绝不参与磁盘路径拼接。
    std::string id;
    int sender_uid = 0;
    int receiver_uid = 0;
    std::string original_name;
    std::string mime_type;
    std::uint64_t total_size = 0;
    std::uint64_t uploaded_size = 0;
    std::string sha256;
    TransferStatus status = TransferStatus::Uploading;
};

inline const char* ToString(TransferStatus status)
{
    switch (status) {
    case TransferStatus::Uploading: return "uploading";
    case TransferStatus::Available: return "available";
    case TransferStatus::Downloaded: return "downloaded";
    case TransferStatus::Cancelled: return "cancelled";
    case TransferStatus::Expired: return "expired";
    }
    return "expired";
}

} // namespace chat::files
