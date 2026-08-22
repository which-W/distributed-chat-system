#include "ChatUserList.h"

ChatUserList::ChatUserList(QWidget* parent):_load_pending(false)
{
    Q_UNUSED(parent);
	this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 设置水平滚动条为始终不显示
	this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 设置垂直滚动条为始终不显示
    // 安装事件过滤器
    this->viewport()->installEventFilter(this);

}

bool ChatUserList::eventFilter(QObject* watched, QEvent* event)
{
    // 检查事件是否是鼠标悬浮进入或离开
    if (watched == this->viewport()) {
        if (event->type() == QEvent::Enter) {
            // 鼠标悬浮，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        else if (event->type() == QEvent::Leave) {
            // 鼠标离开，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 检查事件是否是鼠标滚轮事件
    if (watched == this->viewport() && event->type() == QEvent::Wheel) {
        auto wheelEvent = static_cast<QWheelEvent*>(event);
		int numDegrees = wheelEvent->angleDelta().y() / 8; // 获取滚轮滚动的角度，单位为度
        int numSteps = numDegrees / 15; // 计算滚动步数

        // 设置滚动幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        // 检查是否滚动到底部
        QScrollBar* scrollBar = this->verticalScrollBar();
        int maxScrollValue = scrollBar->maximum();
        int currentValue = scrollBar->value();

        if (maxScrollValue - currentValue <= 0) {
            auto b_loaded = UserMgr::Getinstance()->IsLoadChatFin();
            if (b_loaded) {
                return true;
            }
            //如果未加载完成则直接返回
            if (_load_pending) {
                return true;
            }
            // 滚动到底部，加载新的联系人
            qDebug() << "load more chat user";
            _load_pending = true;

            QTimer::singleShot(100, [this]() {
                _load_pending = false;
                QCoreApplication::quit(); // 完成后退出应用程序
                });
            //发送信号通知聊天界面加载更多聊天内容
            emit sig_loading_chat_user();
        }

        return true; // 停止事件传递
    }

    return QListWidget::eventFilter(watched, event);
}
