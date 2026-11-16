// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageProcessControl.h"
#include "ImageSelectorTooltipBuilder.h"
#include "ImageViewControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ViewChrome.h"
#include "app/AppContextHelpers.h"
#include "app/ProcessRegistry.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QEvent>
#include <QTimer>
#include <QVariant>

#include <vector>

namespace
{

/// The role a channel selector item carries its scope in.
constexpr int kChannelScopeRole = Qt::UserRole;

/// Runs a process's per-plane body as the engine's channel scope operation.
///
/// A run handed a destination that starts unwritten is one whose body declared it writes every sample, over a source
/// every sample of which is valid: its per-plane body writes every sample of the plane it is handed, and the scope
/// starts the plane as nothing.
class ProcessIterationOperation final : public an::ChannelScopeOperation
{
public:
    using Iteration = std::function<an::Status(const an::ChannelScopeInput&, const an::ChannelScopeOutput&)>;

    ProcessIterationOperation(Iteration iteration, bool writesEverySample)
        : m_iteration(std::move(iteration)), m_writesEverySample(writesEverySample)
    {
    }

    [[nodiscard]] an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                                 const an::ChannelScopeOutput& output) override
    {
        return m_iteration(input, output);
    }

    [[nodiscard]] bool writesEveryOutputSample() const noexcept override
    {
        return m_writesEverySample;
    }

private:
    Iteration m_iteration;
    bool m_writesEverySample = false;
};

/// Keeps a mirror page's view fitted to the canvas - when it is first shown, when it shows an image of another size and
/// whenever the canvas is resized - until the person zooms it.
class MirrorFit final : public QObject, public ImageViewControlListener
{
public:
    explicit MirrorFit(ImageViewControl& mirror)
        : QObject(&mirror), ImageViewControlListener(&mirror), m_mirror(&mirror)
    {
        if (mirror.getScrollArea() != nullptr && mirror.getScrollArea()->viewport() != nullptr)
        {
            mirror.getScrollArea()->viewport()->installEventFilter(this);
        }
    }

    /// The image shown changed: fitted again when it is another size and the person has not zoomed.
    void shown()
    {
        const ImageViewWidget* widget = m_mirror->getViewWidget();
        const QSize size = widget != nullptr ? widget->imageSize() : QSize();
        if (size != m_size)
        {
            m_size = size;
            queueFit();
        }
    }

