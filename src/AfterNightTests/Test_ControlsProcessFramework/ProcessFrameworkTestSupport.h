// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the process framework's cases share: an application context that owns the one engine session and lends it,
/// makes the views a run publishes, names temporary sequence files and keeps every message; and a process built on the
/// typed framework whose body each case steers - it scales the channel it is handed, records every run, and can wait at
/// a gate, wait for its stop, fail, or write nothing.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProcessExecution.h"
#include "controls/TypedProcessControl.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace an::tests::processes
{

using an::tests::viewer::Channels;
using an::tests::viewer::kPatience;
using an::tests::viewer::kQuietPeriod;
using an::tests::viewer::makeAsset;
using an::tests::viewer::makeDocument;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;

/// Icons the framework asks for by name, all empty.
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

/// An application context over one engine session, implementing what the framework and the viewer reach.
class ProcessTestContext final : public an::ui::IAppContext,
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
                                 public an::ui::IProcessRegistry,
                                 public an::ui::IProcessSettingsProvider
{
public:
    explicit ProcessTestContext(an::consumer::EngineSession session)
        : m_session(std::move(session)), m_icons(std::make_shared<StandInIcons>())
    {
    }

    ~ProcessTestContext() override
    {
        // Every worker reads the session; the views and the session go only after they have all finished.
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        for (ImageViewControl* view : m_made)
        {
            delete view;
        }
        m_made.clear();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
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
        return nullptr;
    }
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override
    {
        return nullptr;
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

    /// Shuts the session down once every worker has finished and every view this context made is gone: it succeeds
    /// only when nothing a view or a process took from it is still held.
    [[nodiscard]] an::Status shutdownSession()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        for (ImageViewControl* view : m_made)
        {
            delete view;
        }
        m_made.clear();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        return m_session.shutdown();
    }

    // ---- IDialogService: every message is kept, and a question is answered as the case says.

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

    /// Whether a read of the settings throws, as the application's store does once it has been shut down.
    std::atomic_bool settingsReadsThrow = false;

    bool upsert(const QString& key, const QString& value) override
    {
        m_settings.insert(key, value);
        return true;
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        throwIfReadsThrow();
        return m_settings.value(key, defaultValue);
    }
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        throwIfReadsThrow();
        const auto found = m_settings.constFind(key);
        return found == m_settings.constEnd() ? std::nullopt : std::optional<QString>(*found);
    }

    // ---- IViewRegistry and IViewControlManager: the views the viewer registers, in order. A view a case deletes
    // without closing leaves the registry with it, so a window that lists the views later never reads a deleted one.

    [[nodiscard]] ImageViewControl* getActiveImageViewControl() override
    {
        return m_active.data();
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
        std::erase_if(m_views,
                      [view](const QPointer<ImageViewControl>& held) { return held.isNull() || held == view; });
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
        for (ImageViewControl* view : getListOfViewControls())
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
        std::erase_if(m_views, [](const QPointer<ImageViewControl>& held) { return held.isNull(); });
        std::vector<ImageViewControl*> views;
        views.reserve(m_views.size());
        for (const QPointer<ImageViewControl>& held : m_views)
        {
            views.push_back(held.data());
        }
        return views;
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

    // ---- IViewFactory: the views a run publishes, kept and deleted with the context.

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
        auto* view = new ImageViewControl();
        view->initialize(document.names().displayName.toStdString(), mode);
        view->resize(640, 480);
        view->setDocument(std::move(document));
        view->initializeNewImageHistory(historyDesc);
        m_made.push_back(view);
        madeViews.push_back(view);
        madeNeighbours.push_back(neighbour);
        madeHistories.push_back(historyDesc);
        return view;
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
        auto* view = new ImageViewControl();
        view->initialize(document.names().displayName.toStdString(), mode);
        view->resize(640, 480);
        view->setSequenceDocument(std::move(document), autoDebayer);
        view->initializeNewImageHistory(historyDesc);
        m_made.push_back(view);
        madeViews.push_back(view);
        madeNeighbours.push_back(neighbour);
        madeHistories.push_back(historyDesc);
        return view;
    }

    // ---- ITempFileService: paths in a directory of the case's own.

    [[nodiscard]] bool isTempFile(const std::string& fileName) override
    {
        return QString::fromStdString(fileName).startsWith(m_temporary.path());
    }
    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override
    {
        return temporarySequencePath(m_nextTemporary.fetch_add(1), format).toStdString();
    }
    [[nodiscard]] QString getTempDirPath() const override
    {
        return m_temporary.path();
    }

    // ---- IProcessRegistry and IProcessSettingsProvider: the open process windows, and no remembered geometry.

    void registerProcessInstance(TopLevelProcessBaseControl* process) override
    {
        openProcesses.push_back(process);
    }
    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override
    {
        openProcesses.erase(std::remove(openProcesses.begin(), openProcesses.end(), process), openProcesses.end());
    }
    bool upsert(const QString&, int, int, int, const QString&) override
    {
        return true;
    }
    [[nodiscard]] bool select(const QString&, int&, int&, int&, QString&) override
    {
        return false;
    }

    // ---- What the cases read and steer.

    /// The process windows registered and not yet unregistered.
    std::vector<TopLevelProcessBaseControl*> openProcesses;

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

    /// The views the factory made, the neighbour each was asked to sit by, and the history each started with.
    std::vector<ImageViewControl*> madeViews;
    std::vector<ImageViewControl*> madeNeighbours;
    std::vector<std::string> madeHistories;

    /// How many errors were shown.
    [[nodiscard]] int errorCount() const
    {
        return static_cast<int>(std::count_if(messages.cbegin(), messages.cend(),
                                              [](const QString& message) { return message.startsWith("error:"); }));
    }

    /// The path the next new sequence of a container will be given, without giving it.
    [[nodiscard]] QString nextTempFilePathForNewSequence(an::SequenceFormat format) const
    {
        return temporarySequencePath(m_nextTemporary.load(), format);
    }

private:
    /// The temporary path of the new sequence of a container given out at an index.
    [[nodiscard]] QString temporarySequencePath(int index, an::SequenceFormat format) const
    {
        const char* extension = format == an::SequenceFormat::Ser ? "ser" : "anseq";
        return QStringLiteral("%1/sequence-%2.%3")
            .arg(m_temporary.path())
            .arg(index)
            .arg(QString::fromLatin1(extension));
    }

    void throwIfReadsThrow() const
    {
        if (settingsReadsThrow.load())
        {
            throw std::runtime_error("the settings store has shut down");
        }
    }

    an::consumer::EngineSession m_session;
    std::shared_ptr<StandInIcons> m_icons;
    QHash<QString, QString> m_settings;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<ImageViewControl*> m_made;
    QPointer<ImageViewControl> m_active;
    QTemporaryDir m_temporary;
    mutable std::atomic<int> m_nextTemporary{0};
};

