#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// What the shell needs to know about what the page plays besides MPRIS: whether it keeps
// playing when the app is left (its kind is one the shell's Settings let play on), and
// whether it shows only a cover (nothing on screen worth keeping it awake for).
//
// Everything else about playing comes from Qt WebEngine's own MPRIS player, which Chromium
// registers for the program's process (org.mpris.MediaPlayer2.chromium.instance<pid>):
// status, title, artist, cover, length, Play and Pause. See DEVELOPMENT.md, "Playing".
//
// On the session bus:
//
//   busctl --user get-property local.webapp.youtube /local/webapp local.webapp Background
//
// Properties Background (b), CoverOnly (b) and Kind (s: "song", "music-video", "podcast",
// "video" or ""); PropertiesChanged when they change. Method Resume(): continue what leaving
// paused, once the page runs again (MPRIS Play doesn't reach a frozen page, and Chromium's
// player says "Stopped" until something plays).
//
//   busctl --user call local.webapp.youtube /local/webapp local.webapp Resume
class MediaKind : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by main.cpp for the site")

    Q_PROPERTY(bool background READ background WRITE setBackground NOTIFY changed)
    Q_PROPERTY(bool coverOnly READ coverOnly WRITE setCoverOnly NOTIFY changed)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY changed)

public:
    // Takes local.webapp.<name> on the session bus.
    explicit MediaKind(const QString &name, QObject *parent = nullptr);

    bool background() const { return m_background; }
    void setBackground(bool background);
    bool coverOnly() const { return m_coverOnly; }
    void setCoverOnly(bool coverOnly);
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);

signals:
    void changed();
    // Resume() over the bus.
    void resumeRequested();

private:
    void announce();

    bool m_background = false;
    bool m_coverOnly = false;
    QString m_kind;
};

// MediaKind on the bus, under the names D-Bus uses (QML can't take names with a capital).
class MediaKindAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.webapp")

    Q_PROPERTY(bool Background READ background)
    Q_PROPERTY(bool CoverOnly READ coverOnly)
    Q_PROPERTY(QString Kind READ kind)

public:
    explicit MediaKindAdaptor(MediaKind *kind)
        : QDBusAbstractAdaptor(kind)
        , m_kind(kind)
    {
    }

    bool background() const { return m_kind->background(); }
    bool coverOnly() const { return m_kind->coverOnly(); }
    QString kind() const { return m_kind->kind(); }

public slots:
    void Resume() { emit m_kind->resumeRequested(); }

private:
    MediaKind *m_kind;
};
