// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the service interfaces' cases share: an application context that implements every service
/// interface the way the application will - it owns the one engine session and lends it, keeps settings, paths and
/// listeners in memory, delivers application events on the GUI thread and makes a view around each document its
/// factory is handed, as the application's factory makes one - with documents built through the engine as assets of
/// that session, and bounded waits that keep the GUI thread's event loop turning, so a case whose subject never answers
/// fails instead of hanging the suite.
#pragma once

#include "app/AfterNightAppListener.h"
#include "app/AfterNightEvent.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ViewControlMode.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPixmap>
#include <QStringList>
#include <QThread>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace an::tests::services
{

/// How long a case waits for something that should happen at once before calling it a failure.
inline constexpr std::chrono::seconds kPatience{10};

/// Icons a view asks for by name, all empty.
class EmptyIcons final : public an::ui::IIconProvider
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

/// A view the factory made, deleted with the context that made it.
struct ViewDeleter
{
    void operator()(ImageViewControl* view) const
    {
        delete view;
    }
};
using ViewHandle = std::unique_ptr<ImageViewControl, ViewDeleter>;

/// What the factory was asked for when it made a view.
struct ViewRequest
{
    /// Whether the factory's sequence entry point was asked.
    bool fromSequence = false;

    /// Whether a sequence view was asked to demosaic its frames.
    bool autoDebayer = false;

    /// The description of the view's first history step.
    std::string historyDescription;

    /// What kind of view was asked for.
    ViewControlMode mode = ViewControlMode::standard;
};

/// Delivers application events on the thread it lives on, as the application's main window does.
class EventReceiver final : public QObject
{
protected:
    void customEvent(QEvent* event) override
    {
        if (event->type() == AfterNightEvent::getEventType())
        {
            event->accept();
            static_cast<AfterNightEvent*>(event)->run();
        }
    }
};

/// One process notification the event bus was handed.
struct ProcessNotification
{
    std::string processName;
    const ImageViewControl* target = nullptr;
};

/// An application context over one engine session, implementing every service interface.
///
/// It owns its session as the application does and lends it through `IEngineSessionProvider`. Its view factory makes
/// an `ImageViewControl` stand-in around each document it is handed and keeps it; `refuseViews` makes it refuse
/// instead, leaving the document with its caller. Everything it is asked is kept for the case to read.
class TestAppContext final : public an::ui::IAppContext,
                             public an::ui::IDialogService,
                             public an::ui::IGlobalSettingsProvider,
                             public an::ui::IInterfaceSettingsRuntimeService,
                             public an::ui::IProcessSettingsProvider,
                             public an::ui::ITempFileService,
                             public an::ui::IPathMemoryService,
                             public an::ui::IFileDialogService,
                             public an::ui::IViewFactory,
                             public an::ui::IViewRegistry,
                             public an::ui::IAppEventBus,
                             public an::ui::IProcessRegistry,
                             public an::ui::IOutputIntentProvider,
                             public an::ui::IRecentFilesService,
                             public an::ui::IThumbnailPathProvider,
                             public an::ui::IViewControlManager,
                             public an::ui::IStatusBarService,
                             public an::ui::IActionStateManager,
                             public an::ui::IEngineSessionProvider
{
public:
    explicit TestAppContext(an::consumer::EngineSession session)
        : m_session(std::move(session)), m_icons(std::make_shared<EmptyIcons>())
    {
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
        return this;
    }
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return this;
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
        return this;
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
        return this;
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return this;
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
            *ret = 0;
        }
    }
    void notifyErrorMessage(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("error:") + title);
    }
    void notifyErrorMessageAsync(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("error:") + title);
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

    // ---- IGlobalSettingsProvider and IProcessSettingsProvider, in memory.

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
    bool upsert(const QString& processName, int, int, int, const QString& jsonSettings) override
    {
        m_processSettings.insert(processName, jsonSettings);
        return true;
    }
    [[nodiscard]] bool select(const QString& processName, int&, int&, int&, QString& jsonSettings) override
    {
        const auto found = m_processSettings.constFind(processName);
        if (found == m_processSettings.constEnd())
        {
            return false;
        }
        jsonSettings = *found;
        return true;
    }

    // ---- IInterfaceSettingsRuntimeService.

    void applyTheme(const QString& themeId) override
    {
        messages.push_back(QStringLiteral("theme:") + themeId);
    }
    void refreshAllZoomSettings() override {}

    // ---- ITempFileService: a path per sequence, named after the container it is asked for.

    [[nodiscard]] bool isTempFile(const std::string& fileName) override
    {
        return !tempDirectory.isEmpty() && QString::fromStdString(fileName).startsWith(tempDirectory);
    }
    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override
    {
        sequenceFormatsAsked.push_back(format);
        const char* extension = format == an::SequenceFormat::Ser ? ".ser" : ".anseq";
        return (tempDirectory + QStringLiteral("/sequence_%1").arg(sequenceFormatsAsked.size())).toStdString() +
               extension;
    }
    [[nodiscard]] QString getTempDirPath() const override
    {
        return tempDirectory;
    }

    // ---- IPathMemoryService and IFileDialogService.

    [[nodiscard]] QString selectPath(const QString& contextKey, const QString& defaultValue = {}) override
    {
        return m_paths.value(contextKey, defaultValue);
    }
    bool upsertPath(const QString& contextKey, const QString& path) override
    {
        m_paths.insert(contextKey, path);
        return true;
    }
    [[nodiscard]] QStringList selectImageFiles(QWidget*, const QString&, const QString&, bool) override
    {
        return {};
    }
    [[nodiscard]] QStringList selectFiles(QWidget*, const QString&, const QString&, const QStringList&) override
    {
        return {};
    }
    [[nodiscard]] QStringList saveFiles(QWidget*, const QString&, const QString&, const QString&,
                                        const QStringList&) override
    {
        return {};
    }

    // ---- IViewFactory: a view around each document, made as the application's factory makes one, or a refusal that
    // leaves the document alone.

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string& historyDesc,
                                                                       ViewControlMode mode, ImageViewControl*, bool,
                                                                       bool) override
    {
        if (refuseViews || !document.valid())
        {
            return nullptr;
        }
        ImageViewControl* view = makeView(document, mode);
        view->setDocument(std::move(document));
        view->initializeNewImageHistory(historyDesc);
        requests.push_back(ViewRequest{.historyDescription = historyDesc, .mode = mode});
        return view;
    }
    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&& document, QWidget*,
                                          bool autoDebayer, const std::string& historyDesc, ViewControlMode mode,
                                          ImageViewControl*, bool, bool) override
    {
        if (refuseViews || !document.valid() || !document.sequence().has_value())
        {
            return nullptr;
        }
        ImageViewControl* view = makeView(document, mode);
        view->setSequenceDocument(std::move(document), autoDebayer);
        view->initializeNewImageHistory(historyDesc);
        requests.push_back(ViewRequest{
            .fromSequence = true, .autoDebayer = autoDebayer, .historyDescription = historyDesc, .mode = mode});
        return view;
    }

    // ---- IViewRegistry and IViewControlManager.

    [[nodiscard]] ImageViewControl* getActiveImageViewControl() override
    {
        return m_active;
    }
    void setActiveImageViewControl(ImageViewControl* view) override
    {
        m_active = view;
    }
    void addViewControl(ImageViewControl*) override {}
    void removeViewControl(ImageViewControl*, bool) override {}
    void renameViewControl(ImageViewControl*) override {}
    void maximizeViewControl(ImageViewControl*) override {}
    void restoreViewControl(ImageViewControl*) override {}
    void notifyViewControlUpdated(const ImageViewControl*) override {}
    void createContextMenuForViewControl(ImageViewControl*, QContextMenuEvent*) override {}
    [[nodiscard]] bool supportsDockWidgets() const override
    {
        return false;
    }
    [[nodiscard]] ImageViewControl* getViewControlByName(const std::string&) override
    {
        return nullptr;
    }
    [[nodiscard]] std::vector<ImageViewControl*> getListOfViewControls() override
    {
        std::vector<ImageViewControl*> list;
        for (const ViewHandle& view : views)
        {
            list.push_back(view.get());
        }
        return list;
    }
    [[nodiscard]] std::string createViewControlUniqueName(const std::string& name) override
    {
        return name;
    }

    // ---- IAppEventBus: events delivered on the GUI thread, listeners and notifications kept.

    void subscribeListener(AfterNightAppListener* listener) override
    {
        listeners.push_back(listener);
    }
    void unsubscribeListener(AfterNightAppListener* listener) override
    {
        listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
    }
    void postAfterNightEvent(AfterNightEvent* event, int priority) override
    {
        ++eventsPosted;
        QCoreApplication::postEvent(&m_receiver, event, priority);
    }
    void notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl) override
    {
        opened.push_back({processName, targetCtrl});
    }
    void notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl) override
    {
        executed.push_back({processName, targetCtrl});
    }

    // ---- IProcessRegistry.

    void registerProcessInstance(TopLevelProcessBaseControl*) override {}
    void unregisterProcessInstance(TopLevelProcessBaseControl*) override {}

    // ---- IOutputIntentProvider: the source's representation, the container's compression from the settings.

    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const override
    {
        if (container == an::CodecFormat::Raw)
        {
            return an::Status::failure(an::ErrorCode::Unsupported, "camera raw is not a format the product writes");
        }
        an::OutputIntentRequest request{.sampleType = source.sampleType()};
        if (container == an::CodecFormat::Tiff)
        {
            request.compression = tiffCompression;
        }
        return an::OutputIntent::create(request);
    }

    // ---- IRecentFilesService and IThumbnailPathProvider.

    [[nodiscard]] std::deque<std::string> getRecentFilesList() const override
    {
        return {};
    }
    void openRecentSingleFile(const QString&) override {}
    void openDroppedFiles(const QStringList&) override {}
    [[nodiscard]] QString selectThumbnailPath(const QString&) const override
    {
        return {};
    }

    // ---- IStatusBarService and IActionStateManager.

    void updateImageInfoInStatusBar() override {}
    void updateImageZoomInfoInStatusBar() override {}
    void updateImageCelestialCoordsInStatusBar(double, double) override {}
    void clearImageCelestialCoordsInStatusBar() override {}
    void setCustomImageActionEnabled(ImageViewControl*, GlobalViewControlActions, bool) override {}
    void updateActiveImageActions() override {}
    void updateActiveImageHistory() override {}

    // ---- What the cases read and steer.

    /// Whether the view factory refuses every document.
    bool refuseViews = false;

    /// The compression the export settings choose for a TIFF file.
    an::OutputCompression tiffCompression = an::OutputCompression::Lzw;

    /// The temporary directory the context states; empty states none.
    QString tempDirectory;

    /// The views the factory made, in order, and what it was asked for each.
    std::vector<ViewHandle> views;
    std::vector<ViewRequest> requests;

    /// The listeners subscribed now, in subscription order.
    std::vector<AfterNightAppListener*> listeners;

    /// Every dialog, toast and settings request, as "kind:title".
    QStringList messages;

    /// The containers temporary sequence paths were asked for, in order.
    mutable std::vector<an::SequenceFormat> sequenceFormatsAsked;

    /// How many application events were posted.
    int eventsPosted = 0;

    /// The process notifications, in order.
    std::vector<ProcessNotification> opened;
    std::vector<ProcessNotification> executed;

