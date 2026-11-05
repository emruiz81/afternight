// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/AfterNightApp.h"
#include "app/ApplicationSession.h"
#include "app/ModernizationFeatureFlags.h"
#include "app/SessionConsoleBridge.h"
#include "theme/AnProxyStyle.h"

#include <AfterNightEngine/Core/Version.h>

#include <AfterNightPlatform/CrashHandler.h>
#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <filesystem>
#include <cstdio>

#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QIcon>
#include <QSize>

#ifdef WIN32
#include <Windows.h>
#endif

static const QString UNIQUE_SERVER_ID = "__AfterNightAppServer__";

bool isAppAlreadyRunning()
{
    QLocalSocket socket;
    socket.connectToServer(UNIQUE_SERVER_ID);
    if (socket.waitForConnected(100))
    {
        socket.write("raise");
        socket.flush();
        socket.waitForBytesWritten(100);
        return true;
    }
    return false;
}

static const char* QT_STYLE_PARAM_NAME = "-style";
static const char* QT_DEFAULT_STYLE_PARAM = "fusion";
static const char* QT_PLATFORM_PARAM_NAME = "-platform";
static const char* QT_DEFAULT_MODE_PARAM = "windows:darkmode=2"; // darkmode=2 is the system default

namespace
{
std::filesystem::path getExecutablePath(const char* argv0)
{
#ifdef WIN32
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
    {
        return {};
    }

    return std::filesystem::path(exePath);
#else
    std::error_code ec;
    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exePath.empty())
    {
        return exePath;
    }

    auto absPath = std::filesystem::absolute(std::filesystem::path(argv0), ec);
    if (!ec)
    {
        return absPath;
    }

    return {};
#endif
}

void logCrashInitWarning(const std::string& message)
{
#ifdef WIN32
    OutputDebugStringA(message.c_str());
#else
    std::fprintf(stderr, "%s", message.c_str());
#endif
}
} // namespace

