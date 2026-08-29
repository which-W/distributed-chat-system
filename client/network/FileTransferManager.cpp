#include "FileTransferManager.h"

#include "TcpMgr.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QUuid>

namespace {
constexpr qint64 MaxFileBytes = 100LL * 1024LL * 1024LL;
constexpr qint64 ChunkBytes = 32LL * 1024LL;

void sendJson(Req id, const QJsonObject& value)
{
    emit TcpMgr::Getinstance()->sig_send_data(id,
        QJsonDocument(value).toJson(QJsonDocument::Compact));
}
}

FileTransferManager::FileTransferManager()
{
    hashTimer_.setInterval(0);
    connect(&hashTimer_, &QTimer::timeout, this, &FileTransferManager::hashUploadStep);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_file_frame,
        this, &FileTransferManager::handleFrame);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_file_available, this,
        [this](const QJsonObject& metadata) {
            const auto id=metadata["id"].toString();
            for(const auto& old:available_) if(old["id"].toString()==id) return;
            available_.append(metadata);emit transferAvailable(metadata);
        });
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_connection_state, this,
        [this](const QString&, bool connected) {
            // 只有重新鉴权成功后才续传，服务端返回的确认偏移会覆盖本地旧值。
            if (connected) resumeActiveTransfers();
        });
}

QString FileTransferManager::startUpload(const QString& path, int receiverUid)
{
    QFileInfo info(path);
    if (upload_.file.isOpen() || !info.isFile() || info.size() > MaxFileBytes || receiverUid <= 0) {
        emit transferFailed({}, tr("文件无效、超过 100 MB，或已有上传正在进行"));
        return {};
    }
    upload_.file.setFileName(path);
    if (!upload_.file.open(QIODevice::ReadOnly)) {
        emit transferFailed({}, tr("无法读取所选文件")); return {};
    }
    upload_.localToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    upload_.name = info.fileName(); upload_.receiverUid = receiverUid; upload_.total = info.size();
    upload_.mime = QMimeDatabase().mimeTypeForFile(info).name();
    upload_.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    hashTimer_.start();
    return upload_.localToken;
}

void FileTransferManager::hashUploadStep()
{
    const QByteArray data = upload_.file.read(1024 * 1024);
    if (!data.isEmpty()) { upload_.hash->addData(data); return; }
    hashTimer_.stop(); upload_.file.seek(0);
    upload_.sha256 = QString::fromLatin1(upload_.hash->result().toHex());
    sendUploadInit();
}

void FileTransferManager::sendUploadInit()
{
    QJsonObject request{{"touid", upload_.receiverUid}, {"name", upload_.name},
        {"mime", upload_.mime}, {"total_size", upload_.total},
        {"sha256", upload_.sha256}};
    if (!upload_.id.isEmpty()) request["id"] = upload_.id;
    sendJson(Req::ID_UPLOAD_FILE_REQ, request);
}

void FileTransferManager::sendNextUploadChunk()
{
    if (!upload_.file.seek(upload_.offset)) { emit transferFailed(upload_.id, tr("无法定位上传文件")); return; }
    const QByteArray data = upload_.file.read(ChunkBytes);
    if (data.isEmpty()) {
        sendJson(Req::ID_UPLOAD_FILE_FINISH_REQ, QJsonObject{{"id", upload_.id}}); return;
    }
    sendJson(Req::ID_UPLOAD_FILE_CHUNK_REQ, QJsonObject{{"id", upload_.id},
        {"offset", upload_.offset}, {"data", QString::fromLatin1(data.toBase64())}});
}

void FileTransferManager::startDownload(const QJsonObject& metadata, const QString& savePath)
{
    if (download_.file.isOpen() || savePath.isEmpty()) {
        emit transferFailed(metadata["id"].toString(), tr("已有下载正在进行")); return;
    }
    download_.metadata = metadata; download_.targetPath = savePath; download_.partPath = savePath + ".part";
    download_.file.setFileName(download_.partPath);
    if (!download_.file.open(QIODevice::ReadWrite)) {
        emit transferFailed(metadata["id"].toString(), tr("无法创建下载文件")); return;
    }
    qint64 size = download_.file.size();
    if (size < 0 || size > metadata["total_size"].toVariant().toLongLong() || size % ChunkBytes != 0) {
        download_.file.resize(0); size = 0;
    }
    download_.offset = size; download_.file.seek(size); requestDownloadChunk();
}

void FileTransferManager::requestDownloadChunk()
{
    sendJson(Req::ID_DOWNLOAD_FILE_REQ, QJsonObject{{"id", download_.metadata["id"].toString()},
        {"offset", download_.offset}});
}