    void onImageViewScaled(ImageViewControl* /*ctrl*/) override
    {
        if (!m_fitting)
        {
            m_fitted = false;
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (m_fitted && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        {
            queueFit();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    /// Fits once the layout the event belongs to has settled.
    void queueFit()
    {
        QTimer::singleShot(0, this, [this]() { fitNow(); });
    }

    void fitNow()
    {
        if (!m_fitted || !m_mirror->hasImage() || !m_mirror->isVisible())
        {
            return;
        }
        m_fitting = true;
        m_mirror->fitToWindow();
        m_fitting = false;
    }

    ImageViewControl* m_mirror;
    QSize m_size;
    bool m_fitted = true;
    bool m_fitting = false;
};

/// The button box a process window lays its buttons in: of the boxes it holds, the one nearest the window that holds a
/// standard button a page's footer shows.
[[nodiscard]] QDialogButtonBox* processButtonBox(const QWidget& process)
{
    QDialogButtonBox* nearest = nullptr;
    int nearestDepth = 0;
    for (QDialogButtonBox* box : process.findChildren<QDialogButtonBox*>())
    {
        const bool holdsOne =
            box->button(QDialogButtonBox::Apply) != nullptr || box->button(QDialogButtonBox::Ok) != nullptr ||
            box->button(QDialogButtonBox::Close) != nullptr || box->button(QDialogButtonBox::Cancel) != nullptr;
        if (!holdsOne)
        {
            continue;
        }
        int depth = 0;
        for (const QWidget* at = box; at != nullptr && at != &process; at = at->parentWidget())
        {
            ++depth;
        }
        if (nearest == nullptr || depth < nearestDepth)
        {
            nearest = box;
            nearestDepth = depth;
        }
    }
    return nearest;
}

} // namespace

struct ImageProcessControl::Impl
{
    explicit Impl(const std::string& processName) : m_ProcessName(processName) {}

    std::string m_ProcessName;

    std::atomic_bool m_ExecutionInProgress = false;
    std::atomic_bool m_ProcessCloseRequested = false;

    bool m_SupportTargetViewControlSelection = false;
    std::set<std::string> m_ListOfSupportedViewControls;
    QComboBox* m_UiListOfSupportedViewControls = nullptr;
    bool m_IgnoreLastUiListTextChangedEvent = false;

    bool m_SupportPerChannelProcessing = false;
    QComboBox* m_UiListOfSupportedChannels = nullptr;
    bool m_IgnoreLastUiListOfSupportedChannelsEvent = false;

    QPointer<ProgressDialogControl> m_CurrentProgressDialog;

    /// Runs started and not yet published; read and written on the GUI thread only.
    int m_RunsInFlight = 0;

    /// Whether a close was asked for while a run was on its way, and is made when the last one lands.
    bool m_CloseDeferred = false;

    /// What waits for the runs on their way to land, in the order it was asked; GUI thread only.
    std::vector<std::function<void()>> m_RunIdleContinuations;

    /// Whether the target view let the process go because it is closing.
    bool m_DetachedFromView = false;

    /// A mirror page's view of the target, and what keeps it fitted; null while the process is a window.
    QPointer<ImageViewControl> m_Mirror;
    MirrorFit* m_MirrorFit = nullptr;

    /// The stop of the run that took one last (`beginRunStop`), and whether one did.
    std::stop_source m_RunStop;
    bool m_RunStopTaken = false;

    /// The images the process still needs, by their names in its controls.
    QStringList m_MissingInputs;
};

ImageProcessControl::ImageProcessControl(ImageViewControl* control, const std::string& name)
    : TopLevelProcessBaseControl(name), ImageViewControlListener(control),
      AfterNightAppListener(process_utils::currentAppEventBus()), m_impl(std::make_unique<Impl>(name)),
      m_ProcessName(m_impl->m_ProcessName), m_ExecutionInProgress(m_impl->m_ExecutionInProgress),
      m_ProcessCloseRequested(m_impl->m_ProcessCloseRequested),
      m_SupportTargetViewControlSelection(m_impl->m_SupportTargetViewControlSelection),
      m_ListOfSupportedViewControls(m_impl->m_ListOfSupportedViewControls),
      m_UiListOfSupportedViewControls(m_impl->m_UiListOfSupportedViewControls),
      m_IgnoreLastUiListTextChangedEvent(m_impl->m_IgnoreLastUiListTextChangedEvent),
      m_SupportPerChannelProcessing(m_impl->m_SupportPerChannelProcessing),
      m_UiListOfSupportedChannels(m_impl->m_UiListOfSupportedChannels),
      m_IgnoreLastUiListOfSupportedChannelsEvent(m_impl->m_IgnoreLastUiListOfSupportedChannelsEvent)
{
    if (control)
    {
        control->attachProcess(*this);
    }
    notifyTargetChanged(control);
}

ImageViewControl* ImageProcessControl::processTarget() const
{
    return m_image_view_control;
}

ImageViewControl* ImageProcessControl::interactionView() const
{
    if (m_image_view_control == nullptr)
    {
        return nullptr;
    }
    return m_impl->m_Mirror ? m_impl->m_Mirror.data() : m_image_view_control;
}

std::unique_ptr<an::ui::ProcessPageLayout> ImageProcessControl::makePageLayout()
{
    if (processSurface() == an::ui::ProcessSurface::FullPage)
    {
        return TopLevelProcessBaseControl::makePageLayout();
    }
    return makeMirrorPageLayout(false);
}

std::unique_ptr<an::ui::ProcessPageLayout> ImageProcessControl::makeMirrorPageLayout(const bool controlsFillTheBody)
{
    if (processSurface() != an::ui::ProcessSurface::MirrorPage || layout() == nullptr)
    {
        return nullptr;
    }
    const an::ui::ProcessStandardParts parts{.mainLayout = layout(),
                                             .previewLayout = nullptr,
                                             .previewCheckbox = nullptr,
                                             .buttonBox = processButtonBox(*this),
                                             .targetSelector = m_UiListOfSupportedViewControls,
                                             .channelSelector = m_UiListOfSupportedChannels};
    an::ui::ProcessPageLayout::Description description;
    description.title = QString::fromUtf8(getProcessName());
    description.description = property("anProcessDescription").toString();
    description.columnWidth = processColumnWidth();
    description.settingsKey = QStringLiteral("%1#page").arg(description.title);
    description.hasPreview = false;
    description.controlsFillTheBody = controlsFillTheBody;
    return std::make_unique<an::ui::ProcessPageLayout>(*this, parts, std::move(description));
}

std::stop_token ImageProcessControl::beginRunStop()
{
    m_impl->m_RunStop = std::stop_source();
    m_impl->m_RunStopTaken = true;
    return m_impl->m_RunStop.get_token();
}

bool ImageProcessControl::cancelRun()
{
    if (!runState().running() || !m_impl->m_RunStopTaken)
    {
        return false;
    }
    return m_impl->m_RunStop.request_stop();
}

const QStringList& ImageProcessControl::missingInputs() const
{
    return m_impl->m_MissingInputs;
}

void ImageProcessControl::setMissingInputs(QStringList inputs)
{
    if (inputs == m_impl->m_MissingInputs)
    {
        return;
    }
    m_impl->m_MissingInputs = std::move(inputs);
    if (an::ui::ProcessPageLayout* const page = pageLayout())
    {
        page->setMissingInputs(m_impl->m_MissingInputs);
    }
}

void ImageProcessControl::pageLayoutMade()
{
    an::ui::ProcessPageLayout* const page = pageLayout();
    if (page != nullptr)
    {
        page->setMissingInputs(m_impl->m_MissingInputs);
    }
    if (page == nullptr || processSurface() != an::ui::ProcessSurface::MirrorPage || m_impl->m_Mirror)
    {
        return;
    }
    // The canvas shows the target in a view of its own, whose toolbar floats over the canvas, outside the view.
    auto* mirror = new ImageViewControl(page->canvas());
    mirror->setObjectName(QStringLiteral("pageMirrorView"));
    mirror->setChromeMode(an::ui::ViewChromeMode::External);
    mirror->initialize(getWindowTitle(), ViewControlMode::st_preview);
    mirror->setUniqueName(getWindowTitle());
    m_impl->m_Mirror = mirror;
    m_impl->m_MirrorFit = new MirrorFit(*mirror);
    page->setCanvasView(mirror);
    showTargetInMirror(true);
    interactionViewChanged(m_image_view_control, mirror);
}

void ImageProcessControl::showTargetInMirror(const bool asTheTargetShowsIt)
{
    ImageViewControl* const mirror = m_impl->m_Mirror.data();
    if (mirror == nullptr)
    {
        return;
    }
    ImageViewControl* const target = m_image_view_control;
    std::shared_ptr<const an::consumer::ImageAsset> asset =
        target != nullptr && target->hasImage() ? target->document().asset() : nullptr;
    if (asset == nullptr)
    {
        if (mirror->hasImage())
        {
            mirror->clear();
        }
        return;
    }
    if (!mirror->hasImage() || mirror->document().asset() != asset)
    {
        // The target's own image, shared: never copied, and never written through the mirror's document.
        an::Result<an::ui::ImageDocument> shared = an::ui::ImageDocument::share(
            std::move(asset), an::ui::DocumentNames{.displayName = QString::fromUtf8(target->getUniqueName())});
        if (!shared.hasValue())
        {
            LOG_WARNING("'{}': the page cannot show its target: {}", m_ProcessName, shared.status().message());
            mirror->clear();
            return;
        }
        mirror->setDocument(std::move(*shared));
    }
    if (asTheTargetShowsIt)
    {
        if (ImageViewWidget* const widget = mirror->getViewWidget())
        {
            widget->setAutostretchSettings(target->getAutostretchSettings(), false);
        }
        mirror->setViewMode(target->isAutoStretchEnabled() ? ImageViewMode::AUTOSTRETCH_MODE
                                                           : ImageViewMode::LINEAR_MODE);
    }
    if (m_impl->m_MirrorFit != nullptr)
    {
        m_impl->m_MirrorFit->shown();
    }
}

void ImageProcessControl::changeTargetViewControl(ImageViewControl* control)
{
    if (!control)
        return;

    if (m_image_view_control)
    {
        m_image_view_control->detachProcess(*this);
    }

    ImageViewControl* old = m_image_view_control;

    setViewControl(control);

    if (m_image_view_control)
    {
        m_image_view_control->attachProcess(*this);
    }

    refreshListOfSupportedChannels();

    updateWindowTitle();

    // A mirror page shows the new target before the process reads it, so what the process draws lies on its image.
    showTargetInMirror(true);

    notifyTargetChanged(m_image_view_control);

    onViewControlChanged(old, m_image_view_control);
}

void ImageProcessControl::onImageRenamed(ImageViewControl* ctrl)
{
    updateWindowTitle();
}

void ImageProcessControl::onImageLoaded(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        showTargetInMirror(false);
    }
    if (m_SupportPerChannelProcessing && ctrl == m_image_view_control)
    {
        refreshListOfSupportedChannels();
    }
}

void ImageProcessControl::onImageUpdated(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        // A mirror page follows each edit of its target: it shows the image the target holds now.
        showTargetInMirror(false);

        if (m_SupportTargetViewControlSelection && !isViewControlSupported(m_image_view_control))
        {
            auto stdtitle = fmt::format("{}", getProcessName());
            auto stdmsg = fmt::format("The target image '{}' has changed and isn't supported by this process any more. "
                                      "The process will be closed.",
                                      ctrl->getUniqueName());

            if (auto* dialogService = process_utils::currentDialogService())
            {
                dialogService->notifyWarnMessage(this, QString::fromUtf8(stdtitle), QString::fromUtf8(stdmsg));
            }
            close();
        }

        if (m_SupportPerChannelProcessing)
        {
            refreshListOfSupportedChannels();
        }
    }
}

void ImageProcessControl::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (m_SupportTargetViewControlSelection && !isViewControlSupported(newCtrl))
    {
        auto stdtitle = fmt::format("{}", getProcessName());
        auto stdmsg = fmt::format("The target image '{}' has changed and isn't supported by this process any more. The "
                                  "process will be closed.",
                                  newCtrl->getUniqueName());
        if (auto* dialogService = process_utils::currentDialogService())
        {
            dialogService->notifyWarnMessage(this, QString::fromUtf8(stdtitle), QString::fromUtf8(stdmsg));
        }
        close();
    }

