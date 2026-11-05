// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ColorSaturationProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/StretchProcessSupport.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

/// The background at a factor of one of each revision of the dialog's target, shared by the dialog and its runs: the
/// engine's background is that times the factor, so one measurement serves every factor a slider passes through.
struct SaturationBackgrounds
{
    an::ui::processes::StretchStatisticsCache<double> atFactorOne;
};

namespace
{

/// The background of the whole target that a run over a view of it takes, measured at a factor of one once per
/// revision of the target (`ColorSaturationProcess::measureBackgroundAtFactorOne`), and scaled by the run's factor as
/// the engine scales it.
[[nodiscard]] an::Result<double> targetBackground(const an::EngineContext& engine,
                                                  const an::ExecutionContext& execution,
                                                  const ColorSaturationParams& params)
{
    // A non-positive factor is a background of zero, which the engine returns without reading the image.
    if (!(params.bgFactor > 0.0))
    {
        return 0.0;
    }
    if (!params.target || !params.backgrounds)
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the preview's image is not known");
    }
    std::shared_ptr<const double> atOne = params.backgrounds->atFactorOne.find(params.target->revision());
    if (!atOne)
    {
        // The framework holds the target still while a view of it is processed; a writer waiting for it means the
        // image is about to change, and the preview with it.
        an::Result<an::consumer::AssetReadLease> lease = params.target->tryRead();
        if (!lease.hasValue())
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the image changes; the preview is asked for again");
        }
        const an::Result<double> measured =
            ColorSaturationProcess::measureBackgroundAtFactorOne(engine, execution, *lease);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        atOne = std::make_shared<const double>(*measured);
        params.backgrounds->atFactorOne.store(lease->revision(), atOne);
    }
    return *atOne * params.bgFactor;
}

} // namespace

ColorSaturationProcess::ColorSaturationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<ColorSaturationParams>(control, getProcessNameStatic()),
      m_backgrounds(std::make_shared<SaturationBackgrounds>())
{
    m_ui = an::ui::processes::buildColorSaturationProcessUi(this);
    setProcessDescription(QStringLiteral("Boost or reduce saturation by hue range with background-aware protection."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr,
                              /*supportPerChannel=*/false,
                              /*showWaitingSpinner=*/false);
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);

    QObject::connect(m_ui.hueSelector, &QComboBox::currentTextChanged, this,
                     &ColorSaturationProcess::on_hueSelector_currentTextChanged);

    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &ColorSaturationProcess::on_amountChanged);
    INIT_D_SPINBOX_CTRL(bgFactorControl, m_ui.bgFactorValue, m_ui.bgFactorSlider,
                        &ColorSaturationProcess::on_bgFactorChanged);

    m_ui.hueSelector->addItem("Global");
    m_ui.hueSelector->addItem("Pink-Red / Red-Orange");
    m_ui.hueSelector->addItem("Orange-Brown / Yellow");
    m_ui.hueSelector->addItem("Yellow-Green / Green-Cyan");
    m_ui.hueSelector->addItem("Cyan");
    m_ui.hueSelector->addItem("Cyan-Blue / Blue-Magenta");
    m_ui.hueSelector->addItem("Magenta / Pink");

    m_ui.hueSelector->setCurrentText("Global");

    updateWindowTitle();

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setupPreviewControl(false);

    refreshTheme();
}

std::string ColorSaturationProcess::getProcessNameStatic()
{
    return "Color Saturation";
}

bool ColorSaturationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void ColorSaturationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

ColorSaturationProcess::~ColorSaturationProcess() {}

void ColorSaturationProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void ColorSaturationProcess::on_bgFactorChanged(double value)
{
    settingsUpdated();
}

void ColorSaturationProcess::on_hueSelector_currentTextChanged(const QString& str)
{
    settingsUpdated();
}

bool ColorSaturationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void ColorSaturationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

