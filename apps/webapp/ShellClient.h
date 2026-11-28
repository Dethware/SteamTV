#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// The program's side of living inside the shell: the Back key, and asking the shell to take
// the screen back.
//
// While `enabled`, Back (the remote's Back, the controller's B) is taken from the window
// before the page sees it and reported as backPressed(), so the site can say what it means
// there. sendEscape() then gives the page Escape, which TV web apps take as back; leave()
// asks the shell (its LeaveApp on the session bus), which pauses the program and shows
// itself, as Home does.
class ShellClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit ShellClient(QObject *parent = nullptr);

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    Q_INVOKABLE void sendEscape();
    Q_INVOKABLE void leave();

signals:
    void windowChanged();
    void enabledChanged();
    void backPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    bool m_enabled = true;
};