void FileTransferManager::cancel(const QString& transferId)
{
    // 服务器分配 ID 前，本地令牌并不是合法传输 ID，不能将它发给服务端导致连接被拒绝。
    QString remoteId = transferId;
    if (upload_.localToken == transferId) remoteId = upload_.id;
    if (!remoteId.isEmpty()) {
        sendJson(Req::ID_FILE_TRANSFER_CANCEL, QJsonObject{{"id", remoteId}});
    }
    if (upload_.id == transferId || upload_.localToken == transferId) {
        upload_.file.close();upload_.localToken.clear();upload_.id.clear();upload_.name.clear();
        upload_.mime.clear();upload_.sha256.clear();upload_.receiverUid=0;upload_.total=0;
        upload_.offset=0;upload_.hash.reset();hashTimer_.stop();
    }
    if (download_.metadata["id"].toString() == transferId) {
        download_.file.close();download_.metadata={};download_.targetPath.clear();
        download_.partPath.clear();download_.offset=0;
    }
    for (auto iterator = available_.begin(); iterator != available_.end();) {
        if ((*iterator)["id"].toString() == remoteId) iterator = available_.erase(iterator);
        else ++iterator;
    }
    emit transferFailed(transferId, tr("传输已取消"));
}

void FileTransferManager::handleFrame(Req id, const QJsonObject& value)
{
    if (value["error"].toInt() != 0) { emit transferFailed(value["id"].toString(), tr("服务器拒绝文件传输")); return; }
    if (id == Req::ID_UPLOAD_FILE_RSP) {
        upload_.id=value["id"].toString();emit transferRegistered(upload_.localToken,upload_.id);
        upload_.offset=value["offset"].toVariant().toLongLong();sendNextUploadChunk();return;
    }
    if (id == Req::ID_UPLOAD_FILE_CHUNK_RSP) {
        upload_.offset=value["offset"].toVariant().toLongLong();
        emit progressChanged(upload_.id,upload_.offset,upload_.total);sendNextUploadChunk();return;
    }
    if (id == Req::ID_UPLOAD_FILE_FINISH_RSP) {
        const auto idValue=upload_.id;
        // 缓存服务端确认的可信元数据，切换会话后仍能恢复已发送附件气泡。
        bool known=false;for(const auto& item:available_)if(item["id"].toString()==idValue){known=true;break;}
        if(!known)available_.append(value);
        upload_.file.close();upload_.localToken.clear();upload_.id.clear();
        upload_.name.clear();upload_.mime.clear();upload_.sha256.clear();upload_.receiverUid=0;
        upload_.total=0;upload_.offset=0;upload_.hash.reset();
        emit transferFinished(idValue,{});return;
    }
    if (id != Req::ID_DOWNLOAD_FILE_CHUNK) return;
    const auto total=value["total_size"].toVariant().toLongLong();
    if (value["complete"].toBool()) {
        download_.file.flush();download_.file.seek(0);QCryptographicHash hash(QCryptographicHash::Sha256);
        while(!download_.file.atEnd()) hash.addData(download_.file.read(1024*1024));
        const auto idValue=download_.metadata["id"].toString();
        if(QString::fromLatin1(hash.result().toHex())!=download_.metadata["sha256"].toString()){
            download_.file.close();emit transferFailed(idValue,tr("文件完整性校验失败"));download_.metadata={};return;}
        download_.file.close();QFile::remove(download_.targetPath);
        if(!QFile::rename(download_.partPath,download_.targetPath)){emit transferFailed(idValue,tr("无法保存下载文件"));download_.metadata={};return;}
        sendJson(Req::ID_DOWNLOAD_FILE_DONE,QJsonObject{{"id",idValue}});
        const auto path=download_.targetPath;download_.metadata={};download_.targetPath.clear();download_.partPath.clear();download_.offset=0;
        emit transferFinished(idValue,path);return;
    }
    if(value["offset"].toVariant().toLongLong()!=download_.offset){emit transferFailed(value["id"].toString(),tr("下载偏移不一致"));return;}
    const auto data=QByteArray::fromBase64(value["data"].toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
    if(data.isNull()||download_.file.write(data)!=data.size()){emit transferFailed(value["id"].toString(),tr("下载写入失败"));return;}
    download_.offset=value["next_offset"].toVariant().toLongLong();
    emit progressChanged(value["id"].toString(),download_.offset,total);requestDownloadChunk();
}

void FileTransferManager::resumeActiveTransfers()
{
    if (upload_.file.isOpen() && !upload_.sha256.isEmpty()) sendUploadInit();
    if (download_.file.isOpen() && !download_.metadata.isEmpty()) requestDownloadChunk();
}

QList<QJsonObject> FileTransferManager::availableForPeer(int peerUid) const
{
    QList<QJsonObject> result;
    for(const auto& item:available_) {
        if(item["fromuid"].toInt()==peerUid||item["touid"].toInt()==peerUid)result.append(item);
    }
    return result;
}
