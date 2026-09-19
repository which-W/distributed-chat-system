#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QSet>
#include <functional>
#include <memory>

// 资源 HTTP 请求入口：统一维护账号凭证、有限重试和退出后的请求隔离。
class ResourceHttp : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(int, const QByteArray&)>;
    // 排队、发送和重试共用一个控制对象，取消操作因此也能阻止尚未发出的请求。
    struct RequestControl {
        bool cancelled = false;
        quint64 generation = 0;
        QPointer<QNetworkReply> reply;
    };
    using RequestHandle = std::shared_ptr<RequestControl>;
    static ResourceHttp& instance();
    void configure(const QString& url, int uid);
    // 先使旧回调失效，再中止网络请求，最后通知传输管理器和头像缓存清理账号状态。
    void reset();
    RequestHandle request(const QByteArray& method, const QString& path, const QByteArray& body, Callback callback,
                 const QList<QPair<QByteArray,QByteArray>>& headers = {}, unsigned retry = 0, RequestHandle control = {});
    static void cancel(const RequestHandle& control);
    QString accountKey() const;
signals:
    void credentialReady();
    void resetAccount();
private:
    friend class ResourceBoundaryTests;
    ResourceHttp();
    void renew();
    QNetworkAccessManager manager_; QUrl base_; int uid_ = 0; QByteArray token_; QTimer renewal_;
    // 正常运行使用内置管理器；边界测试只替换网络层，保留实际的业务状态机。
    QNetworkAccessManager* network_ = &manager_;
    bool renewing_ = false; quint64 generation_ = 0;
    QString renewalRequestId_;
    QList<std::function<void()>> waiting_;
    QSet<QNetworkReply*> active_;
};
