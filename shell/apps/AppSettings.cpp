#include "AppSettings.h"

#include <QSettings>
#include <QStandardPaths>

namespace {

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/tvshell.conf");
}

QString key(const QString &app)
{
    return QStringLiteral("apps/") + app + QStringLiteral("/closeAfter");
}

// What every web app reads, before its own file.
QString webAppSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/apps/webapp.conf");
}

const QString SuspendKey = QStringLiteral("suspendAfter");

} // namespace

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    for (auto it = m_closeAfter.begin(); it != m_closeAfter.end(); ++it) {
        bool ok = false;
        const int seconds = s.value(key(it.key())).toInt(&ok);
        if (ok && seconds >= 0)
            it.value() = seconds;
    }
    QSettings webApps(webAppSettingsPath(), QSettings::IniFormat);
    bool ok = false;
    const int seconds = webApps.value(SuspendKey).toInt(&ok);
    if (ok && seconds >= 0)
        m_webAppSuspendAfter = seconds;
    else
        webApps.setValue(SuspendKey, m_webAppSuspendAfter);
}

void AppSettings::setWebAppSuspendAfter(int seconds)
{
    if (seconds < 0 || m_webAppSuspendAfter == seconds)
        return;
    m_webAppSuspendAfter = seconds;
    QSettings webApps(webAppSettingsPath(), QSettings::IniFormat);
    webApps.setValue(SuspendKey, seconds);
    emit changed();
}

void AppSettings::setCloseAfter(const QString &app, int seconds)
{
    if (!m_closeAfter.contains(app) || seconds < 0 || m_closeAfter.value(app).toInt() == seconds)
        return;
    m_closeAfter.insert(app, seconds);
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(key(app), seconds);
    emit changed();
}