/// A process context over a session of a given width and memory allowances that reads and writes FITS files; fails the
/// case when the session cannot be opened.
[[nodiscard]] inline std::unique_ptr<ProcessTestContext> openProcessContext(std::uint32_t width = 4u,
                                                                            an::consumer::MemoryAllowances memory = {})
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = an::tests::viewer::openFileProvider(width, memory);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<ProcessTestContext>(std::move(provider->owned()));
}

/// Installs an application context for one case and puts back the one it replaced - none, or a suite's own - when the
/// case ends, passing or failing.
class InstalledContext
{
public:
    explicit InstalledContext(an::ui::IAppContext& context) : m_replaced(an::ui::appContext())
    {
        // setAppContext installs over nothing, so the context installed now is taken down first.
        an::ui::clearAppContext();
        an::ui::setAppContext(&context);
    }
    ~InstalledContext()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        an::ui::clearAppContext();
        if (m_replaced != nullptr)
        {
            an::ui::setAppContext(m_replaced);
        }
    }
    InstalledContext(const InstalledContext&) = delete;
    InstalledContext& operator=(const InstalledContext&) = delete;

private:
    an::ui::IAppContext* m_replaced = nullptr;
};

/// A view, deleted with the case.
struct ViewDeleter
{
    void operator()(ImageViewControl* view) const
    {
        delete view;
    }
};
using ViewHandle = std::unique_ptr<ImageViewControl, ViewDeleter>;

