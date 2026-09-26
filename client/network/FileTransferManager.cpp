#include "FileTransferManager.h"

#include "TcpMgr.h"
#include "ResourceHttp.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QUuid>

namespace {
constexpr qint64 MaxFileBytes = 100LL * 1024LL * 1024LL;
constexpr qint64 ChunkBytes = 256LL * 1024LL;


} // namespace

FileTransferManager::FileTransferManager() {
    uploadAckTimer_.setParent(this);
    uploadAckTimer_.setObjectName("uploadAckTimer");
    uploadAckTimer_.setSingleShot(true);
    uploadAckTimer_.setInterval(180000);
    connect(&uploadAckTimer_, &QTimer::timeout, this, [this]() {
        failUpload(tr("上传超时：服务器未确认，请检查连接后重新发送。"));
    });
    connect(&ResourceHttp::instance(),&ResourceHttp::resetAccount,this,[this] {
        // 先使上传、下载回调失效，再停止请求和计时器；文件路径与附件列表也属于账号状态。
        ++uploadGeneration_; ++downloadGeneration_; uploadAckTimer_.stop(); hashTimer_.stop();
        ResourceHttp::cancel(uploadRequest_); ResourceHttp::cancel(downloadRequest_);
        uploadRequest_.reset(); downloadRequest_.reset();
        upload_.file.close(); download_.file.close(); upload_.id.clear(); upload_.localToken.clear(); download_.metadata={};
        upload_.name.clear(); upload_.mime.clear(); upload_.sha256.clear(); upload_.hash.reset();
        upload_.receiverUid=0; upload_.total=0; upload_.offset=0;
        download_.targetPath.clear(); download_.partPath.clear(); download_.offset=0;
        available_.clear(); localPaths_.clear();
    });
    hashTimer_.setInterval(0);
    connect(&hashTimer_, &QTimer::timeout, this, &FileTransferManager::hashUploadStep);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_file_available, this,
            [this](const QJsonObject& metadata) {
                const auto user = UserMgr::Getinstance()->GetUserInfo();
                if (!user || (metadata["fromuid"].toInt() != user->_uid &&
                              metadata["touid"].toInt() != user->_uid)) return;
                const auto id = metadata["id"].toString();
                if (id.isEmpty()) return;
                for (const auto& old : available_)
                    if (old["id"].toString() == id)
                        return;
                auto complete = metadata;
                complete["completed"] = true;
                available_.append(complete);
                emit transferAvailable(complete);
            });
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_connection_state, this,
            [this](const QString&, bool connected) {
                // 只有重新鉴权成功后才续传，服务端返回的确认偏移会覆盖本地旧值。
                if (connected)
                    resumeActiveTransfers();
            });
}

QString FileTransferManager::startUpload(const QString& path, int receiverUid) {
    QFileInfo info(path);
    if (upload_.file.isOpen() || !info.isFile() || info.size() > MaxFileBytes || receiverUid <= 0) {
        emit transferFailed({}, tr("文件无效、超过 100 MB，或已有上传正在进行"));
        return {};
    }
    ++uploadGeneration_;
    upload_.file.setFileName(path);
    if (!upload_.file.open(QIODevice::ReadOnly)) {
        emit transferFailed({}, tr("无法读取所选文件"));
        return {};
    }
    upload_.localToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    upload_.id.clear();
    upload_.sha256.clear();
    upload_.offset = 0;
    upload_.name = info.fileName();
    upload_.receiverUid = receiverUid;
    upload_.total = info.size();
    upload_.mime = QMimeDatabase().mimeTypeForFile(info).name();
    upload_.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    hashTimer_.start();
    return upload_.localToken;
}

void FileTransferManager::hashUploadStep() {
    const QByteArray data = upload_.file.read(1024 * 1024);
    if (!data.isEmpty()) {
        upload_.hash->addData(data);
        return;
    }
    hashTimer_.stop();
    upload_.file.seek(0);
    upload_.sha256 = QString::fromLatin1(upload_.hash->result().toHex());
    sendUploadInit();
}

