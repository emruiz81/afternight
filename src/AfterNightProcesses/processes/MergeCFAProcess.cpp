// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MergeCFAProcess.h"
#include "app/FileCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ProcessCompanions.h"
#include "controls/ProgressDialogControl.h"
#include "processes/CfaProcessSupport.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/ProcessCommonUtils.h"

#include <QCoreApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QThreadPool>

#include <fmt/format.h>

#include <stop_token>

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

/// The arrangements the dialog offers, in its order.
const std::array<std::pair<QString, an::CfaPattern>, 4u> kPatterns{{{QStringLiteral("RGGB"), an::CfaPattern::Rggb},
                                                                    {QStringLiteral("BGGR"), an::CfaPattern::Bggr},
                                                                    {QStringLiteral("GBRG"), an::CfaPattern::Gbrg},
                                                                    {QStringLiteral("GRBG"), an::CfaPattern::Grbg}}};

} // namespace

/// A join on its way: whether one is, what the joins came to, and the stop the window and the dialog share.
struct MergeCFAProcess::Run
{
    /// Whether the window's own run - four sequences frame by frame - is on its way.
    bool running = false;
    std::uint64_t published = 0u;
    std::uint64_t failed = 0u;
    std::stop_source stop;

    /// Whether the framework's Apply ending now published its view.
    bool publishedNow = false;
};

MergeCFAProcess::MergeCFAProcess(QWidget* parent)
    : an::ui::TypedProcessControl<MergeCfaParams>(nullptr, getProcessNameStatic()), m_run(std::make_unique<Run>())
{
    Q_UNUSED(parent);

    m_ui = an::ui::processes::buildMergeCFAProcessUi(this);
    setProcessDescription(QStringLiteral("Merge four monochrome CFA quadrants into a single Bayer-pattern image."));

    m_SupportTargetViewControlSelection = false;
    m_SupportPerChannelProcessing = false;
    m_ShowWaitingSpinner = true;
    m_SupportMasking = false;
    // The result opens beside the first input.
    m_CreatedViewControlsAreNeighbour = true;
    m_ImagePreviewLayout = m_ui.previewLayout;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    // The mosaic is twice the size of a plane: no part of it is an area of the first plane, so there is no preview
    // area.
    initializeStandardProcess(
        m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox, nullptr, nullptr,
        {.supportPerChannel = false, .showWaitingSpinner = true, .previewAreaSelectionEnabled = false});
    // Ok joins four sequences by a run of the window's own, so the box is answered here and not by the framework.
    QObject::disconnect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, nullptr);
    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &MergeCFAProcess::on_dialog_box_button_clicked);
    for (QComboBox* selector : selectors())
    {
        QObject::connect(selector, &QComboBox::currentTextChanged, this, [this](const QString&) {
            validateSelectedImages();
            settingsUpdated();
        });
    }

    setWindowTitle(QString::fromUtf8(getProcessNameStatic()));

    for (const auto& [text, pattern] : kPatterns)
    {
        m_ui.patternSelector->addItem(text);
    }
    m_ui.patternSelector->setCurrentText(kPatterns.front().first);
    QObject::connect(m_ui.patternSelector, &QComboBox::currentTextChanged, this,
                     [this](const QString&) { settingsUpdated(); });

    setExecutionAutostretchSupported(true);
    refreshTheme();
    initialize();
}

MergeCFAProcess::~MergeCFAProcess()
{
    m_run->stop.request_stop();
}

std::string MergeCFAProcess::getProcessNameStatic()
{
    return "Merge CFA";
}

std::array<QComboBox*, 4u> MergeCFAProcess::selectors() const
{
    return {m_ui.cfa0Selector, m_ui.cfa1Selector, m_ui.cfa2Selector, m_ui.cfa3Selector};
}

bool MergeCFAProcess::isRunning() const
{
    return m_run->running || m_ExecutionInProgress;
}

std::uint64_t MergeCFAProcess::runsPublished() const noexcept
{
    return m_run->published;
}

std::uint64_t MergeCFAProcess::runsFailed() const noexcept
{
    return m_run->failed;
}

void MergeCFAProcess::requestStop()
{
    m_run->stop.request_stop();
    (void)cancelRun();
}

