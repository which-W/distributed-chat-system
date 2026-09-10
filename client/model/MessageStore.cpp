#include "MessageStore.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

MessageStore::MessageStore() : name_(QUuid::createUuid().toString()) {}
MessageStore::~MessageStore() {
    close();
}
void MessageStore::close() {
    if (db_.isValid())
        db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(name_);
    uid_ = 0;
}
bool MessageStore::fail(const QString& error) {
    error_ = error;
    if (db_.isOpen())
        db_.rollback();
    return false;
}
bool MessageStore::open(const QString& path, int uid) {
    close();
    if (uid <= 0)
        return fail(QStringLiteral("Invalid account"));
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
    db_.setDatabaseName(path);
    db_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=2000"));
    if (!db_.open())
        return fail(db_.lastError().text());
    QSqlQuery query(db_);
    const QStringList schema{
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("PRAGMA synchronous=FULL"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS account (uid INTEGER PRIMARY KEY)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages (seq INTEGER PRIMARY KEY, sender INTEGER NOT "
            "NULL, "
            "receiver INTEGER NOT NULL, msgid TEXT NOT NULL, content TEXT NOT NULL, state TEXT NOT "
            "NULL, "
            "next_attempt INTEGER NOT NULL DEFAULT 0, attempts INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(sender,msgid))"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS pending_messages ON messages(state,next_attempt,seq)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS message_peers ON messages(sender,receiver,seq)")};
    for (const auto& sql : schema)
        if (!query.exec(sql))
            return fail(query.lastError().text());
    if (!query.exec(QStringLiteral("SELECT uid FROM account")))
        return fail(query.lastError().text());
    if (query.next()) {
        if (query.value(0).toInt() != uid || query.next())
            return fail(QStringLiteral("Account mismatch"));
    } else {
        query.prepare(QStringLiteral("INSERT INTO account(uid) VALUES(?)"));
        query.addBindValue(uid);
        if (!query.exec())
            return fail(query.lastError().text());
    }
    uid_ = uid;
    error_.clear();
    return true;
}
bool MessageStore::save(int from, int to, const QJsonArray& messages, QJsonArray& fresh) {
    fresh = {};
    if (!uid_ || from <= 0 || to <= 0 || from == to || (from != uid_ && to != uid_) ||
        messages.isEmpty() || messages.size() > 50)
        return fail(QStringLiteral("Invalid message batch"));
    if (!db_.transaction())
        return fail(db_.lastError().text());
    QJsonArray inserted;
    for (const auto& value : messages) {
        const auto object = value.toObject();
        const auto id = object["msgid"].toString();
        const auto content = object["content"].toString();
        if (id.isEmpty() || id.toUtf8().size() > 128 || content.isEmpty() ||
            content.toUtf8().size() > 2048)
            return fail(QStringLiteral("Invalid message content"));
        QSqlQuery query(db_);
        query.prepare(
            QStringLiteral("SELECT receiver,content FROM messages WHERE sender=? AND msgid=?"));
        query.addBindValue(from);
        query.addBindValue(id);
        if (!query.exec())
            return fail(query.lastError().text());
        if (query.next()) {
            if (query.value(0).toInt() != to || query.value(1).toString() != content)
                return fail(QStringLiteral("Message ID has conflicting content"));
            continue;
        }
        query.prepare(QStringLiteral(
            "INSERT INTO messages(sender,receiver,msgid,content,state) VALUES(?,?,?,?,?)"));
        query.addBindValue(from);
        query.addBindValue(to);
        query.addBindValue(id);
        query.addBindValue(content);
        query.addBindValue(from == uid_ ? QStringLiteral("pending") : QStringLiteral("received"));
        if (!query.exec())
            return fail(query.lastError().text());
        inserted.append(object);
    }
    if (!db_.commit())
        return fail(db_.lastError().text());
    fresh = inserted;
    return true;
}
bool MessageStore::accept(const QJsonObject& response) {
    if (!uid_ || response["fromuid"].toInt() != uid_ || response["error"].toInt(-1) != 0 ||
        response["delivery"].toString() != "accepted")
        return false;
    const auto messages = response["text_array"].toArray();
    if (messages.isEmpty() || messages.size() > 50)
        return false;
    if (!db_.transaction())
        return fail(db_.lastError().text());
    for (const auto& value : messages) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral("UPDATE messages SET state='accepted' WHERE sender=? AND "
                                     "receiver=? AND msgid=? AND content=?"));
        query.addBindValue(uid_);
        query.addBindValue(response["touid"].toInt());
        query.addBindValue(value.toObject()["msgid"].toString());
        query.addBindValue(value.toObject()["content"].toString());
        if (!query.exec())
            return fail(query.lastError().text());
        if (query.numRowsAffected() != 1)
            return fail(QStringLiteral("Acceptance does not match stored message"));
    }
    if (!db_.commit())
        return fail(db_.lastError().text());
    return true;
}
QJsonArray MessageStore::pending(qint64 now, int limit) {
    QJsonArray result;
    if (!uid_)
        return result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT receiver,msgid,content FROM messages WHERE state='pending' AND sender=? "
        "AND next_attempt<=? ORDER BY next_attempt,seq LIMIT ?"));
    query.addBindValue(uid_);
    query.addBindValue(now);
    query.addBindValue(qBound(1, limit, 16));
    if (!query.exec()) {
        fail(query.lastError().text());
        return {};
    }
    while (query.next())
        result.append(QJsonObject{
            {"fromuid", uid_},
            {"touid", query.value(0).toInt()},
            {"text_array", QJsonArray{QJsonObject{{"msgid", query.value(1).toString()},
                                                  {"content", query.value(2).toString()}}}}});
    return result;
}
bool MessageStore::attempted(const QJsonObject& message, qint64 now) {
    if (!uid_)
        return false;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "UPDATE messages SET next_attempt=? + MIN(60,5 * (1 << MIN(attempts,4))), "
        "attempts=MIN(attempts+1,5) WHERE sender=? AND msgid=? AND state='pending'"));
    query.addBindValue(now);
    query.addBindValue(uid_);
    query.addBindValue(message["text_array"].toArray().first().toObject()["msgid"].toString());
    if (!query.exec())
        return fail(query.lastError().text());
    return query.numRowsAffected() == 1;
}
QJsonArray MessageStore::history(int peer, int limit) {
    QJsonArray result;
    if (!uid_)
        return result;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT sender,receiver,msgid,content,state FROM messages WHERE "
        "(sender=? AND receiver=?) OR (sender=? AND receiver=?) ORDER BY seq DESC LIMIT ?"));
    query.addBindValue(uid_);
    query.addBindValue(peer);
    query.addBindValue(peer);
    query.addBindValue(uid_);
    query.addBindValue(qBound(1, limit, 200));
    if (!query.exec()) {
        fail(query.lastError().text());
        return {};
    }
    while (query.next())
        result.prepend(QJsonObject{{"fromuid", query.value(0).toInt()},
                                   {"touid", query.value(1).toInt()},
                                   {"msgid", query.value(2).toString()},
                                   {"content", query.value(3).toString()},
                                   {"state", query.value(4).toString()}});
    return result;
}
