#pragma once

#include <QWidget>
#include "ui_ConUserItem.h"
#include "ListItemBase.h"
#include "UserData.h"
#include <QPixmap>
#include "statewidget.h"
QT_BEGIN_NAMESPACE
namespace Ui { class ConUserItemClass; };
QT_END_NAMESPACE

class ConUserItem : public ListItemBase
{
	Q_OBJECT
public:
    explicit ConUserItem(QWidget* parent = nullptr);
    ~ConUserItem();
    QSize sizeHint() const override;
    void SetInfo(std::shared_ptr<AuthInfo> auth_info);
    void SetInfo(std::shared_ptr<AuthRsp> auth_rsp);
    void SetInfo(int uid, QString name, QString icon);
    void ShowRedPoint(bool show = false);
    std::shared_ptr<UserInfo> GetInfo();
private:
    Ui::ConUserItemClass* ui;
    std::shared_ptr<UserInfo> _info;
};
