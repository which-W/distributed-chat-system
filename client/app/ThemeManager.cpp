#include "ThemeManager.h"

#include <QColor>
#include <QGuiApplication>
#include <QSettings>

#include "ElaApplication.h"
#include "ElaTheme.h"

namespace {
constexpr auto kThemeKey = "appearance/theme";
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {}

void ThemeManager::initialize() {
    const bool isOffscreen = QGuiApplication::platformName() == QStringLiteral("offscreen");
    if (!isOffscreen) {
        eApp->init();
    }
    eApp->setWindowDisplayMode(isOffscreen ? ElaApplicationType::Normal
                                           : ElaApplicationType::ElaMica);

    const QColor accent(8, 102, 255);
    eTheme->setThemeColor(ElaThemeType::Dark, ElaThemeType::PrimaryNormal, accent);
    eTheme->setThemeColor(ElaThemeType::Dark, ElaThemeType::PrimaryHover, QColor(36, 120, 255));
    eTheme->setThemeColor(ElaThemeType::Dark, ElaThemeType::PrimaryPress, QColor(0, 84, 214));
    eTheme->setThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryNormal, accent);
    eTheme->setThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryHover, QColor(36, 120, 255));
    eTheme->setThemeColor(ElaThemeType::Light, ElaThemeType::PrimaryPress, QColor(0, 84, 214));

    const QSettings settings;
    const auto saved = settings.value(kThemeKey, static_cast<int>(ElaThemeType::Light)).toInt();
    setThemeMode(saved == static_cast<int>(ElaThemeType::Light) ? ElaThemeType::Light
                                                                : ElaThemeType::Dark);
}

ElaThemeType::ThemeMode ThemeManager::themeMode() const {
    return eTheme->getThemeMode();
}

void ThemeManager::setThemeMode(ElaThemeType::ThemeMode mode) {
    if (eTheme->getThemeMode() != mode) {
        eTheme->setThemeMode(mode);
    }
    QSettings settings;
    settings.setValue(kThemeKey, static_cast<int>(mode));
    emit themeChanged(mode);
}

void ThemeManager::toggleTheme() {
    setThemeMode(themeMode() == ElaThemeType::Dark ? ElaThemeType::Light : ElaThemeType::Dark);
}
