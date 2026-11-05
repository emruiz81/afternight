// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "HistogramTransformationProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/HistogramComputation.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{

using an::ui::processes::ClippedSamples;
using an::ui::processes::ScreenTransferStatistics;
using an::ui::processes::StretchRecord;

/// The name the processing-state cards record the stretch under.
constexpr const char* kProcessingOperation = "histogram-mtf";

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The history's and the lineage's account of a run's parameters.
[[nodiscard]] std::string describeParameters(const HistogramTransformationParams& params)
{
    return fmt::format("Channel: {}, Shadows: {:.6f}, Midtones: {:.6f}, Highlights: {:.6f}", params.channelLabel,
                       params.shadows, params.midtones, params.highlights);
}

/// The engine's stretch request for a transfer: a screen transfer per channel on the channels it changes.
[[nodiscard]] an::StretchImageRequest stretchRequestOf(const HistogramTransfer& transfer)
{
    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::ScreenTransfer;
    request.stretch.screenTransfer = transfer.channels;
    request.colorMode = an::StretchColorMode::Independent;
    request.channelEnabled = transfer.changed;
    return request;
}

/// What the dialog shows of its preview: the original counts remapped through the transfer, and what it clips.
struct PreviewCounts
{
    an::ui::HistogramDisplayData counts;
    ClippedSamples clipped;
    std::uint64_t samples = 0u;
};

} // namespace

an::Result<HistogramTransfer> histogramTransferOf(const HistogramTransformationParams& params,
                                                  const ScreenTransferStatistics* statistics)
{
    HistogramTransfer transfer;
    if (params.autoStretch == HistogramAutoStretch::None)
    {
        transfer.channels.fill(an::MidtonesTransferParameters{
            .midtones = params.midtones, .shadows = params.shadows, .highlights = params.highlights});
        switch (params.channel)
        {
        case an::ChannelScope::Red:
            transfer.changed = {true, false, false, false};
            break;
        case an::ChannelScope::Green:
            transfer.changed = {false, true, false, false};
            break;
        case an::ChannelScope::Blue:
            transfer.changed = {false, false, true, false};
            break;
        default:
            transfer.changed = {true, true, true, false};
            break;
        }
        return transfer;
    }
    if (statistics == nullptr || statistics->channels.empty())
    {
        return refusal(an::ErrorCode::NotInitialized, "the automatic stretch has no statistics of the image");
    }
    // The colour channels' statistics; an alpha channel's take no part.
    const std::span<const an::StretchChannelStatistics> colour(
        statistics->channels.data(), (std::min)(statistics->channels.size(), std::size_t{3u}));
    if (params.autoStretch == HistogramAutoStretch::Linked)
    {
        const an::Result<an::MidtonesTransferParameters> linked = an::deriveLinkedAutoStretch(
            colour, kHistogramAutoStretchShadowsClipping, kHistogramAutoStretchTargetBackground);
        if (!linked.hasValue())
        {
            return linked.status();
        }
        transfer.channels.fill(*linked);
    }
    else
    {
        std::array<an::MidtonesTransferParameters, 3u> derived{};
        const an::Status unlinked = an::deriveUnlinkedAutoStretch(
            colour, kHistogramAutoStretchShadowsClipping, kHistogramAutoStretchTargetBackground,
            std::span<an::MidtonesTransferParameters>(derived.data(), colour.size()));
        if (unlinked.failed())
        {
            return unlinked;
        }
        for (std::size_t channel = 0u; channel < transfer.channels.size(); ++channel)
        {
            transfer.channels[channel] = derived[(std::min)(channel, colour.size() - 1u)];
        }
    }
    transfer.changed = {true, true, true, false};
    return transfer;
}

