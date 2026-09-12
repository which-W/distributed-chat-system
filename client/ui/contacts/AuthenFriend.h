#pragma once
#include "FriendDialog.h"
#include "UserData.h"
#include <QLineEdit>
#include <memory>
class AuthenFriend : public FriendDialog {
    Q_OBJECT
public:
    explicit AuthenFriend(QWidget* parent = nullptr);
    void SetApplyInfo(std::shared_ptr<ApplyInfo> info);
public slots:
    void SlotApplySure();
    void SlotApplyCancel() { reject(); }
private:
    std::shared_ptr<ApplyInfo> info_;
    QLabel* invitation_{};
    QLabel* status_{};
    QLineEdit* remark_{};
    QPushButton* accept_{};
    bool pending_{false};
};
