// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the sequence cases share: an application context that owns the one engine session and lends it, makes the
/// views a sequence opens in, names temporary sequence files, answers file dialogs and keeps every message and setting;
/// and fixtures that write SER - timed by a trailer and dated by its header when asked - AVI, MP4 and image-list
/// sequences, an image list of X-Trans stills, and read one back, straight through the engine's sources and sinks, so a
/// case's oracle never runs the code it holds.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/AviSequence.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/IO/FfmpegSink.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/IO/SerSequence.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::sequences
{

using an::tests::viewer::channelCountOf;
using an::tests::viewer::Channels;
using an::tests::viewer::describeImage;
using an::tests::viewer::kPatience;
using an::tests::viewer::kQuietPeriod;
using an::tests::viewer::makeAsset;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;

/// Icons the windows ask for by name, all empty.
class StandInIcons final : public an::ui::IIconProvider
{
public:
    [[nodiscard]] QIcon getIcon(const QString&) override
    {
        return QIcon();
    }
    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString&, QSize size, QColor) override
    {
        return QPixmap(size);
    }
    [[nodiscard]] QColor getColorNormal() const override
    {
        return QColor(Qt::white);
    }
    [[nodiscard]] QColor getColorDisabled() const override
    {
        return QColor(Qt::gray);
    }
};

