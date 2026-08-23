#pragma once
#include <QSslSocket>
#include <QSslCipher>
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
    QByteArray _buffer;
    bool _b_recv_pending;
    quint16 _message_id;
    quint16 _message_len;
    void initHandlers();
    void handleMsg(Req id, int len, QByteArray data);
	QMap<Req, std::function<void(Req id, int len, QByteArray data)>> _handlers;
public slots:
    void slot_tcp_connect(ServerInfo);
    void slot_send_data(Req reqId, QByteArray data);
    void slot_disconnect();
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
};
