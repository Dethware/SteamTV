#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// A program that is handed the screen while a key is still held (the OK or YouTube key that
// opened it) gets that key's repeats and its release. Taken as its own, they open whatever
// has the focus (a video in YouTube). FreshKeys filters a window's key events: from the
// moment `armed` becomes true until the next press of a key that isn't a repeat, nothing
// passes. Arm it each time the program is shown.
class FreshKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool armed READ armed WRITE setArmed NOTIFY armedChanged)

public:
    explicit FreshKeys(QObject *parent = nullptr);

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    bool armed() const { return m_armed; }
    void setArmed(bool armed);

signals:
    void windowChanged();
    void armedChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    bool m_armed = false;
    // Armed with a key possibly still down: nothing passes until the next fresh press.
    bool m_waiting = false;
};