HistogramTransformationProcess::HistogramTransformationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<HistogramTransformationParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildHistogramTransformationProcessUi(this);

    setProcessDescription(
        QStringLiteral("Adjust shadows, midtones, and highlights through histogram-based transfer controls."));

    QObject::connect(m_ui.shadowsValue, &QDoubleSpinBox::valueChanged, this,
                     &HistogramTransformationProcess::on_shadowsValueChanged);
    QObject::connect(m_ui.midtonesValue, &QDoubleSpinBox::valueChanged, this,
                     &HistogramTransformationProcess::on_midtonesValueChanged);
    QObject::connect(m_ui.highlightsValue, &QDoubleSpinBox::valueChanged, this,
                     &HistogramTransformationProcess::on_highlightsValueChanged);
    QObject::connect(m_ui.channelSelector2, &QComboBox::currentTextChanged, this,
                     &HistogramTransformationProcess::on_channelSelector2_currentTextChanged);
    QObject::connect(m_ui.plotResolution, &QComboBox::currentTextChanged, this,
                     &HistogramTransformationProcess::on_plotResolution_currentTextChanged);

    QObject::connect(m_ui.preview, &QCheckBox::checkStateChanged, this,
                     &HistogramTransformationProcess::on_PreviewClicked2);
    QObject::connect(m_ui.gridButton, &QPushButton::clicked, this, &HistogramTransformationProcess::on_gridClicked);
    QObject::connect(m_ui.transferCurve, &QPushButton::clicked, this,
                     &HistogramTransformationProcess::on_transferCurveClicked);
    QObject::connect(m_ui.zoomIn, &QPushButton::clicked, this, &HistogramTransformationProcess::on_ZoomInButton);
    QObject::connect(m_ui.zoomOut, &QPushButton::clicked, this, &HistogramTransformationProcess::on_ZoomOutButton);
    QObject::connect(m_ui.zoomReset, &QPushButton::clicked, this, &HistogramTransformationProcess::on_ZoomResetButton);
    QObject::connect(m_ui.localChannelNormalization, &QPushButton::clicked, this,
                     &HistogramTransformationProcess::on_LocalChannelNormalization);
    QObject::connect(m_ui.showHistogramAreas, &QPushButton::clicked, this,
                     &HistogramTransformationProcess::on_showHistogramAreas);
    QObject::connect(m_ui.autostretchLinked, &QPushButton::clicked, this,
                     &HistogramTransformationProcess::on_AutostretchLinkedButton);
    QObject::connect(m_ui.autostretchUnlinked, &QPushButton::clicked, this,
                     &HistogramTransformationProcess::on_AutostretchUnlinkedButton);

    m_original_histogram_widget = std::make_unique<HistogramMTFWidget>(m_ui.originalChartScrollArea);
    m_original_histogram_widget->setRangeSelectorEnabled(true);
    m_original_histogram_widget->setListener(this);
    m_original_histogram_widget->setGuideLineEnabled(true);
    m_ui.originalChartScrollArea->setWidget(m_original_histogram_widget.get());

    m_preview_histogram_widget = std::make_unique<HistogramMTFWidget>(m_ui.previewChartScrollArea);
    m_preview_histogram_widget->setRangeSelectorEnabled(false);
    m_preview_histogram_widget->setTransferCurveEnabled(false);
    m_ui.previewChartScrollArea->setWidget(m_preview_histogram_widget.get());

    updateSupportedPlotResolutions();
    {
        const QSignalBlocker blocker(m_ui.plotResolution);
        m_ui.plotResolution->setCurrentText("8-bit (256)");
    }
    m_original_histogram_widget->setHistogramResolution(an::ui::HistogramResolution::Bits8);
    m_preview_histogram_widget->setHistogramResolution(an::ui::HistogramResolution::Bits8);

    updateSupportedImageChannels();

    m_ui.channelSelector2->setCurrentText("RGB");

    updateWindowTitle();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_ui.zoomIn->setIcon(iconProvider->getIcon("zoom_in"));
        m_ui.zoomOut->setIcon(iconProvider->getIcon("zoom_out"));
        m_ui.zoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
        m_ui.gridButton->setIcon(iconProvider->getIcon("grid"));
        m_ui.transferCurve->setIcon(iconProvider->getIcon("stat_line"));
        m_ui.localChannelNormalization->setIcon(iconProvider->getIcon("scale"));
        m_ui.showHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));
    }

    m_FixedLayout = false;
    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, nullptr, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    setupPreviewControl(false);

    refreshTheme();

    recountOriginal();
}

void HistogramTransformationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string HistogramTransformationProcess::getProcessNameStatic()
{
    return "Histogram Transformation";
}

bool HistogramTransformationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

bool HistogramTransformationProcess::isColourTarget() const
{
    if (m_image_view_control == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor = m_image_view_control->document().descriptor();
    return descriptor.has_value() && descriptor->extent().channelCount() == 3u;
}

void HistogramTransformationProcess::targetChanged()
{
    const bool colourSelector = m_ui.channelSelector2->count() > 1;
    if (colourSelector != isColourTarget())
    {
        updateSupportedImageChannels();
    }
    recountOriginal();
}

void HistogramTransformationProcess::onImageLoaded(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageLoaded(ctrl);
    if (ctrl == m_image_view_control)
    {
        targetChanged();
    }
}

void HistogramTransformationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    if (ctrl == m_image_view_control)
    {
        targetChanged();
    }
}

void HistogramTransformationProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
        return;
    }
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    targetChanged();
}

void HistogramTransformationProcess::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    // Both the original and the preview histograms are drawn; the alternate image changes neither.
}

void HistogramTransformationProcess::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    // Both the original and the preview histograms are drawn; the alternate image changes neither.
}

void HistogramTransformationProcess::onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode)
{
    if (isPreviewEnabled())
    {
        ImageCompositionMode cMode = (ImageCompositionMode)mode;
        if (cMode == ImageCompositionMode::r_mode)
        {
            m_original_histogram_widget->setDisplayChannels(true, false, false);
            m_preview_histogram_widget->setDisplayChannels(true, false, false);
        }
        else if (cMode == ImageCompositionMode::g_mode)
        {
            m_original_histogram_widget->setDisplayChannels(false, true, false);
            m_preview_histogram_widget->setDisplayChannels(false, true, false);
        }
        else if (cMode == ImageCompositionMode::b_mode)
        {
            m_original_histogram_widget->setDisplayChannels(false, false, true);
            m_preview_histogram_widget->setDisplayChannels(false, false, true);
        }
        else
        {
            m_original_histogram_widget->setDisplayChannels(true, true, true);
            m_preview_histogram_widget->setDisplayChannels(true, true, true);
        }
    }
}

void HistogramTransformationProcess::onShadowsChanged(double shadows)
{
    clearAutoStretchState();
    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (m_ui.shadowsValue->value() != shadows)
        m_ui.shadowsValue->setValue(shadows);

    m_IgnoreLastValueChangeForRangeSelectors = false;

    recalculateHistogramPreview();
}

void HistogramTransformationProcess::onMidtonesChanged(double midtones)
{
    clearAutoStretchState();
    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (m_ui.midtonesValue->value() != midtones)
        m_ui.midtonesValue->setValue(midtones);

    m_IgnoreLastValueChangeForRangeSelectors = false;

    recalculateHistogramPreview();
}

void HistogramTransformationProcess::onHightlightsChanged(double highlights)
{
    clearAutoStretchState();
    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (m_ui.highlightsValue->value() != highlights)
        m_ui.highlightsValue->setValue(highlights);

    m_IgnoreLastValueChangeForRangeSelectors = false;

    recalculateHistogramPreview();
}

void HistogramTransformationProcess::onSettingsUpdated()
{
    settingsUpdated();
}