int main(int argc, char* argv[])
{
    // Always prefer native OS file/folder dialogs.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    qunsetenv("QT_FILEDIALOG_FORCE_NON_NATIVE");

#ifndef WIN32
    // Prefer desktop-portal backed native dialogs on Linux (user can override externally).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
    {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }

    // Suppress the harmless "Failed to register with host portal" warning that
    // appears on non-containerised Linux desktops.  The portal backend tries to
    // bind an app-ID to the D-Bus connection, but the desktop session has
    // already done so.  Native file dialogs still work regardless.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.services=false"));
#endif

    // Initialize crashpad FIRST, before anything else
    // This ensures we catch crashes during Qt/application initialization
    try
    {
        const std::filesystem::path appPath = getExecutablePath(argv[0]);
        if (appPath.empty())
        {
            logCrashInitWarning("Warning: Failed to determine executable path for crashpad\n");
        }
        else
        {
            // The product's version, as the application reports it below; the engine's is annotated beside it.
            auto crashConfig = an::platform::defaultCrashHandlerConfig(appPath, an::ui::kProductVersion);

            if (!an::platform::initializeCrashHandler(crashConfig))
            {
                // Log a warning but continue - app should still run without crash reporting
                logCrashInitWarning("Warning: Failed to initialize crashpad crash handler\n");
            }
        }
    }
    catch (const std::exception& e)
    {
        // Crashpad init failed, but we should continue anyway
        logCrashInitWarning("Exception during crashpad init: ");
        logCrashInitWarning(e.what());
        logCrashInitWarning("\n");
    }
    catch (...)
    {
        logCrashInitWarning("Unknown exception during crashpad init\n");
    }

#ifdef WIN32
    // Right now Qt theme is harcoded to Fusion->no dark mode.
    // Once we get a full set of icons compatible with dark mode, I'll
    // enable the darkmode option.
    char* argv2[5];
    argv2[0] = argv[0];
    argv2[1] = (char*)QT_STYLE_PARAM_NAME;
    argv2[2] = (char*)QT_DEFAULT_STYLE_PARAM;
    argv2[3] = (char*)QT_PLATFORM_PARAM_NAME;
    argv2[4] = (char*)QT_DEFAULT_MODE_PARAM;

    argc = 5;
    QApplication app(argc, argv2);
#else

    // if (comparesEqual(qEnvironmentVariable("XDG_SESSION_TYPE"), "wayland"))
    // {
    //    qputenv("QT_QPA_PLATFORM", "xcb");
    // }

    QApplication app(argc, argv);
#endif

    // Install the high-performance QProxyStyle theme before any widgets are created.
    // This replaces the QSS-based theming with direct C++ paint calls using AnDesignTokens.
    if (an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled())
    {
        an::ui::installAnProxyStyle();
    }

    try
    {
        if (isAppAlreadyRunning())
        {
            // Another instance will raise its window
            return 1;
        }

        // Start the AfterNight engine and logging system
        // Load logging config from file (can be edited before Qt is initialized)
        an::platform::LoggingConfig logConfig;
        an::platform::loadLoggingConfig(logConfig);
        an::platform::initializeLoggingFromConfig(logConfig);
        (void)an::ui::SessionConsoleBridge::initialize(&app);
        LOG_INFO("AfterNight version {} - engine {}", an::ui::kProductVersion, an::engineVersionString());

        // Add useful crash annotations now that logging is available
        an::platform::setCrashAnnotation("qt_version", QT_VERSION_STR);
        an::platform::setCrashAnnotation("engine_version", an::engineVersionString());
#ifdef WIN32
        an::platform::setCrashAnnotation("os", "Windows");
#else
        an::platform::setCrashAnnotation("os", "Linux");
#endif

        QCoreApplication::setApplicationName("AfterNight");
        QCoreApplication::setApplicationVersion(QString::fromLatin1(an::ui::kProductVersion));
        // Set application icon (X11 reads QApplication::windowIcon directly;
        // Windows ignores this in favour of the embedded .exe resource icon).
        {
            QIcon appIcon;
            appIcon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_512x512_blue.png"), QSize(512, 512));
            appIcon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_256x256_blue.png"), QSize(256, 256));
            appIcon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_128x128_blue.png"), QSize(128, 128));
            QApplication::setWindowIcon(appIcon);
        }

#ifndef WIN32
        // Associate the running process with its .desktop entry so that
        // Wayland compositors can look up the correct icon from the XDG
        // icon theme (the compositor matches app_id → desktopFileName).
        QApplication::setDesktopFileName(QStringLiteral("AfterNight"));
#endif

        // Theme is now applied from settings during AfterNightApp::initialize()

        // The engine session the process runs in. The main window opens it once it has read the settings store, from
        // the processing limits stored there, before anything reads an image; it outlives the window.
        an::ui::ApplicationSession session;

        AfterNightApp* mainWin = AfterNightApp::getInstance();
        mainWin->initialize(session);

        mainWin->showWithSavedGeometry();

        // Setup QLocalServer to receive future requests
        QLocalServer* server = new QLocalServer(&app);
        server->removeServer(UNIQUE_SERVER_ID); // Clean up stale socket
        server->listen(UNIQUE_SERVER_ID);

        QObject::connect(server, &QLocalServer::newConnection, [&]() {
            QLocalSocket* client = server->nextPendingConnection();
            if (client->waitForReadyRead(100))
            {
                QByteArray message = client->readAll();
                if (message == "raise")
                {
                    // Bring main window to front (see below)
                    QWidgetList topLevels = QApplication::topLevelWidgets();
                    for (QWidget* w : topLevels)
                    {
                        if (w->isWindow())
                        {
                            w->raise();
                            w->activateWindow();
                        }
                    }
                }
            }
            client->disconnectFromServer();
        });

        int ret = app.exec();

        // Admission closes first; the window, its views and its context go while the session is still open; the
        // session is shut down last, before the logger and the crash handler.
        (void)an::ui::shutDownApplication(session, []() { AfterNightApp::destroyInstance(); });

        an::platform::shutdownLogging();

        an::platform::shutdownCrashHandler();

        return ret;
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, "Error", QString("An error occurred: %1").arg(e.what()));
        return 1;
    }
    catch (...)
    {
        QMessageBox::critical(nullptr, "Error", "An unknown error occurred");
        return 1;
    }
}
