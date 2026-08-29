#pragma once

#include "Singleton.h"
#include "global.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class FileTransferManager : public QObject, public Singleton<FileTransferManager> {
    Q_OBJECT
    friend class Singleton<FileTransferManager>;
public:
    // 当前实现对上传和下载分别串行化，避免多个大文件同时占满聊天连接。
    QString startUpload(const QString& path, int receiverUid);
    void startDownload(const QJsonObject& metadata, const QString& savePath);
    void cancel(const QString& transferId);
    QList<QJsonObject> availableForPeer(int peerUid) const;

signals:
    void transferAvailable(const QJsonObject& metadata);
    void transferRegistered(const QString& localToken, const QString& id);
    void progressChanged(const QString& id, qint64 current, qint64 total);
    void transferFinished(const QString& id, const QString& localPath);
    void transferFailed(const QString& id, const QString& reason);

private:
    FileTransferManager();
    void handleFrame(Req id, const QJsonObject& value);
    void hashUploadStep();
    void sendUploadInit();
    void sendNextUploadChunk();
    void requestDownloadChunk();
    void resumeActiveTransfers();

    struct UploadState {
        // 服务端只确认已持久化的 offset，断线后以响应值为续传起点。
        QFile file;
        QString localToken;
        QString id;
        QString name;
        QString mime;
        QString sha256;
        int receiverUid = 0;
        qint64 total = 0;
        qint64 offset = 0;
        std::unique_ptr<QCryptographicHash> hash;
    } upload_;
    struct DownloadState {
        QFile file;
        QJsonObject metadata;
        QString targetPath;
        QString partPath;
        qint64 offset = 0;
    } download_;
    QTimer hashTimer_;
    QList<QJsonObject> available_;
};