/// A view of a document, as the application makes one: its history begins with the state it was made with.
[[nodiscard]] inline ViewHandle makeTargetView(const std::string& name, an::ui::ImageDocument&& document)
{
    ViewHandle view(new ImageViewControl());
    view->initialize(name, ViewControlMode::standard);
    view->resize(640, 480);
    view->setDocument(std::move(document));
    view->initializeNewImageHistory("Image opened");
    return view;
}

/// A grey Float32 document whose samples are `value(x, y)`.
[[nodiscard]] inline std::optional<an::ui::ImageDocument>
greyDocument(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
             const std::function<float(std::uint32_t, std::uint32_t)>& value)
{
    return makeDocument<float>(session, width, height, Channels::Grey,
                               [&](std::uint32_t x, std::uint32_t y, std::uint32_t) { return value(x, y); });
}

/// The sample of what a document holds now at a position and channel, read under a lease, as a double; NaN when it
/// cannot be read.
[[nodiscard]] inline double documentSample(const an::ui::ImageDocument& document, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t channel = 0u)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!lease.hasValue())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::visit(
        [&](const auto& typed) -> double {
            const auto view = typed.view();
            if (!view.has_value())
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const auto* sample = view->sampleAt(channel, x, y, 0u);
            return sample != nullptr ? static_cast<double>(*sample) : std::numeric_limits<double>::quiet_NaN();
        },
        lease->image());
}

/// The sample of an asset at a position and channel, as a double; NaN when it cannot be read.
[[nodiscard]] inline double assetSample(const an::consumer::ImageAsset& asset, std::uint32_t x, std::uint32_t y,
                                        std::uint32_t channel = 0u)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::visit(
        [&](const auto& typed) -> double {
            const auto view = typed.view();
            if (!view.has_value())
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const auto* sample = view->sampleAt(channel, x, y, 0u);
            return sample != nullptr ? static_cast<double>(*sample) : std::numeric_limits<double>::quiet_NaN();
        },
        lease->image());
}

/// A gate a body waits at until the case opens it, or until its run is stopped.
class Gate
{
public:
    /// Closes the gate: every body that reaches it waits.
    void close()
    {
        const std::lock_guard lock(m_mutex);
        m_open = false;
    }

    /// Opens the gate: every body waiting goes on.
    void open()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_open = true;
        }
        m_changed.notify_all();
    }

    /// Waits until the gate is open or `stopped` says the run was stopped.
    ///
    /// @return whether the gate opened; false when the run was stopped first.
    bool pass(const std::function<bool()>& stopped)
    {
        std::unique_lock lock(m_mutex);
        ++m_arrived;
        m_changed.notify_all();
        while (!m_open)
        {
            if (stopped())
            {
                return false;
            }
            m_changed.wait_for(lock, std::chrono::milliseconds(2));
        }
        return true;
    }

    /// How many bodies have reached the gate.
    [[nodiscard]] int arrived()
    {
        const std::lock_guard lock(m_mutex);
        return m_arrived;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_open = true;
    int m_arrived = 0;
};

/// What a test process's body does, decided by the parameters it is run with.
enum class TestBody : std::uint8_t
{
    /// Multiplies the channel it is handed by the factor, through the channel scope.
    Scale,

    /// Returns at once, having written nothing.
    WriteNothing,

    /// Fails with an error of its own.
    Fail,

    /// Waits until its run is stopped, then reports the stop.
    WaitForStop,