bool MergeCFAProcess::cancelRun()
{
    // A join of sequences runs on its own stop, which a page's Cancel reaches here.
    if (m_run->running)
    {
        m_run->stop.request_stop();
        return true;
    }
    return an::ui::TypedProcessControl<MergeCfaParams>::cancelRun();
}

void MergeCFAProcess::onApplicationClose()
{
    close();
}

an::CfaPattern MergeCFAProcess::chosenPattern() const
{
    const QString patternText = m_ui.patternSelector->currentText();
    for (const auto& [text, stated] : kPatterns)
    {
        if (text == patternText)
        {
            return stated;
        }
    }
    return kPatterns.front().second;
}

bool MergeCFAProcess::isPlaneViewSupported(const ImageViewControl* ctrl) const
{
    // Only grey images are planes of a mosaic.
    if (ctrl == nullptr || !ctrl->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = ctrl->imageDescriptor();
    return described.has_value() && described->extent().channelCount() == 1u;
}

void MergeCFAProcess::addViewToSelectors(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : selectors())
    {
        if (selector->findText(imageName) == -1)
        {
            selector->addItem(imageName);
            selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
}

void MergeCFAProcess::removeViewFromSelectors(const QString& name)
{
    for (QComboBox* selector : selectors())
    {
        const int idx = selector->findText(name);
        if (idx != -1)
        {
            if (selector->currentText() == name)
            {
                selector->setCurrentText(NO_IMAGE_STR);
            }
            selector->removeItem(idx);
        }
    }
}

void MergeCFAProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    if (isPlaneViewSupported(ctrl))
    {
        addViewToSelectors(ctrl);
    }
}

void MergeCFAProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    removeViewFromSelectors(QString::fromUtf8(ctrl->getUniqueName()));
    validateSelectedImages();
}

void MergeCFAProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                           const ImageViewControl* ctrl)
{
    const QString newImageName = QString::fromUtf8(newName);
    const QString oldImageName = QString::fromUtf8(oldName);
    for (QComboBox* selector : selectors())
    {
        const int idx = selector->findText(oldImageName);
        if (idx != -1)
        {
            selector->setItemText(idx, newImageName);
            selector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
    validateSelectedImages();
}

void MergeCFAProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    if (isPlaneViewSupported(ctrl))
    {
        addViewToSelectors(ctrl);
    }
    else
    {
        removeViewFromSelectors(QString::fromUtf8(ctrl->getUniqueName()));
    }
    validateSelectedImages();
    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : selectors())
    {
        if (selector->currentText() == name)
        {
            settingsUpdated();
            break;
        }
    }
}

void MergeCFAProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void MergeCFAProcess::initialize()
{
    for (QComboBox* selector : selectors())
    {
        const QSignalBlocker blocker(selector);
        selector->addItem(NO_IMAGE_STR);
    }
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (isPlaneViewSupported(viewControl))
        {
            addViewToSelectors(viewControl);
        }
    }
    for (QComboBox* selector : selectors())
    {
        selector->setCurrentText(NO_IMAGE_STR);
    }
    validateSelectedImages();
    adjustSize();
    resize(m_DefaultSizeNoPreview);
}

void MergeCFAProcess::validateSelectedImages()
{
    // The first plane is the window's target: the preview joins over it, and the mosaic opens beside it.
    const std::array<QLabel*, 4u> labels{m_ui.label, m_ui.channel0Label, m_ui.channel1Label, m_ui.channel2Label};
    QStringList missing;
    std::size_t index = 0u;
    for (QComboBox* selector : selectors())
    {
        const QString text = selector->currentText();
        if (text == NO_IMAGE_STR || text.isEmpty() || process_utils::findViewControlByName(text) == nullptr)
        {
            missing.append(labels[index]->text());
        }
        ++index;
    }
    const QString first = m_ui.cfa0Selector->currentText();
    ImageViewControl* const firstView =
        first == NO_IMAGE_STR || first.isEmpty() ? nullptr : process_utils::findViewControlByName(first);
    if (firstView != nullptr && firstView != m_image_view_control)
    {
        changeTargetViewControl(firstView);
    }
    const bool ready =
        missing.isEmpty() && firstView != nullptr && firstView == m_image_view_control && firstView->hasImage();
    setMissingInputs(missing);
    m_ui.preview->setEnabled(ready);
    if (ready)
    {
        if (isPreviewEnabled())
        {
            createPreviewControl();
        }
    }
    else if (isPreviewEnabled())
    {
        destroyPreviewControl();
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!isRunning() && ready);
}

