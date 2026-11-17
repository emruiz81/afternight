// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the cases about projects in the Observatory window share: the project service and the window's part in
/// projects, a root for a case's projects and files, an image written to a file and opened as a person's file is, and
/// the projects used last forgotten between cases.

#pragma once

#include "ObservatoryWindowCase.h"

#include "chrome/TopBar.h"
#include "projects/ProjectFlows.h"

#include "Test_ShellClassic/AppTestSupport.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "projects/ProjectService.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <optional>

namespace an::tests::observatory
{

/// How long a file is given to open, in milliseconds.
inline constexpr int kProjectOpenMs = 20000;

/// Lets the window settle: posted work delivered, the deletions done.
inline void settleProjects()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The Observatory window with the project service, each case starting with no project among the ones used last.
class ProjectCase : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::shell::ProjectService& projects()
    {
        return core().projects();
    }

    [[nodiscard]] static an::ui::observatory::ProjectFlows& flows()
    {
        return window().projectFlows();
    }

    /// The projects used last are forgotten: the settings hold none and the service reads them again.
    static void forgetProjects()
    {
        an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("0"));
        projects().setSettings(settings);
        window().topBar()->refreshProjectMenu();
    }

    /// A root for the case's projects and files, made once the suite's temporary directory is in force.
    [[nodiscard]] static QString root()
    {
        static QTemporaryDir files(QDir::temp().filePath(QStringLiteral("observatory-projects-XXXXXX")));
        return QDir::cleanPath(files.path());
    }

    /// A grey FITS image written under a name in the case's root.
    [[nodiscard]] static QString writeImage(const QString& name, const float value = 0.4f)
    {
        const QString path = QDir(root()).filePath(name);
        EXPECT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
        std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), value);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return {};
        }
        const an::Result<an::OutputIntent> intent = an::ui::appContext()->getOutputIntentProvider()->buildOutputIntent(
            *asset->descriptor(), an::CodecFormat::Fits);
        EXPECT_TRUE(intent.hasValue()) << intent.status().message();
        EXPECT_TRUE(intent.hasValue() && an::ui::writeViewImage(session(), *asset, path, *intent, {}).ok());
        return QDir::cleanPath(path);
    }

    /// The view that shows a file; null for none.
    [[nodiscard]] static ImageViewControl* viewOf(const QString& path)
    {
        for (ImageViewControl* view : views())
        {
            if (view->hasImage() && QDir::cleanPath(view->document().names().sourcePath) == path)
            {
                return view;
            }
        }
        return nullptr;
    }

    /// Opens a file as a person's file is opened, and waits for its view.
    static void openFile(const QString& path)
    {
        an::ui::appContext()->getRecentFilesService()->openDroppedFiles({path});
        EXPECT_TRUE(an::tests::app::processEventsUntil([&path]() { return viewOf(path) != nullptr; }, kProjectOpenMs))
            << path.toStdString() << " did not open";
    }
};

} // namespace an::tests::observatory
