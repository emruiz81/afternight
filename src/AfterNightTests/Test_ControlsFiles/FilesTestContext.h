// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the file workflows' cases run under: the viewer's application context over a session that reads and writes
/// every container the product does, with a view factory that keeps what it made, the export settings' intent as the
/// product builds it, and a file manager wired as the application wires it, over a settings database and a thumbnail
/// table of the case's own.
#pragma once

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/FileManager.h"
#include "app/FilePolicies.h"
#include "app/FileWorkflowRunner.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/ThumbnailPersistence.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/RawCodec.h>

#include <QObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::files
{

using an::tests::controls::TestSessionProvider;
using an::tests::viewer::Channels;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeAsset;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;

/// A provider over a session whose context reads and writes every container the product does, its camera RAW adapter
/// configured as stated; fails the case when the session cannot be opened.
[[nodiscard]] inline std::optional<TestSessionProvider> openProductProvider(std::uint32_t width = 2u,
                                                                            const an::RawCodecOptions& raw = {})
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    settings.codecRegistry = [raw]() { return an::ui::createProductCodecRegistry(raw); };
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::optional<TestSessionProvider>(std::in_place, std::move(*session));
}

/// The viewer's context over a product session, with a view factory and the product's export intent.
class FilesTestContext final : public an::tests::viewer::ViewerTestContext, public an::ui::IViewFactory
{
public:
    explicit FilesTestContext(an::consumer::EngineSession session) : ViewerTestContext(std::move(session))
    {
        an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::ui::createProductCodecRegistry();
        EXPECT_TRUE(registry.hasValue()) << registry.status().message();
        if (registry.hasValue())
        {
            m_registry = *registry;
        }
    }

    ~FilesTestContext() override
    {
        // The views it made go before the session the base lets go.
        m_made.clear();
    }

    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }

    // ---- IOutputIntentProvider: what the export settings stored in the context give, as the product builds it.

    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const override
    {
        auto* self = const_cast<FilesTestContext*>(this);
        const an::ui::ExportSettings settings = an::ui::readExportSettings(
            [self](const QString& key, const QString& fallback) { return self->select(key, fallback); });
        return an::ui::buildExportIntent(settings, *m_registry, source, container);
    }

    // ---- IViewFactory: the views an open makes, kept and deleted with the context.

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string& historyDesc,
                                                                       ViewControlMode mode,
                                                                       ImageViewControl* neighbour, bool, bool) override
    {
        if (refuseViews)
        {
            return nullptr;
        }
        auto view = an::tests::viewer::makeView(document.names().displayName.toStdString(), mode);
        view->setDocument(std::move(document));
        view->initializeNewImageHistory(historyDesc);
        madeHistories.push_back(historyDesc);
        neighbours.push_back(neighbour);
        sequenceAutoDebayer.push_back(false);
        m_made.push_back(std::move(view));
        return m_made.back().get();
    }

    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&& document, QWidget*,
                                          bool autoDebayer, const std::string& historyDesc, ViewControlMode mode,
                                          ImageViewControl* neighbour, bool, bool) override
    {
        if (refuseViews || !document.sequence().has_value())
        {
            return nullptr;
        }
        auto view = an::tests::viewer::makeView(document.names().displayName.toStdString(), mode);
        view->setSequenceDocument(std::move(document), autoDebayer);
        view->initializeNewImageHistory(historyDesc);
        madeHistories.push_back(historyDesc);
        neighbours.push_back(neighbour);
        sequenceAutoDebayer.push_back(autoDebayer);
        m_made.push_back(std::move(view));
        return m_made.back().get();
    }

    /// The views the factory made, in order.
    [[nodiscard]] std::vector<ImageViewControl*> madeViews() const
    {
        std::vector<ImageViewControl*> views;
        for (const auto& view : m_made)
        {
            views.push_back(view.get());
        }
        return views;
    }

    /// Destroys a view the factory made, as closing it does.
    void destroyView(ImageViewControl* view)
    {
        for (auto made = m_made.begin(); made != m_made.end(); ++made)
        {
            if (made->get() == view)
            {
                m_made.erase(made);
                return;
            }
        }
    }

    /// The product's registry, for a case that reads what a save wrote.
    [[nodiscard]] const an::CodecRegistry& registry() const noexcept
    {
        return *m_registry;
    }

    /// Whether the factory refuses every view.
    bool refuseViews = false;

    /// The history descriptions the factory's views were made with.
    std::vector<std::string> madeHistories;

    /// The neighbour each view was docked beside.
    std::vector<ImageViewControl*> neighbours;

    /// Whether each view was told to demosaic its sequence's frames; false for a still image.
    std::vector<bool> sequenceAutoDebayer;

