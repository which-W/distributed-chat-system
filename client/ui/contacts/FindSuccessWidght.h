#pragma once

#include <QDialog>
#include "ui_FindSuccessWidght.h"
#include <QDir>
#include "global.h"
#include "UserData.h"
#include "ApplyFriend.h"
QT_BEGIN_NAMESPACE
namespace Ui { class FindSuccessWidghtClass; };
QT_END_NAMESPACE

class FindSuccessWidght : public QDialog
{
	Q_OBJECT

public:
    explicit FindSuccessWidght(QWidget* parent = nullptr);
    ~FindSuccessWidght();
    void SetSearchInfo(std::shared_ptr<SearchInfo> si);
private slots:
    void on_add_friend_btn_clicked();

private:
    QTextCodec* codec;
    Ui::FindSuccessWidghtClass* ui;
    QWidget* _parent;
    std::shared_ptr<SearchInfo> _si;
};