/// An application context over one engine session, implementing what the sequences, the viewer and the windows reach.
class SequencesTestContext final : public an::ui::IAppContext,
                                   public an::ui::IDialogService,
                                   public an::ui::IGlobalSettingsProvider,
                                   public an::ui::IViewRegistry,
                                   public an::ui::IViewControlManager,
                                   public an::ui::IStatusBarService,
                                   public an::ui::IActionStateManager,
                                   public an::ui::IOutputIntentProvider,
                                   public an::ui::IEngineSessionProvider,
                                   public an::ui::IViewFactory,
                                   public an::ui::ITempFileService,
                                   public an::ui::IFileDialogService,
                                   public an::ui::IPathMemoryService,
                                   public an::ui::IProcessRegistry,
                                   public an::ui::IProcessSettingsProvider
{
public:
    explicit SequencesTestContext(an::consumer::EngineSession session)
        : m_session(std::move(session)), m_icons(std::make_shared<StandInIcons>())
    {
    }

    ~SequencesTestContext() override
    {
        // Every worker reads the session; the views and the session go only after they have all finished.
        closeViews();
    }

    SequencesTestContext(const SequencesTestContext&) = delete;
    SequencesTestContext& operator=(const SequencesTestContext&) = delete;

    /// Deletes every view this context made, once every worker has finished.
    void closeViews()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        for (ImageViewControl* view : m_made)
        {
            delete view;
        }
        m_made.clear();
        madeViews.clear();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    }

    /// Deletes one view this context made at once, waiting for nothing, as the application closes a view.
    void closeView(ImageViewControl* view)
    {
        m_made.erase(std::remove(m_made.begin(), m_made.end(), view), m_made.end());
        madeViews.erase(std::remove(madeViews.begin(), madeViews.end(), view), madeViews.end());
        delete view;
    }

    /// A directory of the case's own, removed with the context.
    [[nodiscard]] QString directory() const
    {
        return m_temporary.path();
    }

    /// A path in the case's directory.
    [[nodiscard]] QString pathOf(const QString& name) const
    {
        return QDir(m_temporary.path()).filePath(name);
    }

    // ---- IAppContext.

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return m_icons;
    }
    [[nodiscard]] an::ui::IDialogService* getDialogService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IGlobalSettingsProvider* getGlobalSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return lendTemporaryFiles ? this : nullptr;
    }
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IViewRegistry* getViewRegistry() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IAppEventBus* getAppEventBus() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewControlManager* getViewControlManager() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IStatusBarService* getStatusBarService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IActionStateManager* getActionStateManager() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IEngineSessionProvider* getEngineSessionProvider() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return nullptr;
    }
    [[nodiscard]] QWidget* getMainWindow() override
    {
        return nullptr;
    }

    // ---- IEngineSessionProvider.

    [[nodiscard]] const an::consumer::EngineSession& engineSession() const noexcept override
    {
        return m_session;
    }

    /// The session, for a case that changes its settings.
    [[nodiscard]] an::consumer::EngineSession& mutableSession() noexcept
    {
        return m_session;
    }

    // ---- IDialogService: every message is kept.

    void openSettingsAt(const QString& pageName) override
    {
        messages.push_back(QStringLiteral("settings:") + pageName);
    }
    void messageBoxYesNo(QWidget*, const QString& title, const QString&, bool, int* ret) override
    {
        messages.push_back(QStringLiteral("question:") + title);
        if (ret != nullptr)
        {
            *ret = answerYes ? QMessageBox::Yes : QMessageBox::No;
        }
    }
    void notifyErrorMessage(QWidget*, const QString& title, const QString& text) override
    {
        messages.push_back(QStringLiteral("error:") + title);
        errorTexts.push_back(text);
    }
    void notifyErrorMessageAsync(QWidget*, const QString& title, const QString& text) override
    {
        notifyErrorMessage(nullptr, title, text);
    }
    void notifyWarnMessage(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("warning:") + title);
    }
    void notifyWarnMessageAsync(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("warning:") + title);
    }
    void notifyInfoMessage(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("info:") + title);
    }
    void notifyInfoMessageAsync(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("info:") + title);
    }

    // ---- IGlobalSettingsProvider, in memory.

    bool upsert(const QString& key, const QString& value) override
    {
        m_settings.insert(key, value);
        return true;
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return m_settings.value(key, defaultValue);
    }
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const auto found = m_settings.constFind(key);
        return found == m_settings.constEnd() ? std::nullopt : std::optional<QString>(*found);
    }

    // ---- IViewRegistry and IViewControlManager: the views the viewer registers, in order.

    [[nodiscard]] ImageViewControl* getActiveImageViewControl() override
    {
        return m_active;
    }
    void setActiveImageViewControl(ImageViewControl* view) override
    {
        m_active = view;
    }
    void addViewControl(ImageViewControl* view) override
    {
        m_views.push_back(view);
    }
    void removeViewControl(ImageViewControl* view, bool) override
    {
        m_views.erase(std::remove(m_views.begin(), m_views.end(), view), m_views.end());
        if (m_active == view)
        {
            m_active = nullptr;
        }
    }
    void renameViewControl(ImageViewControl*) override {}
    void maximizeViewControl(ImageViewControl*) override {}
    void restoreViewControl(ImageViewControl*) override {}
    void notifyViewControlUpdated(const ImageViewControl*) override {}
    void createContextMenuForViewControl(ImageViewControl*, QContextMenuEvent*) override {}
    [[nodiscard]] bool supportsDockWidgets() const override
    {
        return false;
    }
    [[nodiscard]] ImageViewControl* getViewControlByName(const std::string& name) override
    {
        for (ImageViewControl* view : m_views)
        {
            if (view->getUniqueName() == name)
            {
                return view;
            }
        }
        return nullptr;
    }
    [[nodiscard]] std::vector<ImageViewControl*> getListOfViewControls() override
    {
        return m_views;
    }
    [[nodiscard]] std::string createViewControlUniqueName(const std::string& name) override
    {
        return name;
    }

    // ---- IStatusBarService and IActionStateManager.

    void updateImageInfoInStatusBar() override {}
    void updateImageZoomInfoInStatusBar() override {}
    void updateImageCelestialCoordsInStatusBar(double, double) override {}
    void clearImageCelestialCoordsInStatusBar() override {}
    void setCustomImageActionEnabled(ImageViewControl*, GlobalViewControlActions, bool) override {}
    void updateActiveImageActions() override {}
    void updateActiveImageHistory() override {}

    // ---- IOutputIntentProvider: the source's representation.

    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const override
    {
        if (container == an::CodecFormat::Unknown || container == an::CodecFormat::Raw)
        {
            return an::Status::failure(an::ErrorCode::Unsupported, "not a container the product writes");
        }
        return an::OutputIntent::create(an::OutputIntentRequest{.sampleType = source.sampleType()});
    }

    // ---- IViewFactory: the views a sequence opens in, kept and deleted with the context.

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string& historyDesc,
                                                                       ViewControlMode mode, ImageViewControl*, bool,
                                                                       bool) override
    {
        if (refuseViews)
        {
            return nullptr;
        }
        auto* view = new ImageViewControl();
        view->initialize(document.names().displayName.toStdString(), mode);
        view->resize(640, 480);
        view->setDocument(std::move(document));
        view->initializeNewImageHistory(historyDesc);
        m_made.push_back(view);
        madeViews.push_back(view);
        return view;
    }

    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&& document, QWidget*,
                                          bool autoDebayer, const std::string& historyDesc, ViewControlMode mode,
                                          ImageViewControl*, bool, bool) override
    {
        if (refuseViews || !document.sequence().has_value())
        {
            return nullptr;
        }
        auto* view = new ImageViewControl();
        view->initialize(document.names().displayName.toStdString(), mode);
        view->resize(640, 480);
        view->setSequenceDocument(std::move(document), autoDebayer);
        view->initializeNewImageHistory(historyDesc);
        m_made.push_back(view);
        madeViews.push_back(view);
        madeAutoDebayer.push_back(autoDebayer);
        return view;
    }

    // ---- ITempFileService: paths in the case's directory, with the containers' extensions.

    [[nodiscard]] bool isTempFile(const std::string& fileName) override
    {
        return QString::fromStdString(fileName).startsWith(m_temporary.path());
    }
    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override
    {
        const int index = m_nextTemporary.fetch_add(1);
        const char* extension = "";
        switch (format)
        {
        case an::SequenceFormat::Ser:
            extension = ".ser";
            break;
        case an::SequenceFormat::ImageList:
            extension = ".anseq";
            break;
        case an::SequenceFormat::Avi:
            extension = ".avi";
            break;
        case an::SequenceFormat::Mp4:
            extension = ".mp4";
            break;
        case an::SequenceFormat::Video:
            extension = ".mkv";
            break;
        case an::SequenceFormat::Fits:
        case an::SequenceFormat::Dummy:
            break;
        }
        return QStringLiteral("%1/SEQ_SWAP%2%3")
            .arg(m_temporary.path())
            .arg(index)
            .arg(QString::fromLatin1(extension))
            .toStdString();
    }
    [[nodiscard]] QString getTempDirPath() const override
    {
        return m_temporary.path();
    }

    // ---- IFileDialogService: the files the case chose.

    [[nodiscard]] QStringList selectImageFiles(QWidget*, const QString&, const QString&, bool) override
    {
        return chosenFiles;
    }
    [[nodiscard]] QStringList selectFiles(QWidget*, const QString&, const QString&, const QStringList&) override
    {
        return chosenFiles;
    }
    [[nodiscard]] QStringList saveFiles(QWidget*, const QString&, const QString&, const QString&,
                                        const QStringList&) override
    {
        return chosenFiles;
    }

    // ---- IPathMemoryService, in memory.

    [[nodiscard]] QString selectPath(const QString& contextKey, const QString& defaultValue = {}) override
    {
        return m_paths.value(contextKey, defaultValue);
    }
    bool upsertPath(const QString& contextKey, const QString& path) override
    {
        m_paths.insert(contextKey, path);
        return true;
    }

    // ---- IProcessRegistry and IProcessSettingsProvider: the open process windows and their remembered settings.

    void registerProcessInstance(TopLevelProcessBaseControl* process) override
    {
        openProcesses.push_back(process);
    }
    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override
    {
        openProcesses.erase(std::remove(openProcesses.begin(), openProcesses.end(), process), openProcesses.end());
    }
    bool upsert(const QString& processName, int, int, int, const QString& json) override
    {
        processSettings.insert(processName, json);
        return true;
    }
    [[nodiscard]] bool select(const QString& processName, int&, int&, int&, QString& json) override
    {
        const auto found = processSettings.constFind(processName);
        if (found == processSettings.constEnd())
        {
            return false;
        }
        json = *found;
        return true;
    }

    // ---- What the cases read and steer.

    /// The process windows registered and not yet unregistered.
    std::vector<TopLevelProcessBaseControl*> openProcesses;

    /// The files a file dialog answers with.
    QStringList chosenFiles;

    /// How a yes-or-no question is answered.
    bool answerYes = false;

    /// Whether the view factory refuses to make a view.
    bool refuseViews = false;

    /// Whether the temporary-file service is lent.
    bool lendTemporaryFiles = true;

    /// Every dialog and toast, as "kind:title".
    QStringList messages;

    /// The text of every error shown.
    QStringList errorTexts;

    /// The remembered settings of the process windows, by name.
    QHash<QString, QString> processSettings;

    /// The views the factory made, and whether each demosaics a sequence's mosaic frames.
    std::vector<ImageViewControl*> madeViews;
    std::vector<bool> madeAutoDebayer;

    /// How many errors were shown.
    [[nodiscard]] int errorCount() const
    {
        return static_cast<int>(std::count_if(messages.cbegin(), messages.cend(),
                                              [](const QString& message) { return message.startsWith("error:"); }));
    }

