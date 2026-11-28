#include "MediaKind.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcMediaKind, "webapp.media")

namespace {

const QString Path = QStringLiteral("/local/webapp");
const QString Interface = QStringLiteral("local.webapp");

} // namespace

MediaKind::MediaKind(const QString &name, QObject *parent)
    : QObject(parent)
{
    new MediaKindAdaptor(this);
    auto bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("local.webapp.") + name;
    if (!bus.registerObject(Path, this, QDBusConnection::ExportAdaptors)
        || !bus.registerService(service)) {
        qCWarning(lcMediaKind) << "can't offer" << service << "on the session bus:" << bus.lastError().message();
    }
}

void MediaKind::setBackground(bool background)
{
    if (m_background == background)
        return;
    m_background = background;
    qCInfo(lcMediaKind) << (background ? "keeps playing when left" : "pauses when left");
    emit changed();
    announce();
}

void MediaKind::setCoverOnly(bool coverOnly)
{
    if (m_coverOnly == coverOnly)
        return;
    m_coverOnly = coverOnly;
    emit changed();
    announce();
}

void MediaKind::setKind(const QString &kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    emit changed();
    announce();
}

// Qt doesn't send PropertiesChanged for exported properties by itself.
void MediaKind::announce()
{
    auto signal = QDBusMessage::createSignal(Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"));
    signal << Interface
           << QVariantMap{{QStringLiteral("Background"), m_background},
                          {QStringLiteral("CoverOnly"), m_coverOnly},
                          {QStringLiteral("Kind"), m_kind}}
           << QStringList();
    QDBusConnection::sessionBus().send(signal);
}
