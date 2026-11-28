#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// What happens to a managed app that is left (Home, or its own way back to the shell) with
// nothing playing. Web apps are suspended after a while (each hides its page, which then
// does next to nothing); any app may be closed after a while.
//
// closeAfter is saved in ~/.config/tvshell/tvshell.conf, [apps] group, as <app>/closeAfter.
// webAppSuspendAfter goes to the file every web app reads, ~/.config/tvshell/apps/webapp.conf
// (apps/webapp, SiteSettings), as suspendAfter.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Seconds by app ("youtube", "jellyfin"); 0 = keep it loaded.
    Q_PROPERTY(QVariantMap closeAfter READ closeAfter NOTIFY changed)
    // Seconds a web app left with nothing playing stays open; 0 = never suspended.
    Q_PROPERTY(int webAppSuspendAfter READ webAppSuspendAfter WRITE setWebAppSuspendAfter NOTIFY changed)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QVariantMap closeAfter() const { return m_closeAfter; }
    Q_INVOKABLE void setCloseAfter(const QString &app, int seconds);
    int webAppSuspendAfter() const { return m_webAppSuspendAfter; }
    void setWebAppSuspendAfter(int seconds);

signals:
    void changed();

private:
    // YouTube comes back where it was (as when it was part of the shell); Jellyfin closes
    // after ten minutes.
    QVariantMap m_closeAfter = {{QStringLiteral("youtube"), 0}, {QStringLiteral("jellyfin"), 600}};
    // Open for half an hour, alongside the idle times (a woken YouTube reloads its video).
    int m_webAppSuspendAfter = 1800;
};