private:
    an::consumer::EngineSession m_session;
    std::shared_ptr<StandInIcons> m_icons;
    QHash<QString, QString> m_settings;
    QHash<QString, QString> m_paths;
    std::vector<ImageViewControl*> m_views;
    std::vector<ImageViewControl*> m_made;
    ImageViewControl* m_active = nullptr;
    QTemporaryDir m_temporary;
    mutable std::atomic<int> m_nextTemporary{0};
};

/// A sequences context over a session of a given width that reads and writes FITS files; fails the case when the
/// session cannot be opened.
[[nodiscard]] inline std::unique_ptr<SequencesTestContext> openSequencesContext(std::uint32_t width = 4u)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = an::tests::viewer::openFileProvider(width);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<SequencesTestContext>(std::move(provider->owned()));
}

/// Installs an application context for one case and clears it when the case ends, passing or failing.
class InstalledContext
{
public:
    explicit InstalledContext(an::ui::IAppContext& context)
    {
        an::ui::setAppContext(&context);
    }
    ~InstalledContext()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        an::ui::clearAppContext();
    }
    InstalledContext(const InstalledContext&) = delete;
    InstalledContext& operator=(const InstalledContext&) = delete;
};

/// The sample a fixture frame holds at a place: distinct per frame, column, row and channel, and below 4096 so it fits
/// twelve bits of any representation the fixtures use.
[[nodiscard]] inline std::uint16_t fixtureSample(std::uint32_t frame, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t channel)
{
    return static_cast<std::uint16_t>((frame * 397u + x * 31u + y * 7u + channel * 101u) % 4096u);
}

