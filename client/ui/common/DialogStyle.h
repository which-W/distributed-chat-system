#pragma once
#include "ChatStyle.h"
#include "ThemeManager.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

inline void styleFriendDialog(QDialog* dialog, const QString& title) {
    dialog->setWindowTitle(title);
    dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    dialog->setMinimumWidth(380);
    dialog->setStyleSheet(chatStyle(ThemeManager::instance().themeMode() == ElaThemeType::Dark));
    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, dialog,
        [dialog](ElaThemeType::ThemeMode mode) { dialog->setStyleSheet(chatStyle(mode == ElaThemeType::Dark)); });
    if (auto* layout = qobject_cast<QVBoxLayout*>(dialog->layout())) {
        layout->setContentsMargins(24, 20, 24, 24);
        layout->setSpacing(12);
        auto* header = new QHBoxLayout;
        auto* label = new QLabel(title, dialog);
        label->setStyleSheet("font-size: 20px; font-weight: 600; border: none;");
        auto* close = new QPushButton(QString::fromUtf8("×"), dialog);
        close->setObjectName("cancel_btn");
        close->setFixedSize(32, 32);
        close->setStyleSheet("padding: 0; font-size: 22px;");
        close->setToolTip(QObject::tr("关闭"));
        header->addWidget(label, 1);
        header->addWidget(close);
        layout->insertLayout(0, header);
        QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    }
    QTimer::singleShot(0, dialog, [dialog]() {
        if (auto* parent = dialog->parentWidget()) {
            auto* window = parent->window();
            dialog->move(window->mapToGlobal(window->rect().center()) - dialog->rect().center());
        }
    });
}
