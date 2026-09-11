#include "ChatView.h"

ChatView::ChatView(QWidget* parent) : QWidget(parent), isAppended(false) {
    // 创建主布局
    QVBoxLayout* pMainLayout = new QVBoxLayout();
    this->setLayout(pMainLayout);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    // 添加滚动区域
    m_pScrollArea = new QScrollArea();
    m_pScrollArea->setObjectName("chat_area");
    pMainLayout->addWidget(m_pScrollArea);

    // 构建消息内容容器
    QWidget* w = new QWidget(this);
    w->setObjectName("chat_bg");
    w->setAutoFillBackground(true);

    QVBoxLayout* pVLayout_1 = new QVBoxLayout();
    pVLayout_1->setContentsMargins(20, 18, 20, 18);
    pVLayout_1->setSpacing(12);
    pVLayout_1->addWidget(new QWidget(), 100000);
    w->setLayout(pVLayout_1);
    m_pScrollArea->setWidget(w);

    // 滚动条相关设置
    m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScrollBar* pVScrollBar = m_pScrollArea->verticalScrollBar();
    connect(pVScrollBar, &QScrollBar::rangeChanged, this, &ChatView::onVScrollBarMoved);

    // 其它设置
    m_pScrollArea->setWidgetResizable(true); // 允许内容自适应大小
    initStyleSheet();                        // 初始化样式表
}

void ChatView::appendChatItem(QWidget* item) {
    QVBoxLayout* vl = qobject_cast<QVBoxLayout*>(m_pScrollArea->widget()->layout());
    // UI 控件也必须与模型使用同一数量预算，否则仅限制 vector 仍会耗尽内存。
    if (vl->count() - 1 >= static_cast<int>(MAX_CHAT_HISTORY_MESSAGES)) {
        QLayoutItem* oldest = vl->takeAt(0);
        if (oldest) {
            delete oldest->widget();
            delete oldest;
        }
    }
    vl->insertWidget(vl->count() - 1, item);
    isAppended = true;
}

void ChatView::prependChatItem(QWidget* item) {
    QVBoxLayout* vl = qobject_cast<QVBoxLayout*>(m_pScrollArea->widget()->layout());
    if (vl->count() - 1 >= static_cast<int>(MAX_CHAT_HISTORY_MESSAGES)) {
        QLayoutItem* newest = vl->takeAt(vl->count() - 2);
        if (newest) {
            delete newest->widget();
            delete newest;
        }
    }
    vl->insertWidget(0, item);
    isAppended = true;
}

void ChatView::insertChatItem(QWidget* before, QWidget* item) {
    QVBoxLayout* vl = qobject_cast<QVBoxLayout*>(m_pScrollArea->widget()->layout());
    int index = vl->indexOf(before);
    if (index != -1) {
        vl->insertWidget(index, item);
        isAppended = true;
    } else {
        // 如果没有找到before，则直接添加到末尾
        appendChatItem(item);
    }
}

void ChatView::removeAllItem() {
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(m_pScrollArea->widget()->layout());

    int count = layout->count();

    for (int i = 0; i < count - 1; ++i) {
        QLayoutItem* item = layout->takeAt(0); // 始终从第一个控件开始删除
        if (item) {
            if (QWidget* widget = item->widget()) {
                delete widget;
            }
            delete item;
        }
    }
}

bool ChatView::eventFilter(QObject* o, QEvent* e) {
    /*if(e->type() == QEvent::Resize && o == )
    {

    }
    else */
    if (e->type() == QEvent::Enter && o == m_pScrollArea) {
        m_pScrollArea->verticalScrollBar()->setHidden(
            m_pScrollArea->verticalScrollBar()->maximum() == 0);
    } else if (e->type() == QEvent::Leave && o == m_pScrollArea) {
        m_pScrollArea->verticalScrollBar()->setHidden(true);
    }
    return QWidget::eventFilter(o, e);
}
void ChatView::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatView::onVScrollBarMoved(int min, int max) {
    if (isAppended) // 添加item可能调用多次
    {
        QScrollBar* pVScrollBar = m_pScrollArea->verticalScrollBar();
        pVScrollBar->setSliderPosition(pVScrollBar->maximum());
        // 500毫秒内可能调用多次
        QTimer::singleShot(500, this, [this]() { isAppended = false; });
    }
}

void ChatView::initStyleSheet() {}
