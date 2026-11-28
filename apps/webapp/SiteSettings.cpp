#include "SiteSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcSettings, "webapp.settings")

SiteSettings::SiteSettings(const QString &name, QObject *parent)
    : QObject(parent)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/apps/");
    m_paths = {folder + QStringLiteral("webapp.conf"), folder + name + QStringLiteral(".conf")};
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &SiteSettings::read);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &SiteSettings::read);
    read();
}

void SiteSettings::watch()
{
    const QString folder = QFileInfo(m_paths.first()).absolutePath();
    QDir().mkpath(folder);
    if (!m_watcher.directories().contains(folder))
        m_watcher.addPath(folder);
    for (const QString &path : std::as_const(m_paths)) {
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
            m_watcher.addPath(path);
    }
}

void SiteSettings::read()
{
    watch();
    QVariantMap values;
    for (const QString &path : std::as_const(m_paths)) {
        QSettings file(path, QSettings::IniFormat);
        for (const QString &key : file.allKeys())
            values.insert(key, file.value(key));
    }
    if (values == m_values)
        return;
    m_values = values;
    qCInfo(lcSettings) << "settings" << m_values;
    emit valuesChanged();
}
