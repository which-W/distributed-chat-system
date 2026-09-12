#pragma once
#include "FriendDialog.h"
#include "UserData.h"
#include <memory>
class FindSuccessWidght : public FriendDialog {
    Q_OBJECT
public:
    explicit FindSuccessWidght(QWidget* parent = nullptr);
    void SetSearchInfo(std::shared_ptr<SearchInfo> info);
private:
    std::shared_ptr<SearchInfo> info_;
    QLabel* description_{};
};
