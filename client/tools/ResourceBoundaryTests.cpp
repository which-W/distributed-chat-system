#include "ResourceHttp.h"
#include "FileTransferManager.h"
#include "AvatarLoader.h"
#include "TcpMgr.h"
#include "usermgr.h"
#include <QApplication>
#include <QBuffer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <cstring>
#include <cstdio>

namespace {
// 仅替换网络传输，请求重试、文件传输及账号清理均使用实际业务代码；测试不会访问服务器。
class Reply : public QNetworkReply {
public:
    bool aborted = false;
    QByteArray received;
    qint64 offset = 0;
    Reply(const QNetworkRequest& request, QNetworkAccessManager::Operation operation, QObject* parent)
        : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); setOperation(operation); open(QIODevice::ReadOnly);
    }
    void abort() override {
        if (isFinished()) return;
        aborted = true; setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished();
    }
    void finish(int status, const QByteArray& body) {
        if (isFinished()) return;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        received = body; setFinished(true);
        if (!body.isEmpty()) emit readyRead();
        emit finished();
    }
    qint64 bytesAvailable() const override { return received.size() - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = qMin(maximum, received.size() - offset);
        if (count <= 0) return -1;
        std::memcpy(data, received.constData() + offset, static_cast<std::size_t>(count)); offset += count; return count;
    }
};
class Network : public QNetworkAccessManager {
public:
    struct Sent { QByteArray method; QUrl url; QByteArray body; QPointer<Reply> reply; };
    QList<Sent> sent;
protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* data) override {
        auto* reply = new Reply(request, operation, this);
        sent.append({request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(), request.url(), data ? data->readAll() : QByteArray(), reply});
        return reply;
    }
};
void settle(int ms = 0) {
    // 驱动 Qt 计时器与延迟销毁，覆盖请求结束后才触发的重试和回调。
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
}

