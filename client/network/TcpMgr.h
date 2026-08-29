#pragma once
#include <QSslSocket>
#include <QSslCipher>
#include <QTimer>
#include <QSet>
#include "Singleton.h"
#include "global.h"
#include "usermgr.h"
#include "UserData.h"
class TcpMgr :
	public QObject, public Singleton<TcpMgr>, public std::enable_shared_from_this<TcpMgr>
{
	Q_OBJECT
public:
	~TcpMgr() = default;
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
    QTimer _retry_timer;
    QTimer _heartbeat_timer;
    QByteArray _buffer;
    bool _b_recv_pending;
    quint16 _message_id;
    quint16 _message_len;
	QSet<QString> _received_message_keys;
    void initHandlers();
    void handleMsg(Req id, int len, QByteArray data);
    void beginConnection();
    void handleTransportReady();
    void scheduleReconnect();
    void resetParser();
	QMap<Req, std::function<void(Req id, int len, QByteArray data)>> _handlers;
public slots:
    void slot_tcp_connect(ServerInfo);
    void slot_send_data(Req reqId, QByteArray data);
    void slot_disconnect();
    void slot_reconnect_for_proxy();
signals:
    void sig_con_success(bool bsuccess);
    void sig_send_data(Req reqId, QByteArray data);
    void sig_login_failed(int err);
    void sig_swich_chatdlg();
	void sig_user_search(std::shared_ptr<SearchInfo> si);
    void sig_friend_apply(std::shared_ptr<AddFriendApply>);
    void sig_add_auth_friend(std::shared_ptr<AuthInfo>);
    void sig_auth_rsp(std::shared_ptr<AuthRsp>);
    void sig_text_chat_msg(std::shared_ptr<TextChatMsg>);
    void sig_connection_state(const QString& message, bool connected);
    void sig_file_frame(Req id, const QJsonObject& value);
    void sig_file_available(const QJsonObject& metadata);
};
