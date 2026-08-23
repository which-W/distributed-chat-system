#include <QApplication>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "ElaTheme.h"

namespace {
QWidget* createAuthPage(const QString& buttonText, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(new ElaLineEdit(page));
    layout->addWidget(new ElaPushButton(buttonText, page));
    return page;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QStackedWidget pages;
    auto* login = createAuthPage(QStringLiteral("Login"), &pages);
    auto* registration = createAuthPage(QStringLiteral("Register"), &pages);
    auto* reset = createAuthPage(QStringLiteral("Reset password"), &pages);
    pages.addWidget(login);
    pages.addWidget(registration);
    pages.addWidget(reset);

    pages.setCurrentWidget(registration);
    const bool registrationSelected = pages.currentWidget() == registration;
    pages.setCurrentWidget(reset);
    const bool resetSelected = pages.currentWidget() == reset;
    pages.setCurrentWidget(login);
    const bool loginSelected = pages.currentWidget() == login;

    const auto initialTheme = eTheme->getThemeMode();
    eTheme->setThemeMode(ElaThemeType::Light);
    const bool lightApplied = eTheme->getThemeMode() == ElaThemeType::Light;
    eTheme->setThemeMode(ElaThemeType::Dark);
    const bool darkApplied = eTheme->getThemeMode() == ElaThemeType::Dark;
    eTheme->setThemeMode(initialTheme);

    return pages.count() == 3 && registrationSelected && resetSelected && loginSelected &&
                   lightApplied && darkApplied && eTheme->getThemeMode() == initialTheme
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
