#include "FriendAcceptanceTransaction.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct FakeStore {
    int first_insert = 1;
    int second_insert = 1;
    int pending_update = 1;
    int insert_count = 0;
    int throw_on_insert = 0;
    bool committed = false;
    bool rolled_back = false;
    std::vector<std::tuple<int, int, std::string>> rows;

    void begin() {}
    int insertFriend(int self_uid, int friend_uid, const std::string& back)
    {
        ++insert_count;
        if (throw_on_insert == insert_count) throw std::runtime_error("insert failed");
        rows.emplace_back(self_uid, friend_uid, back);
        return insert_count == 1 ? first_insert : second_insert;
    }
    int acceptPendingApplication(int applicant_uid, int accepter_uid)
    {
        rows.emplace_back(applicant_uid, accepter_uid, "pending");
        return pending_update;
    }
    void commit() { committed = true; }
    void rollback() { rolled_back = true; }
};

bool Require(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main()
{
    {
        FakeStore store;
        if (!Require(chat::storage::ExecuteFriendAcceptanceTransaction(store, 20, 10, "备注")
                == chat::storage::FriendAcceptanceResult::Success,
                "合法 pending 申请未提交")
            || !Require(store.committed && !store.rolled_back, "成功事务状态错误")
            || !Require(store.rows.at(0) == std::make_tuple(20, 10, std::string("备注"))
                    && store.rows.at(1) == std::make_tuple(10, 20, std::string(""))
                    && store.rows.at(2) == std::make_tuple(10, 20, std::string("pending")),
                "好友申请方向或双向关系错误")) return 1;
    }
    {
        FakeStore store;
        store.pending_update = 0;
        if (!Require(chat::storage::ExecuteFriendAcceptanceTransaction(store, 20, 10, "")
                == chat::storage::FriendAcceptanceResult::PendingMissing,
                "无 pending 申请不应成功")
            || !Require(store.rolled_back && !store.committed, "无 pending 时未回滚")) return 1;
    }
    {
        FakeStore store;
        store.second_insert = -1;
        if (!Require(chat::storage::ExecuteFriendAcceptanceTransaction(store, 20, 10, "")
                == chat::storage::FriendAcceptanceResult::StorageError,
                "第二次写入失败不应成功")
            || !Require(store.rolled_back, "第二次写入失败未回滚")) return 1;
    }
    {
        FakeStore store;
        store.throw_on_insert = 2;
        bool threw = false;
        try {
            chat::storage::ExecuteFriendAcceptanceTransaction(store, 20, 10, "");
        }
        catch (const std::runtime_error&) {
            threw = true;
        }
        if (!Require(threw && store.rolled_back && !store.committed,
                "数据库异常未回滚或被错误吞掉")) return 1;
    }
    return 0;
}