/// A textured sample of a fixture frame: every spatial frequency, where demosaic methods disagree - `fixtureSample` is
/// a ramp, which every method reproduces exactly.
[[nodiscard]] inline std::uint16_t texturedSample(std::uint32_t frame, std::uint32_t x, std::uint32_t y)
{
    return static_cast<std::uint16_t>((x * x * 977u + y * y * 613u + x * y * 331u + frame * 7919u) % 65536u);
}

/// A fixture frame of a representation as an engine image of a session's context, planar, each sample `value`.
template <typename SampleT>
[[nodiscard]] an::Result<an::Image<SampleT>>
fixtureImage(const an::EngineContext& engine, std::uint32_t width, std::uint32_t height, Channels channels,
             const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
             an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    const std::optional<an::ImageDescriptor> descriptor = describeImage(
        engine, an::kSampleTypeOf<SampleT>, width, height, channels, an::NumericDomain::NormalizedLinear, cfa);
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the fixture frame has no description");
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        engine, *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    if (!image.hasValue())
    {
        return image.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        for (std::uint32_t channel = 0u; channel < channelCountOf(channels); ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(channel, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = value(x, y, channel);
                }
            }
        }
    }
    return image;
}

/// What a fixture sequence holds.
struct FixtureSequence
{
    /// The frames.
    std::uint32_t frames = 3u;

    /// Each frame's width.
    std::uint32_t width = 8u;

    /// Each frame's height.
    std::uint32_t height = 6u;

    /// Each frame's channels.
    Channels channels = Channels::Grey;

    /// The filter array a grey frame states, or none.
    an::CfaDescriptor cfa{};

    /// The frames per second the container states.
    an::SequenceRate rate{2u, 1u};

    /// Whether each frame holds `texturedSample` rather than `fixtureSample`.
    bool textured = false;

