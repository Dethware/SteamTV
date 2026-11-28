#include "TvPower.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcTv, "tvshell.tv")

namespace {

const QString Service = QStringLiteral("com.steampowered.CecDaemon1");
// cecd's one CEC adapter (/dev/cec0).
const QString Device = QStringLiteral("/com/steampowered/CecDaemon1/Devices/Cec0");
const QString Interface = QStringLiteral("com.steampowered.CecDaemon1.CecDevice1");
// The TV's CEC logical address.
constexpr uchar TvAddress = 0;

} // namespace

TvPower::TvPower(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)));
    // Once `on` has its first value: a shell (re)started while the TV was in standby (a
    // deploy during screen off) would otherwise leave it there, since only changes are sent.
    QTimer::singleShot(0, this, [this] {
        if (!m_on)
            return;
        qCInfo(lcTv) << "started with the screen on: turning the TV on";
        call(QStringLiteral("Wake"), {});
    });
}

void TvPower::prepareForSleep(bool start)
{
    if (start)
        return;
    // After IdleController has heard of the resume too: it may keep the screen off.
    QTimer::singleShot(0, this, [this] {
        if (!m_on)
            return;
        qCInfo(lcTv) << "resumed with the screen on: turning the TV on";
        call(QStringLiteral("Wake"), {});
    });
}

void TvPower::setOn(bool on)
{
    if (m_on == on)
        return;
    m_on = on;
    emit onChanged();
    qCInfo(lcTv) << (on ? "turning the TV on" : "sending the TV to standby");
    // Wake: Image View On and Active Source, so the TV also switches to this input.
    if (on)
        call(QStringLiteral("Wake"), {});
    else
        call(QStringLiteral("Standby"), {QVariant::fromValue(TvAddress)});
}

void TvPower::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Device, Interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (watcher->isError())
            qCWarning(lcTv) << "cecd" << method << "failed:" << watcher->error().message();
    });
}
