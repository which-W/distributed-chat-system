#pragma once
#include <QListWidget>
#include <QEvent>
#include <QWheelEvent>
#include <QScrollbar>
class ApplyFriendList : public QListWidget
{
    Q_OBJECT
public:
    ApplyFriendList(QWidget* parent = nullptr);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:

signals:
    void sig_show_search(bool);
};
