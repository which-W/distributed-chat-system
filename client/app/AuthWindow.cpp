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

    void setDarkMode(bool dark)
    {
        if (darkMode_ == dark) {
            return;
        }
        darkMode_ = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
        gradient.setColorAt(0.0, darkMode_ ? QColor(39, 30, 104) : QColor(91, 72, 225));
        gradient.setColorAt(0.48, darkMode_ ? QColor(62, 52, 148) : QColor(105, 95, 225));
        gradient.setColorAt(1.0, darkMode_ ? QColor(12, 91, 112) : QColor(35, 181, 201));
        painter.setBrush(gradient);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect().adjusted(8, 8, -4, -8), 24, 24);

        painter.setBrush(QColor(255, 255, 255, 24));
        painter.drawEllipse(QPointF(width() * 0.75, height() * 0.22), 130, 130);
        painter.drawEllipse(QPointF(width() * 0.18, height() * 0.82), 190, 190);
    }

private:
    bool darkMode_{true};
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
    setObjectName("authWindow");
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(tr("Nebula Chat · Sign in"));
    setWindowIcon(QIcon(":/new/prefix1/res/R-C.png"));
    resize(960, 640);
    setMinimumSize(860, 580);
    setIsFixedSize(false);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, false);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 46, 18, 18);
    rootLayout->setSpacing(18);

    heroPanel_ = new HeroPanel(this);
    heroPanel_->setObjectName("heroPanel");
    heroPanel_->setMinimumWidth(350);
    auto* heroLayout = new QVBoxLayout(heroPanel_);
    heroLayout->setContentsMargins(44, 56, 44, 42);
    heroLayout->addStretch();
    auto* brand = new ElaText(tr("NEBULA"), heroPanel_);
    brand->setTextPixelSize(38);
    brand->setStyleSheet("color: white; font-weight: 700; background: transparent;");
    auto* title = new ElaText(tr("Connect without boundaries."), heroPanel_);
    title->setTextPixelSize(24);
    title->setWordWrap(true);
    title->setStyleSheet("color: white; font-weight: 600; background: transparent;");
    auto* subtitle = new ElaText(tr("Secure distributed messaging, wrapped in a calm Fluent workspace."), heroPanel_);
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
    pages_->setObjectName("authPages");
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

    rootLayout->addWidget(heroPanel_, 5);
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
    static_cast<HeroPanel*>(heroPanel_)->setDarkMode(dark);
    themeButton_->setAwesome(dark ? ElaIconType::SunBright : ElaIconType::MoonStars);
    themeButton_->setToolTip(dark ? tr("Switch to light theme") : tr("Switch to dark theme"));
    setStyleSheet(QString(
        "#authWindow { background: %5; }"
        "#authCard { background: %1; border: 1px solid %2; border-radius: 20px; }"
        "#authPages { background: transparent; border: none; }"
        "#authCard QDialog { background: transparent; }"
        "#authCard ElaLineEdit, #authCard ElaPushButton { min-height: 34px; }"
        "#authCard TimerBtn { min-height: 34px; padding: 0 10px; border-radius: 8px; "
        "border: 1px solid %2; background: %3; color: %4; }"
        "#authCard QLabel { color: %4; background: transparent; }")
        .arg(dark ? "rgba(27,29,40,232)" : "rgba(255,255,255,238)",
             dark ? "#414555" : "#D7DAE3",
             dark ? "#20222D" : "#F5F6FA",
             dark ? "#F4F5F8" : "#20222A")
        .arg(dark
                 ? "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #11131C, stop:1 #20243A)"
                 : "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #F4F6FC, stop:1 #E8EDFA)"));

    refreshWidgetTree(this);
}
