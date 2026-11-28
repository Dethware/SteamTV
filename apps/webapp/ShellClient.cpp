#include "ShellClient.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QKeyEvent>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShell, "webapp.shell")

ShellClient::ShellClient(QObject *parent)
    : QObject(parent)
{
}

void ShellClient::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    emit windowChanged();
}

void ShellClient::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

void ShellClient::sendEscape()
{
    if (!m_window)
        return;
    for (QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        QKeyEvent escape(type, Qt::Key_Escape, Qt::NoModifier, QStringLiteral("\x1b"));
        QCoreApplication::sendEvent(m_window, &escape);
    }
}

void ShellClient::leave()
{
    qCInfo(lcShell) << "asking the shell to take the screen back";
    QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
        QStringLiteral("local.tvshell"), QStringLiteral("/local/tvshell"), QStringLiteral("local.tvshell"),
        QStringLiteral("LeaveApp")));
}

bool ShellClient::eventFilter(QObject *, QEvent *event)
{
    if (!m_enabled || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease))
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() != Qt::Key_Back)
        return false;
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat())
        emit backPressed();
    return true;
}