    if (m_SupportPerChannelProcessing)
    {
        refreshListOfSupportedChannels();
    }
}

void ImageProcessControl::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        // A window the person closed already - out of sight while its last run lands - says nothing: its box would
        // run an event loop over a window nobody sees, in which that run lands, completes the close and deletes the
        // window beneath the box it owns.
        if (!m_impl->m_DetachedFromView && !m_ProcessCloseRequested)
        {
            auto stdtitle = fmt::format("{}", getProcessName());
            auto stdmsg = fmt::format("The target image '{}' has been closed. The process will be closed as well.",
                                      ctrl->getUniqueName());
            if (auto* dialogService = process_utils::currentDialogService())
            {
                dialogService->notifyWarnMessage(this, QString::fromUtf8(stdtitle), QString::fromUtf8(stdmsg));
            }
        }

        m_image_view_control = nullptr;
        showTargetInMirror(false);
        notifyTargetChanged(nullptr);
        close();
    }
}

void ImageProcessControl::onViewControlCreated(const ImageViewControl* ctrl)
{
    if (m_SupportTargetViewControlSelection && m_UiListOfSupportedViewControls)
    {
        if (isViewControlSupported(ctrl))
        {
            m_ListOfSupportedViewControls.insert(ctrl->getUniqueName());
            m_UiListOfSupportedViewControls->addItem(QString::fromUtf8(ctrl->getUniqueName()));
            int idx = m_UiListOfSupportedViewControls->count() - 1;
            m_UiListOfSupportedViewControls->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
            listOfSupportedViewControlsUpdated(ctrl, vc_update_op::added);
        }
    }
}

