#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// One site's description, read from <site folder>/site.json (DEVELOPMENT.md, "A site").
// Read once at start; main.cpp exits if it isn't valid.
class Site : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by main.cpp from the site folder")

    // The folder's name, e.g. "youtube": the profile, the settings file, the D-Bus name.
    Q_PROPERTY(QString name READ name CONSTANT)
    // What it is called, e.g. "YouTube".
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString url READ url CONSTANT)
    // Empty: Qt WebEngine's own.
    Q_PROPERTY(QString userAgent READ userAgent CONSTANT)
    // The site's scripts, as maps for WebEngineView.userScripts (name, sourceUrl,
    // injectionPoint, worldId).
    Q_PROPERTY(QVariantList scripts READ scripts CONSTANT)
    // Address prefixes the site's scripts may fetch through the host (webappFetch).
    Q_PROPERTY(QStringList fetch READ fetch CONSTANT)
    // Where its cookies and storage live: ~/.local/share/webapp/<name>.
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)

public:
    // Reads <folder>/site.json; error() says what is wrong if it can't be used.
    explicit Site(const QString &folder, QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString title() const { return m_title; }
    QString url() const { return m_url; }
    QString userAgent() const { return m_userAgent; }
    QVariantList scripts() const { return m_scripts; }
    QStringList fetch() const { return m_fetch; }
    QString storagePath() const;
    QString error() const { return m_error; }

private:
    QString m_name;
    QString m_title;
    QString m_url;
    QString m_userAgent;
    QVariantList m_scripts;
    QStringList m_fetch;
    QString m_error;
};