void FileTransferManager::sendUploadInit() {
    uploadAckTimer_.start();
    QJsonObject request{{"touid", upload_.receiverUid},
                        {"name", upload_.name},
                        {"mime", upload_.mime},
                        {"total_size", upload_.total},
                        {"sha256", upload_.sha256}};
    request["idempotency_key"] = upload_.localToken;
    if (!upload_.id.isEmpty()) httpJson(HttpStage::UploadInit,"GET","/uploads/"+upload_.id);
    else httpJson(HttpStage::UploadInit,"POST","/uploads",QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void FileTransferManager::sendNextUploadChunk() {
    if (!upload_.file.seek(upload_.offset)) {
        failUpload(tr("无法定位上传文件"));
        return;
    }
    uploadAckTimer_.start();
    const QByteArray data = upload_.file.read(ChunkBytes);
    if (data.isEmpty()) {
        httpJson(HttpStage::UploadFinish,"POST","/uploads/"+upload_.id+"/complete");
        return;
    }
    httpJson(HttpStage::UploadChunk,"PUT","/uploads/"+upload_.id+"/chunks?offset="+QString::number(upload_.offset),data);
}

void FileTransferManager::startDownload(const QJsonObject& metadata, const QString& savePath) {
    if (download_.file.isOpen() || savePath.isEmpty()) {
        emit transferFailed(metadata["id"].toString(), tr("已有下载正在进行"));
        return;
    }
    ++downloadGeneration_;
    download_.metadata = metadata;
    download_.targetPath = savePath;
    download_.partPath = savePath + ".part";
    download_.file.setFileName(download_.partPath);
    if (!download_.file.open(QIODevice::ReadWrite)) {
        emit transferFailed(metadata["id"].toString(), tr("无法创建下载文件"));
        return;
    }
    qint64 size = download_.file.size();
    if (size < 0 || size > metadata["total_size"].toVariant().toLongLong() ||
        size % (32*1024) != 0) {
        download_.file.resize(0);
        size = 0;
    }
    download_.offset = size;
    download_.file.seek(size);
    requestDownloadChunk();
}

void FileTransferManager::requestDownloadChunk() {
    // 重连续传可能替换尚未结束的 Range 请求，避免同一偏移有多个回调落盘。
    ResourceHttp::cancel(downloadRequest_);
    const auto id=download_.metadata["id"].toString();
    const auto total=download_.metadata["total_size"].toVariant().toLongLong();
    if (download_.offset==total) {
        QJsonObject v=download_.metadata; v["complete"]=true; handleFrame(HttpStage::DownloadChunk,v); return;
    }
    const auto generation=downloadGeneration_;
    const auto offset=download_.offset;
    downloadRequest_=ResourceHttp::instance().request("GET","/files/"+id+"/content",{},[this,generation,offset,id,total](int code,const QByteArray& body) {
        if (generation!=downloadGeneration_ || download_.metadata["id"].toString()!=id) return;
        if (code!=206 || body.isEmpty() || body.size()>qMin(ChunkBytes,total-offset)) {
            download_.file.close(); download_.metadata={}; emit transferFailed(id,tr("下载失败或响应不完整")); return;
        }
        QJsonObject v=download_.metadata; v["offset"]=offset; v["next_offset"]=offset+body.size();
        v["data"]=QString::fromLatin1(body.toBase64()); handleFrame(HttpStage::DownloadChunk,v);
    },{{"Range","bytes="+QByteArray::number(offset)+"-"+QByteArray::number(qMin(total-1,offset+ChunkBytes-1))}});
}

void FileTransferManager::cancel(const QString& transferId) {
    // 服务器分配 ID 前，本地令牌并不是合法传输 ID，不能将它发给服务端导致连接被拒绝。
    QString remoteId = transferId;
    if (upload_.localToken == transferId)
        remoteId = upload_.id;
    if (!remoteId.isEmpty() && (upload_.id==remoteId))
        ResourceHttp::instance().request("DELETE","/uploads/"+remoteId,{},[](int,const QByteArray&) {});
    if (upload_.id == transferId || upload_.localToken == transferId) {
        ++uploadGeneration_;
        ResourceHttp::cancel(uploadRequest_); uploadRequest_.reset();
        uploadAckTimer_.stop();
        upload_.file.close();
        upload_.localToken.clear();
        upload_.id.clear();
        upload_.name.clear();
        upload_.mime.clear();
        upload_.sha256.clear();
        upload_.receiverUid = 0;
        upload_.total = 0;
        upload_.offset = 0;
        upload_.hash.reset();
        hashTimer_.stop();
    }
    if (download_.metadata["id"].toString() == transferId) {
        ++downloadGeneration_;
        ResourceHttp::cancel(downloadRequest_); downloadRequest_.reset();
        download_.file.close();
        download_.metadata = {};
        download_.targetPath.clear();
        download_.partPath.clear();
        download_.offset = 0;
    }
    // 取消下载只关闭本地请求与文件，保留远端附件、已下载的 .part 和列表中的重试入口。
    emit transferFailed(transferId, tr("传输已取消"));
}

void FileTransferManager::failUpload(const QString& reason) {
    ++uploadGeneration_;
    ResourceHttp::cancel(uploadRequest_); uploadRequest_.reset();
    const auto token = upload_.id.isEmpty() ? upload_.localToken : upload_.id;
    uploadAckTimer_.stop();
    hashTimer_.stop();
    upload_.file.close();
    upload_.id.clear();
    upload_.localToken.clear();
    upload_.sha256.clear();
    upload_.hash.reset();
    upload_.offset = 0;
    emit transferFailed(token, reason);
}

void FileTransferManager::handleFrame(HttpStage id, const QJsonObject& value) {
    const bool uploading = id == HttpStage::UploadInit || id == HttpStage::UploadChunk ||
                           id == HttpStage::UploadFinish;
    if (uploading) {
        if (!upload_.file.isOpen()) return;
        if (!upload_.id.isEmpty() && value.contains("id") && value["id"].toString() != upload_.id) return;
        uploadAckTimer_.stop();
    }
    if (value["error"].toInt() != 0) {
        if (uploading) {
            failUpload(tr("服务器拒绝上传（错误码 %1），请检查连接和文件后重试。").arg(value["error"].toInt()));
            return;
        }
        download_.file.close();
        download_.metadata = {};
        emit transferFailed(value["id"].toString(), tr("服务器拒绝文件传输"));
        return;
    }
    if (id == HttpStage::UploadInit) {
        upload_.id = value["id"].toString();
        emit transferRegistered(upload_.localToken, upload_.id);
        upload_.offset = value["offset"].toVariant().toLongLong();
        sendNextUploadChunk();
        return;
    }
    if (id == HttpStage::UploadChunk) {
        upload_.offset = value["offset"].toVariant().toLongLong();
        emit progressChanged(upload_.id, upload_.offset, upload_.total);
        sendNextUploadChunk();
        return;
    }
    if (id == HttpStage::UploadFinish) {
        const auto idValue = upload_.id;
        const auto localPath = upload_.file.fileName();
        localPaths_[idValue] = localPath;
        // 缓存服务端确认的可信元数据，切换会话后仍能恢复已发送附件气泡。
        bool known = false;
        for (const auto& item : available_)
            if (item["id"].toString() == idValue) {
                known = true;
                break;
            }
        if (!known) {
            auto complete = value;
            complete["completed"] = true;
            available_.append(complete);
        }
        upload_.file.close();
        upload_.localToken.clear();
        upload_.id.clear();
        upload_.name.clear();
        upload_.mime.clear();
        upload_.sha256.clear();
        upload_.receiverUid = 0;
        upload_.total = 0;
        upload_.offset = 0;
        upload_.hash.reset();
        emit transferFinished(idValue, localPath);
        return;
    }
    if (id != HttpStage::DownloadChunk)
        return;
    const auto total = value["total_size"].toVariant().toLongLong();
    if (value["complete"].toBool()) {
        download_.file.flush();
        download_.file.seek(0);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!download_.file.atEnd())
            hash.addData(download_.file.read(1024 * 1024));
        const auto idValue = download_.metadata["id"].toString();
        if (QString::fromLatin1(hash.result().toHex()) != download_.metadata["sha256"].toString()) {
            download_.file.close();
            emit transferFailed(idValue, tr("文件完整性校验失败"));
            download_.metadata = {};
            return;
        }
        download_.file.close();
        QFile::remove(download_.targetPath);
        if (!QFile::rename(download_.partPath, download_.targetPath)) {
            emit transferFailed(idValue, tr("无法保存下载文件"));
            download_.metadata = {};
            return;
        }
        ResourceHttp::instance().request("POST","/files/"+idValue+"/downloaded",{},[](int,const QByteArray&) {});
        const auto path = download_.targetPath;
        localPaths_[idValue] = path;
        download_.metadata = {};
        download_.targetPath.clear();
        download_.partPath.clear();
        download_.offset = 0;
        emit transferFinished(idValue, path);
        return;
    }
    if (value["offset"].toVariant().toLongLong() != download_.offset) {
        emit transferFailed(value["id"].toString(), tr("下载偏移不一致"));
        return;
    }
    const auto data = QByteArray::fromBase64(value["data"].toString().toLatin1(),
                                             QByteArray::AbortOnBase64DecodingErrors);
    if (data.isNull() || download_.file.write(data) != data.size()) {
        emit transferFailed(value["id"].toString(), tr("下载写入失败"));
        return;
    }
    download_.offset = value["next_offset"].toVariant().toLongLong();
    emit progressChanged(value["id"].toString(), download_.offset, total);
    requestDownloadChunk();
}

void FileTransferManager::resumeActiveTransfers() {
    if (upload_.file.isOpen() && !upload_.sha256.isEmpty())
        sendUploadInit();
    if (download_.file.isOpen() && !download_.metadata.isEmpty())
        requestDownloadChunk();
}

QList<QJsonObject> FileTransferManager::availableForPeer(int peerUid) const {
    QList<QJsonObject> result;
    const auto user = UserMgr::Getinstance()->GetUserInfo();
    if (!user) return result;
    for (const auto& item : available_) {
        if ((item["fromuid"].toInt() == peerUid && item["touid"].toInt() == user->_uid) ||
            (item["touid"].toInt() == peerUid && item["fromuid"].toInt() == user->_uid))
            result.append(item);
    }
    return result;
}
void FileTransferManager::httpJson(HttpStage response,const QByteArray& method,const QString& path,const QByteArray& body) {
    // 上传初始化、分片和完成按顺序推进；重连替换请求时一并取消旧请求的排队或重试。
    ResourceHttp::cancel(uploadRequest_);
    const auto generation=uploadGeneration_;
    uploadRequest_=ResourceHttp::instance().request(method,path,body,[this,response,generation](int code,const QByteArray& data) {
        if (generation!=uploadGeneration_) return;
        if (code==409 && response==HttpStage::UploadChunk && QJsonDocument::fromJson(data).object().contains("offset")) { sendUploadInit(); return; }
        auto value=QJsonDocument::fromJson(data).object();
        if (code>=400 || value.isEmpty()) { failUpload(tr("资源服务请求失败 (%1)").arg(code)); return; }
        handleFrame(response,value);
    },{{"Content-Type",method=="PUT" ? "application/octet-stream" : "application/json"}});
}