void HistogramTransformationProcess::recountOriginal()
{
    m_ui.localChannelNormalization->setEnabled(isColourTarget());
    if (m_CountRunning)
    {
        m_CountPending = true;
        return;
    }
    const an::consumer::EngineSession* session = engineSession();
    std::shared_ptr<const an::consumer::ImageAsset> target =
        m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr;
    if (session == nullptr || !target)
    {
        m_OriginalCounts.reset();
        m_original_histogram_widget->setHistogram(nullptr);
        recalculateHistogramPreview();
        return;
    }
    const an::ui::HistogramResolution resolution = m_original_histogram_widget->getHistogramResolution();
    m_CountRunning = true;
    runThenPublish(
        [session, target, resolution]() -> an::Result<an::ui::HistogramDisplayData> {
            an::Result<an::ui::processes::DialogWork> work = an::ui::processes::admitDialogWork(*session, target.get());
            if (!work.hasValue())
            {
                return work.status();
            }
            return an::ui::computeHistogramDisplayData(work->scope.execution(), *work->lease, resolution);
        },
        [this](an::Result<an::ui::HistogramDisplayData> counted) {
            m_CountRunning = false;
            if (m_CountPending)
            {
                m_CountPending = false;
                recountOriginal();
                return;
            }
            if (counted.hasValue())
            {
                m_OriginalCounts = std::make_shared<const an::ui::HistogramDisplayData>(std::move(*counted));
            }
            else
            {
                m_OriginalCounts.reset();
            }
            m_original_histogram_widget->setHistogram(m_OriginalCounts);
            recalculateHistogramPreview();
        });
}

void HistogramTransformationProcess::recalculateHistogramPreview()
{
    if (m_RemapRunning)
    {
        m_RemapPending = true;
        return;
    }
    const an::consumer::EngineSession* session = engineSession();
    std::shared_ptr<const an::ui::HistogramDisplayData> counts = m_OriginalCounts;
    if (session == nullptr || !counts)
    {
        m_preview_histogram_widget->setHistogram(nullptr);
        showClippedInfo(ClippedSamples{}, 0u);
        return;
    }
    const HistogramTransformationParams params = getParams();
    m_RemapRunning = true;
    runThenPublish(
        [session, counts, params]() -> an::Result<PreviewCounts> {
            // An automatic stretch is the whole target's, measured once per revision and kept for the runs.
            const bool automatic =
                params.autoStretch != HistogramAutoStretch::None && params.target && params.statistics;
            std::shared_ptr<const ScreenTransferStatistics> statistics =
                automatic ? params.statistics->find(params.target->revision()) : nullptr;
            an::Result<an::ui::processes::DialogWork> work =
                an::ui::processes::admitDialogWork(*session, automatic && !statistics ? params.target.get() : nullptr);
            if (!work.hasValue())
            {
                return work.status();
            }
            const an::consumer::OperationScope* scope = &work->scope;
            if (automatic && !statistics)
            {
                const an::consumer::AssetReadLease& lease = *work->lease;
                an::Result<ScreenTransferStatistics> measured = an::ui::processes::measureScreenTransferStatistics(
                    scope->engine(), scope->execution(), lease.image(), lease.validity());
                if (!measured.hasValue())
                {
                    return measured.status();
                }
                statistics = std::make_shared<const ScreenTransferStatistics>(std::move(*measured));
                params.statistics->store(lease.revision(), statistics);
            }
            const an::Result<HistogramTransfer> transfer = histogramTransferOf(params, statistics.get());
            if (!transfer.hasValue())
            {
                return transfer.status();
            }
            std::array<an::GeneralizedStretchParameters, 3u> stretches{};
            for (std::size_t channel = 0u; channel < stretches.size(); ++channel)
            {
                stretches[channel].function = an::StretchFunction::ScreenTransfer;
                stretches[channel].screenTransfer.fill(transfer->channels[channel]);
            }
            an::Result<an::ui::HistogramDisplayData> remapped = an::ui::processes::remapHistogramThroughStretch(
                scope->execution(), *counts, stretches, transfer->changed);
            if (!remapped.hasValue())
            {
                return remapped.status();
            }
            PreviewCounts preview;
            preview.counts = std::move(*remapped);
            preview.clipped =
                an::ui::processes::clippedByScreenTransfer(*counts, transfer->channels, transfer->changed);
            for (const an::ui::HistogramDisplayChannel& channel : counts->channels)
            {
                preview.samples += channel.samples;
            }
            return preview;
        },
        [this](an::Result<PreviewCounts> preview) {
            m_RemapRunning = false;
            if (m_RemapPending)
            {
                m_RemapPending = false;
                recalculateHistogramPreview();
                return;
            }
            if (!preview.hasValue())
            {
                m_preview_histogram_widget->setHistogram(nullptr);
                showClippedInfo(ClippedSamples{}, 0u);
                return;
            }
            m_preview_histogram_widget->setHistogram(
                std::make_shared<const an::ui::HistogramDisplayData>(std::move(preview->counts)));
            showClippedInfo(preview->clipped, preview->samples);
        });
}

