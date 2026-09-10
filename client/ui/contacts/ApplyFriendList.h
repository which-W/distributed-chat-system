#pragma once
#include <QEvent>
#include <QListWidget>
#include <QScrollbar>
#include <QWheelEvent>
class ApplyFriendList : public QListWidget {
    Q_OBJECT
  public:
    ApplyFriendList(QWidget* parent = nullptr);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:

  signals:
    void sig_show_search(bool);
};
