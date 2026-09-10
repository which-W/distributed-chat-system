#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>

// GUI-thread owned. Each database is scoped to one gateway and account.
class MessageStore {
  public:
    MessageStore();
    ~MessageStore();
    MessageStore(const MessageStore&) = delete;
    MessageStore& operator=(const MessageStore&) = delete;
    bool open(const QString& path, int uid);
    void close();
    bool save(int from, int to, const QJsonArray& messages, QJsonArray& fresh);
    bool accept(const QJsonObject& response);
    QJsonArray pending(qint64 now, int limit = 16);
    bool attempted(const QJsonObject& message, qint64 now);
    QJsonArray history(int peer, int limit = 200);
    QString error() const {
        return error_;
    }

  private:
    bool fail(const QString& error);
    QString name_, error_;
    QSqlDatabase db_;
    int uid_ = 0;
};