void ImageProcessControl::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    if (m_SupportTargetViewControlSelection && m_UiListOfSupportedViewControls)
    {
        if (ctrl == m_image_view_control)
            return;

        auto it = m_ListOfSupportedViewControls.find(ctrl->getUniqueName());
        if (it != m_ListOfSupportedViewControls.end())
        {
            m_ListOfSupportedViewControls.erase(it);

            m_UiListOfSupportedViewControls->removeItem(
                m_UiListOfSupportedViewControls->findText(QString::fromUtf8(ctrl->getUniqueName())));
            listOfSupportedViewControlsUpdated(ctrl, vc_update_op::removed);
        }
    }
}

void ImageProcessControl::onViewControlUpdated(const ImageViewControl* ctrl)
{
    if (m_SupportTargetViewControlSelection && m_UiListOfSupportedViewControls)
    {
        auto it = m_ListOfSupportedViewControls.find(ctrl->getUniqueName());

        if (isViewControlSupported(ctrl))
        {
            if (it == m_ListOfSupportedViewControls.end())
            {
                m_ListOfSupportedViewControls.insert(ctrl->getUniqueName());
                m_UiListOfSupportedViewControls->addItem(QString::fromUtf8(ctrl->getUniqueName()));
                int idx = m_UiListOfSupportedViewControls->count() - 1;
                m_UiListOfSupportedViewControls->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl),
                                                             Qt::ToolTipRole);
                listOfSupportedViewControlsUpdated(ctrl, vc_update_op::added);
            }
        }
        else
        {
            if (it != m_ListOfSupportedViewControls.end())
            {
                m_ListOfSupportedViewControls.erase(it);
                m_UiListOfSupportedViewControls->removeItem(
                    m_UiListOfSupportedViewControls->findText(QString::fromUtf8(ctrl->getUniqueName())));
                listOfSupportedViewControlsUpdated(ctrl, vc_update_op::removed);
            }
        }
    }
}

