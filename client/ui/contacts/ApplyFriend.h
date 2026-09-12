#pragma once
#include "FriendDialog.h"
#include "UserData.h"
#include <memory>
class ApplyFriend : public FriendDialog {
    Q_OBJECT
public:
    explicit ApplyFriend(QWidget* parent = nullptr);
    void SetSearchInfo(std::shared_ptr<SearchInfo> info);
public slots:
    void SlotApplySure();
    void SlotApplyCancel() { reject(); }
private:
    std::shared_ptr<SearchInfo> info_;
    QLabel* status_{};
    QPushButton* send_{};
    bool pending_{false};
};
