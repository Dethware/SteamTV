#include "YouTubeSettings.h"

#include <QSettings>
#include <QStandardPaths>

namespace {

QString configPath(const QString &file)
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/tvshell/") + file;
}

// The web app reads this file (apps/webapp, SiteSettings) and follows its changes.
QString appSettingsPath()
{
    return configPath(QStringLiteral("apps/youtube.conf"));
}

const QString SkipKey = QStringLiteral("skipCategories");
// Read by the web app itself (apps/webapp/Main.qml), not by YouTube's scripts.
const QString BackgroundKey = QStringLiteral("backgroundKinds");
const QStringList Kinds = {QStringLiteral("song"), QStringLiteral("music-video"), QStringLiteral("podcast"),
                           QStringLiteral("video")};

const QStringList Categories = {
    QStringLiteral("sponsor"), QStringLiteral("selfpromo"),      QStringLiteral("interaction"), QStringLiteral("intro"),
    QStringLiteral("outro"),   QStringLiteral("preview"),        QStringLiteral("music_offtopic"), QStringLiteral("filler"),
};

} // namespace

YouTubeSettings::YouTubeSettings(QObject *parent)
    : QObject(parent)
{
    QSettings app(appSettingsPath(), QSettings::IniFormat);
    if (app.contains(BackgroundKey)) {
        m_backgroundKinds.clear();
        for (const QString &kind : app.value(BackgroundKey).toStringList()) {
            if (Kinds.contains(kind) && !m_backgroundKinds.contains(kind))
                m_backgroundKinds << kind;
        }
    }
    if (app.contains(SkipKey)) {
        load(app.value(SkipKey));
        save();
        return;
    }
    // Saved by the shell before YouTube was a program of its own.
    QSettings old(configPath(QStringLiteral("tvshell.conf")), QSettings::IniFormat);
    const QVariant saved = old.value(QStringLiteral("youtube/skipCategories"));
    if (saved.isValid())
        load(saved);
    save();
}

void YouTubeSettings::load(const QVariant &saved)
{
    m_skipCategories.clear();
    for (const QString &category : saved.toStringList()) {
        if (Categories.contains(category) && !m_skipCategories.contains(category))
            m_skipCategories << category;
    }
}

void YouTubeSettings::save()
{
    QSettings app(appSettingsPath(), QSettings::IniFormat);
    app.setValue(SkipKey, m_skipCategories);
    app.setValue(BackgroundKey, m_backgroundKinds);
}

void YouTubeSettings::toggleBackground(const QString &kind)
{
    if (!Kinds.contains(kind))
        return;
    if (!m_backgroundKinds.removeOne(kind))
        m_backgroundKinds << kind;
    save();
    emit changed();
}

void YouTubeSettings::toggleSkip(const QString &category)
{
    if (!Categories.contains(category))
        return;
    if (!m_skipCategories.removeOne(category))
        m_skipCategories << category;
    save();
    emit changed();
}
