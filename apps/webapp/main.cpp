// webapp <site folder>: one website full screen, as a program of its own (DEVELOPMENT.md).

#include "MediaKind.h"
#include "Site.h"
#include "SiteSettings.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSocketNotifier>
#include <QtQml/QQmlExtensionPlugin>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <csignal>
#include <sys/eventfd.h>
#include <unistd.h>
#include <xcb/xcb.h>

Q_IMPORT_QML_PLUGIN(TvShared_InputPlugin)

namespace {

// Sizes are for a screen 1440 pixels high, as in the shell (shell/main.cpp): the page sees
// the same CSS size it saw when it was the shell's page.
constexpr int DesignHeight = 1440;

int outputHeight()
{
    int screenNumber = 0;
    xcb_connection_t *connection = xcb_connect(nullptr, &screenNumber);
    int height = 0;
    if (!xcb_connection_has_error(connection)) {
        xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
        for (int i = 0; i < screenNumber && screens.rem > 0; ++i)
            xcb_screen_next(&screens);
        if (screens.rem > 0)
            height = screens.data->height_in_pixels;
    }
    xcb_disconnect(connection);
    return height;
}

void scaleToOutput()
{
    const int height = outputHeight();
    if (height <= 0 || qEnvironmentVariableIsSet("QT_SCALE_FACTOR"))
        return;
    const double factor = double(height) / DesignHeight;
    if (qAbs(factor - 1.0) > 0.01)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(factor, 'f', 4));
}

int quitFd = -1;

void requestQuit(int)
{
    const quint64 one = 1;
    [[maybe_unused]] auto written = ::write(quitFd, &one, sizeof(one));
}

// systemd stops the unit with SIGTERM: a normal quit, so the profile is written out.
void quitOnTermination(QGuiApplication &app)
{
    quitFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (quitFd < 0)
        return;
    auto *notifier = new QSocketNotifier(quitFd, QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    struct sigaction action = {};
    action.sa_handler = requestQuit;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: webapp <site folder>\n");
        return 2;
    }
    const QString folder = QString::fromLocal8Bit(argv[argc - 1]);
    const QString name = QDir(folder).dirName();

    // Inside Gaming Mode only X11 windows can be put on screen.
    if (qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    // The window's WM_CLASS, how the shell finds it: both parts webapp-<name>.
    qputenv("RESOURCE_NAME", ("webapp-" + name).toLocal8Bit());
    scaleToOutput();

    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("webapp-") + name);
    quitOnTermination(app);

    Site site(folder);
    if (!site.error().isEmpty()) {
        qCritical("%s", qPrintable(site.error()));
        return 1;
    }
    SiteSettings settings(name);
    MediaKind mediaKind(name);
    qInfo("%s from %s", qPrintable(site.title()), qPrintable(site.url()));

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("site"), QVariant::fromValue(&site)},
        {QStringLiteral("settings"), QVariant::fromValue(&settings)},
        {QStringLiteral("mediaKind"), QVariant::fromValue(&mediaKind)},
    });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("WebApp", "Main");
    return app.exec();
}