class ResourceBoundaryTests {
    int failures_ = 0;
    void check(bool ok, const char* description) {
        std::fprintf(ok ? stdout : stderr, "%s %s\n", ok ? "PASS" : "FAIL", description);
        if (!ok) ++failures_;
    }
public:
    int run() {
        QCoreApplication::setOrganizationName("DeepechoResourceTests");
        QCoreApplication::setApplicationName("BoundaryTests");
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        if (!directory.isValid()) return 2;
        auto& http = ResourceHttp::instance(); auto transfer = FileTransferManager::Getinstance();
        auto& avatars = AvatarLoader::instance(); Network network;
        http.reset(); http.network_ = &network;
        const QString endpoint = "https://resource.invalid/api/resources/v1";
        const auto login = [&](int uid) {
            http.configure(endpoint, uid);
            UserMgr::Getinstance()->SetUserInfo(std::make_shared<UserInfo>(uid, "test", ":/res/head_1.jpg"));
            http.token_ = QByteArray(64, 'a');
        };
        const QString id = "11111111-1111-4111-8111-111111111111";
        const QByteArray part(32*1024, 'x');
        const QJsonObject metadata{{"id",id},{"fromuid",1},{"touid",2},{"name","test.bin"},
            {"total_size",64*1024},{"sha256",QString(64,'0')}};
        const auto save = directory.filePath("test.bin");
        { QFile file(save + ".part"); file.open(QIODevice::WriteOnly); file.write(part); }
        login(2); transfer->available_.append(metadata);
        // 预置一个完整加密记录大小的明文前缀，验证取消后仍从原偏移继续下载。
        transfer->startDownload(metadata,save);
        auto first = network.sent.last().reply;
        check(network.sent.last().method == "GET", "download starts with GET");
        check(first->request().rawHeader("Range").startsWith("bytes=32768-"), "download resumes existing confirmed prefix");
        const auto beforeCancel = network.sent.size();
        transfer->cancel(id);
        check(first->aborted, "cancel aborts active HTTP download");
        check(network.sent.size() == beforeCancel, "cancel download sends no DELETE");
        check(transfer->availableForPeer(1).size() == 1, "cancel preserves available attachment for retry");
        check(!transfer->download_.file.isOpen(), "cancel closes the partial file");
        first->finish(206,part);
        { QFile file(save + ".part"); file.open(QIODevice::ReadOnly); check(file.readAll()==part,"late response cannot change cancelled partial file"); }
        transfer->startDownload(metadata,save);
        check(network.sent.last().reply->request().rawHeader("Range").startsWith("bytes=32768-"),"cancelled download can resume again");
        transfer->cancel(id); settle();

        // 凭证尚未到达时取消，稍后队列被唤醒也不应产生实际请求。
        login(2); http.token_.clear();
        transfer->startDownload(metadata,save);
        transfer->cancel(id);
        const auto queuedCount = network.sent.size();
        http.token_ = QByteArray(64,'b');
        auto pending = std::move(http.waiting_); http.waiting_.clear();
        for (auto& request : pending) request();
        check(network.sent.size()==queuedCount,"cancel while waiting for credential prevents later network request");
        http.reset(); settle();

        login(2); transfer->available_.append(metadata); transfer->localPaths_[id]=save;
        avatars.cache_.insert("old-account",new QPixmap(8,8)); avatars.versions_[1]=7;
        transfer->startDownload(metadata,save); auto active=network.sent.last().reply;
        http.request("GET","/files/"+id,{},[](int,const QByteArray&) {});
        network.sent.last().reply->finish(503,"{}"); // 为旧账号安排延迟重试，随后立即退出。
        http.token_.clear(); bool oldCallback=false;
        http.request("GET","/files/"+id,{},[&](int,const QByteArray&) { oldCallback=true; });
        http.reset();
        check(active->aborted,"logout aborts in-flight requests");
        check(http.uid_==0 && http.base_.isEmpty(),"logout clears identity and endpoint");
        check(http.token_.isEmpty() && http.waiting_.isEmpty() && !http.renewal_.isActive(),"logout clears credentials, waiting requests and renewal timer");
        check(transfer->available_.isEmpty() && transfer->localPaths_.isEmpty(),"logout clears attachment and local path caches");
        check(avatars.cache_.isEmpty() && avatars.versions_.isEmpty(),"logout clears avatar memory cache");
        check(!transfer->download_.file.isOpen() && transfer->download_.metadata.isEmpty(),"logout closes transfer state");
        const auto retiredKey=http.accountKey();
        login(3); const auto requestsBefore=network.sent.size(); settle(1100);
        check(network.sent.size()==requestsBefore && !oldCallback,"old-account retry and callbacks cannot run after login to another account");
        check(http.accountKey()!=retiredKey,"avatar disk cache namespace changes with account");

        // 切换账号后，旧续领响应不得解锁新账号的等待队列；同账号旧请求也必须被拒绝。
        login(2); http.token_.clear();
        http.request("GET","/files/"+id,{},[](int,const QByteArray&) {});
        const auto oldRenewal=http.renewalRequestId_;
        login(3); http.token_.clear();
        http.request("GET","/files/"+id,{},[](int,const QByteArray&) {});
        const auto newRenewal=http.renewalRequestId_;
        const auto beforeCredential=network.sent.size();
        emit TcpMgr::Getinstance()->sig_resource_token(QJsonObject{{"uid",2},{"request_id",oldRenewal},{"token",QString(64,'c')}});
        check(http.token_.isEmpty() && network.sent.size()==beforeCredential,"old-account credential cannot unlock new-account queue");
        emit TcpMgr::Getinstance()->sig_resource_token(QJsonObject{{"uid",3},{"request_id",oldRenewal},{"token",QString(64,'c')}});
        check(http.token_.isEmpty(),"stale renewal request id is rejected even for current uid");
        emit TcpMgr::Getinstance()->sig_resource_token(QJsonObject{{"uid",3},{"request_id",newRenewal},{"token",QString(64,'d')}});
        check(http.token_==QByteArray(64,'d') && network.sent.size()==beforeCredential+1,"matching credential releases current-account queue once");
        http.reset(); settle(); login(3);

        // 请求进入退避等待后已没有活动 reply，取消控制对象仍必须阻止下一轮发送。
        bool cancelledCallback=false;
        auto retry=http.request("GET","/files/"+id,{},[&](int,const QByteArray&) { cancelledCallback=true; });
        network.sent.last().reply->finish(503,"{}");
        ResourceHttp::cancel(retry);
        const auto beforeRetry=network.sent.size(); settle(1100);
        check(network.sent.size()==beforeRetry && !cancelledCallback,"cancel prevents scheduled retry and callback");

        // 空文件也必须先校验 SHA-256；校验失败不得向服务端确认下载完成。
        const auto emptyPath=directory.filePath("empty.bin");
        auto empty=metadata; empty["total_size"]=0;
        const auto beforeBad=network.sent.size(); transfer->startDownload(empty,emptyPath);
        check(network.sent.size()==beforeBad && !QFile::exists(emptyPath),"failed checksum never sends download confirmation");
        empty["sha256"]=QString::fromLatin1(QCryptographicHash::hash({},QCryptographicHash::Sha256).toHex());
        transfer->startDownload(empty,emptyPath);
        check(network.sent.last().method=="POST" && network.sent.last().url.path().endsWith("/downloaded"),"verified empty file sends download confirmation");
        check(QFile::exists(emptyPath),"verified download is saved locally");
        http.reset(); settle(); http.network_=&http.manager_;
        std::fprintf(stdout,"Resource boundary failures: %d\n",failures_);
        return failures_ ? 1 : 0;
    }
};
int runResourceBoundaryTests() { return ResourceBoundaryTests().run(); }
