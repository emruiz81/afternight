// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The consumer measurement driver: one measured run of the product's ImageIntegration workflow, full or Lite, over a
// pinned frame set, in a process of its own. build/scripts/tools/product_measurement.py launches it from each side of
// a pair and reads the result it writes.

#include "WorkflowDriver.h"

#include "app/FilePolicies.h"
#include "controls/settings/ProcessingLimits.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightPlatform/Logging.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSettings>

#include <cstdio>

int main(int argc, char** argv)
{
    QStringList arguments;
    for (int index = 1; index < argc; ++index)
    {
        arguments << QString::fromLocal8Bit(argv[index]);
    }
    const an::measurement::DriverOptionsParse parsed = an::measurement::parseDriverOptions(arguments);
    if (!parsed.options.has_value())
    {
        std::fprintf(stderr, "%s\n\n%s", qPrintable(parsed.error), qPrintable(an::measurement::driverUsage()));
        return 1;
    }
    an::measurement::DriverOptions options = *parsed.options;

    // Everything the product keeps under the user's storage and settings goes under the run's own directory, so a
    // measurement neither reads the host user's settings nor writes into the product's storage.
    const QString runDirectory = QFileInfo(options.resultPath).absolutePath();
    QDir().mkpath(runDirectory + QStringLiteral("/storage"));
    if (qEnvironmentVariableIsEmpty("AFTERNIGHT_TEST_STORAGE_ROOT"))
    {
        qputenv("AFTERNIGHT_TEST_STORAGE_ROOT",
                QDir::toNativeSeparators(runDirectory + QStringLiteral("/storage")).toUtf8());
    }
    // The product's windows are never shown, so the offscreen platform stands in for a display.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("AfterNightMeasurement"));
    QCoreApplication::setApplicationName(QStringLiteral("AfterNightWorkflowDriver"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runDirectory + QStringLiteral("/settings"));

    an::platform::initializeLogging((runDirectory + QStringLiteral("/driver.log")).toStdString());

    // The session the application opens: the product's codecs, under the processing limits a new installation
    // stores - every logical processor and no engine memory budget.
    an::consumer::EngineSessionSettings base;
    base.codecRegistry = []() { return an::ui::createProductCodecRegistry(); };
    an::Result<an::consumer::EngineSession> session =
        an::consumer::EngineSession::open(an::ui::sessionSettingsForLimits(an::ui::ProcessingLimits{}, base));
    if (!session.hasValue())
    {
        std::fprintf(stderr, "the engine session cannot be opened: %s\n", session.status().message());
        an::platform::shutdownLogging();
        return 2;
    }
    options.engine = QJsonObject{
        {QStringLiteral("generation"), QStringLiteral("next")},
        {QStringLiteral("library"), QStringLiteral("AfterNightEngine")},
        {QStringLiteral("version"), QString::fromLatin1(an::engineVersionString())},
        {QStringLiteral("abiGeneration"), an::engineAbiGeneration()},
        {QStringLiteral("accounting"),
         QStringLiteral("the session's engine context's memory resource and the consumer allowances, in region")}};

    const QJsonObject result = an::measurement::runWorkflowMeasurement(options, *session);
    const int exitCode = an::measurement::exitCodeFor(result);

    const an::Status shut = session->shutdown();
    if (shut.failed())
    {
        LOG_WARNING("AfterNightWorkflowDriver: the engine session did not shut down cleanly: {}", shut.message());
    }
    an::platform::shutdownLogging();
    std::printf("%s: %s\n", qPrintable(options.resultPath),
                exitCode == 0 ? "completed" : (exitCode == 3 ? "cancelled as asked" : "did not complete"));
    return exitCode;
}
