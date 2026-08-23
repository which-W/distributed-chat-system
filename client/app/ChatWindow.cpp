#include "ChatWindow.h"

#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include "ChatDialog.h"
#include "ElaIconButton.h"
#include "ElaPushButton.h"
#include "ElaText.h"
#include "ThemeManager.h"
#include "usermgr.h"

namespace {
QWidget* createSettingsPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName("settingsPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 42, 48, 42);
    layout->setSpacing(16);
    auto* title = new ElaText(QObject::tr("Appearance"), page);
    title->setTextPixelSize(28);
    auto* description = new ElaText(QObject::tr("Choose the theme used across authentication, conversations and contacts."), page);
    description->setTextPixelSize(14);
    description->setWordWrap(true);
    auto* themeButton = new ElaPushButton(QObject::tr("Switch light / dark theme"), page);
    themeButton->setMaximumWidth(260);
    QObject::connect(themeButton, &QPushButton::clicked,
                     &ThemeManager::instance(), &ThemeManager::toggleTheme);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(10);
    layout->addWidget(themeButton);
    layout->addStretch();
    return page;
}
}

ChatWindow::ChatWindow(QWidget* parent)
    : ElaWindow(parent)
{
    setWindowTitle(tr("Nebula Chat"));
    setWindowIcon(QIcon(":/new/prefix1/res/R-C.png"));
    resize(1280, 800);
    setMinimumSize(1000, 680);
    setIsNavigationBarEnable(false);
    setUserInfoCardVisible(false);
    setIsCentralStackedWidgetTransparent(true);

    auto* shell = new QWidget(this);
    shell->setObjectName("chatShell");
    auto* shellLayout = new QHBoxLayout(shell);
    shellLayout->setContentsMargins(10, 10, 10, 10);
    shellLayout->setSpacing(10);

    auto* rail = new QWidget(shell);
    rail->setObjectName("navigationRail");
    rail->setFixedWidth(64);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 12, 8, 12);
    railLayout->setSpacing(10);

    messagesButton_ = new ElaIconButton(ElaIconType::Messages, 19, 48, 48, rail);
    contactsButton_ = new ElaIconButton(ElaIconType::AddressBook, 19, 48, 48, rail);
    settingsButton_ = new ElaIconButton(ElaIconType::GearComplex, 19, 48, 48, rail);
    messagesButton_->setToolTip(tr("Messages"));
    contactsButton_->setToolTip(tr("Contacts"));
    settingsButton_->setToolTip(tr("Settings"));
    railLayout->addWidget(messagesButton_);
    railLayout->addWidget(contactsButton_);
    railLayout->addStretch();
    railLayout->addWidget(settingsButton_);

    themeButton_ = new ElaIconButton(ElaIconType::MoonStars, 18, 48, 48, rail);
    themeButton_->setToolTip(tr("Switch theme"));
    railLayout->addWidget(themeButton_);

    auto* logoutButton = new ElaIconButton(ElaIconType::RightFromBracket, 18, 48, 48, rail);
    logoutButton->setToolTip(tr("Sign out"));
    railLayout->addWidget(logoutButton);

    contentStack_ = new QStackedWidget(shell);
    contentStack_->setObjectName("contentStack");
    workspace_ = new ChatDialog(contentStack_);
    workspace_->setExternalNavigationEnabled(true);
    workspace_->setMinimumSize(0, 0);
    workspace_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    workspace_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QFile legacyStyle(QCoreApplication::applicationDirPath() + "/style/this_style.qss");
    if (legacyStyle.open(QFile::ReadOnly)) {
        workspace_->setStyleSheet(QString::fromUtf8(legacyStyle.readAll()));
    }

    contentStack_->addWidget(workspace_);
    contentStack_->addWidget(createSettingsPage(contentStack_));
    shellLayout->addWidget(rail);
    shellLayout->addWidget(contentStack_, 1);
    addCentralWidget(shell);

    const QString icon = UserMgr::Getinstance()->GetIcon();
    if (!icon.isEmpty()) {
        auto* avatar = new QLabel(rail);
        QPixmap pixmap(icon);
        avatar->setPixmap(pixmap.scaled(40, 40, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        avatar->setFixedSize(40, 40);
        avatar->setScaledContents(true);
        railLayout->insertWidget(0, avatar, 0, Qt::AlignHCenter);
    }

    connect(messagesButton_, &QPushButton::clicked, this, &ChatWindow::showMessages);
    connect(contactsButton_, &QPushButton::clicked, this, &ChatWindow::showContacts);
    connect(settingsButton_, &QPushButton::clicked, this, &ChatWindow::showSettings);
    connect(themeButton_, &QPushButton::clicked, &ThemeManager::instance(), &ThemeManager::toggleTheme);
    connect(logoutButton, &QPushButton::clicked, this, &ChatWindow::logoutRequested);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ChatWindow::applyTheme);

    showMessages();
    applyTheme(ThemeManager::instance().themeMode());
}

void ChatWindow::showMessages()
{
    contentStack_->setCurrentWidget(workspace_);
    workspace_->slot_side_chat();
    selectRailButton(messagesButton_);
}

void ChatWindow::showContacts()
{
    contentStack_->setCurrentWidget(workspace_);
    workspace_->slot_side_contact();
    selectRailButton(contactsButton_);
}

void ChatWindow::showSettings()
{
    contentStack_->setCurrentIndex(1);
    selectRailButton(settingsButton_);
}

void ChatWindow::selectRailButton(ElaIconButton* selected)
{
    messagesButton_->setIsSelected(selected == messagesButton_);
    contactsButton_->setIsSelected(selected == contactsButton_);
    settingsButton_->setIsSelected(selected == settingsButton_);
}

void ChatWindow::applyTheme(ElaThemeType::ThemeMode mode)
{
    const bool dark = mode == ElaThemeType::Dark;
    themeButton_->setAwesome(dark ? ElaIconType::SunBright : ElaIconType::MoonStars);
    themeButton_->setToolTip(dark ? tr("Switch to light theme") : tr("Switch to dark theme"));
    setStyleSheet(QString(
        "#navigationRail { background: %1; border: 1px solid %2; border-radius: 16px; }"
        "#contentStack, #settingsPage { background: %3; border: 1px solid %2; border-radius: 16px; }")
        .arg(dark ? "rgba(25,27,38,225)" : "rgba(252,252,255,235)",
             dark ? "#3B3F50" : "#D8DAE4",
             dark ? "rgba(30,32,43,220)" : "rgba(255,255,255,235)"));
    const auto widgets = findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    emit themeChanged(mode);
}
