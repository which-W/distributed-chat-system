#include "ProxyManager.h"

#include <QCoreApplication>
#include <QNetworkProxyFactory>
#include <QSettings>

ProxyManager& ProxyManager::instance()
{
    static ProxyManager manager;
    return manager;
}

ProxyManager::ProxyManager(QObject* parent)
    : QObject(parent)
{
}

void ProxyManager::initialize(const QString& configPath)
{
    QSettings defaults(configPath, QSettings::IniFormat);
    const Mode defaultMode = modeFromString(defaults.value("Proxy/Mode", "direct").toString());
    const QString defaultHost = defaults.value("Proxy/Host", "127.0.0.1").toString().trimmed();
    const quint16 defaultPort = static_cast<quint16>(defaults.value("Proxy/Port", 10808).toUInt());

    QSettings userSettings;
    mode_ = modeFromString(userSettings.value("network/proxyMode", modeToString(defaultMode)).toString());
    host_ = userSettings.value("network/proxyHost", defaultHost).toString().trimmed();
    port_ = static_cast<quint16>(userSettings.value("network/proxyPort", defaultPort).toUInt());
    if (host_.isEmpty()) {
        host_ = QStringLiteral("127.0.0.1");
    }
    if (port_ == 0) {
        port_ = 10808;
    }
    initialized_ = true;
    applyToApplication();
}

ProxyManager::Mode ProxyManager::mode() const
{
    return mode_;
}

QString ProxyManager::host() const
{
    return host_;
}

quint16 ProxyManager::port() const
{
    return port_;
}

QNetworkProxy ProxyManager::socketProxy() const
{
    switch (mode_) {
    case Mode::Direct:
        return QNetworkProxy(QNetworkProxy::NoProxy);
    case Mode::Socks5:
        return QNetworkProxy(QNetworkProxy::Socks5Proxy, host_, port_);
    case Mode::System:
    default:
        return QNetworkProxy(QNetworkProxy::DefaultProxy);
    }
}

QString ProxyManager::modeName() const
{
    switch (mode_) {
    case Mode::Direct: return tr("Direct");
    case Mode::System: return tr("System proxy");
    case Mode::Socks5: return tr("SOCKS5 backup");
    }
    return tr("Direct");
}

bool ProxyManager::apply(Mode mode, const QString& host, quint16 port, QString* error)
{
    const QString normalizedHost = host.trimmed();
    if (mode == Mode::Socks5 && (normalizedHost.isEmpty() || port == 0)) {
        if (error) {
            *error = tr("SOCKS5 mode requires a valid host and port.");
        }
        return false;
    }

    const bool changed = mode_ != mode || host_ != normalizedHost || port_ != port;
    mode_ = mode;
    if (!normalizedHost.isEmpty()) {
        host_ = normalizedHost;
    }
    if (port != 0) {
        port_ = port;
    }
    applyToApplication();

    QSettings userSettings;
    userSettings.setValue("network/proxyMode", modeToString(mode_));
    userSettings.setValue("network/proxyHost", host_);
    userSettings.setValue("network/proxyPort", port_);
    userSettings.sync();

    if (changed && initialized_) {
        emit proxyChanged();
    }
    return true;
}

ProxyManager::Mode ProxyManager::modeFromString(const QString& value)
{
    const QString mode = value.trimmed().toLower();
    if (mode == "system") {
        return Mode::System;
    }
    if (mode == "socks5" || mode == "socks") {
        return Mode::Socks5;
    }
    return Mode::Direct;
}

QString ProxyManager::modeToString(Mode mode)
{
    switch (mode) {
    case Mode::System: return QStringLiteral("system");
    case Mode::Socks5: return QStringLiteral("socks5");
    case Mode::Direct:
    default: return QStringLiteral("direct");
    }
}

void ProxyManager::applyToApplication()
{
    if (mode_ == Mode::System) {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        return;
    }

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(socketProxy());
}
