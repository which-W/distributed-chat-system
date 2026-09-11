#include "BubbleFrame.h"
#include "ElaTheme.h"

const int WIDTH_SANJIAO = 8; // 三角宽

BubbleFrame::BubbleFrame(ChatRole role, QWidget* parent)
    : QFrame(parent), m_role(role), m_margin(3) {
    m_pHLayout = new QHBoxLayout();
    m_pHLayout->setContentsMargins(12, 7, 12, 7);

    this->setLayout(m_pHLayout);
}

void BubbleFrame::setWidget(QWidget* w) {
    if (m_pHLayout->count() > 0)
        return;
    else {
        m_pHLayout->addWidget(w);
    }
}

void BubbleFrame::paintEvent(QPaintEvent* e) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const bool dark = eTheme->getThemeMode() == ElaThemeType::Dark;
    painter.setBrush(m_role == ChatRole::Self ? QColor("#0866ff")
                                             : QColor(dark ? "#303238" : "#f0f2f5"));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 16, 16);
    Q_UNUSED(e);
}