bool MergeCFAProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void MergeCFAProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void MergeCFAProcess::settingsUpdatedImpl() {}

void MergeCFAProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                                  std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = true;
    executeInBackground = true;
    newImageName = "CFA MERGED";
}

MergeCfaParams MergeCFAProcess::getParams() const
{
    MergeCfaParams params;
    std::size_t index = 0u;
    for (QComboBox* selector : selectors())
    {
        ImageViewControl* view = process_utils::findViewControlByName(selector->currentText());
        if (view != nullptr)
        {
            params.names[index] = view->getUniqueName();
            if (index > 0u && view->hasImage())
            {
                params.others[index - 1u] = view->document().asset();
            }
        }
        ++index;
    }
    params.pattern = chosenPattern();
    params.patternText = m_ui.patternSelector->currentText();
    params.admitted = admissionHook();
    return params;
}

an::Status MergeCFAProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                    an::ui::ProcessDestination& destination,
                                                    const an::ui::ProcessRun& run, const MergeCfaParams& params,
                                                    an::ui::ExecutionScope& scope)
{
    if (params.admitted && !run.isForPreview())
    {
        params.admitted(scope);
    }
    if (scope.stopRequested())
    {
        return refusal(an::ErrorCode::Cancelled, "the join was stopped");
    }
    const std::optional<an::ImageDescriptor> firstDescribed = source.descriptor();
    if (!firstDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the first plane holds nothing");
    }
    std::array<std::optional<process_utils::LeasedImage>, 3u> others;
    std::array<std::optional<an::ui::ProcessSource>, 3u> sources;
    std::array<const an::ui::ProcessSource*, 4u> planes{&source, nullptr, nullptr, nullptr};
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        if (!params.others[index])
        {
            return refusal(an::ErrorCode::InvalidArgument, "Missing images to assemble the CFA");
        }
        an::Result<process_utils::LeasedImage> leased =
            process_utils::LeasedImage::read(scope, *params.others[index], run, QRect(), *firstDescribed);
        if (!leased.hasValue())
        {
            return leased.status();
        }
        others[index].emplace(std::move(*leased));
        sources[index].emplace(others[index]->source());
        const std::optional<an::ImageDescriptor> described = sources[index]->descriptor();
        if (!described.has_value() || described->extent().width() != firstDescribed->extent().width() ||
            described->extent().height() != firstDescribed->extent().height())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "All the images must have the same size");
        }
        planes[index + 1u] = &*sources[index];
    }
    an::Result<an::consumer::ImageAsset> joined = an::ui::processes::joinMosaicPlanes(scope, planes, params.pattern);
    if (!joined.hasValue())
    {
        return joined.status();
    }
    return destination.replace(std::move(*joined));
}

an::ui::PreviewScaling MergeCFAProcess::previewScaling() const
{
    // A plane's pixel lands where its place in the two-by-two block puts it, in a mosaic twice the size: every frame
    // runs over the whole of every plane.
    return an::ui::PreviewScaling::FullOnly;
}

an::ui::ProcessStep MergeCFAProcess::describeTypedProcessStep(const MergeCfaParams& params) const
{
    return an::ui::ProcessStep{
        .name = historyStepName(),
        .description = fmt::format("MergeCFA: created '{}' pattern from image channels CFA0:'{}' CFA1:'{}' CFA2:'{} "
                                   "CFA3:'{}'",
                                   params.patternText.toStdString(), params.names[0], params.names[1], params.names[2],
                                   params.names[3])};
}

// The second, third and fourth planes: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
MergeCFAProcess::typedImagesRead(const MergeCfaParams& params) const
{
    return {params.others[0], params.others[1], params.others[2]};
}

void MergeCFAProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool /*sequenceMode*/)
{
    if (previewMode == RTPreviewMode::NoPreview)
    {
        ++m_run->published;
        m_run->publishedNow = true;
    }
}

void MergeCFAProcess::executeProcessEndedImpl(const RTPreviewMode previewMode, const bool /*sequenceMode*/)
{
    if (previewMode == RTPreviewMode::NoPreview)
    {
        if (!m_run->publishedNow)
        {
            ++m_run->failed;
        }
        m_run->publishedNow = false;
    }
    validateSelectedImages();
}

