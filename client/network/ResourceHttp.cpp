#include "ResourceHttp.h"
#include "TcpMgr.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

ResourceHttp& ResourceHttp::instance() { static ResourceHttp instance; return instance; }
ResourceHttp::ResourceHttp() {
    renewal_.setInterval(12*60*1000);
    connect(&renewal_,&QTimer::timeout,this,&ResourceHttp::renew);
    connect(TcpMgr::Getinstance().get(),&TcpMgr::sig_resource_token,this,[this](const QJsonObject& v) {
        // 同时匹配账号和本轮请求，避免上次登录或上次续领的迟到响应覆盖当前凭证。
        if (!renewing_ || uid_<=0 || v["uid"].toInt()!=uid_ ||
            v["request_id"].toString()!=renewalRequestId_) return;
        renewing_=false;
        token_=v["token"].toString().toLatin1();
        if (token_.isEmpty()) {
            const auto generation=generation_;
            QTimer::singleShot(3000,this,[this,generation] { if (generation==generation_) renew(); }); return;
        }
        renewal_.start(); auto pending=std::move(waiting_); waiting_.clear();
        for (auto& request:pending) request(); emit credentialReady();
    });
    connect(TcpMgr::Getinstance().get(),&TcpMgr::sig_connection_state,this,[this](const QString&,bool connected) { if (connected) renew(); });
}
void ResourceHttp::configure(const QString& url,int uid) {
    reset(); base_=QUrl(url); uid_=uid;
    if (base_.path().isEmpty() || base_.path()=="/") base_.setPath("/api/resources/v1");
}
QString ResourceHttp::accountKey() const {
    // 将服务器地址与账号共同纳入缓存命名空间，隔离同一设备上的不同登录身份。
    return QString::fromLatin1(QCryptographicHash::hash(base_.toEncoded()+":"+QByteArray::number(uid_),QCryptographicHash::Sha256).toHex());
}
void ResourceHttp::reset() {
    // abort() 可能立即触发 finished，必须先增加代次，防止回调继续写入旧传输状态。
    ++generation_; renewal_.stop(); token_.clear(); waiting_.clear(); renewing_=false;
    uid_=0; base_=QUrl(); renewalRequestId_.clear();
    const auto active=active_; for (auto* reply:active) reply->abort(); emit resetAccount();
}
void ResourceHttp::renew() {
    if (renewing_ || uid_<=0) return;
    renewing_=true; renewalRequestId_=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto requestId=renewalRequestId_;
    emit TcpMgr::Getinstance()->sig_send_data(Req::ID_RESOURCE_TOKEN_REQ,
        QJsonDocument(QJsonObject{{"request_id",requestId}}).toJson(QJsonDocument::Compact));
    const auto generation=generation_;
    QTimer::singleShot(10000,this,[this,generation,requestId] {
        // 超时只重试当前续领；旧计时器不得干扰已完成或被替换的请求。
        if (generation==generation_ && renewing_ && renewalRequestId_==requestId) { renewing_=false; renew(); }
    });
}
void ResourceHttp::cancel(const RequestHandle& control) {
    if (!control) return;
    // 先标记再 abort，确保同步触发的结束回调也不会安排重试。
    control->cancelled=true;
    if (control->reply) control->reply->abort();
}
ResourceHttp::RequestHandle ResourceHttp::request(const QByteArray& method,const QString& path,const QByteArray& body,Callback callback,
                           const QList<QPair<QByteArray,QByteArray>>& headers,unsigned retry,RequestHandle control) {
    if (!control) { control=std::make_shared<RequestControl>(); control->generation=generation_; }
    if (control->cancelled || control->generation!=generation_) return control;
    if (uid_<=0) { callback(401,"Resource account is signed out"); return control; }
    if (base_.scheme()!="https" || base_.host().isEmpty() || !base_.userInfo().isEmpty() || !path.startsWith('/')) { callback(400,"Invalid HTTPS resource endpoint"); return control; }
    if (token_.isEmpty()) {
        // 排队闭包保留同一控制对象，凭证到达时仍需检查取消标记和账号代次。
        if (waiting_.size()>=64) { callback(503,"Resource credential queue full"); return control; }
        waiting_.append([=] { request(method,path,body,callback,headers,retry,control); }); renew(); return control;
    }
    auto url=base_; const auto queryAt=path.indexOf('?');
    url.setPath(base_.path()+ (queryAt<0 ? path : path.left(queryAt))); url.setQuery(queryAt<0 ? QString() : path.mid(queryAt+1));
    QNetworkRequest req(url); req.setRawHeader("Authorization","Bearer "+token_);
    req.setRawHeader("Content-Type","application/octet-stream");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(30000); for (const auto& h:headers) req.setRawHeader(h.first,h.second);
    auto* reply=network_->sendCustomRequest(req,method,body); reply->setReadBufferSize(1024*1024); active_.insert(reply);
    control->reply=reply;
    auto data=std::make_shared<QByteArray>();
    connect(reply,&QNetworkReply::readyRead,this,[reply,data] {
        // 下载按分段请求处理；对单次响应设硬上限，防止异常响应持续占用内存。
        if (data->size()+reply->bytesAvailable()>1024*1024) { reply->abort(); return; } data->append(reply->readAll());
    });
    const auto generation=generation_;
    connect(reply,&QNetworkReply::finished,this,[=] {
        active_.remove(reply); const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool failed=reply->error()!=QNetworkReply::NoError;
        data->append(reply->readAll()); reply->deleteLater(); control->reply=nullptr;
        if (generation!=generation_ || control->cancelled) return;
        if (retry<5 && (code==401 || code==429 || code==503 || code==0)) {
            // 延迟重试必须再次检查取消和退出状态，不能仅依赖当前 reply 是否存在。
            if (code==401) token_.clear();
            QTimer::singleShot(qMin(1000*(1<<retry),16000),this,[=] {
                if (generation==generation_ && !control->cancelled) request(method,path,body,callback,headers,retry+1,control);
            }); return;
        }
        callback(code==0 || (failed && code<400) ? 503 : code,*data);
    });
    return control;
}
