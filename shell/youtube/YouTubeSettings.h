#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// YouTube's options in the shell's Settings. YouTube itself is a program of its own (a web
// app, apps/webapp), which reads them from ~/.config/tvshell/apps/youtube.conf: they are saved
// there, at once.
class YouTubeSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // SponsorBlock categories to skip ("sponsor", "selfpromo", "interaction", "intro", "outro",
    // "preview", "music_offtopic", "filler"). Empty: SponsorBlock isn't asked at all.
    Q_PROPERTY(QStringList skipCategories READ skipCategories NOTIFY changed)
    // What keeps playing when YouTube is left: "song" (a song showing only its cover),
    // "music-video", "podcast", "video" (the web app tells them apart by YouTube's own mark on
    // a video). The rest pauses.
    Q_PROPERTY(QStringList backgroundKinds READ backgroundKinds NOTIFY changed)

public:
    explicit YouTubeSettings(QObject *parent = nullptr);

    QStringList skipCategories() const { return m_skipCategories; }
    Q_INVOKABLE void toggleSkip(const QString &category);
    QStringList backgroundKinds() const { return m_backgroundKinds; }
    Q_INVOKABLE void toggleBackground(const QString &kind);

signals:
    void changed();

private:
    void load(const QVariant &saved);
    void save();

    QStringList m_skipCategories = {QStringLiteral("sponsor")};
    QStringList m_backgroundKinds = {QStringLiteral("song"), QStringLiteral("music-video"), QStringLiteral("podcast")};
};
