// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QStyleFactory>
#include <QtGlobal>

#include <cstdio>
#include <cstring>

namespace an::tests::fixtures
{
inline QtMessageHandler g_previousQtMessageHandler = nullptr;

inline void qtTestFilteringMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (msg.contains(QStringLiteral("This plugin does not support propagateSizeHints()")))
    {
        return;
    }

    if (g_previousQtMessageHandler)
    {
        g_previousQtMessageHandler(type, context, msg);
        return;
    }

    std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
}

inline bool hasQtPlatformArg(int argc, char** argv)
{
    for (int i = 0; i < argc; ++i)
    {
        if (argv[i] && std::strcmp(argv[i], "-platform") == 0)
        {
            return true;
        }
    }

    return false;
}

inline void ensureQtOffscreenHasFonts()
{
    if (qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
    {
        return;
    }

#if defined(Q_OS_WIN)
    QString windir = qEnvironmentVariable("WINDIR");
    if (windir.trimmed().isEmpty())
    {
        windir = QStringLiteral("C:/Windows");
    }

    const QString fontsDir = QDir::cleanPath(windir + QStringLiteral("/Fonts"));
    if (QDir(fontsDir).exists())
    {
        qputenv("QT_QPA_FONTDIR", fontsDir.toUtf8());
    }
#elif defined(Q_OS_LINUX)
    const QStringList candidates = {
        QStringLiteral("/usr/share/fonts"),
        QStringLiteral("/usr/local/share/fonts"),
    };
    for (const auto& path : candidates)
    {
        if (QDir(path).exists())
        {
            qputenv("QT_QPA_FONTDIR", path.toUtf8());
            break;
        }
    }
#elif defined(Q_OS_MACOS)
    const QStringList candidates = {
        QStringLiteral("/System/Library/Fonts"),
        QStringLiteral("/Library/Fonts"),
    };
    for (const auto& path : candidates)
    {
        if (QDir(path).exists())
        {
            qputenv("QT_QPA_FONTDIR", path.toUtf8());
            break;
        }
    }
#endif
}

inline void configureHeadlessQtPlatform(int argc, char** argv)
{
    if (hasQtPlatformArg(argc, argv) || qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    {
        return;
    }

#if defined(Q_OS_WIN)
    qputenv("QT_QPA_PLATFORM", "minimal");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
}

inline void prepareHeadlessQtEnvironment(int argc, char** argv)
{
    configureHeadlessQtPlatform(argc, argv);
    ensureQtOffscreenHasFonts();
}

inline void installQtTestMessageFilters()
{
    static bool installed = false;
    if (installed)
    {
        return;
    }

    g_previousQtMessageHandler = qInstallMessageHandler(qtTestFilteringMessageHandler);
    installed = true;
}

inline void applyFusionStyle(QApplication& app)
{
    if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
    {
        app.setStyle(fusion);
    }
}

class HeadlessQtFixture : public ::testing::Test
{
protected:
    static void prepareQtEnvironment(int argc, char** argv)
    {
        prepareHeadlessQtEnvironment(argc, argv);
    }
};

} // namespace an::tests::fixtures