void ImageProcessControl::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                               const ImageViewControl* ctrl)
{
    if (m_SupportTargetViewControlSelection && m_UiListOfSupportedViewControls)
    {
        auto it = m_ListOfSupportedViewControls.find(oldName);
        if (it != m_ListOfSupportedViewControls.end())
        {
            m_IgnoreLastUiListTextChangedEvent = true;
            m_ListOfSupportedViewControls.erase(it);
            m_ListOfSupportedViewControls.insert(newName);
            m_UiListOfSupportedViewControls->removeItem(
                m_UiListOfSupportedViewControls->findText(QString::fromUtf8(oldName)));
            m_UiListOfSupportedViewControls->addItem(QString::fromUtf8(newName));
            {
                int idx = m_UiListOfSupportedViewControls->count() - 1;
                m_UiListOfSupportedViewControls->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl),
                                                             Qt::ToolTipRole);
            }
            listOfSupportedViewControlsUpdated(ctrl, vc_update_op::renamed);

            if (ctrl == m_image_view_control)
            {
                m_UiListOfSupportedViewControls->setCurrentText(QString::fromUtf8(newName));
            }

            m_IgnoreLastUiListTextChangedEvent = false;
        }
    }
}

bool ImageProcessControl::isRTPreviewProcess() const
{
    return false;
}

std::string ImageProcessControl::attachmentName() const
{
    return m_ProcessName;
}

bool ImageProcessControl::requiresExclusiveAccess() const
{
    // Whether a process needs its view to itself is its registered descriptor's to say, the fact the launcher refuses
    // to open it beside another process by; a process no descriptor names shares its view.
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        if (!m_ProcessName.empty() && descriptor.process_name == m_ProcessName)
        {
            return descriptor.requires_exclusive_access;
        }
    }
    return false;
}

bool ImageProcessControl::showsRealtimePreview() const
{
    return isRTPreviewProcess();
}

bool ImageProcessControl::requiresExclusivePreviewAccess() const
{
    return false;
}

void ImageProcessControl::disableRealtimePreview() {}

void ImageProcessControl::detachAndClose()
{
    // The view has already let its attachments go; the warning the view's closing would bring is not needed either.
    m_impl->m_DetachedFromView = true;
    close();
}

std::string ImageProcessControl::getProcessName() const
{
    return m_ProcessName;
}

an::ChannelScope ImageProcessControl::selectedChannelScope() const
{
    if (!m_UiListOfSupportedChannels || m_UiListOfSupportedChannels->count() == 0)
    {
        return an::ChannelScope::Whole;
    }
    const QVariant carried = m_UiListOfSupportedChannels->currentData(kChannelScopeRole);
    if (carried.isValid())
    {
        const auto scope = static_cast<an::ChannelScope>(carried.toInt());
        return an::isChannelScope(scope) ? scope : an::ChannelScope::Whole;
    }
    return an::ui::processChannelScopeForLabel(m_UiListOfSupportedChannels->currentText())
        .value_or(an::ChannelScope::Whole);
}

bool ImageProcessControl::isRunIdle() const noexcept
{
    return m_impl->m_RunsInFlight == 0;
}

ImageProcessControl::~ImageProcessControl()
{
    if (m_image_view_control && !m_impl->m_DetachedFromView)
    {
        m_image_view_control->detachProcess(*this);
    }
}

void ImageProcessControl::on_targetImageSelector_currentTextChanged(const QString& text)
{
    if (m_IgnoreLastUiListTextChangedEvent || !m_image_view_control)
        return;

    auto textStd = text.toUtf8().toStdString();

    if (textStd != m_image_view_control->getUniqueName())
    {
        auto* viewControlManager = process_utils::currentViewControlManager();
        auto* vc = viewControlManager ? viewControlManager->getViewControlByName(textStd) : nullptr;
        if (vc && vc != m_image_view_control)
        {
            changeTargetViewControl(vc);
        }
    }

    on_targetImageSelector_currentTextChangedInternal(text);
}

void ImageProcessControl::on_channelSelector_currentTextChanged(const QString& text)
{
    if (m_IgnoreLastUiListOfSupportedChannelsEvent)
        return;

    on_channelSelector_currentTextChangedInternal(text);

    updateNeeded();
}

void ImageProcessControl::onStopWorkInFlight()
{
    m_ProcessCloseRequested = true;
    cancelCurrentProgressDialog();
}

void ImageProcessControl::whenRunIdle(std::function<void()> continuation)
{
    if (!continuation)
    {
        return;
    }
    if (m_impl->m_RunsInFlight == 0 && m_impl->m_RunIdleContinuations.empty())
    {
        continuation();
        return;
    }
    m_impl->m_RunIdleContinuations.push_back(std::move(continuation));
}