    /// For a SER, the SER ticks between one frame's timestamp and the next, written into its trailer from
    /// `kFixtureTimelineOrigin`; nothing writes no trailer.
    std::optional<std::int64_t> trailerInterval{};

    /// For a SER, the recording's UTC start time its header states, in SER ticks; zero states it unknown.
    std::uint64_t serDateTimeUtcTicks = 0u;
};

/// The SER ticks a timed fixture's first frame is stamped with: an instant in late 2023, a whole second.
inline constexpr std::uint64_t kFixtureTimelineOrigin = an::kSerUnixEpochTicks + 17000000000000000u;

/// The sample fixture frame `frame` holds at a place, before a container narrows it.
[[nodiscard]] inline std::uint16_t fixtureSequenceSample(const FixtureSequence& fixture, std::uint32_t frame,
                                                         std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    return fixture.textured ? texturedSample(frame, x, y) : fixtureSample(frame, x, y, channel);
}

/// Writes a fixture sequence through an engine sink of the container, published whole: frame `f` holds
/// `fixtureSequenceSample(fixture, f, x, y, c)` - eight bits of it for AVI and video, sixteen for SER. Fails the case
/// when it cannot.
[[nodiscard]] inline bool writeFixture(const an::consumer::EngineSession& session, const QString& path,
                                       an::SequenceFormat container, const FixtureSequence& fixture)
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return false;
    }
    const std::string destination = path.toUtf8().toStdString();
    an::Result<an::SequenceOutput> output = an::SequenceOutput::begin(
        scope->engine(), scope->execution(), an::SequenceOutputRequest{.destinationPath = destination});
    EXPECT_TRUE(output.hasValue()) << output.status().message();
    if (!output.hasValue())
    {
        return false;
    }
    const bool wide = container == an::SequenceFormat::Ser;
    const an::SampleType type = wide ? an::SampleType::UInt16 : an::SampleType::UInt8;
    const std::optional<an::ImageDescriptor> frameDescriptor =
        describeImage(scope->engine(), type, fixture.width, fixture.height, fixture.channels,
                      an::NumericDomain::NormalizedLinear, fixture.cfa);
    EXPECT_TRUE(frameDescriptor.has_value());
    if (!frameDescriptor.has_value())
    {
        return false;
    }
    an::SequenceDescriptor descriptor;
    descriptor.format = container;
    descriptor.fixedFrame = an::RawFrameDescriptor::fromImageDescriptor(*frameDescriptor);
    descriptor.frameCount = fixture.frames;
    descriptor.nominalFrameRate = fixture.rate;
    const bool timed = wide && fixture.trailerInterval.has_value();
    if (timed)
    {
        // A trailer from presentation ticks: a time base of one SER tick, every frame carrying its ticks.
        descriptor.timeBase = an::SequenceRate{1u, static_cast<std::uint32_t>(an::kSerTicksPerSecond)};
        descriptor.timestampsRequired = true;
    }
    an::Result<an::FrameSink> sink = an::Status::failure(an::ErrorCode::Unsupported, "no sink");
    switch (container)
    {
    case an::SequenceFormat::Ser: {
        an::SerSinkRequest request;
        request.dateTimeUtcTicks = fixture.serDateTimeUtcTicks;
        if (timed)
        {
            request.timestamps = an::SerTimestampSource::PresentationTicks;
            request.timelineOriginTicks = kFixtureTimelineOrigin;
        }
        sink = an::createSerSink(scope->engine(), scope->execution(), *output, descriptor, request);
        break;
    }
    case an::SequenceFormat::Avi:
        sink = an::createAviSink(scope->engine(), scope->execution(), *output, descriptor, an::AviSinkRequest{});
        break;
    case an::SequenceFormat::Mp4:
        sink = an::createFfmpegSink(scope->engine(), scope->execution(), *output, descriptor,
                                    an::FfmpegSinkRequest{.quality = an::FfmpegEncodeQuality::Best});
        break;
    default:
        break;
    }
    EXPECT_TRUE(sink.hasValue()) << sink.status().message();
    if (!sink.hasValue())
    {
        return false;
    }
    for (std::uint32_t frame = 0u; frame < fixture.frames; ++frame)
    {
        an::Status appended = an::Status::success();
        an::SequencePosition position{.index = frame};
        if (timed)
        {
            position.presentationTicks = static_cast<std::int64_t>(frame) * *fixture.trailerInterval;
        }
        if (wide)
        {
            an::Result<an::Image<std::uint16_t>> image = fixtureImage<std::uint16_t>(
                scope->engine(), fixture.width, fixture.height, fixture.channels,
                [&fixture, frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                    return fixtureSequenceSample(fixture, frame, x, y, c);
                },
                fixture.cfa);
            EXPECT_TRUE(image.hasValue()) << image.status().message();
            an::Result<an::RawFrame> borrowed = an::borrowImageFrame(*image);
            an::SequenceFrame sequenceFrame{.frame = std::move(*borrowed), .position = position};
            const an::Result<an::SequenceWriteResult> written =
                sink->append(scope->engine(), scope->execution(), sequenceFrame);
            appended = written.hasValue() ? an::Status::success() : written.status();
        }
        else
        {
            an::Result<an::Image<std::uint8_t>> image = fixtureImage<std::uint8_t>(
                scope->engine(), fixture.width, fixture.height, fixture.channels,
                [&fixture, frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                    return static_cast<std::uint8_t>(fixtureSequenceSample(fixture, frame, x, y, c) % 256u);
                },
                fixture.cfa);
            EXPECT_TRUE(image.hasValue()) << image.status().message();
            an::Result<an::RawFrame> borrowed = an::borrowImageFrame(*image);
            an::SequenceFrame sequenceFrame{.frame = std::move(*borrowed), .position = position};
            const an::Result<an::SequenceWriteResult> written =
                sink->append(scope->engine(), scope->execution(), sequenceFrame);
            appended = written.hasValue() ? an::Status::success() : written.status();
        }
        EXPECT_TRUE(appended.ok()) << appended.message();
        if (appended.failed())
        {
            return false;
        }
    }
    const an::Result<an::SequenceWriteResult> finished = sink->finish(scope->execution());
    EXPECT_TRUE(finished.hasValue()) << finished.status().message();
    if (!finished.hasValue())
    {
        return false;
    }
    (void)sink->close();
    const an::Result<an::SequenceOutputReport> committed = output->commit(scope->execution(), *finished);
    EXPECT_TRUE(committed.hasValue()) << committed.status().message();
    return committed.hasValue();
}

