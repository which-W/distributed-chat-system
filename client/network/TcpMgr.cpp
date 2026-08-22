#include "TcpMgr.h"

TcpMgr::TcpMgr():_socket(nullptr),_host(""),_transport("tls"),_tls_server_name(""),_port(0),
	_use_tls(true),_connect_result_emitted(false),_b_recv_pending(false), _message_id(0), _message_len(0) {
	// 初始化QTcpSocket并建立连接
	connect(&_socket, &QTcpSocket::connected, this, [this]() {
		if (!_use_tls && !_connect_result_emitted) {
			_connect_result_emitted = true;
			emit sig_con_success(true);
		}
		});
	connect(&_socket, &QSslSocket::encrypted, this, [this]() {
		if (_use_tls && !_connect_result_emitted) {
			_connect_result_emitted = true;
			qDebug() << "TLS established with" << _host
				<< "using" << _socket.sessionCipher().name();
			emit sig_con_success(true);
		}
		});
	connect(&_socket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
		this, [this](const QList<QSslError>& errors) {
			for (const auto& error : errors) {
				qWarning() << "Chat TLS verification failed:" << error.errorString();
			}
			_socket.abort();
			if (!_connect_result_emitted) {
				_connect_result_emitted = true;
				emit sig_con_success(false);
			}
		});
	connect(&_socket, &QTcpSocket::readyRead, this, [this]() {
		_buffer.append(_socket.readAll());	// 读取接收到的数据


		QDataStream stream(&_buffer, QIODevice::ReadOnly);
		stream.setVersion(QDataStream::Qt_5_0);

		// 检查是否有足够的数据来解析消息头
		forever{
			if (!_b_recv_pending) {
				if (_buffer.size() < static_cast<int>(sizeof(quint16) * 2)) {
					return; // 不足以读取消息头，等待更多数据
				}

				// 读取消息头
				 // 标记读取前位置
				stream.device()->seek(0); // 保证从头读
				stream >> _message_id >> _message_len;

				_buffer.remove(0, sizeof(quint16) * 2);
				_b_recv_pending = true;

				// 输出读取的数据
				qDebug() << "Message ID:" << _message_id << ", Length:" << _message_len;
		}
		if (_buffer.size() < _message_len) {
			return; // 不足以读取完整消息体，等待更多数据
		}
		// 读取完整消息体
		QByteArray messageData = _buffer.left(_message_len);
		qDebug() << "recive data is :" << messageData;
		_buffer.remove(0, _message_len); // 移除已处理正文
		_b_recv_pending = false;// 重置接收状态
		handleMsg(Req(_message_id), _message_len, messageData); // 处理消息

		}
	});

	// 处理错误（适用于Qt 5.15之前的版本）
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
	connect(&_socket, &QAbstractSocket::errorOccurred, this,
		[this](QAbstractSocket::SocketError socketError) {
			qWarning() << "Chat socket error:" << socketError << _socket.errorString();
			if (!_connect_result_emitted) {
				_connect_result_emitted = true;
				emit sig_con_success(false);
			}
		});
#else
	QObject::connect(&_socket, static_cast<void (QTcpSocket::*)(QTcpSocket::SocketError)>(&QTcpSocket::error),
		[&](QTcpSocket::SocketError socketError) {
			qDebug() << "Error:" << _socket.errorString();
			switch (socketError) {
			case QTcpSocket::ConnectionRefusedError:
				qDebug() << "Connection Refused!";
				if (!_connect_result_emitted) {
					_connect_result_emitted = true;
					emit sig_con_success(false);
				}
				break;
			case QTcpSocket::RemoteHostClosedError:
				qDebug() << "Remote Host Closed Connection!";
				break;
			case QTcpSocket::HostNotFoundError:
				qDebug() << "Host Not Found!";
				if (!_connect_result_emitted) {
					_connect_result_emitted = true;
					emit sig_con_success(false);
				}
				break;
			case QTcpSocket::SocketTimeoutError:
				qDebug() << "Connection Timeout!";
				if (!_connect_result_emitted) {
					_connect_result_emitted = true;
					emit sig_con_success(false);
				}
				break;
			case QTcpSocket::NetworkError:
				qDebug() << "Network Error!";
				break;
			default:
				qDebug() << "Other Error!";
				break;
			}
		});
#endif



	// 处理连接断开
	connect(&_socket, &QTcpSocket::disconnected, [&]() {
		qDebug() << "Disconnected from server.";
		});

	connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
	//注册消息
	initHandlers();
}


