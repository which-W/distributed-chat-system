#pragma once

#include "PasswordHasher.h"

#include <string>
#include <utility>

namespace chat::security {

// 口令迁移只能覆盖刚刚完成校验的旧凭据，避免并发重置后恢复旧口令。
template <typename CompareAndSwap>
bool ApplyPasswordUpgradeIfCurrent(
    const PasswordVerificationResult& verification,
    const std::string& stored_hash,
    const std::string& stored_scheme,
    CompareAndSwap&& compare_and_swap)
{
    if (!verification.upgrade_required) {
        return true;
    }
    return std::forward<CompareAndSwap>(compare_and_swap)(
        verification.upgraded_hash, stored_hash, stored_scheme) == 1;
}

} // namespace chat::security
