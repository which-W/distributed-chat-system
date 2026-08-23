#include "AuthWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include "ElaIconButton.h"
#include "ElaText.h"
#include "LoginDialog.h"
#include "RegisterDialog.h"
#include "ResetDialog.h"
#include "TcpMgr.h"
#include "ThemeManager.h"

namespace {
class HeroPanel final : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
        gradient.setColorAt(0.0, QColor(64, 47, 150));
        gradient.setColorAt(0.48, QColor(75, 70, 190));
        gradient.setColorAt(1.0, QColor(21, 167, 189));
        painter.setBrush(gradient);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect().adjusted(8, 8, -4, -8), 24, 24);

        painter.setBrush(QColor(255, 255, 255, 24));
        painter.drawEllipse(QPointF(width() * 0.75, height() * 0.22), 130, 130);
        painter.drawEllipse(QPointF(width() * 0.18, height() * 0.82), 190, 190);
    }
};

void prepareEmbeddedDialog(QDialog* dialog)
{
    dialog->setWindowFlags(Qt::Widget);
    dialog->setMinimumSize(0, 0);
    dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    dialog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void refreshWidgetTree(QWidget* root)
{
    root->style()->unpolish(root);
    root->style()->polish(root);
    root->update();
    const auto children = root->findChildren<QWidget*>();
    for (QWidget* child : children) {
        child->style()->unpolish(child);
        child->style()->polish(child);
        child->update();
    }
}
}

AuthWindow::AuthWindow(QWidget* parent)
    : ElaWidget(parent)
{
    setWindowTitle(tr("Nebula Chat · Sign in"));
    setWindowIcon(QIcon(":/new/prefix1/res/R-C.png"));
    resize(960, 640);
    setMinimumSize(860, 580);
    setIsFixedSize(false);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, false);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 46, 18, 18);
    rootLayout->setSpacing(18);

    auto* hero = new HeroPanel(this);
    hero->setMinimumWidth(350);
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(44, 56, 44, 42);
    heroLayout->addStretch();
    auto* brand = new ElaText(tr("NEBULA"), hero);
    brand->setTextPixelSize(38);
    brand->setStyleSheet("color: white; font-weight: 700; background: transparent;");
    auto* title = new ElaText(tr("Connect without boundaries."), hero);
    title->setTextPixelSize(24);
    title->setWordWrap(true);
    title->setStyleSheet("color: white; font-weight: 600; background: transparent;");
    auto* subtitle = new ElaText(tr("Secure distributed messaging, wrapped in a calm Fluent workspace."), hero);
    subtitle->setTextPixelSize(14);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color: rgba(255,255,255,205); background: transparent;");
    heroLayout->addWidget(brand);
    heroLayout->addSpacing(14);
    heroLayout->addWidget(title);
    heroLayout->addSpacing(12);
    heroLayout->addWidget(subtitle);
    heroLayout->addStretch(2);

    auto* formCard = new QWidget(this);
    formCard->setObjectName("authCard");
    auto* formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(22, 18, 22, 18);

    auto* tools = new QHBoxLayout;
    tools->addStretch();
    themeButton_ = new ElaIconButton(ElaIconType::MoonStars, 16, 36, 36, formCard);
    themeButton_->setToolTip(tr("Switch theme"));
    tools->addWidget(themeButton_);
    formLayout->addLayout(tools);

    pages_ = new QStackedWidget(formCard);
    login_ = new LoginDialog(pages_);
    register_ = new RegisterDialog(pages_);
    reset_ = new ResetDialog(pages_);
    prepareEmbeddedDialog(login_);
    prepareEmbeddedDialog(register_);
    prepareEmbeddedDialog(reset_);
    pages_->addWidget(login_);
    pages_->addWidget(register_);
    pages_->addWidget(reset_);
    formLayout->addWidget(pages_, 1);

    rootLayout->addWidget(hero, 5);
    rootLayout->addWidget(formCard, 6);

    connect(themeButton_, &QPushButton::clicked, &ThemeManager::instance(), &ThemeManager::toggleTheme);
    connect(login_, &LoginDialog::switch_RegisterDialog, this, &AuthWindow::showRegister);
    connect(login_, &LoginDialog::sig_switch_Reset, this, &AuthWindow::showReset);
    connect(register_, &RegisterDialog::sig_retrun_login, this, &AuthWindow::showLogin);
    connect(reset_, &ResetDialog::switchLogin, this, &AuthWindow::showLogin);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_swich_chatdlg,
            this, &AuthWindow::authenticationSucceeded);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &AuthWindow::applyTheme);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &AuthWindow::themeChanged);
    connect(pages_, &QStackedWidget::currentChanged, this, [this] {
        applyTheme(ThemeManager::instance().themeMode());
    });
    applyTheme(ThemeManager::instance().themeMode());
}

void AuthWindow::showLogin() { pages_->setCurrentWidget(login_); }
void AuthWindow::showRegister() { pages_->setCurrentWidget(register_); }
void AuthWindow::showReset() { pages_->setCurrentWidget(reset_); }

void AuthWindow::applyTheme(ElaThemeType::ThemeMode mode)
{
    const bool dark = mode == ElaThemeType::Dark;
    themeButton_->setAwesome(dark ? ElaIconType::SunBright : ElaIconType::MoonStars);
    themeButton_->setToolTip(dark ? tr("Switch to light theme") : tr("Switch to dark theme"));
    setStyleSheet(QString(
        "#authCard { background: %1; border: 1px solid %2; border-radius: 20px; }"
        "#authCard QDialog { background: transparent; }"
        "#authCard ElaLineEdit, #authCard ElaPushButton { min-height: 34px; }"
        "#authCard TimerBtn { min-height: 34px; padding: 0 10px; border-radius: 8px; "
        "border: 1px solid %2; background: %3; color: %4; }"
        "#authCard QLabel { color: %4; background: transparent; }"
        "#authCard #err_tip { min-height: 32px; padding: 5px 12px; border-radius: 9px; "
        "font-weight: 600; }"
        "#authCard #err_tip[feedback='success'] { color: %5; background: %6; border: 1px solid %7; }"
        "#authCard #err_tip[feedback='error'] { color: %8; background: %9; border: 1px solid %10; }"
        "#authCard #err_tip[feedback='none'] { min-height: 0; padding: 0; border: none; background: transparent; }")
        .arg(dark ? "rgba(27,29,40,232)" : "rgba(255,255,255,238)",
             dark ? "#414555" : "#D7DAE3",
             dark ? "#20222D" : "#F5F6FA",
             dark ? "#F4F5F8" : "#20222A",
             dark ? "#6EE7A7" : "#087A46",
             dark ? "rgba(21,128,82,70)" : "rgba(16,185,129,35)",
             dark ? "#267A55" : "#65C99A",
             dark ? "#FF9B9B" : "#B4232D",
             dark ? "rgba(190,45,55,68)" : "rgba(239,68,68,32)",
             dark ? "#8F3941" : "#E69A9F"));

    refreshWidgetTree(login_);
    refreshWidgetTree(register_);
    refreshWidgetTree(reset_);
}