private:
    /// A view initialized in a mode under its document's name, kept by the context.
    ImageViewControl* makeView(const an::ui::ImageDocument& document, ViewControlMode mode)
    {
        views.emplace_back(new ImageViewControl());
        ImageViewControl* view = views.back().get();
        view->initialize(document.names().displayName.toStdString(), mode);
        view->resize(320, 240);
        return view;
    }

    an::consumer::EngineSession m_session;
    std::shared_ptr<EmptyIcons> m_icons;
    EventReceiver m_receiver;
    QHash<QString, QString> m_settings;
    QHash<QString, QString> m_processSettings;
    QHash<QString, QString> m_paths;
    ImageViewControl* m_active = nullptr;
};

/// An application context over a session of a given width; fails the case when the session cannot be opened.
[[nodiscard]] inline std::unique_ptr<TestAppContext> openContext(std::uint32_t width = 2u)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return nullptr;
    }
    return std::make_unique<TestAppContext>(std::move(*session));
}

/// Installs an application context for one case and clears it when the case ends, passing or failing, once the views
/// its factory made are gone.
class InstalledContext
{
public:
    explicit InstalledContext(TestAppContext& context) : m_context(context)
    {
        an::ui::setAppContext(&context);
    }
    ~InstalledContext()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        m_context.views.clear();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        an::ui::clearAppContext();
    }
    InstalledContext(const InstalledContext&) = delete;
    InstalledContext& operator=(const InstalledContext&) = delete;

private:
    TestAppContext& m_context;
};

/// The description of a single-channel Float32 image.
[[nodiscard]] inline std::optional<an::ImageDescriptor> monoFloatDescriptor(std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                  .sampleType = an::SampleType::Float32,
                                                                  .channelSchema = an::ChannelSchema::Mono,
                                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                                  .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                  .cfa = an::CfaDescriptor{},
                                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                  .pixelAspectRatio = 1.0});
}

/// A document around a new single-channel Float32 asset of the session, filled with one value; fails the case when
/// it cannot be built.
[[nodiscard]] inline std::optional<an::ui::ImageDocument> filledDocument(const an::consumer::EngineSession& session,
                                                                         float value, an::ui::DocumentNames names = {})
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    const std::optional<an::ImageDescriptor> descriptor = monoFloatDescriptor(8u, 4u);
    EXPECT_TRUE(descriptor.has_value());
    if (!hold.hasValue() || !descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, value);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset), std::move(names));
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// Turns the GUI thread's event loop until a condition holds or the patience runs out.
[[nodiscard]] inline bool pumpUntil(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    return condition();
}

} // namespace an::tests::services