void TcpMgr::initHandlers()
{
	//登录回包
	_handlers.insert(Req::ID_CHAT_LOGIN_RSP, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			qDebug() << "data is " << data;
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();
		qDebug() << "data jsonobj is " << jsonObj;

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "Login Failed, err is Json Parse Err" << err;
			emit sig_login_failed(err);
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "Login Failed, err is " << err;
			emit sig_login_failed(err);
			return;
		}

		auto uid = jsonObj["uid"].toInt();
		auto name = jsonObj["name"].toString();
		auto nick = jsonObj["nick"].toString();
		auto icon = jsonObj["icon"].toString();
		auto sex = jsonObj["sex"].toInt();
		auto desc = jsonObj["desc"].toString();
		auto user_info = std::make_shared<UserInfo>(uid, name, nick, icon, sex, "", desc);

		UserMgr::Getinstance()->SetUserInfo(user_info);
		UserMgr::Getinstance()->SetToken(jsonObj["token"].toString());
		if (jsonObj.contains("apply_list")) {
			UserMgr::Getinstance()->AppendApplyList(jsonObj["apply_list"].toArray());
		}

		//添加好友列表
		if (jsonObj.contains("friend_list")) {
			UserMgr::Getinstance()->AppendFriendList(jsonObj["friend_list"].toArray());
		}



		emit sig_swich_chatdlg();

		});
	//搜索用户回包
	_handlers.insert(Req::ID_SEARCH_USER_RSP, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			qDebug() << "data is " << data;
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();
		qDebug() << "data jsonobj is " << jsonObj;

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "Search Failed, err is Json Parse Err" << err;
			emit sig_user_search(nullptr);
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "Search Failed, err is " << err;
			emit sig_user_search(nullptr);
			return;
		}
		auto search_info = std::make_shared<SearchInfo>(
			jsonDoc["uid"].toInt(), jsonObj["name"].toString(), jsonObj["nick"].toString(),
			jsonObj["desc"].toString(),jsonDoc["sex"].toInt(), jsonObj["icon"].toString()
		);
		emit sig_user_search(search_info);
		});
	//添加用户申请回包
	_handlers.insert(Req::ID_ADD_FRIEND_RSP, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			qDebug() << "data is " << data;
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();
		qDebug() << "data jsonobj is " << jsonObj;

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "ADD Friend Failed, err is Json Parse Err" << err;
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "ADD Friend Failed, err is " << err;
			return;
		}
		qDebug() << "ADD User res has accept";
		});
	//收到用户添加申请数据包
	_handlers.insert(Req::ID_NOTIFY_ADD_FRIEND_REQ, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			qDebug() << "data is " << data;
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();
		qDebug() << "data jsonobj is " << jsonObj;

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "Notify ADD Friend Failed, err is Json Parse Err" << err;
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "Notify ADD Friend, err is " << err;
			return;
		}
		qDebug() << "Notify ADD Friend res has accept";

		int from_uid = jsonObj["applyuid"].toInt();
		QString name = jsonObj["name"].toString();
		QString desc = jsonObj["desc"].toString();
		QString icon = jsonObj["icon"].toString();
		QString nick = jsonObj["nick"].toString();
		int sex = jsonObj["sex"].toInt();

		auto apply_info = std::make_shared<AddFriendApply>(
			from_uid, name, desc,
			icon, nick, sex);

		emit sig_friend_apply(apply_info);

		qDebug() << "sig_friend_apply has been notify";
		});
	//收到认证请求数据包
	_handlers.insert(Req::ID_NOTIFY_AUTH_FRIEND_REQ, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			qDebug() << "data is " << data;
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();
		qDebug() << "data jsonobj is " << jsonObj;

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "AUTH Friend Failed, err is Json Parse Err" << err;
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "AUTH Friend Failed, err is " << err;
			return;
		}
		qDebug() << "AUTH Friend res has accept";

		int from_uid = jsonObj["fromuid"].toInt();
		QString name = jsonObj["name"].toString();
		QString nick = jsonObj["nick"].toString();
		QString icon = jsonObj["icon"].toString();
		int sex = jsonObj["sex"].toInt();

		auto auth_info = std::make_shared<AuthInfo>(from_uid, name,
			nick, icon, sex);

		emit sig_add_auth_friend(auth_info);

		qDebug() << "sig_add_auth_friend has been notify";
		});
	//收到认证响应数据包
	_handlers.insert(Req::ID_AUTH_FRIEND_RSP, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id << " data is " << data;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "Auth Friend Failed, err is Json Parse Err" << err;
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "Auth Friend Failed, err is " << err;
			return;
		}

		auto name = jsonObj["name"].toString();
		auto nick = jsonObj["nick"].toString();
		auto icon = jsonObj["icon"].toString();
		auto sex = jsonObj["sex"].toInt();
		auto uid = jsonObj["uid"].toInt();
		auto rsp = std::make_shared<AuthRsp>(uid, name, nick, icon, sex);
		emit sig_auth_rsp(rsp);

		qDebug() << "Auth Friend Success ";
		});

	_handlers.insert(Req::ID_TEXT_CHAT_MSG_RSP, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id << " data is " << data;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "Chat Msg Rsp Failed, err is Json Parse Err" << err;
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "Chat Msg Rsp Failed, err is " << err;
			return;
		}

		qDebug() << "Receive Text Chat Rsp Success ";
		//ui设置送达等标记 todo...

		});

	_handlers.insert(Req::ID_NOTIFY_TEXT_CHAT_MSG_REQ, [this](Req id, int len, QByteArray data) {
		Q_UNUSED(len);
		qDebug() << "handle id is " << id << " data is " << data;
		// 将QByteArray转换为QJsonDocument
		QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

		// 检查转换是否成功
		if (jsonDoc.isNull()) {
			qDebug() << "Failed to create QJsonDocument.";
			return;
		}

		QJsonObject jsonObj = jsonDoc.object();

		if (!jsonObj.contains("error")) {
			int err = ErrorCode::ERR_JSON;
			qDebug() << "Notify Chat Msg Failed, err is Json Parse Err" << err;
			return;
		}

		int err = jsonObj["error"].toInt();
		if (err != ErrorCode::ERR_OK) {
			qDebug() << "Notify Chat Msg Failed, err is " << err;
			return;
		}

		qDebug() << "Receive Text Chat Notify Success ";
		auto msg_ptr = std::make_shared<TextChatMsg>(jsonObj["fromuid"].toInt(),
			jsonObj["touid"].toInt(), jsonObj["text_array"].toArray());
		emit sig_text_chat_msg(msg_ptr);
		});
}