private:
    std::shared_ptr<const an::CodecRegistry> m_registry;
    std::vector<an::tests::viewer::ViewHandle> m_made;
};

/// A files context over a product session of a given width; fails the case when the session cannot be opened.
[[nodiscard]] inline std::unique_ptr<FilesTestContext> openFilesContext(std::uint32_t width = 2u)
{
    std::optional<TestSessionProvider> provider = openProductProvider(width);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<FilesTestContext>(std::move(provider->owned()));
}

/// A file manager wired as the application wires one, over a thumbnail table and folders of the case's own.
class FileManagerFixture
{
public:
    FileManagerFixture()
        : m_executor(m_directory.filePath(QStringLiteral("files.db")), QStringLiteral("files-test-connection")),
          m_thumbnails(m_executor)
    {
        EXPECT_TRUE(m_directory.isValid());
        EXPECT_TRUE(m_thumbnails.initializeTable());
        QDir().mkpath(thumbnailsDirectory());
    }

    ~FileManagerFixture()
    {
        // Every thumbnail on its way lands, or is dropped, before the manager and the table go.
        QThreadPool::globalInstance()->waitForDone();
        pumpQuietly();
        m_manager.reset();
    }

    FileManagerFixture(const FileManagerFixture&) = delete;
    FileManagerFixture& operator=(const FileManagerFixture&) = delete;

    /// The manager, made with the dependencies as they stand; `rawOpenPolicy` and the callbacks are read once.
    an::ui::FileManager& manager()
    {
        if (!m_manager)
        {
            an::ui::FileManager::Dependencies deps;
            deps.signalContext = &m_context;
            deps.recentFiles = &recentFiles;
            deps.lastOpenDirectory = &lastOpenDirectory;
            deps.thumbnailPersistence = &m_thumbnails;
            deps.thumbnailsDirPath = [this]() { return thumbnailsDirectory(); };
            deps.reloadRecentFilesThumbnails = [this]() { ++thumbnailReloads; };
            deps.rawOpenPolicy = [this]() {
                ++policiesTaken;
                return policy;
            };
            deps.openFinished = [this](const an::ui::FileOpenOutcome& outcome) { opens.push_back(outcome); };
            deps.saveFinished = [this](const an::ui::FileSaveOutcome& outcome) { saves.push_back(outcome); };
            deps.thumbnailRecorded = [this](const std::string& file, const QString& thumbnail) {
                recordedThumbnails.emplace_back(file, thumbnail);
            };
            m_manager = std::make_unique<an::ui::FileManager>(deps);
        }
        return *m_manager;
    }

    /// The folder thumbnails are written into.
    [[nodiscard]] QString thumbnailsDirectory() const
    {
        return m_directory.filePath(QStringLiteral("thumbnails"));
    }

    /// A path in the case's own folder.
    [[nodiscard]] QString path(const QString& name) const
    {
        return m_directory.filePath(name);
    }

    /// The thumbnail recorded for a file, or empty.
    [[nodiscard]] QString recordedThumbnail(const std::string& file)
    {
        return m_thumbnails.select(QString::fromUtf8(file));
    }

    /// Waits for an open to end; false when none ended in time.
    [[nodiscard]] bool waitForOpens(std::size_t count)
    {
        return pumpUntil([this, count]() { return opens.size() >= count; });
    }

    /// Waits for a save to end; false when none ended in time.
    [[nodiscard]] bool waitForSaves(std::size_t count)
    {
        return pumpUntil([this, count]() { return saves.size() >= count; });
    }

    std::deque<std::string> recentFiles;
    std::string lastOpenDirectory;
    std::shared_ptr<const an::ui::RawOpenPolicy> policy;
    int policiesTaken = 0;
    int thumbnailReloads = 0;
    std::vector<an::ui::FileOpenOutcome> opens;
    std::vector<an::ui::FileSaveOutcome> saves;
    std::vector<std::pair<std::string, QString>> recordedThumbnails;

private:
    QTemporaryDir m_directory;
    QObject m_context;
    PersistenceDbExecutor m_executor;
    ThumbnailPersistence m_thumbnails;
    std::unique_ptr<an::ui::FileManager> m_manager;
};

} // namespace an::tests::files