void ImageProcessControl::closeEvent(QCloseEvent* event)
{
    m_ProcessCloseRequested = true;
    cancelCurrentProgressDialog();
    if (m_impl->m_RunsInFlight > 0)
    {
        // Nothing waits on the GUI thread: the window goes out of sight and closes when its last run has landed.
        m_impl->m_CloseDeferred = true;
        setEnabled(false);
        hide();
        event->ignore();
        return;
    }

    TopLevelProcessBaseControl::closeEvent(event);
    if (event->isAccepted() && m_impl->m_Mirror && m_impl->m_Mirror->hasImage())
    {
        // A closed task holds nothing of its target: its page's view lets the target's image go.
        m_impl->m_Mirror->clear();
    }
}

std::string ImageProcessControl::getWindowTitle() const
{
    return fmt::format("{} <{}>", getProcessName(),
                       m_image_view_control ? m_image_view_control->getUniqueName() : std::string());
}

void ImageProcessControl::updateWindowTitle()
{
    setWindowTitle(QString::fromUtf8(getWindowTitle()));
}

void ImageProcessControl::notifyErrorMessage(const QString& title, const QString& text)
{
    showProcessMessage(title, text, ProcessMessageTone::Error);
    // A page says it in its banner and opens no dialog over the main window.
    if (hasPageLayout())
    {
        return;
    }
    if (auto* dialogService = process_utils::currentDialogService())
    {
        dialogService->notifyErrorMessage(nullptr, title, text);
    }
}

void ImageProcessControl::notifyErrorMessageAsync(const QString& title, const QString& text)
{
    QMetaObject::invokeMethod(this, [this, title, text]() { notifyErrorMessage(title, text); }, Qt::QueuedConnection);
}

void ImageProcessControl::notifyWarnMessage(const QString& title, const QString& text)
{
    showProcessMessage(title, text, ProcessMessageTone::Warning);
    // A page says it in its banner and opens no dialog over the main window.
    if (hasPageLayout())
    {
        return;
    }
    if (auto* dialogService = process_utils::currentDialogService())
    {
        dialogService->notifyWarnMessage(nullptr, title, text);
    }
}

void ImageProcessControl::notifyWarnMessageAsync(const QString& title, const QString& text)
{
    QMetaObject::invokeMethod(this, [this, title, text]() { notifyWarnMessage(title, text); }, Qt::QueuedConnection);
}

void ImageProcessControl::notifyInfoMessage(const QString& title, const QString& text)
{
    showProcessMessage(title, text, ProcessMessageTone::Info);
    // A page says it in its banner and opens no dialog over the main window.
    if (hasPageLayout())
    {
        return;
    }
    if (auto* dialogService = process_utils::currentDialogService())
    {
        dialogService->notifyInfoMessage(nullptr, title, text);
    }
}

void ImageProcessControl::notifyInfoMessageAsync(const QString& title, const QString& text)
{
    QMetaObject::invokeMethod(this, [this, title, text]() { notifyInfoMessage(title, text); }, Qt::QueuedConnection);
}

void ImageProcessControl::notifyCloseEventAsync()
{
    QMetaObject::invokeMethod(
        this,
        [this]() {
            clearProcessMessage();
            close();
        },
        Qt::QueuedConnection);
}

void ImageProcessControl::notifyCustomProcessEvent(const std::string& msg)
{
    onCustomProcessEventNotified(msg);
}

void ImageProcessControl::notifyCustomProcessEventAsync(const std::string& msg)
{
    QMetaObject::invokeMethod(this, [this, msg]() { onCustomProcessEventNotified(msg); }, Qt::QueuedConnection);
}

void ImageProcessControl::notifyCustomProcessEventID(const int id)
{
    onCustomProcessEventIDNotified(id);
}

void ImageProcessControl::notifyCustomProcessEventIDAsync(const int id)
{
    QMetaObject::invokeMethod(this, [this, id]() { onCustomProcessEventIDNotified(id); }, Qt::QueuedConnection);
}

void ImageProcessControl::notifyCustomProcessCodeEvent(const std::function<void(ImageProcessControl* instance)>& func)
{
    if (func)
    {
        func(this);
    }
}

void ImageProcessControl::notifyCustomProcessCodeEventAsync(
    const std::function<void(ImageProcessControl* instance)>& func)
{
    QMetaObject::invokeMethod(this, [this, func]() { notifyCustomProcessCodeEvent(func); }, Qt::QueuedConnection);
}

