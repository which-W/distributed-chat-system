#pragma once
#include "MessageStore.h"
#include "Singleton.h"
#include "UserData.h"
#include "global.h"
#include "usermgr.h"
#include <QSet>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSslCipher>
#include <QSslSocket>
#include <QTimer>
class TcpMgr : public QObject,
               public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr> {
    Q_OBJECT
  public:
    ~TcpMgr() = default;
    bool enqueueText(const QJsonObject& message);
    void refreshFriends();
    void submitFriendOperation(Req request, int peer, const QString& remark);

  private:
    friend class Singleton<TcpMgr>;
    TcpMgr();
    QSslSocket _socket;
    QString _host;
    QString _transport;
    QString _tls_server_name;
    uint16_t _port;
    bool _use_tls;
    bool _connect_result_emitted;
    bool _authenticated;
    bool _reconnecting;
    bool _manual_disconnect;
    int _retry_attempt;
    int _missed_heartbeats;
    QByteArray _login_payload;
    QString _resume_token;
    QString _gate_base_url;
    QString _resource_base_url;
    int _uid{0};
    bool _allow_insecure{false};
    QNetworkAccessManager _credential_manager;
    QPointer<QNetworkReply> _credential_reply;
    quint64 _credential_generation{0};
    QTimer _renew_timer;
    QTimer _retry_timer;
    QTimer _heartbeat_timer;
    QByteArray _buffer;
    bool _b_recv_pending;
    quint16 _message_id;
    quint16 _message_len;
    MessageStore _messages;
    QTimer _outbox_timer;
    QTimer _friend_timer;
    QString _friend_request_id;
    int _friend_peer{0};
    Req _friend_request{Req::ID_ADD_FRIEND_REQ};
    void finishFriendOperation(Req request, const QJsonObject& response);
    void mergeFriendSnapshot(const QJsonObject& response);
    void flushOutbox();
    void writeFrame(Req reqId, const QByteArray& data);
    void initHandlers();
    void handleMsg(Req id, int len, QByteArray data);
    void beginConnection();
    void handleTransportReady();
    void scheduleReconnect();
    void requestSession(bool renew);
    void cancelCredentialRequest();
    void logoutResumeToken();
    bool credentialUrlAllowed(const QUrl& url) const;
    void resetParser();
    QMap<Req, std::function<void(Req id, int len, QByteArray data)>> _handlers;
  public slots:
    void slot_tcp_connect(ServerInfo);
    void slot_send_data(Req reqId, QByteArray data);
    void slot_disconnect();
    void slot_reconnect_for_proxy();
  signals:
    void sig_resource_token(const QJsonObject& value);
    void sig_avatar_changed(const QJsonObject& value);
    void sig_con_success(bool bsuccess);
    void sig_send_data(Req reqId, QByteArray data);
    void sig_login_failed(int err);
    void sig_swich_chatdlg();
    void sig_user_search(std::shared_ptr<SearchInfo> si);
    void sig_friend_apply(std::shared_ptr<AddFriendApply>);
    void sig_friend_snapshot();
    void sig_friend_operation(int request, int peer, bool success, const QString& message);
    void sig_add_auth_friend(std::shared_ptr<AuthInfo>);
    void sig_auth_rsp(std::shared_ptr<AuthRsp>);
    void sig_text_chat_msg(std::shared_ptr<TextChatMsg>);
    void sig_connection_state(const QString& message, bool connected);
    void sig_file_available(const QJsonObject& metadata);
};