void MergeCFAProcess::finishRun(const an::Status& outcome, bool published)
{
    m_run->running = false;
    setRunState(an::ui::endedRunState(outcome, published));
    if (published)
    {
        ++m_run->published;
    }
    else
    {
        ++m_run->failed;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
    validateSelectedImages();
    if (outcome.failed())
    {
        if (outcome.code() != an::ErrorCode::Cancelled)
        {
            const std::string text =
                fmt::format("An error occurred when executing the Merge CFA process:\n{}", outcome.message());
            LOG_WARNING("{}", text);
            notifyErrorMessage(QStringLiteral("Error running Merge CFA"), QString::fromUtf8(text));
        }
        return;
    }
    if (published)
    {
        close();
    }
}

void MergeCFAProcess::executeCFAComposition()
{
    if (isRunning())
    {
        return;
    }
    // What the run reads is chosen here: the planes' assets, or their sequences, and the names it records.
    struct Plane
    {
        std::string name;
        std::shared_ptr<const an::consumer::ImageAsset> asset;
        std::optional<an::ui::DocumentSequence> sequence;
        std::optional<an::ImageDescriptor> descriptor;
        ImageViewControl* view = nullptr;
    };
    const auto refuse = [this](const std::string& why) {
        finishRun(refusal(an::ErrorCode::InvalidArgument, why), false);
    };
    std::array<Plane, 4u> planes;
    std::size_t index = 0u;
    for (QComboBox* selector : selectors())
    {
        ImageViewControl* view = process_utils::findViewControlByName(selector->currentText());
        if (view == nullptr || !view->hasImage())
        {
            refuse("Missing images to assemble the CFA");
            return;
        }
        planes[index] = Plane{.name = view->getUniqueName(),
                              .asset = view->document().asset(),
                              .sequence = view->document().sequence(),
                              .descriptor = view->imageDescriptor(),
                              .view = view};
        ++index;
    }
    const bool sequences = planes[0].sequence.has_value();
    for (const Plane& plane : planes)
    {
        if (plane.sequence.has_value() != sequences)
        {
            refuse("All the selected images must be either sequences or images");
            return;
        }
        if (sequences && plane.sequence->descriptor.frameCount != planes[0].sequence->descriptor.frameCount)
        {
            refuse("All the selected sequences must have the same number of frames");
            return;
        }
        if (!plane.descriptor.has_value() ||
            plane.descriptor->extent().width() != planes[0].descriptor->extent().width() ||
            plane.descriptor->extent().height() != planes[0].descriptor->extent().height())
        {
            refuse(sequences ? "All the sequences must have the same size" : "All the images must have the same size");
            return;
        }
    }
    if (!sequences)
    {
        // Four images: the framework's run, which the preview showed.
        m_IgnoreLastImageUpdatedEvent = true;
        applyProcess();
        return;
    }
    joinSequences();
}

void MergeCFAProcess::joinSequences()
{
    struct Plane
    {
        std::string name;
        std::optional<an::ui::DocumentSequence> sequence;
        ImageViewControl* view = nullptr;
    };
    std::array<Plane, 4u> planes;
    std::size_t index = 0u;
    for (QComboBox* selector : selectors())
    {
        ImageViewControl* view = process_utils::findViewControlByName(selector->currentText());
        if (view == nullptr)
        {
            return;
        }
        planes[index] = Plane{.name = view->getUniqueName(), .sequence = view->document().sequence(), .view = view};
        ++index;
    }
    const QString patternText = m_ui.patternSelector->currentText();
    const an::CfaPattern pattern = chosenPattern();
    const an::consumer::EngineSession* session = an::ui::processes::lentEngineSession();
    if (session == nullptr)
    {
        finishRun(refusal(an::ErrorCode::NotInitialized, "no engine session is lent"), false);
        return;
    }

    m_run->running = true;
    m_run->stop = std::stop_source();
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    setRunState(an::ui::sequenceRunningState());
    // A page shows the run in its own row, Overall and Processing: no progress window opens.
    ProgressDialogControl* progress = nullptr;
    if (!hasPageLayout())
    {
        progress = new ProgressDialogControl(process_utils::currentMainWindow(), "Merge CFA");
        progress->setText("Executing Merge CFA");
        progress->setProgressBarRange(0, 100);
        progress->show();
    }
    const std::string description =
        fmt::format("MergeCFA: created '{}' pattern from sequence channels CFA0:'{}' CFA1:'{}' CFA2:'{} CFA3:'{}'",
                    patternText.toStdString(), planes[0].name, planes[1].name, planes[2].name, planes[3].name);
    QPointer<MergeCFAProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = m_run->stop.get_token();
    const std::stop_token dialogStop = progress != nullptr ? progress->stopToken() : std::stop_token();
    ImageViewControl* neighbour = planes[0].view;
    const std::string tempPath = process_utils::createTempSequenceFilePath(planes[0].sequence->descriptor.format);
    const an::ui::processes::AdmissionHook hook = admissionHook();
    LOG_INFO("Executing process 'Merge CFA' under {}... ", patternText.toStdString());

    const std::stop_token processStop = windowStopToken();
    QThreadPool::globalInstance()->start([session, planes, pattern, guard, dialog, windowStop, dialogStop, processStop,
                                          neighbour, tempPath, hook, description]() {
        // One stop for the run's Stop, the window's going and the dialog's Cancel.
        std::stop_source stop;
        const std::stop_callback windowCallback(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback processCallback(processStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback dialogCallback(dialogStop, [&stop]() { stop.request_stop(); });

        std::optional<an::ui::DocumentSequence> written;
        an::Status outcome = an::Status::success();
        an::Result<an::ui::ExecutionScope> admitted = an::ui::ExecutionScope::admit(
            *session,
            an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop.get_token()},
            an::ui::ExecutionObserver{});
        if (!admitted.hasValue())
        {
            outcome = admitted.status();
        }
        else
        {
            an::ui::ExecutionScope& scope = *admitted;
            if (hook)
            {
                hook(scope);
            }
            // Four sequences frame by frame, written into a new sequence beside the first plane's, published only
            // when every frame is written.
            std::array<std::unique_ptr<an::ui::IProcessFrameReader>, 4u> readers;
            for (std::size_t index = 0u; index < 4u && outcome.ok(); ++index)
            {
                an::ui::DocumentSequence every = *planes[index].sequence;
                every.selectedFrames.reset();
                an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
                    an::ui::openProcessFrames(*session, every, false);
                if (!reader.hasValue())
                {
                    outcome = reader.status();
                    break;
                }
                readers[index] = std::move(*reader);
            }
            std::unique_ptr<an::ui::IProcessFrameWriter> writer;
            if (outcome.ok())
            {
                an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> made =
                    an::ui::createProcessFrameOutput(*session, *planes[0].sequence, QString::fromStdString(tempPath));
                if (!made.hasValue())
                {
                    outcome = made.status();
                }
                else
                {
                    writer = std::move(*made);
                }
            }
            const std::uint64_t total = outcome.ok() ? readers[0]->frameCount() : 0u;
            for (std::uint64_t frame = 0u; outcome.ok(); ++frame)
            {
                if (stop.stop_requested())
                {
                    outcome = refusal(an::ErrorCode::Cancelled, "the join was stopped");
                    break;
                }
                std::array<std::optional<an::consumer::ImageAsset>, 4u> frames;
                std::array<std::optional<an::consumer::AssetReadLease>, 4u> leases;
                std::array<std::optional<an::ui::ProcessSource>, 4u> sources;
                std::array<const an::ui::ProcessSource*, 4u> read{};
                std::size_t ended = 0u;
                for (std::size_t index = 0u; index < 4u; ++index)
                {
                    an::Result<std::optional<an::ui::ProcessSequenceFrame>> next =
                        readers[index]->next(scope.engine(), scope.execution());
                    if (!next.hasValue())
                    {
                        outcome = next.status();
                        break;
                    }
                    if (!next->has_value())
                    {
                        ++ended;
                        continue;
                    }
                    frames[index].emplace(std::move((*next)->asset));
                    an::Result<an::consumer::AssetReadLease> lease = frames[index]->read(scope.execution().stopToken());
                    if (!lease.hasValue())
                    {
                        outcome = lease.status();
                        break;
                    }
                    leases[index].emplace(std::move(*lease));
                    sources[index].emplace(an::ui::ProcessSource::of(*leases[index]));
                    read[index] = &*sources[index];
                }
                if (outcome.failed() || ended == 4u)
                {
                    break;
                }
                if (ended != 0u)
                {
                    outcome = refusal(an::ErrorCode::InvalidData,
                                      "All the selected sequences must have the same number of frames");
                    break;
                }
                an::Result<an::consumer::ImageAsset> joined = an::ui::processes::joinMosaicPlanes(scope, read, pattern);
                if (!joined.hasValue())
                {
                    outcome = joined.status();
                    break;
                }
                an::Result<an::consumer::AssetReadLease> joinedLease = joined->read(scope.execution().stopToken());
                if (!joinedLease.hasValue())
                {
                    outcome = joinedLease.status();
                    break;
                }
                outcome = writer->write(scope.engine(), scope.execution(), frame, joinedLease->image());
                if (total > 0u)
                {
                    const int percent = static_cast<int>(((frame + 1u) * 100u) / total);
                    postToGui([dialog, guard, percent]() {
                        if (dialog)
                        {
                            dialog->setValue(percent);
                        }
                        if (guard)
                        {
                            guard->reportRunProgress(static_cast<double>(percent) / 100.0);
                        }
                    });
                }
            }
            if (outcome.ok() && writer)
            {
                an::Result<an::ui::DocumentSequence> finished = writer->finish(scope.engine(), scope.execution());
                if (!finished.hasValue())
                {
                    outcome = finished.status();
                }
                else
                {
                    written.emplace(std::move(*finished));
                }
            }
            if (outcome.ok() && stop.stop_requested())
            {
                outcome = refusal(an::ErrorCode::Cancelled, "the join was stopped");
            }
            scope.finish(outcome);
        }
        std::optional<an::ui::ImageDocument> sequenceDocument;
        if (outcome.ok() && written.has_value())
        {
            an::Result<an::ui::ImageDocument> opened = an::ui::openSequenceDocument(
                *session, written->sourcePath, written->descriptor.format, stop.get_token());
            if (!opened.hasValue())
            {
                outcome = opened.status();
            }
            else
            {
                sequenceDocument.emplace(std::move(*opened));
            }
        }
        if (outcome.failed() && written.has_value())
        {
            (void)an::ui::removeSequenceFromDisk(written->sourcePath);
        }
        // A sequence no view comes to show - the window closed, or no view could be made - is removed once it lands.
        const QString writtenPath = outcome.ok() && written.has_value() ? written->sourcePath : QString();
        auto landing = std::make_shared<std::optional<an::ui::ImageDocument>>(std::move(sequenceDocument));
        postToGui([guard, dialog, outcome, landing, neighbour, planes, description, writtenPath]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            const auto removeUnshown = [&landing, &writtenPath]() {
                landing->reset();
                if (!writtenPath.isEmpty())
                {
                    if (an::Status removed = an::ui::removeSequenceFromDisk(writtenPath); removed.failed())
                    {
                        LOG_WARNING("Could not remove the sequence '{}': {}", writtenPath.toStdString(),
                                    std::string(removed.message()));
                    }
                }
            };
            if (!guard)
            {
                removeUnshown();
                return;
            }
            bool published = false;
            if (outcome.ok())
            {
                an::ui::IViewFactory* factory = process_utils::currentViewFactory();
                ImageViewControl* beside =
                    neighbour != nullptr &&
                            process_utils::findViewControlByName(QString::fromStdString(planes[0].name)) == neighbour
                        ? neighbour
                        : nullptr;
                const QString name = QStringLiteral("CFA MERGED");
                if (factory != nullptr && landing->has_value())
                {
                    an::ui::DocumentNames names = (*landing)->names();
                    names.displayName = name;
                    (*landing)->setNames(std::move(names));
                    published = factory->createNewImageViewControlFromSequence(
                                    nullptr, std::move(**landing), nullptr, false, description,
                                    ViewControlMode::standard, beside) != nullptr;
                }
            }
            if (!published)
            {
                removeUnshown();
            }
            guard->finishRun(published
                                 ? an::Status::success()
                                 : (outcome.ok() ? refusal(an::ErrorCode::Internal, "no view could be made") : outcome),
                             published);
        });
    });
}

void MergeCFAProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeCFAComposition();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
