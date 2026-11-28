#include "ImmichConfig.h"
#include "NetworkAccess.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>
#include <QScreen>
#include <QSocketNotifier>

#include <csignal>
#include <sys/eventfd.h>
#include <unistd.h>
#include <xcb/xcb.h>

// The shell's modules, one per topic (CMakeLists.txt), linked in as static libraries.
Q_IMPORT_QML_PLUGIN(TvShell_CommonPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_SessionPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_PowerPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_AudioPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_PhotosPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_SteamPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_SwitchPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_AppsPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_YouTubePlugin)
Q_IMPORT_QML_PLUGIN(TvShell_ControllersPlugin)
Q_IMPORT_QML_PLUGIN(TvShell_AssistantPlugin)
// Shared with the programs the shell shows (../shared).
Q_IMPORT_QML_PLUGIN(TvShared_InputPlugin)

namespace {

// The UI is laid out for a screen 1440 pixels high; on any other it is scaled as a whole.
constexpr int DesignHeight = 1440;
// Exit code that makes systemd start the shell again (Restart=on-failure).
constexpr int RestartExitCode = 75;

// The height of the X screen (gamescope's output resolution), 0 if there is no X display.
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

// Qt's scale factor has to be set before the application object exists. An explicit
// QT_SCALE_FACTOR wins.
void scaleToOutput(int height)
{
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

// systemd stops the unit with SIGTERM. Turn it into a normal quit, so the shell
// hands the screen back to Steam on the way out (BaseLayer's destructor).
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
    // Inside Gaming Mode only X11 windows can be put on screen (see docs/gabecube-findings.md).
    if (qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    const int startHeight = outputHeight();
    scaleToOutput(startHeight);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tvshell"));
    quitOnTermination(app);
    qInfo("output %d pixels high, UI scale %.2f", startHeight, app.devicePixelRatio());

    // The scale can't change while running: on another output resolution, start afresh.
    if (QScreen *screen = app.primaryScreen()) {
        QObject::connect(screen, &QScreen::geometryChanged, &app, [startHeight] {
            const int height = outputHeight();
            if (height > 0 && height != startHeight) {
                qInfo("output is now %d pixels high (was %d): restarting", height, startHeight);
                QCoreApplication::exit(RestartExitCode);
            }
        });
    }

    QQmlApplicationEngine engine;
    // The network layer for QML (disk cache), with the Immich API key on requests for the
    // Immich server.
    NetworkAccessManager::addRequestRule(ImmichConfig::requestRule());
    static NetworkAccessManagerFactory networkFactory;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TvShell", "Main");

    return app.exec();
}
