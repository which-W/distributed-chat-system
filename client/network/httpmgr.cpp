#include "httpmgr.h"

Httpmgr::Httpmgr() {
    // 连接信号槽
    connect(this, &Httpmgr::sig_http_finish, this, &Httpmgr::slot_http_finished);
}

Httpmgr::~Httpmgr() {
    // 清理资源
    _manager.deleteLater();
}

void Httpmgr::PostHttpRequest(const QString& url, const QJsonObject& jsonObj, Req req_id,
                              Modules mod) {
    // 将 QJsonObject 转换为 QByteArray 并设置请求头
    QByteArray postData = QJsonDocument(jsonObj).toJson();
    QNetworkRequest request(url);
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(postData.length()));
    auto self = shared_from_this();
    QNetworkReply* reply = _manager.post(request, postData);
    connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError>& errors) {
        for (const auto& error : errors) {
            qWarning() << "Gate TLS verification failed:" << error.errorString();
        }
        reply->abort();
    });
    // 连接信号槽处理网络请求完成
    connect(reply, &QNetworkReply::finished, this, [self, req_id, mod, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString response = reply->readAll();
            emit self->sig_http_finish(req_id, response, ERR_OK, mod);

        } else {
            qDebug() << "HTTP request failed:" << reply->errorString();
            emit self->sig_http_finish(req_id, QString(), ERR_NETWORK, mod);
        }
        reply->deleteLater();
        return;
    });
}

void Httpmgr::slot_http_finished(Req id, QString res, ErrorCode error, Modules mod) {
    // HTTP 完成仅代表传输结果；业务成功由各页面解析响应后判断。
    if (mod == Modules::MOD_REGISTER) {
        emit sig_reg_mod_finish(id, res, error);
    } else if (mod == Modules::RESETMOD) {
        emit sig_reset_mod_finish(id, res, error);
    } else if (mod == Modules::LODINMOD) {
        emit sig_login_finish(id, res, error);
    } else {
        qDebug() << "Unhandled module in slot_http_finished:" << mod;
    }
}