    /// Makes the destination an image of half the size.
    Shrink,

    /// Makes the destination a three-channel image of the source's size, each channel the source's first.
    MakeColour,

    /// Scales a sequence's first two frames as `Scale` does and makes every later one half their size, which a
    /// container whose frames share one size refuses to write.
    ShrinkFromThirdFrame
};

/// A test process's parameters: the widgets it holds, as one value.
struct TestParams
{
    TestBody body = TestBody::Scale;
    double factor = 2.0;
    double modulation = 1.0;
    int generation = 0;

    /// Whether the process declares that its body writes every sample, which `TestBody::Scale` does.
    bool writesEverySample = false;
};

/// One run a test process's body saw.
struct TestRun
{
    an::ui::ProcessRunKind kind = an::ui::ProcessRunKind::Apply;
    an::ChannelScope channel = an::ChannelScope::Whole;
    TestParams params;
    std::uint64_t frame = 0u;
    std::uint32_t sourceWidth = 0u;
    std::uint32_t sourceHeight = 0u;

    /// Whether the destination the body was handed held the source's samples.
    bool destinationHeldSource = false;
};

/// How a test process is set up.
struct TestProcessOptions
{
    bool preview = false;
    bool perChannel = false;
    bool previewArea = false;
    QSize previewAreaSize = QSize(4, 4);
    bool createNewImage = false;
    bool closeAtEnd = false;
    bool progressDialog = false;
    bool supportStretch = false;
};

/// Multiplies a plane of any representation by a factor, through the samples' own type, rounding to it.
inline an::Status scalePlane(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output, double factor)
{
    const an::SampleType type = input.descriptor->sampleType();
    an::Status done = an::Status::success();
    const bool dispatched = an::dispatchSampleType(type, [&]<typename SampleT>() {
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        const std::uint32_t width = in.extent().width();
        const std::uint32_t height = in.extent().height();
        const std::uint32_t channels = in.extent().channelCount();
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const std::uint32_t inPlane = in.samplesPerPixel() == 1u ? channel : 0u;
            const std::uint32_t outPlane = out.samplesPerPixel() == 1u ? channel : 0u;
            const std::uint32_t inStep = in.samplesPerPixel();
            const std::uint32_t outStep = out.samplesPerPixel();
            const std::uint32_t inOffset = in.samplesPerPixel() == 1u ? 0u : channel;
            const std::uint32_t outOffset = out.samplesPerPixel() == 1u ? 0u : channel;
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto* source = reinterpret_cast<const SampleT*>(static_cast<const std::byte*>(input.samples) +
                                                                      *in.rowByteOffset(inPlane, y));
                auto* destination = reinterpret_cast<SampleT*>(static_cast<std::byte*>(output.samples) +
                                                               *out.rowByteOffset(outPlane, y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double scaled = static_cast<double>(source[x * inStep + inOffset]) * factor;
                    destination[x * outStep + outOffset] =
                        an::convertSample<double, SampleT>(scaled,
                                                           an::ConversionPolicy{.domain = an::DomainMapping::CodeValue})
                            .value;
                }
            }
        }
    });
    if (!dispatched)
    {
        done = an::Status::failure(an::ErrorCode::Unsupported, "not one of the eight representations");
    }
    return done;
}

/// What a case keeps of a window's last moment, outside the window: whether it was destroyed, and how much work the
/// thread pool still ran then.
struct ExitRecord
{
    std::atomic<bool> destroyed{false};
    std::atomic<int> poolActiveWhenDestroyed{-1};
};