an::Status ImageProcessControl::executeProcessIteration(const an::ChannelScopeInput&, const an::ChannelScopeOutput&,
                                                        const an::ui::ProcessRun&, const ProcessExecutionParams*,
                                                        an::ui::ExecutionScope&)
{
    return an::Status::success();
}

an::Status ImageProcessControl::executeProcessUsingChannels(const an::ui::ProcessSource& source,
                                                            an::ui::ProcessDestination& destination,
                                                            const an::ui::ProcessRun& run,
                                                            const ProcessExecutionParams* params,
                                                            an::ui::ExecutionScope& scope)
{
    if (!scope.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the run holds no admitted execution");
    }
    // The channel is the run's, snapshotted on the GUI thread when it was issued: no widget is read here.
    ProcessIterationOperation operation(
        [this, &run, params, &scope](const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output) {
            return executeProcessIteration(input, output, run, params, scope);
        },
        !destination.holdsSource());
    const an::Result<an::ChannelScopeResult> scoped =
        an::ui::applyProcessChannelScope(scope.execution(), run.channel, source, destination, operation);
    return scoped.hasValue() ? an::Status::success() : scoped.status();
}

void ImageProcessControl::resetSettings() {}

void ImageProcessControl::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
}

void ImageProcessControl::resetSettingsAsync()
{
    QMetaObject::invokeMethod(this, [this]() { resetSettings(); }, Qt::QueuedConnection);
}

void ImageProcessControl::refreshListOfSupportedViewControls()
{
    m_ListOfSupportedViewControls.clear();
    auto viewControls = process_utils::listViewControls();

    for (auto& vc : viewControls)
    {
        if (isViewControlSupported(vc))
        {
            m_ListOfSupportedViewControls.insert(vc->getUniqueName());
        }
    }
}

void ImageProcessControl::refreshListOfSupportedChannels()
{
    if (!m_UiListOfSupportedChannels || !m_image_view_control || !m_image_view_control->hasImage())
        return;

    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    if (!described.has_value())
    {
        return;
    }

    m_IgnoreLastUiListOfSupportedChannelsEvent = true;

    m_UiListOfSupportedChannels->clear();

    const auto iconManager = process_utils::currentIconProvider();
    const bool colour =
        described->channelSchema() == an::ChannelSchema::Rgb || described->channelSchema() == an::ChannelSchema::Rgba;
    if (!colour)
    {
        m_UiListOfSupportedChannels->addItem(QString::fromLatin1(an::ui::kProcessGreyscaleChannelLabel),
                                             QVariant(static_cast<int>(an::ChannelScope::Whole)));
        m_UiListOfSupportedChannels->setEnabled(false);
    }
    else
    {
        m_UiListOfSupportedChannels->setEnabled(true);

        // One table names every choice and the scope it runs on; the label is only what the selector shows.
        for (const an::ui::ProcessChannelChoice& choice : an::ui::processChannelChoices())
        {
            if (choice.separatorBefore)
            {
                m_UiListOfSupportedChannels->insertSeparator(m_UiListOfSupportedChannels->count());
            }
            const QString label = QString::fromLatin1(choice.label);
            const QVariant scope(static_cast<int>(choice.scope));
            if (iconManager)
            {
                m_UiListOfSupportedChannels->addItem(iconManager->getIcon(QString::fromLatin1(choice.icon)), label,
                                                     scope);
            }
            else
            {
                m_UiListOfSupportedChannels->addItem(label, scope);
            }
        }

        m_UiListOfSupportedChannels->setCurrentIndex(0);
    }

    m_IgnoreLastUiListOfSupportedChannelsEvent = false;
}

void ImageProcessControl::initializeTargetViewControlComboBox(QComboBox* qtList)
{
    m_UiListOfSupportedViewControls = qtList;
    if (m_UiListOfSupportedViewControls)
    {
        m_UiListOfSupportedViewControls->clear();

        QObject::connect(m_UiListOfSupportedViewControls, &QComboBox::currentTextChanged, this,
                         &ImageProcessControl::on_targetImageSelector_currentTextChanged);
        refreshListOfSupportedViewControls();

        m_IgnoreLastUiListTextChangedEvent = true;

        auto viewControls = process_utils::listViewControls();
        for (auto& vcName : m_ListOfSupportedViewControls)
        {
            m_UiListOfSupportedViewControls->addItem(QString::fromUtf8(vcName));
            int idx = m_UiListOfSupportedViewControls->count() - 1;
            // Find the matching view control to build a rich tooltip
            for (auto* vc : viewControls)
            {
                if (vc && vc->getUniqueName() == vcName)
                {
                    m_UiListOfSupportedViewControls->setItemData(idx, an::ui::buildImageSelectorTooltip(vc),
                                                                 Qt::ToolTipRole);
                    break;
                }
            }
        }

        if (m_image_view_control)
        {
            m_UiListOfSupportedViewControls->setCurrentText(QString::fromUtf8(m_image_view_control->getUniqueName()));
        }

        m_IgnoreLastUiListTextChangedEvent = false;
    }
}

