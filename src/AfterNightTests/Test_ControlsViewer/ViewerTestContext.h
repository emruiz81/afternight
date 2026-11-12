// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application context the viewer's cases run views under: it owns the one engine session and lends it, answers
/// the viewer's questions as the case steers, keeps settings and the views it is told of in memory, and writes through
/// the export settings' representation. Views made under it are destroyed, and every worker the viewer started has
/// finished, before the context lets its session go.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "controls/ImageViewFiles.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QCoreApplication>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QStringList>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::viewer
{

/// Icons the viewer asks for by name, all empty.
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

/// An application context over one engine session, implementing what the viewer reaches. A suite that needs more of
/// the context derives from it.
class ViewerTestContext : public an::ui::IAppContext,
                          public an::ui::IDialogService,
                          public an::ui::IGlobalSettingsProvider,
                          public an::ui::IViewRegistry,
                          public an::ui::IViewControlManager,
                          public an::ui::IStatusBarService,
                          public an::ui::IActionStateManager,
                          public an::ui::IOutputIntentProvider,
                          public an::ui::IEngineSessionProvider
{
public:
    explicit ViewerTestContext(an::consumer::EngineSession session)
        : m_session(std::move(session)), m_icons(std::make_shared<StandInIcons>())
    {
    }

    ~ViewerTestContext() override
    {
        // Every worker a view started reads the session; the session goes only after they have all finished.
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
        return nullptr;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return nullptr;
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
        return nullptr;
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
        return nullptr;
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
        return lendsSession ? this : nullptr;
    }

    /// Whether the context lends its session to the views: a case turns it off to stand for a process that lends none.
    bool lendsSession = true;
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return rawOpenPolicyService;
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

    /// Closes the session's admission, so the next work a view asks to be admitted is refused.
    void closeAdmission() noexcept
    {
        m_session.closeAdmission();
    }

    /// What the session counts as outstanding now.
    [[nodiscard]] an::consumer::SessionOutstanding outstanding() const
    {
        return m_session.outstanding();
    }

    /// Shuts the session down once every worker has finished: it succeeds only when nothing a view took from it -
    /// an asset, a lease, an admitted operation - is still held.
    [[nodiscard]] an::Status shutdownSession()
    {
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
        messages.push_back(QStringLiteral("error:") + title);
        errorTexts.push_back(text);
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
    void notifyViewControlUpdated(const ImageViewControl*) override
    {
        ++viewsUpdated;
    }
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
    void updateActiveImageActions() override
    {
        ++activeImageActionRefreshes;
    }
    void updateActiveImageHistory() override {}

    /// How often a view asked for the application's image actions to be read again.
    int activeImageActionRefreshes = 0;

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

    // ---- What the cases read and steer.

    /// How a yes-or-no question is answered.
    bool answerYes = false;

    /// Every dialog and toast, as "kind:title".
    QStringList messages;

    /// The text of every error shown, in order.
    QStringList errorTexts;

    /// How many times a view said it was updated.
    int viewsUpdated = 0;

    /// The RAW policy the views' reads run under; none unless a case hands one over.
    an::ui::IRawOpenPolicyService* rawOpenPolicyService = nullptr;

private:
    an::consumer::EngineSession m_session;
    std::shared_ptr<StandInIcons> m_icons;
    QHash<QString, QString> m_settings;
    std::vector<ImageViewControl*> m_views;
    ImageViewControl* m_active = nullptr;
};

/// A viewer context over a session of a given width that reads and writes FITS files; fails the case when the session
/// cannot be opened.
///
/// @param width the session's concurrency.
/// @param memory the session's consumer memory allowances; the session defaults unless a case states its own.
[[nodiscard]] inline std::unique_ptr<ViewerTestContext> openViewerContext(std::uint32_t width = 2u,
                                                                          an::consumer::MemoryAllowances memory = {})
{
    std::optional<TestSessionProvider> provider = openFileProvider(width, memory);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<ViewerTestContext>(std::move(provider->owned()));
}

/// Installs an application context for one case and, when the case ends, passing or failing, puts back the one
/// installed before it - none in a suite that installs no other.
class InstalledContext
{
public:
    explicit InstalledContext(an::ui::IAppContext& context) : m_previous(an::ui::appContext())
    {
        // setAppContext installs over nothing, so whatever is installed - the suite's own - is taken down first.
        an::ui::clearAppContext();
        an::ui::setAppContext(&context);
    }
    ~InstalledContext()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        an::ui::clearAppContext();
        if (m_previous != nullptr)
        {
            an::ui::setAppContext(m_previous);
        }
    }
    InstalledContext(const InstalledContext&) = delete;
    InstalledContext& operator=(const InstalledContext&) = delete;

private:
    an::ui::IAppContext* m_previous = nullptr;
};

/// A view made and initialized as the application makes one, deleted with the case.
struct ViewDeleter
{
    void operator()(ImageViewControl* view) const
    {
        delete view;
    }
};
using ViewHandle = std::unique_ptr<ImageViewControl, ViewDeleter>;

/// Makes a view in a mode, under the installed context.
[[nodiscard]] inline ViewHandle makeView(const std::string& name, ViewControlMode mode = ViewControlMode::standard)
{
    ViewHandle view(new ImageViewControl());
    view->initialize(name, mode);
    view->resize(640, 480);
    return view;
}

/// Writes an asset to a FITS file in its own representation; fails the case when it cannot.
inline void writeFitsFile(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& asset,
                          const QString& path)
{
    const std::optional<an::ImageDescriptor> described = asset.descriptor();
    ASSERT_TRUE(described.has_value());
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = described->sampleType()});
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    const an::Status written = an::ui::writeViewImage(session, asset, path, *intent, {});
    ASSERT_TRUE(written.ok()) << written.message();
}