/// A real-time preview process over the typed framework whose body the case steers.
class TestProcess final : public an::ui::TypedProcessControl<TestParams>
{
public:
    TestProcess(ImageViewControl* target, const TestProcessOptions& options, const std::string& name = "Test Scale")
        : TypedProcessControl<TestParams>(target, name), m_options(options)
    {
        auto* mainLayout = new QHBoxLayout(this);
        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        auto* controlsPanel = new QWidget(splitter);
        auto* controlsLayout = new QVBoxLayout(controlsPanel);
        auto* previewPanel = new QWidget(splitter);
        auto* previewLayout = new QVBoxLayout(previewPanel);
        mainLayout->addWidget(splitter);

        previewCheckbox = new QCheckBox(QStringLiteral("Preview"), controlsPanel);
        previewCheckbox->setChecked(options.preview);
        controlsLayout->addWidget(previewCheckbox);
        channelSelector = new QComboBox(controlsPanel);
        controlsLayout->addWidget(channelSelector);
        buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                       controlsPanel);
        controlsLayout->addWidget(buttons);

        ProcessInitOptions init;
        init.supportPerChannel = options.perChannel;
        init.previewAreaSelectionEnabled = options.previewArea;
        initializeStandardProcess(mainLayout, previewLayout, previewCheckbox, buttons, nullptr, channelSelector, init);
        setExecutionAutostretchSupported(options.supportStretch);
        setupPreviewControl(options.previewArea, options.previewAreaSize, QStringLiteral("Preview area"));
    }

    // ---- What the case steers.

    /// Changes the parameters the window holds, as a widget's change does, and asks for a preview.
    void setParams(const TestParams& params)
    {
        m_params = params;
        settingsUpdated();
    }

    /// The gate every body waits at.
    Gate gate;

    /// Where the window says, as it is destroyed, how much work the pool still ran; none when unset.
    std::shared_ptr<ExitRecord> exitRecord;

    /// Makes the next run's result hook throw, once: work the run does outside its body's own guard, as a reduction
    /// out of memory would.
    std::atomic<bool> throwWhenReady{false};

    ~TestProcess() override
    {
        if (exitRecord != nullptr)
        {
            exitRecord->poolActiveWhenDestroyed = QThreadPool::globalInstance()->activeThreadCount();
            exitRecord->destroyed = true;
        }
    }

    /// The runs every body saw, in the order they started.
    [[nodiscard]] std::vector<TestRun> runs() const
    {
        const std::lock_guard lock(m_runsLock);
        return m_runs;
    }

    /// How many runs of a kind the body saw.
    [[nodiscard]] int runsOf(an::ui::ProcessRunKind kind) const
    {
        const std::lock_guard lock(m_runsLock);
        return static_cast<int>(
            std::count_if(m_runs.cbegin(), m_runs.cend(), [kind](const TestRun& run) { return run.kind == kind; }));
    }

    /// The first sample of every plane the channel dispatch handed the body, in the order handed: what the dispatch
    /// extracted, whatever the run said its channel was.
    [[nodiscard]] std::vector<double> iterationSamples() const
    {
        const std::lock_guard lock(m_runsLock);
        return m_iterationSamples;
    }

    /// How many times a run ended, and how many ended published.
    int ended = 0;
    int completed = 0;

    /// The last progress dialog a run was given.
    QPointer<ProgressDialogControl> lastProgress;

    /// The step the history is told an Apply is.
    [[nodiscard]] an::ui::ProcessStep stepFor(const TestParams& params) const
    {
        return describeTypedProcessStep(params);
    }

    using ImageRTPreviewControl::schedulePreviewProcessExecution;
    using ImageRTPreviewControl::selectedChannelScope;