void TcpMgr::handleMsg(Req id, int len, QByteArray data)
{
	auto find_iter = _handlers.find(id);
	if (find_iter == _handlers.end()) {
		qDebug() << "not found id [" << id << "] to handle";
		return;
	}
	find_iter.value()(id, len, data);
}

void TcpMgr::slot_tcp_connect(ServerInfo info)
{
	_socket.abort();
	_buffer.clear();
	_b_recv_pending = false;
	_connect_result_emitted = false;
	_host = info.Host;
	_port = static_cast<uint16_t>(info.Port.toUShort());
	_transport = info.Transport.trimmed().toLower();
	_tls_server_name = info.TlsServerName.trimmed();
	if (_tls_server_name.isEmpty()) {
		_tls_server_name = _host;
	}

	if (_host.isEmpty() || _port == 0) {
		qWarning() << "Invalid chat endpoint" << _host << info.Port;
		_connect_result_emitted = true;
		emit sig_con_success(false);
		return;
	}

	_use_tls = (_transport == "tls");
	if (!_use_tls && (_transport != "insecure" || !info.AllowInsecure)) {
		qWarning() << "Refusing insecure chat transport:" << _transport;
		_connect_result_emitted = true;
		emit sig_con_success(false);
		return;
	}

	qDebug() << "Connecting to chat server" << _host << _port << "via" << _transport;
	if (_use_tls) {
		if (!QSslSocket::supportsSsl()) {
			qWarning() << "TLS is unavailable in this Qt runtime:" << QSslSocket::sslLibraryBuildVersionString();
			_connect_result_emitted = true;
			emit sig_con_success(false);
			return;
		}
		QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
		sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
		_socket.setSslConfiguration(sslConfig);
		_socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
		_socket.connectToHostEncrypted(_host, _port, _tls_server_name);
		return;
	}

	_socket.connectToHost(_host, _port);
}

void TcpMgr::slot_send_data(Req reqId, QByteArray data)
{
	if (_socket.state() != QAbstractSocket::ConnectedState) {
		qDebug() << "Socket is not connected!";
		return;
	}
	if (_use_tls && !_socket.isEncrypted()) {
		qWarning() << "Refusing to send chat data before TLS is established";
		return;
	}
	constexpr int maxMessageLength = 2048;
	if (data.isEmpty() || data.size() > maxMessageLength) {
		qWarning() << "Refusing invalid chat payload length:" << data.size();
		return;
	}
	uint16_t id = reqId;

	// 计算长度（使用网络字节序转换）
	quint16 len = static_cast<quint16>(data.length());

	// 创建一个QByteArray用于存储要发送的所有数据
	QByteArray block;
	QDataStream out(&block, QIODevice::WriteOnly);

	// 设置数据流使用网络字节序
	out.setByteOrder(QDataStream::BigEndian);

	// 写入ID和长度
	out << id << len;

	// 添加字符串数据
	block.append(data);

	// 发送数据
	_socket.write(block);
	qDebug() << "tcp mgr send byte data is " << block;
}
