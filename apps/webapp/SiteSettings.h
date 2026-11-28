#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// The site's settings. The shell's Settings own them and write them to
// ~/.config/tvshell/apps/webapp.conf (what every web app reads) and
// ~/.config/tvshell/apps/<name>.conf (this site's, which wins); INI, no groups. This program
// only reads them, again whenever a file changes. A missing file means the defaults.
class SiteSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by main.cpp for the site")

    // The file's keys and values, e.g. { skipCategories: ["sponsor"] }. A value with commas
    // is a list.
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)

public:
    explicit SiteSettings(const QString &name, QObject *parent = nullptr);

    QVariantMap values() const { return m_values; }

signals:
    void valuesChanged();

private:
    void read();
    // The shell saves by replacing the file, so the folder is watched too.
    void watch();

    // webapp.conf, then the site's.
    QStringList m_paths;
    QVariantMap m_values;
    QFileSystemWatcher m_watcher;
};