HistogramTransformationProcess::~HistogramTransformationProcess() = default;

void HistogramTransformationProcess::on_channelSelector2_currentTextChanged(const QString& str)
{
    clearAutoStretchState();
    recalculateHistogramPreview();
    settingsUpdated();
}

void HistogramTransformationProcess::on_plotResolution_currentTextChanged(const QString& str)
{
    const std::optional<an::ui::HistogramResolution> resolution = an::ui::processes::histogramResolutionForLabel(str);
    if (!resolution.has_value())
    {
        return;
    }
    m_preview_histogram_widget->setHistogramResolution(*resolution);
    m_original_histogram_widget->setHistogramResolution(*resolution);
    recountOriginal();
}

void HistogramTransformationProcess::on_shadowsValueChanged(double value)
{
    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        clearAutoStretchState();
        m_original_histogram_widget->setShadowsValue(value);

        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void HistogramTransformationProcess::on_midtonesValueChanged(double value)
{
    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        clearAutoStretchState();
        m_original_histogram_widget->setMidtonesValue(value);

        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void HistogramTransformationProcess::on_highlightsValueChanged(double value)
{
    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        clearAutoStretchState();
        m_original_histogram_widget->setHighlightsValue(value);

        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void HistogramTransformationProcess::on_PreviewClicked2(int state)
{
    if (state == Qt::Checked)
    {
        createPreviewControl();
        m_ui.autostretchLinked->setEnabled(true);
        m_ui.autostretchUnlinked->setEnabled(isColourTarget());
    }
    else
    {
        destroyPreviewControl();
        m_ui.autostretchLinked->setEnabled(false);
        m_ui.autostretchUnlinked->setEnabled(false);
    }

    recalculateHistogramPreview();
}

void HistogramTransformationProcess::on_gridClicked(bool checked)
{
    m_original_histogram_widget->setGridEnabled(checked);
    m_preview_histogram_widget->setGridEnabled(checked);
}

void HistogramTransformationProcess::on_transferCurveClicked(bool checked)
{
    m_original_histogram_widget->setTransferCurveEnabled(checked);
}

void HistogramTransformationProcess::on_ZoomInButton()
{
    m_original_histogram_widget->zoomIn();
    m_preview_histogram_widget->zoomIn();
}

void HistogramTransformationProcess::on_ZoomOutButton()
{
    m_original_histogram_widget->zoomOut();
    m_preview_histogram_widget->zoomOut();
}

void HistogramTransformationProcess::on_ZoomResetButton()
{
    m_original_histogram_widget->resetZoom();
    m_preview_histogram_widget->resetZoom();
}

void HistogramTransformationProcess::on_LocalChannelNormalization(bool checked)
{
    m_original_histogram_widget->setLocalChannelNormalization(checked);
    m_preview_histogram_widget->setLocalChannelNormalization(checked);
}

void HistogramTransformationProcess::on_showHistogramAreas(bool checked)
{
    m_original_histogram_widget->setAreaModeEnabled(checked);
    m_preview_histogram_widget->setAreaModeEnabled(checked);
}

void HistogramTransformationProcess::on_AutostretchLinkedButton()
{
    resetSettings();
    m_AutoStretch = HistogramAutoStretch::Linked;
    recalculateHistogramPreview();
    settingsUpdated();
}

void HistogramTransformationProcess::on_AutostretchUnlinkedButton()
{
    resetSettings();
    m_AutoStretch = HistogramAutoStretch::Unlinked;
    recalculateHistogramPreview();
    settingsUpdated();
}

void HistogramTransformationProcess::updateSupportedImageChannels()
{
    const QSignalBlocker blocker(m_ui.channelSelector2);
    m_ui.channelSelector2->clear();

    // Each entry carries the scope it names, which is what a run reads; its text is the dialog's own.
    if (!isColourTarget())
    {
        m_ui.channelSelector2->addItem(QString::fromLatin1(an::ui::kProcessGreyscaleChannelLabel),
                                       static_cast<int>(an::ChannelScope::Whole));
        m_ui.channelSelector2->setEnabled(false);
    }
    else
    {
        m_ui.channelSelector2->setEnabled(true);
        const auto iconProvider = process_utils::currentIconProvider();
        for (const an::ui::ProcessChannelChoice& choice : an::ui::processChannelChoices())
        {
            if (choice.scope != an::ChannelScope::Whole && choice.scope != an::ChannelScope::Red &&
                choice.scope != an::ChannelScope::Green && choice.scope != an::ChannelScope::Blue)
            {
                continue;
            }
            const QString label = QString::fromLatin1(choice.label);
            if (iconProvider)
            {
                m_ui.channelSelector2->addItem(iconProvider->getIcon(QString::fromLatin1(choice.icon)), label,
                                               static_cast<int>(choice.scope));
            }
            else
            {
                m_ui.channelSelector2->addItem(label, static_cast<int>(choice.scope));
            }
        }
    }

    m_ui.autostretchUnlinked->setEnabled(isColourTarget() && m_ui.preview->isChecked());
}

void HistogramTransformationProcess::updateSupportedPlotResolutions()
{
    m_ui.plotResolution->clear();
    for (const an::ui::processes::HistogramResolutionChoice& choice : an::ui::processes::histogramResolutionChoices())
    {
        m_ui.plotResolution->addItem(QString::fromLatin1(choice.label));
    }
}

void HistogramTransformationProcess::clearAutoStretchState() noexcept
{
    m_AutoStretch = HistogramAutoStretch::None;
}

an::ui::ProcessBodyCoverage
HistogramTransformationProcess::typedProcessBodyCoverage(const HistogramTransformationParams& params) const
{
    // The stretched working copy, made from the source, replaces the destination.
    return an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status HistogramTransformationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                   an::ui::ProcessDestination& destination,
                                                                   const an::ui::ProcessRun& run,
                                                                   const HistogramTransformationParams& params,
                                                                   an::ui::ExecutionScope& scope)
{
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "no engine session is lent to the histogram transformation");
    }
    // An automatic stretch is decided from the whole image the run changes; a preview area from the whole target.
    std::shared_ptr<const ScreenTransferStatistics> statistics;
    if (params.autoStretch != HistogramAutoStretch::None)
    {
        auto cache = params.statistics
                         ? params.statistics
                         : std::make_shared<an::ui::processes::StretchStatisticsCache<ScreenTransferStatistics>>();
        an::Result<std::shared_ptr<const ScreenTransferStatistics>> measured =
            an::ui::processes::screenTransferStatisticsFor(scope.engine(), scope.execution(), source, run, params.area,
                                                           params.target, *cache);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        statistics = std::move(*measured);
    }
    const an::Result<HistogramTransfer> transfer = histogramTransferOf(params, statistics.get());
    if (!transfer.hasValue())
    {
        return transfer.status();
    }

    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity =
        an::ui::processes::stretchValidity(source.metadata(), source.validity(), maskView);
    const StretchRecord record{.effect = an::kStretchImageEffect,
                               .operation = "an::stretchImage",
                               .detail = describeParameters(params),
                               .processingOperation = kProcessingOperation};
    // The result is made from the source - its codes looked up where that is exact, a working copy stretched otherwise
    // - so the destination it replaces need not start as one; a channel the transfer leaves alone keeps its samples.
    return an::ui::processes::publishStretchOfSource(*session, scope.engine(), scope.execution(), source,
                                                     stretchRequestOf(*transfer), validity, record, destination);
}

an::ui::ProcessStep
HistogramTransformationProcess::describeTypedProcessStep(const HistogramTransformationParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(), .description = describeParameters(params)};
}

void HistogramTransformationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();

    m_IgnoreLastValueChangeForRangeSelectors = true;
    m_ui.shadowsValue->setValue(0.0);
    m_ui.midtonesValue->setValue(0.5);
    m_ui.highlightsValue->setValue(1.0);
    m_IgnoreLastValueChangeForRangeSelectors = false;

    m_original_histogram_widget->setValues(0.0, 0.5, 1.0);

    m_original_histogram_widget->resetZoom();
    m_preview_histogram_widget->resetZoom();

    {
        const QSignalBlocker blocker(m_ui.channelSelector2);
        const int whole = m_ui.channelSelector2->findData(static_cast<int>(an::ChannelScope::Whole));
        m_ui.channelSelector2->setCurrentIndex((std::max)(0, whole));
    }

    clearAutoStretchState();
    recalculateHistogramPreview();

    settingsUpdated();
}

bool HistogramTransformationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void HistogramTransformationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void HistogramTransformationProcess::settingsUpdatedImpl()
{
    if (m_ui.shadowsValue->value() == 0.0 && m_ui.midtonesValue->value() == 0.5 &&
        m_ui.highlightsValue->value() == 1.0 && m_AutoStretch == HistogramAutoStretch::None)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void HistogramTransformationProcess::enableProcessControlsImpl(bool enabled)
{
    if (m_ui.shadowsValue->value() == 0.0 && m_ui.midtonesValue->value() == 0.5 &&
        m_ui.highlightsValue->value() == 1.0 && m_AutoStretch == HistogramAutoStretch::None)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_ui.shadowsValue->setEnabled(enabled);
    m_ui.midtonesValue->setEnabled(enabled);
    m_ui.highlightsValue->setEnabled(enabled);

    m_original_histogram_widget->setEnabled(enabled);
    m_preview_histogram_widget->setEnabled(enabled);

    m_ui.channelSelector2->setEnabled(enabled && isColourTarget());

    if (m_ui.preview->isChecked())
    {
        m_ui.autostretchLinked->setEnabled(enabled);
        m_ui.autostretchUnlinked->setEnabled(enabled && isColourTarget());
    }
    else
    {
        m_ui.autostretchLinked->setEnabled(false);
        m_ui.autostretchUnlinked->setEnabled(false);
    }
}

void HistogramTransformationProcess::showClippedInfo(const ClippedSamples& clipped, const std::uint64_t samples)
{
    const double denominator = samples > 0u ? static_cast<double>(samples) : 1.0;

    m_ui.shadowsInfo->setText(QString::fromStdString(
        fmt::format("{} - {:.6f}%", clipped.shadows, static_cast<double>(clipped.shadows) * 100.0 / denominator)));
    m_ui.highlightsInfo->setText(QString::fromStdString(fmt::format(
        "{} - {:.6f}%", clipped.highlights, static_cast<double>(clipped.highlights) * 100.0 / denominator)));
}

HistogramTransformationParams HistogramTransformationProcess::getParams() const
{
    HistogramTransformationParams params;
    params.shadows = m_ui.shadowsValue->value();
    params.midtones = m_ui.midtonesValue->value();
    params.highlights = m_ui.highlightsValue->value();

    const QVariant scope = m_ui.channelSelector2->currentData();
    params.channel = scope.isValid() ? static_cast<an::ChannelScope>(scope.toInt()) : an::ChannelScope::Whole;
    params.channelLabel = m_ui.channelSelector2->currentText().toStdString();
    params.autoStretch = m_AutoStretch;

    params.target = m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr;
    params.area = previewArea();
    params.statistics = m_Statistics;

    return params;
}
