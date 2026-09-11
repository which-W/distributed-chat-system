#pragma once
#include "AddUserItem.h"
#include "FindFailWidget.h"
#include "FindSuccessWidght.h"
#include "Loadingdlg.h"
#include "TcpMgr.h"
#include "UserData.h"
#include "usermgr.h"
#include <QDialog>
#include <QEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>
#include <memory>
class SearchList : public QListWidget {
    Q_OBJECT
  public:
    SearchList(QWidget* parent = nullptr);
    void CloseFindDlg();
    void SetSearchEdit(QWidget* edit);
    void search();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void waitPending(bool pending = true);
    bool _send_pending;
    void addTipItem();
    std::shared_ptr<QDialog> _find_dlg;
    QWidget* _search_edit;
    QTimer* _searchTimer{nullptr};
    UserMgr* _user_mgr;
  private slots:
    void slot_item_clicked(QListWidgetItem* item);
    void slot_user_search(std::shared_ptr<SearchInfo> si);

  signals:
    void sig_jump_chat_item(std::shared_ptr<SearchInfo> si);
};