protected:
    [[nodiscard]] bool isPreviewEnabled() const override
    {
        return previewCheckbox != nullptr && previewCheckbox->isChecked();
    }

    void enablePreview(bool enabled) override
    {
        if (previewCheckbox != nullptr)
        {
            const QSignalBlocker blocker(previewCheckbox);
            previewCheckbox->setChecked(enabled);
        }
    }

    void settingsUpdatedImpl() override {}

    [[nodiscard]] TestParams getParams() const override
    {
        return m_params;
    }

    double getTypedModulationFromParams(const TestParams& params) override
    {
        return params.modulation;
    }

    [[nodiscard]] an::ui::ProcessBodyCoverage typedProcessBodyCoverage(const TestParams& params) const override
    {
        return params.writesEverySample ? an::ui::ProcessBodyCoverage::WritesEverySample
                                        : an::ui::ProcessBodyCoverage::WritesPart;
    }

    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const TestParams& params) const override
    {
        return an::ui::ProcessStep{.name = "Test Scale",
                                   .description = "factor " + QString::number(params.factor, 'f', 6).toStdString()};
    }

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override
    {
        if (!m_options.progressDialog)
        {
            return nullptr;
        }
        lastProgress = new ProgressDialogControl(this, QStringLiteral("Test Scale"), forSequence);
        return lastProgress.data();
    }

    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override
    {
        createNewImage = m_options.createNewImage;
        closeAtEnd = m_options.closeAtEnd;
        executeInBackground = true;
        newImageName = "Test Scale result";
    }

    void executeProcessEndedImpl(const RTPreviewMode, const bool) override
    {
        ++ended;
    }

    void imageReadyToRender(const an::ui::ProcessSource&, const an::ui::ProcessRun&) override
    {
        if (throwWhenReady.exchange(false))
        {
            throw std::bad_alloc();
        }
    }

    void executeProcessCompletedImpl(const RTPreviewMode, const bool) override
    {
        ++completed;
    }

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const TestParams& params,
                                       an::ui::ExecutionScope& scope) override
    {
        const std::optional<an::ImageDescriptor> described = source.descriptor();
        {
            const std::lock_guard lock(m_runsLock);
            m_runs.push_back(TestRun{.kind = run.kind,
                                     .channel = run.channel,
                                     .params = params,
                                     .frame = run.frame,
                                     .sourceWidth = described.has_value() ? described->extent().width() : 0u,
                                     .sourceHeight = described.has_value() ? described->extent().height() : 0u,
                                     .destinationHeldSource = destination.holdsSource()});
        }
        if (!gate.pass([&scope]() { return scope.stopRequested(); }))
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the test body was stopped at its gate");
        }
        switch (params.body)
        {
        case TestBody::WriteNothing:
            return an::Status::success();
        case TestBody::Fail:
            return an::Status::failure(an::ErrorCode::InvalidData, "the test body failed on purpose");
        case TestBody::WaitForStop:
            while (!scope.stopRequested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return an::Status::failure(an::ErrorCode::Cancelled, "the test body saw its stop");
        case TestBody::Shrink:
            return replaceWith(source, destination, scope, described->extent().width() / 2u,
                               described->extent().height() / 2u, 1u);
        case TestBody::MakeColour:
            return replaceWith(source, destination, scope, described->extent().width(), described->extent().height(),
                               3u);
        case TestBody::ShrinkFromThirdFrame:
            if (run.frame >= 2u)
            {
                return replaceWith(source, destination, scope, described->extent().width() / 2u,
                                   described->extent().height() / 2u, 1u);
            }
            break;
        case TestBody::Scale:
            break;
        }
        return executeTypedProcessUsingChannels(source, destination, run, params, scope);
    }

    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun&, const TestParams& params,
                                            an::ui::ExecutionScope&) override
    {
        double first = 0.0;
        (void)an::dispatchSampleType(input.descriptor->sampleType(), [&]<typename SampleT>() {
            first = static_cast<double>(*static_cast<const SampleT*>(input.samples));
        });
        {
            const std::lock_guard lock(m_runsLock);
            m_iterationSamples.push_back(first);
        }
        return scalePlane(input, output, params.factor);
    }