/// Writes one sixteen-bit FITS still through the engine's output transaction: `value(x, y, c)`, or
/// `fixtureSample(frame, x, y, c)` when no value is given, with the cards given. Fails the case when it cannot.
[[nodiscard]] inline bool
writeFitsStill(const an::consumer::EngineSession& session, const QString& path, std::uint32_t frame,
               std::uint32_t width, std::uint32_t height, Channels channels,
               an::CfaDescriptor cfa = an::CfaDescriptor{}, std::vector<an::CodecCard> cards = {},
               std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> value = {})
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return false;
    }
    if (!value)
    {
        value = [frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return fixtureSample(frame, x, y, c); };
    }
    an::Result<an::Image<std::uint16_t>> image =
        fixtureImage<std::uint16_t>(scope->engine(), width, height, channels, value, cfa);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return false;
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    const an::ImageMetadata metadata = an::ImageMetadata::create(
        an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{}, an::Provenance{},
        codec.hasValue() ? std::move(*codec) : an::CodecMetadata{}, std::nullopt);
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt16});
    EXPECT_TRUE(intent.hasValue()) << intent.status().message();
    an::Result<an::RawFrame> borrowed = an::borrowImageFrame(*image);
    const std::string destination = path.toUtf8().toStdString();
    an::OutputRequest request;
    request.destinationPath = destination;
    request.format = an::CodecFormat::Fits;
    request.intent = intent.hasValue() ? &*intent : nullptr;
    const an::Result<an::OutputReport> written =
        an::writeImage(scope->engine(), scope->execution(), request, *borrowed, metadata);
    EXPECT_TRUE(written.hasValue()) << written.status().message();
    return written.hasValue();
}

/// What a sequence file holds, read straight through the engine's source of its container.
struct ReadBack
{
    /// What the source states.
    an::SequenceDescriptor descriptor{};

    /// Each frame's description.
    std::vector<an::RawFrameDescriptor> frames;

