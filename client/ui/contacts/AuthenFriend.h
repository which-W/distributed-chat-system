#pragma once

#include <QDialog>
#include "ui_AuthenFriend.h"
#include "usermgr.h"
#include "ClickedBtn.h"
#include "ClickLabel.h"
#include "FriendLabel.h"
#include "ClickOnceLabel.h"
#include "TcpMgr.h"
#include <QScrollBar>
#include <QTextCodec>
QT_BEGIN_NAMESPACE
namespace Ui { class AuthenFriendClass; };
QT_END_NAMESPACE

class AuthenFriend : public QDialog
{
	Q_OBJECT

public:
    explicit AuthenFriend(QWidget* parent = nullptr);
    ~AuthenFriend();

    void InitTipLbs();
    void AddTipLbs(ClickLabel*, QPoint cur_point, QPoint& next_point, int text_width, int text_height);
    bool eventFilter(QObject* obj, QEvent* event);
    void SetApplyInfo(std::shared_ptr<ApplyInfo> apply_info);
private:
    void resetLabels();
    //已经创建好的标签
    QMap<QString, ClickLabel*> _add_labels;
    std::vector<QString> _add_label_keys;
    QPoint _label_point;
    //用来在输入框显示添加新好友的标签
    QMap<QString, FriendLabel*> _friend_labels;
    std::vector<QString> _friend_label_keys;
    void addLabel(QString name);
    std::vector<QString> _tip_data;
    QPoint _tip_cur_point;
    QTextCodec* codec;
public slots:
    //显示更多label标签
    void ShowMoreLabel();
    //输入label按下回车触发将标签加入展示栏
    void SlotLabelEnter();
    //点击关闭，移除展示栏好友便签
    void SlotRemoveFriendLabel(QString);
    //通过点击tip实现增加和减少好友便签
    void SlotChangeFriendLabelByTip(QString, ClickLbState);
    //输入框文本变化显示不同提示
    void SlotLabelTextChange(const QString& text);
    //输入框输入完成
    void SlotLabelEditFinished();
    //输入标签显示提示框，点击提示框内容后添加好友便签
    void SlotAddFirendLabelByClickTip(QString text);
    //处理确认回调
    void SlotApplySure();
    //处理取消回调
    void SlotApplyCancel();

private:
    std::shared_ptr<ApplyInfo> _apply_info;
    Ui::AuthenFriendClass* ui;
};
