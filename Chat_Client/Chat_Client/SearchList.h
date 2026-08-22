#pragma once
#include <QListWidget>
#include <QDialog>
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QWidget>
#include <QListWidgetItem>
#include <memory>
#include "Loadingdlg.h"
#include "UserData.h"
#include "TcpMgr.h"
#include "AddUserItem.h"
#include "FindSuccessWidght.h"
#include "FindFailWidget.h"
#include "usermgr.h"
class SearchList : public QListWidget
{
    Q_OBJECT
public:
    SearchList(QWidget* parent = nullptr);
    void CloseFindDlg();
    void SetSearchEdit(QWidget* edit);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void waitPending(bool pending = true);
    bool _send_pending;
    void addTipItem();
    std::shared_ptr<QDialog> _find_dlg;
    QWidget* _search_edit;
    Loadingdlg* _loadingDialog;
    UserMgr* _user_mgr;
private slots:
    void slot_item_clicked(QListWidgetItem* item);
    void slot_user_search(std::shared_ptr<SearchInfo> si);

signals:
    void sig_jump_chat_item(std::shared_ptr<SearchInfo> si);
};