    /// Each frame's samples, every channel, as doubles in their own units.
    std::vector<std::vector<double>> samples;

    /// Each frame's metadata, as the source read it: its acquisition record, its solution and its file's cards.
    std::vector<an::ImageMetadata> metadata;
};

/// The exposure start each frame of a sequence read back states, in order.
[[nodiscard]] inline std::vector<std::optional<an::UtcInstant>> exposureStartsOf(const ReadBack& read)
{
    std::vector<std::optional<an::UtcInstant>> starts;
    for (const an::ImageMetadata& metadata : read.metadata)
    {
        starts.push_back(metadata.acquisition().exposureStart());
    }
    return starts;
}

/// One sample of a view, planar or interleaved, as a double; zero outside it.
template <typename ViewT>
[[nodiscard]] double sampleAt(const ViewT& view, std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    const bool planar = view.layout().planeCount() > 1u || view.layout().samplesPerPixel() == 1u;
    const auto row = view.row(planar ? channel : 0u, y);
    if (!row.has_value())
    {
        return 0.0;
    }
    const std::size_t index = planar ? x : static_cast<std::size_t>(x) * view.layout().samplesPerPixel() + channel;
    return index < row->size() ? static_cast<double>((*row)[index]) : 0.0;
}

/// The samples of a raw frame, as doubles, channel by channel and row by row; empty for a representation the fixtures
/// do not write.
[[nodiscard]] inline std::vector<double> samplesOf(const an::RawFrame& frame)
{
    std::vector<double> values;
    const std::optional<an::RawFrameDescriptor> described = frame.descriptor();
    const std::optional<an::ImageLayout> layout = frame.layout();
    const std::optional<an::ImageDescriptor> image =
        described.has_value() ? described->toImageDescriptor() : std::nullopt;
    if (!layout.has_value() || !image.has_value())
    {
        return values;
    }
    const std::span<const std::byte> bytes = frame.bytes();
    const auto dispatched = an::dispatchSampleType(image->sampleType(), [&]<typename SampleT>() {
        const auto view = an::ImageView<const SampleT>::create(
            std::span<const SampleT>(reinterpret_cast<const SampleT*>(bytes.data()), bytes.size() / sizeof(SampleT)),
            *layout);
        if (!view.has_value())
        {
            return;
        }
        const std::uint32_t channels = image->extent().channelCount();
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < image->extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < image->extent().width(); ++x)
                {
                    values.push_back(sampleAt(*view, x, y, channel));
                }
            }
        }
    });
    (void)dispatched;
    return values;
}

/// Reads every frame of a sequence file through the engine's source of its container; fails the case when it cannot.
[[nodiscard]] inline std::optional<ReadBack> readBack(const an::consumer::EngineSession& session, const QString& path,
                                                      an::SequenceFormat container)
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    const std::string file = path.toUtf8().toStdString();
    an::Result<an::FrameSource> source = an::Status::failure(an::ErrorCode::Unsupported, "no source");
    std::vector<std::string> frames;
    switch (container)
    {
    case an::SequenceFormat::Ser:
        source = an::createSerSource(scope->engine(), scope->execution(), file, an::SerSourceOptions{});
        break;
    case an::SequenceFormat::Avi:
    case an::SequenceFormat::Mp4:
    case an::SequenceFormat::Video:
        source =
            an::createFfmpegSource(scope->engine(), scope->execution(), file,
                                   an::FfmpegSourceOptions{.unspecifiedMatrix = an::FfmpegMatrixAssumption::Bt601,
                                                           .unspecifiedRange = an::FfmpegRangeAssumption::Limited,
                                                           .emptyPalette = an::FfmpegEmptyPalettePolicy::Grayscale});
        break;
    case an::SequenceFormat::ImageList: {
        an::Result<an::ImageListManifest> manifest =
            an::readImageListManifest(scope->engine(), scope->execution(), file);
        EXPECT_TRUE(manifest.hasValue()) << manifest.status().message();
        if (!manifest.hasValue())
        {
            return std::nullopt;
        }
        for (const std::string& frame : manifest->framePaths)
        {
            frames.push_back(*an::resolveImageListFramePath(file, frame));
        }
        std::vector<an::CodecReadRequest> requests;
        for (const std::string& frame : frames)
        {
            requests.push_back(an::CodecReadRequest{.path = frame});
        }
        source = an::createImageListSource(scope->engine(), scope->execution(), requests, an::ImageListSourceOptions{});
        break;
    }
    default:
        break;
    }
    EXPECT_TRUE(source.hasValue()) << source.status().message();
    if (!source.hasValue())
    {
        return std::nullopt;
    }
    ReadBack read;
    read.descriptor = *source->descriptor();
    for (;;)
    {
        an::Result<an::SequenceReadResult> next = source->read(scope->engine(), scope->execution());
        EXPECT_TRUE(next.hasValue()) << next.status().message();
        if (!next.hasValue())
        {
            return std::nullopt;
        }
        if (!next->frame.has_value())
        {
            break;
        }
        read.frames.push_back(*next->frame->frame.descriptor());
        read.samples.push_back(samplesOf(next->frame->frame));
        read.metadata.push_back(next->frame->metadata);
    }
    return read;
}

