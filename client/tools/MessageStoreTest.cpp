#include "MessageStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

void check(bool result, const char* reason) {
    if (!result)
        throw std::runtime_error(reason);
}
int main(int argc, char** argv) try {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory unavailable");
    const auto path = directory.filePath("messages.sqlite");
    const QJsonObject item{{"msgid", "stable-id"}, {"content", "hello"}};
    const QJsonArray batch{item};
    QJsonArray fresh;
    {
        MessageStore store;
        check(store.open(path, 1), "open store");
        check(store.save(1, 2, batch, fresh) && fresh.size() == 1, "durable outgoing insert");
        check(store.save(1, 2, batch, fresh) && fresh.isEmpty(), "idempotent insert");
        check(!store.save(1, 3, batch, fresh), "same ID cannot change receiver");
        check(store.pending(100).size() == 1, "outbox before restart");
        check(store.attempted(store.pending(100).first().toObject(), 100),
              "persist retry deadline");
        check(store.pending(104).isEmpty() && store.pending(105).size() == 1, "backoff boundary");
    }
    {
        MessageStore store;
        check(store.open(path, 1), "reopen store");
        const auto pending = store.pending(105);
        check(pending.size() == 1 && pending.first().toObject()["text_array"].toArray() == batch,
              "retry preserves original ID and content across restart");
        auto accepted = pending.first().toObject();
        accepted["error"] = 0;
        accepted["delivery"] = "accepted";
        auto wrong = accepted;
        wrong["touid"] = 3;
        check(!store.accept(wrong) && store.pending(105).size() == 1,
              "wrong acceptance cannot lose pending message");
        check(store.accept(accepted) && store.pending(1000).isEmpty(),
              "durable acceptance removes outbox entry");
        check(store.save(2, 1, batch, fresh) && fresh.size() == 1,
              "sender namespaces do not collide");
        const QJsonArray conflict{QJsonObject{{"msgid", "new"}, {"content", "rollback"}},
                                  QJsonObject{{"msgid", "stable-id"}, {"content", "conflict"}}};
        check(!store.save(2, 1, conflict, fresh) && fresh.isEmpty(),
              "batch failure yields no ACK candidates");
        check(store.history(2).size() == 2, "conflicting batch rolls back all inserts");
    }
    {
        MessageStore store;
        check(store.open(path, 1), "second reopen");
        check(store.save(2, 1, batch, fresh) && fresh.isEmpty(),
              "received duplicate after restart is ACKable but not displayed twice");
        check(store.pending(1000).isEmpty(), "acceptance survives restart");
        check(!store.open(path, 2), "account mismatch rejected");
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
