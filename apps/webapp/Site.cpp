#include "Site.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace {

// WebEngineScript's values, which QML's userScripts take as numbers.
constexpr int DocumentCreation = 2;
constexpr int DocumentReady = 0;
constexpr int MainWorld = 0;
constexpr int ApplicationWorld = 1;

} // namespace

Site::Site(const QString &folder, QObject *parent)
    : QObject(parent)
{
    const QDir dir(folder);
    m_name = dir.dirName();
    QFile file(dir.filePath(QStringLiteral("site.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("can't read %1: %2").arg(file.fileName(), file.errorString());
        return;
    }
    QJsonParseError parseError;
    const QJsonObject json = QJsonDocument::fromJson(file.readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        m_error = QStringLiteral("%1: %2").arg(file.fileName(), parseError.errorString());
        return;
    }
    m_title = json.value(QStringLiteral("name")).toString(m_name);
    m_url = json.value(QStringLiteral("url")).toString();
    m_userAgent = json.value(QStringLiteral("userAgent")).toString();
    for (const QJsonValue &prefix : json.value(QStringLiteral("fetch")).toArray())
        m_fetch << prefix.toString();
    for (const QJsonValue &value : json.value(QStringLiteral("scripts")).toArray()) {
        const QJsonObject script = value.toObject();
        const QString fileName = script.value(QStringLiteral("file")).toString();
        if (!dir.exists(fileName)) {
            m_error = QStringLiteral("%1: no script %2").arg(file.fileName(), fileName);
            return;
        }
        const bool application = script.value(QStringLiteral("world")).toString() == u"application";
        const bool creation = script.value(QStringLiteral("at")).toString() == u"creation";
        m_scripts << QVariantMap{
            {QStringLiteral("name"), fileName},
            {QStringLiteral("sourceUrl"), QUrl::fromLocalFile(dir.absoluteFilePath(fileName))},
            {QStringLiteral("injectionPoint"), creation ? DocumentCreation : DocumentReady},
            {QStringLiteral("worldId"), application ? ApplicationWorld : MainWorld},
        };
    }
    if (m_url.isEmpty())
        m_error = QStringLiteral("%1: no url").arg(file.fileName());
}

QString Site::storagePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/webapp/") + m_name;
}