SaturationHueRange ColorSaturationProcess::hueRange(int selector) noexcept
{
    // The selector's entries in their order: every hue, then six ranges, the first crossing zero.
    static constexpr std::array<SaturationHueRange, 7u> kRanges{{
        {0.0, 360.0}, // Global
        {346.0, 20.0}, // Pink-Red / Red-Orange
        {21.0, 60.0}, // Orange-Brown / Yellow
        {61.0, 200.0}, // Yellow-Green / Green-Cyan
        {170.0, 200.0}, // Cyan
        {201.0, 280.0}, // Cyan-Blue / Blue-Magenta
        {281.0, 345.0}, // Magenta / Pink
    }};
    if (selector < 0 || selector >= static_cast<int>(kRanges.size()))
    {
        return kRanges[0];
    }
    return kRanges[static_cast<std::size_t>(selector)];
}

an::Result<double> ColorSaturationProcess::measureBackgroundAtFactorOne(const an::EngineContext& engine,
                                                                        const an::ExecutionContext& execution,
                                                                        const an::consumer::AssetReadLease& image)
{
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    if (const an::ValidityMask* validity = image.validity(); validity != nullptr)
    {
        mask = validity->view();
    }
    return image.visit([&](const auto& typed) -> an::Result<double> {
        using ImageT = std::decay_t<decltype(typed)>;
        using SampleT = typename ImageT::Sample;
        const auto view = typed.view();
        if (!view.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
        }
        const an::KernelValidityView own{.descriptor = typed.metadata().semantics().validity(),
                                         .mask = mask.has_value() ? &*mask : nullptr};
        // The samples the body measures. A sample holding no number is left out under every rule, as the body leaves
        // it out of its own: a rule calling every sample valid becomes one calling the finite ones valid - every
        // integer - and any rule but those two is judged into a mask over the pixel, on the stored samples and with
        // their finiteness, as the body judges it before it measures. A pixel means something only where every one of
        // its samples does, so a pixel whose red the rule rejects is left out of the green's measurement too.
        an::KernelValidityView selection = process_utils::skippingNonFinite(own);
        std::optional<an::ValidityMask> judged;
        std::optional<an::ValidityMaskView<const std::uint8_t>> judgedView;
        const an::ValidityRule rule = own.descriptor.rule();
        if (rule != an::ValidityRule::AllValid && rule != an::ValidityRule::FiniteOnly)
        {
            an::Result<an::ValidityMask> admitted = process_utils::judgeValidAndFinite(
                engine, execution, ImageT::kSampleType, view->data(), view->layout(), own);
            if (!admitted.hasValue())
            {
                return admitted.status();
            }
            const std::optional<an::ValidityDescriptor> explicitMask =
                an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
            if (!explicitMask.has_value())
            {
                return an::Status::failure(an::ErrorCode::Internal, "an explicit mask rule cannot be stated");
            }
            judged.emplace(std::move(*admitted));
            judgedView = std::as_const(*judged).view();
            selection = an::KernelValidityView{.descriptor = *explicitMask,
                                               .mask = judgedView.has_value() ? &*judgedView : nullptr};
        }
        const an::Result<double> measured = an::deriveSaturationEnhancementBackground(
            execution, ImageT::kSampleType, view->data(), view->layout(), 1.0, an::SaturationBackgroundLocation::Median,
            an::StatisticsSelection{.validity = selection.descriptor, .mask = selection.mask});
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            return measured;
        }
        else
        {
            // The engine's statistics are in the samples' own units, codes for an integer, while the body compares the
            // lightness of fractions of full scale - an integer counted from its representation's smallest value over
            // its range - against the background. A median plus a deviation moves with the samples as they are scaled
            // and shifted into those fractions, so the background measured in codes is read as a fraction the same
            // way, without the floating copy of the whole image the fractions would take.
            if (!measured.hasValue())
            {
                return measured.status();
            }
            constexpr double kSmallest = static_cast<double>((std::numeric_limits<SampleT>::lowest)());
            constexpr double kRange = static_cast<double>((std::numeric_limits<SampleT>::max)()) - kSmallest;
            return (*measured - kSmallest) / kRange;
        }
    });
}