private:
    /// Makes the destination a Float32 image of another shape, every sample the source's first sample times the factor,
    /// an integer sample read as a fraction of its representation's largest value.
    an::Status replaceWith(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                           an::ui::ExecutionScope& scope, std::uint32_t width, std::uint32_t height,
                           std::uint32_t channels)
    {
        const std::optional<float> read = std::visit(
            []<typename ImageT>(const ImageT& typed) -> std::optional<float> {
                const auto view = typed.view();
                const auto* sample = view.has_value() ? view->sampleAt(0u, 0u, 0u, 0u) : nullptr;
                if (sample == nullptr)
                {
                    return std::nullopt;
                }
                using SampleT = std::remove_cvref_t<decltype(*sample)>;
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<float>(*sample);
                }
                else
                {
                    return static_cast<float>(static_cast<double>(*sample) /
                                              static_cast<double>((std::numeric_limits<SampleT>::max)()));
                }
            },
            source.image());
        if (!read.has_value())
        {
            return an::Status::failure(an::ErrorCode::InvalidData,
                                       "the test body cannot read the source's first sample");
        }
        const float first = *read;
        const std::optional<an::ImageDescriptor> described =
            an::tests::viewer::describeImage(scope.engine(), an::SampleType::Float32, (std::max)(1u, width),
                                             (std::max)(1u, height), channels == 1u ? Channels::Grey : Channels::Rgb);
        if (!described.has_value())
        {
            return an::Status::failure(an::ErrorCode::InvalidArgument, "the reshaped image cannot be described");
        }
        an::Result<an::Image<float>> image = an::Image<float>::createFilled(
            scope.engine(), *described, an::StorageArrangement::Planar, static_cast<float>(first * m_params.factor));
        if (!image.hasValue())
        {
            return image.status();
        }
        const an::consumer::EngineSession* session = engineSession();
        if (session == nullptr)
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "no session is lent");
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(*session, an::consumer::AnyImage(std::move(*image)));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        return destination.replace(std::move(*asset));
    }

public:
    QCheckBox* previewCheckbox = nullptr;
    QComboBox* channelSelector = nullptr;
    QDialogButtonBox* buttons = nullptr;

private:
    TestProcessOptions m_options;
    TestParams m_params;
    mutable std::mutex m_runsLock;
    std::vector<TestRun> m_runs;
    std::vector<double> m_iterationSamples;
};

/// A process window, closed and deleted with the case once its runs have landed.
struct ProcessDeleter
{
    void operator()(TestProcess* process) const
    {
        if (process == nullptr)
        {
            return;
        }
        process->gate.open();
        (void)pumpUntil([process]() { return process->isProcessIdle(); });
        delete process;
        QCoreApplication::processEvents();
    }
};
using ProcessHandle = std::unique_ptr<TestProcess, ProcessDeleter>;

/// A process window over a target, as the launcher opens one, without the delete-on-close the framework sets so the
/// case decides when it goes.
[[nodiscard]] inline ProcessHandle openProcess(ImageViewControl& target, const TestProcessOptions& options)
{
    ProcessHandle process(new TestProcess(&target, options));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    process->resize(800, 500);
    return process;
}

/// Turns the event loop until a process's runs and its preview view's work have all landed.
[[nodiscard]] inline bool settleProcess(const TestProcess& process)
{
    if (!pumpUntil([&process]() { return process.isProcessIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return process.isProcessIdle();
}

/// Turns the event loop until a view's work has landed.
[[nodiscard]] inline bool settleView(const ImageViewControl& view)
{
    if (!pumpUntil([&view]() { return view.isWorkIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return view.isWorkIdle();
}

/// Turns the event loop until a view's work has landed and every history of the session has written the snapshots it
/// took. What a view starts when it is made - its first presentation, the snapshot of the state it opened with - has
/// then been admitted, so a case that counts the session's admissions from here counts only the work it starts itself.
[[nodiscard]] inline bool settleViewAndSnapshots(const ImageViewControl& view,
                                                 const an::consumer::EngineSession& session)
{
    if (!settleView(view))
    {
        return false;
    }
    return pumpUntil([&session]() {
        return session.memory().counters(an::consumer::MemoryCategory::PendingSnapshots).entries == 0u;
    });
}

} // namespace an::tests::processes