void ImageProcessControl::initializeListOfSupportedChannelslComboBox(QComboBox* qtList)
{
    m_UiListOfSupportedChannels = qtList;
    if (m_UiListOfSupportedChannels)
    {
        m_UiListOfSupportedChannels->clear();

        QObject::connect(m_UiListOfSupportedChannels, &QComboBox::currentTextChanged, this,
                         &ImageProcessControl::on_channelSelector_currentTextChanged);

        refreshListOfSupportedChannels();
    }
}

void ImageProcessControl::enableProcessControls(bool enabled)
{
    if (m_SupportTargetViewControlSelection && m_UiListOfSupportedViewControls)
    {
        m_UiListOfSupportedViewControls->setEnabled(enabled);
    }

    if (m_SupportPerChannelProcessing && m_UiListOfSupportedChannels)
    {
        if (m_UiListOfSupportedChannels->count() > 1)
        {
            m_UiListOfSupportedChannels->setEnabled(enabled);
        }
        else
        {
            m_UiListOfSupportedChannels->setEnabled(false);
        }
    }

    enableProcessControlsImpl(enabled);
}

void ImageProcessControl::registerNewProgressDialog(ProgressDialogControl* dialog)
{
    m_impl->m_CurrentProgressDialog = dialog;
}

void ImageProcessControl::unregisterCurrentProgressDialog()
{
    m_impl->m_CurrentProgressDialog.clear();
}

void ImageProcessControl::cancelCurrentProgressDialog()
{
    if (m_impl->m_CurrentProgressDialog)
    {
        m_impl->m_CurrentProgressDialog->requestCancel();
        m_impl->m_CurrentProgressDialog.clear();
    }
}

ProgressDialogControl* ImageProcessControl::currentProgressDialog() const
{
    return m_impl->m_CurrentProgressDialog.data();
}

const an::consumer::EngineSession* ImageProcessControl::engineSession() const
{
    an::ui::IEngineSessionProvider* provider = process_utils::currentEngineSessionProvider();
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

std::shared_ptr<void> ImageProcessControl::beginRunInFlight()
{
    ++m_impl->m_RunsInFlight;
    QPointer<ImageProcessControl> guard(this);
    // The last copy may go on a worker; the run's end is told to the GUI thread, after whatever that copy's holder
    // queued before it.
    return std::shared_ptr<void>(new int(0), [guard](void* token) {
        delete static_cast<int*>(token);
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard]() {
                if (guard)
                {
                    guard->endRun();
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageProcessControl::logWorkThatThrew(const std::string& processName, const char* what,
                                           const bool recovered) noexcept
{
    try
    {
        LOG_WARNING("'{}': work on a worker threw and its run {}: {}", processName,
                    recovered ? "ends with a failure" : "was dropped", what);
    }
    catch (...)
    {
        // Nothing is left to tell when the log itself cannot be written.
    }
}

void ImageProcessControl::endRun()
{
    if (m_impl->m_RunsInFlight > 0)
    {
        --m_impl->m_RunsInFlight;
    }
    // Each continuation runs with no run on its way; one that starts a run leaves the rest for that run's landing.
    while (m_impl->m_RunsInFlight == 0 && !m_impl->m_RunIdleContinuations.empty())
    {
        std::function<void()> continuation = std::move(m_impl->m_RunIdleContinuations.front());
        m_impl->m_RunIdleContinuations.erase(m_impl->m_RunIdleContinuations.begin());
        continuation();
    }
    if (m_impl->m_RunsInFlight == 0 && m_impl->m_CloseDeferred)
    {
        m_impl->m_CloseDeferred = false;
        close();
    }
}

namespace an::ui
{
ExecutionObserver withRunProgress(ExecutionObserver observer, TopLevelProcessBaseControl* process)
{
    if (process == nullptr)
    {
        return observer;
    }
    if (observer.receiver.isNull())
    {
        observer.receiver = process;
    }
    QPointer<TopLevelProcessBaseControl> guard(process);
    observer.progress = [shown = std::move(observer.progress), guard](const ExecutionProgress& report) {
        if (shown)
        {
            shown(report);
        }
        if (guard && report.total > 0u)
        {
            guard->reportRunProgress(static_cast<double>(report.completed) / static_cast<double>(report.total));
        }
    };
    return observer;
}
} // namespace an::ui
