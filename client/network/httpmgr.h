#pragma once
#include"global.h"
#include "Singleton.h"
#include <QNetworkAccessManager>
class Httpmgr : public QObject, public Singleton<Httpmgr>, public std::enable_shared_from_this<Httpmgr>
{
	Q_OBJECT
public:

	~Httpmgr();
	void PostHttpRequest(const QString& url, const QJsonObject& jsonObj, Req id, Modules mod);

private:
	friend class Singleton<Httpmgr>;
	Httpmgr();
	QNetworkAccessManager _manager;

public slots:
	void slot_http_finished(Req id, QString res, ErrorCode error, Modules mod);
signals:
	void sig_http_finish(Req id, QString res, ErrorCode err, Modules mod);
	void sig_reg_mod_finish(Req id, QString res, ErrorCode err);
	void sig_reset_mod_finish(Req id, QString res, ErrorCode err);
	void sig_login_finish(Req id, QString res, ErrorCode err);
};
