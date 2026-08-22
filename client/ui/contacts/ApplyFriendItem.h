#pragma once

#include <QWidget>
#include "ui_ApplyFriendItem.h"
#include <memory>
#include "UserData.h"
#include <QPixmap>
#include "ListItemBase.h"
QT_BEGIN_NAMESPACE
namespace Ui { class ApplyFriendItem; };
QT_END_NAMESPACE

class ApplyFriendItem : public ListItemBase
{
	Q_OBJECT
public:
    explicit ApplyFriendItem(QWidget* parent = nullptr);
    ~ApplyFriendItem();
    void SetInfo(std::shared_ptr<ApplyInfo> apply_info);
    void ShowAddBtn(bool bshow);
    QSize sizeHint() const override {
        return QSize(250, 80); // 返回自定义的尺寸
    }
    int GetUid();
private:
    Ui::ApplyFriendItem* ui;
    std::shared_ptr<ApplyInfo> _apply_info;
    bool _added;
public slots:
    void slot_ShowAddBtn();
signals:
    void sig_auth_friend(std::shared_ptr<ApplyInfo> apply_info);
};