/// The sample of an asset at a position and channel, or nothing when it is not of that representation.
template <typename SampleT>
[[nodiscard]] std::optional<SampleT> sampleOf(const an::consumer::ImageAsset& asset, std::uint32_t x, std::uint32_t y,
                                              std::uint32_t channel = 0u)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    const auto* image = std::get_if<an::Image<SampleT>>(&lease->image());
    if (image == nullptr || !image->view().has_value())
    {
        return std::nullopt;
    }
    const SampleT* sample = image->view()->sampleAt(channel, x, y, 0u);
    return sample != nullptr ? std::optional<SampleT>(*sample) : std::nullopt;
}

/// Turns the event loop until the view's reads, edits, writes, detections and presentations have all landed.
[[nodiscard]] inline bool settle(const ImageViewControl& view)
{
    if (!pumpUntil([&view]() { return view.isWorkIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return view.isWorkIdle();
}

/// Keeps, each time the view tells its listeners its image changed, the step its history held at that moment.
class StepShownWith final : public ImageViewControlListener
{
public:
    explicit StepShownWith(ImageViewControl* view) : ImageViewControlListener(view)
    {
        view->subscribeListener(this);
    }
    ~StepShownWith() override
    {
        if (m_image_view_control != nullptr)
        {
            m_image_view_control->unsubscribeListener(this);
        }
    }
    StepShownWith(const StepShownWith&) = delete;
    StepShownWith& operator=(const StepShownWith&) = delete;

    void onImageUpdated(ImageViewControl* view) override
    {
        const an::ui::IViewHistory* history = view->viewHistory();
        steps.push_back(history != nullptr ? history->previousStepName() : std::string());
    }

    /// The step the history named last, for every update, in order.
    std::vector<std::string> steps;
};

/// Undoes the view's last step and turns the event loop until the history names `previous` as the step it would undo
/// next and `next` as the one it would redo: the current step moves only once the restored state is shown.
[[nodiscard]] inline bool undoTo(ImageViewControl& view, const std::string& previous, const std::string& next)
{
    an::ui::IViewHistory* history = view.viewHistory();
    if (history == nullptr || !history->undo())
    {
        return false;
    }
    return pumpUntil([history, &previous, &next]() {
        return history->previousStepName() == previous && history->nextStepName() == next;
    });
}

} // namespace an::tests::viewer
