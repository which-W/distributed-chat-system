#pragma once

#include <string>

namespace chat::storage {

enum class FriendAcceptanceResult {
    Success,
    PendingMissing,
    StorageError,
};

// Store 由生产 MySQL 适配器或内存假事务实现；该函数固定原子操作的执行次序。
template <typename Store>
FriendAcceptanceResult ExecuteFriendAcceptanceTransaction(
    Store& store, int accepter_uid, int applicant_uid, const std::string& back_name)
{
    store.begin();
    try {
        if (store.insertFriend(accepter_uid, applicant_uid, back_name) < 0
            || store.insertFriend(applicant_uid, accepter_uid, "") < 0) {
            store.rollback();
            return FriendAcceptanceResult::StorageError;
        }
        if (store.acceptPendingApplication(applicant_uid, accepter_uid) != 1) {
            // pending 不存在通常表示重复接受或申请方向错误，保留原有业务错误语义。
            store.rollback();
            return FriendAcceptanceResult::PendingMissing;
        }
        store.commit();
        return FriendAcceptanceResult::Success;
    }
    catch (...) {
        // rollback 本身不得覆盖原始数据库异常。
        try {
            store.rollback();
        }
        catch (...) {
        }
        throw;
    }
}

} // namespace chat::storage