/// The samples of an asset, as doubles, channel by channel and row by row.
[[nodiscard]] inline std::vector<double> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::vector<double> values;
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return values;
    }
    lease->visit([&](const auto& typed) {
        const auto view = typed.view();
        const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
        if (!view.has_value() || !descriptor.has_value())
        {
            return;
        }
        for (std::uint32_t channel = 0u; channel < descriptor->extent().channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < descriptor->extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < descriptor->extent().width(); ++x)
                {
                    values.push_back(sampleAt(*view, x, y, channel));
                }
            }
        }
    });
    return values;
}

/// The samples fixture frame `frame` holds, as `writeFixture` wrote them for a container.
[[nodiscard]] inline std::vector<double> fixtureSamples(std::uint32_t frame, const FixtureSequence& fixture,
                                                        bool eightBit)
{
    std::vector<double> values;
    for (std::uint32_t channel = 0u; channel < channelCountOf(fixture.channels); ++channel)
    {
        for (std::uint32_t y = 0u; y < fixture.height; ++y)
        {
            for (std::uint32_t x = 0u; x < fixture.width; ++x)
            {
                const std::uint16_t sample = fixtureSequenceSample(fixture, frame, x, y, channel);
                values.push_back(static_cast<double>(eightBit ? sample % 256u : sample));
            }
        }
    }
    return values;
}

/// The largest difference between two sample lists of one length; a negative number when their lengths differ.
[[nodiscard]] inline double largestDifference(const std::vector<double>& left, const std::vector<double>& right)
{
    if (left.size() != right.size())
    {
        return -1.0;
    }
    double largest = 0.0;
    for (std::size_t i = 0u; i < left.size(); ++i)
    {
        largest = (std::max)(largest, std::abs(left[i] - right[i]));
    }
    return largest;
}

/// Writes an image list of identical X-Trans stills in a context's directory - each a TIFF, the one container of the
/// test sessions that states a six-by-six filter array - and its manifest naming them; fails the case when it cannot.
///
/// @return the manifest's path, or empty when it could not be written.
[[nodiscard]] inline QString writeXTransList(const SequencesTestContext& context, std::uint32_t frames)
{
    an::ImageListManifest manifest;
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        const QString still = context.pathOf(QStringLiteral("xtrans-%1.tif").arg(frame));
        if (!an::tests::viewer::writeXTransTiff(context.engineSession(), still))
        {
            return {};
        }
        manifest.framePaths.push_back(QFileInfo(still).fileName().toStdString());
    }
    const QString list = context.pathOf(QStringLiteral("xtrans.anseq"));
    const an::consumer::EngineSession& session = context.engineSession();
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return {};
    }
    const std::string destination = list.toUtf8().toStdString();
    const an::SequenceOutputRequest request{.destinationPath = destination};
    const auto written = an::writeImageListManifest(scope->engine(), scope->execution(), request, manifest);
    EXPECT_TRUE(written.hasValue()) << written.status().message();
    return written.hasValue() ? list : QString();
}

/// The files directly in a directory, by name.
[[nodiscard]] inline QStringList filesIn(const QString& directory)
{
    return QDir(directory).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

} // namespace an::tests::sequences