an::ui::ProcessBodyCoverage ColorSaturationProcess::typedProcessBodyCoverage(const ColorSaturationParams& params) const
{
    // The enhancement writes every sample of the samples it opens, and the narrowing every sample of the destination.
    return an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status ColorSaturationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                           an::ui::ProcessDestination& destination,
                                                           const an::ui::ProcessRun& run,
                                                           const ColorSaturationParams& params,
                                                           an::ui::ExecutionScope& scope)
{
    const an::ExecutionContext& execution = scope.execution();
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (described->channelSchema() != an::ChannelSchema::Rgb)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "the colour saturation needs an image of red, green and blue");
    }
    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    // A pixel holding no number is left as it was, rather than refused, under every rule.
    const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope.execution());
    if (!skipping.hasValue())
    {
        return skipping.status();
    }
    const an::KernelValidityView validity = *skipping;

    // The background is derived from the green channel in the units the lightness is compared in - of the whole target
    // for a run over a view of it, a preview area or what an interactive frame runs over, so the view is saturated as
    // the target is.
    const an::Result<double> background =
        an::ui::processes::readsViewOfTarget(run, params.area)
            ? targetBackground(scope.engine(), execution, params)
            : an::deriveSaturationEnhancementBackground(
                  execution, samples->sampleType(), samples->samples(), samples->layout(), params.bgFactor,
                  an::SaturationBackgroundLocation::Median,
                  an::StatisticsSelection{.validity = validity.descriptor, .mask = validity.mask});
    if (!background.hasValue())
    {
        return background.status();
    }
    // A factor that takes a bright image's background past one leaves every pixel at or below it: the background is
    // held to the unit interval the operation takes.
    const double heldBackground = (std::min)((std::max)(*background, 0.0), 1.0);
    const SaturationHueRange hues = hueRange(params.hueSelector);
    const an::Result<an::ColorAdjustmentResult> enhanced = an::enhanceImageSaturation(
        execution, samples->sampleType(), samples->samples(), samples->layout(), samples->samples(), samples->layout(),
        an::SaturationEnhancementParameters{.coefficient = params.amount,
                                            .background = heldBackground,
                                            .hueMinimumDegrees = hues.minimumDegrees,
                                            .hueMaximumDegrees = hues.maximumDegrees},
        validity);
    if (!enhanced.hasValue())
    {
        return enhanced.status();
    }
    if (an::Status committed = samples->commit(execution); committed.failed())
    {
        return committed;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    return process_utils::recordNonlinearResult(
        scope, destination,
        process_utils::NonlinearResultRecord{.stateOperation = "saturation-enhancement",
                                             .engineOperation = "an::enhanceImageSaturation",
                                             .detail = fmt::format("amount {}, background {}, hues {} to {}",
                                                                   params.amount, heldBackground, hues.minimumDegrees,
                                                                   hues.maximumDegrees)});
}

an::ui::ProcessStep ColorSaturationProcess::describeTypedProcessStep(const ColorSaturationParams& params) const
{
    const SaturationHueRange hues = hueRange(params.hueSelector);
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Double coeff {}, background factor {}, hue min {}, hue max {}", params.amount,
                                   params.bgFactor, hues.minimumDegrees, hues.maximumDegrees)};
}

void ColorSaturationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    amountControl->setValue(0.0);
    bgFactorControl->setValue(1.0);

    m_ui.hueSelector->setCurrentText("Global");

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void ColorSaturationProcess::settingsUpdatedImpl() {}

void ColorSaturationProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_ui.hueSelector->setEnabled(enabled);
    amountControl->setEnabled(enabled);
    bgFactorControl->setEnabled(enabled);
}

bool ColorSaturationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return process_utils::holdsColourImage(ctrl);
}

ColorSaturationParams ColorSaturationProcess::getParams() const
{
    ColorSaturationParams params;
    params.amount = amountControl->getValue();
    params.bgFactor = bgFactorControl->getValue();
    params.hueSelector = m_ui.hueSelector->currentIndex();
    params.target = m_image_view_control ? m_image_view_control->document().asset() : nullptr;
    params.area = previewArea();
    params.backgrounds = m_backgrounds;

    return params;
}

an::ui::PreviewScaling ColorSaturationProcess::previewScaling() const
{
    // Every pixel is saturated by one rule wherever it lies, against the whole target's background: a frame over the
    // target reduced shows what the whole result shows at that reduction.
    return an::ui::PreviewScaling::Decimated;
}
