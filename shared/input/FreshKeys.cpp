#include "FreshKeys.h"

#include <QKeyEvent>

FreshKeys::FreshKeys(QObject *parent)
    : QObject(parent)
{
}

void FreshKeys::setWindow(QWindow *window)
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

void FreshKeys::setArmed(bool armed)
{
    if (m_armed == armed)
        return;
    m_armed = armed;
    m_waiting = armed;
    emit armedChanged();
}

bool FreshKeys::eventFilter(QObject *, QEvent *event)
{
    if (!m_waiting || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease))
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    if (event->type() != QEvent::KeyPress || key->isAutoRepeat())
        return true;
    m_waiting = false;
    return false;
}
