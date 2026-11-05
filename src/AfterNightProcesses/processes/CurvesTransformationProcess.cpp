// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CurvesTransformationProcess.h"
#include "controls/ImageViewControl.h"

#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/HistogramCompanions.h"

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <array>
#include <utility>
#include <vector>

namespace
{

using an::ui::processes::StretchRecord;

/// The name the processing-state cards record the curve under.
constexpr const char* kProcessingOperation = "curves-transformation";

/// What a curve does to the image it writes, through any scope: the samples are written and their meaning restated -
/// a sampled curve changes what a sample stands for - and the validity is the source's.
constexpr an::OperationEffect kCurveEffect{.samples = an::SampleEffect::Writes,
                                           .semantics = an::SemanticEffect::Restates,
                                           .validity = an::ValidityTransition{.policy = an::ValidityPolicy::Preserve}};

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The interpolation list's entries, in the order the list shows them, with the rule each names.
struct InterpolationChoice
{
    an::CurveInterpolation interpolation;
    const char* label;
};

/// The interpolation rules under the dialog's texts, the last the engine's monotone cubic, which never overshoots a
/// node.
constexpr std::array<InterpolationChoice, 5u> kInterpolations{{
    {an::CurveInterpolation::Linear, "Linear"},
    {an::CurveInterpolation::NaturalCubic, "Cubic"},
    {an::CurveInterpolation::Akima, "Akima"},
    {an::CurveInterpolation::Steffen, "Steffen"},
    {an::CurveInterpolation::MonotoneCubic, "Monotone Cubic"},
}};

/// Whether the dialog offers a scope: every choice of the process channel selector but the CIE XYZ components, which
/// the curves dialog never offered.
[[nodiscard]] constexpr bool isOffered(an::ChannelScope scope) noexcept
{
    return scope != an::ChannelScope::CieX && scope != an::ChannelScope::CieY && scope != an::ChannelScope::CieZ;
}

/// The history's and the lineage's account of a run's parameters.
[[nodiscard]] std::string describeParameters(const CurvesTransformationParams& params)
{
    return fmt::format("Channel: {}, Interpolation: {}", params.channelLabel, params.interpolationLabel);
}

/// Whether the engine's tone curves run over a representation directly.
[[nodiscard]] constexpr bool takesCurveDirectly(an::SampleType sampleType) noexcept
{
    return sampleType == an::SampleType::UInt8 || sampleType == an::SampleType::UInt16 ||
           sampleType == an::SampleType::Float32 || sampleType == an::SampleType::Float64;
}

/// Runs a curve's plan over samples into others of the same representation and extent, or in place.
[[nodiscard]] an::Status runCurve(const an::ExecutionContext& execution, const an::ui::ToneCurveModel& curve,
                                  an::SampleType sampleType, std::uint32_t channels, const void* source,
                                  const an::ImageLayout& sourceLayout, void* destination,
                                  const an::ImageLayout& destinationLayout, const an::KernelValidityView& validity)
{
    const an::Result<an::CurveProgramPlan> plan = curvesProgramFor(execution, curve, sampleType, channels);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source, .layout = &sourceLayout}};
    return an::executePointwiseProgram(
               execution, plan->program(), sources,
               an::PointwiseMutableImageView{.samples = destination, .layout = &destinationLayout}, validity)
        .status();
}

/// The curve over what a channel scope hands over: the whole image, a stored channel or a component's plane.
///
/// It reads what it is handed and writes the output: every sample of it where every sample is valid, which is when the
/// run's destination started unwritten, and otherwise the samples the validity admits.
class CurveOperation final : public an::ChannelScopeOperation
{
public:
    CurveOperation(const an::EngineContext& engine, const an::ui::ToneCurveModel& curve, bool writesEverySample)
        : m_engine(engine), m_curve(curve), m_writesEverySample(writesEverySample)
    {
    }

    [[nodiscard]] bool writesEveryOutputSample() const noexcept override
    {
        return m_writesEverySample;
    }

    [[nodiscard]] an::Status run(const an::ExecutionContext& execution, const an::ChannelScopeInput& input,
                                 const an::ChannelScopeOutput& output) override
    {
        if (input.descriptor == nullptr || input.layout == nullptr || output.layout == nullptr)
        {
            return refusal(an::ErrorCode::InvalidArgument, "the curve was handed no samples");
        }
        const an::SampleType sampleType = input.descriptor->sampleType();
        const std::uint32_t channels = input.descriptor->extent().channelCount();
        if (takesCurveDirectly(sampleType))
        {
            return runCurve(execution, m_curve, sampleType, channels, input.samples, *input.layout, output.samples,
                            *output.layout, input.validity);
        }
        // Another integer is read as fractions, curved and brought back.
        an::ImageDescriptorRequest request = input.descriptor->toRequest();
        request.sampleType = an::SampleType::Float32;
        request.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "the samples have no description as fractions");
        }
        an::Result<an::Image<float>> working = an::Image<float>::create(
            m_engine, *described, an::storageArrangementOf(*input.layout), an::StorageInitialization::Uninitialized);
        if (!working.hasValue())
        {
            return working.status();
        }
        const std::optional<an::ImageWriteAccess<float>> access = working->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the curve's working image holds nothing");
        }
        const an::ImageView<float>& view = access->view();
        an::Status done = an::ui::processes::readAsFractions(execution, sampleType, input.samples, *input.layout,
                                                             an::SampleType::Float32, view.data(), view.layout());
        if (done.failed())
        {
            return done;
        }
        done = runCurve(execution, m_curve, an::SampleType::Float32, channels, view.data(), view.layout(), view.data(),
                        view.layout(), input.validity);
        if (done.failed())
        {
            return done;
        }
        return an::ui::processes::writeFromFractions(m_engine, execution, *input.descriptor, an::SampleType::Float32,
                                                     view.data(), view.layout(), output.samples, *output.layout);
    }

private:
    const an::EngineContext& m_engine;
    const an::ui::ToneCurveModel& m_curve;
    bool m_writesEverySample = false;
};

/// The plan a curve is built under: the representation's shape and the request the widget's curve makes.
struct CurvePlanRequest
{
    an::TonePlanShape shape;
    an::CurveProgramRequest request;
};

[[nodiscard]] CurvePlanRequest curvePlanRequestFor(const an::ui::ToneCurveModel& curve, const an::SampleType sampleType,
                                                   const std::uint32_t channels)
{
    an::TonePlanShape shape;
    shape.sampleType = sampleType;
    shape.inputDomain = an::NumericDomain::NormalizedLinear;
    shape.outputDomain = an::NumericDomain::NormalizedLinear;
    shape.channelCount = channels;
    if (sampleType == an::SampleType::Float64)
    {
        shape.precision = an::ComputePrecision::Float64;
        shape.precisionJustification = an::Float64Justification::StoredInFloat64;
    }
    const bool integer = sampleType == an::SampleType::UInt8 || sampleType == an::SampleType::UInt16;
    an::CurveProgramRequest request;
    request.points = curve.points();
    request.interpolation = curve.effectiveInterpolation();
    request.approximation =
        integer ? an::CurveApproximation::ExactAtResidentValues : an::CurveApproximation::SampledLinear;
    request.tableEntries = kCurvesTableEntries;
    request.domainPolicy = an::CurveDomainPolicy::Clamp;
    return CurvePlanRequest{.shape = shape, .request = request};
}

} // namespace

an::Result<an::CurveProgramPlan> curvesProgramFor(const an::ui::ToneCurveModel& curve, const an::SampleType sampleType,
                                                  const std::uint32_t channels)
{
    if (!takesCurveDirectly(sampleType))
    {
        return refusal(an::ErrorCode::Unsupported, "a tone curve runs over 8- and 16-bit integers and floats");
    }
    const CurvePlanRequest planned = curvePlanRequestFor(curve, sampleType, channels);
    return an::buildCurveProgram(planned.shape, planned.request);
}

an::Result<an::CurveProgramPlan> curvesProgramFor(const an::ExecutionContext& execution,
                                                  const an::ui::ToneCurveModel& curve, const an::SampleType sampleType,
                                                  const std::uint32_t channels)
{
    if (!takesCurveDirectly(sampleType))
    {
        return refusal(an::ErrorCode::Unsupported, "a tone curve runs over 8- and 16-bit integers and floats");
    }
    const CurvePlanRequest planned = curvePlanRequestFor(curve, sampleType, channels);
    return an::buildCurveProgram(execution, planned.shape, planned.request);
}

CurvesTransformationProcess::CurvesTransformationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<CurvesTransformationParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildCurvesTransformationProcessUi(this);

    setProcessDescription(
        QStringLiteral("Adjust image tonal response with spline-based curves and optional histogram guidance."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr,
                              /*supportPerChannel=*/false,
                              /*showWaitingSpinner=*/false);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    QObject::connect(m_ui.channelSelector2, &QComboBox::currentTextChanged, this,
                     &CurvesTransformationProcess::on_channelSelector2_currentTextChanged);
    QObject::connect(m_ui.interpolationSelector, &QComboBox::currentTextChanged, this,
                     &CurvesTransformationProcess::on_interpolationSelector_currentTextChanged);

    QObject::connect(m_ui.gridButton, &QToolButton::clicked, this, &CurvesTransformationProcess::on_gridClicked);
    QObject::connect(m_ui.guideLine, &QToolButton::clicked, this, &CurvesTransformationProcess::on_guideLineClicked);
    QObject::connect(m_ui.histogramButton, &QToolButton::clicked, this,
                     &CurvesTransformationProcess::on_HistogramClicked);

    QObject::connect(m_ui.zoomIn, &QToolButton::clicked, this, &CurvesTransformationProcess::on_ZoomInButton);
    QObject::connect(m_ui.zoomOut, &QToolButton::clicked, this, &CurvesTransformationProcess::on_ZoomOutButton);
    QObject::connect(m_ui.zoomReset, &QToolButton::clicked, this, &CurvesTransformationProcess::on_ZoomResetButton);
    QObject::connect(m_ui.showHistogramAreas, &QToolButton::clicked, this,
                     &CurvesTransformationProcess::on_showHistogramAreas);

    m_curves_widget = std::make_unique<CurvesWidget>(m_ui.curvesEditorScrollArea);
    m_curves_widget->setListener(this);
    m_curves_widget->setClippingInfoEnabled(true);
    m_ui.curvesEditorScrollArea->setWidget(m_curves_widget.get());
    m_HistogramCounts = std::make_unique<an::ui::HistogramCountScheduler>(m_curves_widget.get());

    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

    // Each interpolation entry carries the rule it names, which is what a run reads; its text is the dialog's own.
    {
        const QSignalBlocker blocker(m_ui.interpolationSelector);
        for (const InterpolationChoice& choice : kInterpolations)
        {
            m_ui.interpolationSelector->addItem(QString::fromLatin1(choice.label),
                                                static_cast<int>(choice.interpolation));
        }
        m_ui.interpolationSelector->setCurrentText("Cubic");
    }

    resetCurve();

    updateSupportedImageChannels();

    updateWindowTitle();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_ui.zoomIn->setIcon(iconProvider->getIcon("zoom_in"));
        m_ui.zoomOut->setIcon(iconProvider->getIcon("zoom_out"));
        m_ui.zoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
        m_ui.gridButton->setIcon(iconProvider->getIcon("grid"));
        m_ui.guideLine->setIcon(iconProvider->getIcon("stat_line"));
        m_ui.histogramButton->setIcon(iconProvider->getIcon("histogram"));
        m_ui.showHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));
    }

    m_FixedLayout = false;

    setupPreviewControl(false);

    refreshTheme();
}

void CurvesTransformationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string CurvesTransformationProcess::getProcessNameStatic()
{
    return "Curves Transformation";
}

bool CurvesTransformationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

bool CurvesTransformationProcess::isColourTarget() const
{
    if (m_image_view_control == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor = m_image_view_control->document().descriptor();
    return descriptor.has_value() && descriptor->extent().channelCount() == 3u;
}

void CurvesTransformationProcess::onImageLoaded(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageLoaded(ctrl);
    if (ctrl == m_image_view_control)
    {
        updateSupportedImageChannels();
    }
    else if (ctrl == m_rt_preview_view_control)
    {
        recountHistogram();
    }
}

void CurvesTransformationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    if (ctrl == m_image_view_control)
    {
        updateSupportedImageChannels();
    }
    else if (ctrl == m_rt_preview_view_control)
    {
        recountHistogram();
    }
}

void CurvesTransformationProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
        return;
    }
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    updateSupportedImageChannels();
}

void CurvesTransformationProcess::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    m_ShowingAlternate = true;
    recountHistogram();
}

void CurvesTransformationProcess::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    m_ShowingAlternate = false;
    recountHistogram();
}

void CurvesTransformationProcess::onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode)
{
    if (isPreviewEnabled())
    {
        ImageCompositionMode cMode = (ImageCompositionMode)mode;
        if (cMode == ImageCompositionMode::r_mode)
        {
            m_curves_widget->setDisplayChannels(true, false, false);
        }
        else if (cMode == ImageCompositionMode::g_mode)
        {
            m_curves_widget->setDisplayChannels(false, true, false);
        }
        else if (cMode == ImageCompositionMode::b_mode)
        {
            m_curves_widget->setDisplayChannels(false, false, true);
        }
        else
        {
            m_curves_widget->setDisplayChannels(true, true, true);
        }
    }
}

void CurvesTransformationProcess::onCurvesUpdated(const an::ui::ToneCurveModel& curve)
{
    settingsUpdated();
}

CurvesTransformationProcess::~CurvesTransformationProcess() = default;

void CurvesTransformationProcess::on_channelSelector2_currentTextChanged(const QString& str)
{
    settingsUpdated();
}

void CurvesTransformationProcess::on_interpolationSelector_currentTextChanged(const QString& str)
{
    const QVariant rule = m_ui.interpolationSelector->currentData();
    if (rule.isValid())
    {
        m_curves_widget->setCurveInterpolation(static_cast<an::CurveInterpolation>(rule.toInt()));
    }

    m_curves_widget->update();

    settingsUpdated();
}

void CurvesTransformationProcess::on_gridClicked(bool checked)
{
    m_curves_widget->setGridEnabled(checked);
}

void CurvesTransformationProcess::on_guideLineClicked(bool checked)
{
    m_curves_widget->setGuideLineEnabled(checked);
}

void CurvesTransformationProcess::on_HistogramClicked(bool checked)
{
    if (checked && isPreviewEnabled())
    {
        m_curves_widget->setHistogramEnabled(true);
        m_ui.showHistogramAreas->setEnabled(true);
        recountHistogram();
    }
    else
    {
        m_curves_widget->setHistogramEnabled(false);
        m_ui.showHistogramAreas->setEnabled(false);
    }
}

void CurvesTransformationProcess::on_ZoomInButton()
{
    m_curves_widget->zoomIn();
}

void CurvesTransformationProcess::on_ZoomOutButton()
{
    m_curves_widget->zoomOut();
}

void CurvesTransformationProcess::on_ZoomResetButton()
{
    m_curves_widget->resetZoom();
}

void CurvesTransformationProcess::on_showHistogramAreas(bool checked)
{
    m_curves_widget->setAreaModeEnabled(checked);
}

void CurvesTransformationProcess::recountHistogram()
{
    if (!m_ui.histogramButton->isChecked() || !isPreviewEnabled())
    {
        return;
    }
    // The histogram is of what the preview shows: its result, or the target while it shows the original.
    ImageViewControl* shown = m_ShowingAlternate ? m_image_view_control : m_rt_preview_view_control;
    m_HistogramCounts->count(shown != nullptr ? shown->document().asset() : nullptr);
}

void CurvesTransformationProcess::updateSupportedImageChannels()
{
    const QVariant previous = m_ui.channelSelector2->currentData();
    const QSignalBlocker channel_signals_blocker(m_ui.channelSelector2);

    m_ui.channelSelector2->clear();
    m_ui.channelSelector2->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_ui.channelSelector2->setMaxVisibleItems(18);

    // Each entry carries the scope it names, which is what a run reads; its text and icon are the process channel
    // selector's, in its order.
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
            if (!isOffered(choice.scope))
            {
                continue;
            }
            if (choice.separatorBefore && m_ui.channelSelector2->count() > 0)
            {
                m_ui.channelSelector2->insertSeparator(m_ui.channelSelector2->count());
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

    const int previousIndex = previous.isValid() ? m_ui.channelSelector2->findData(previous) : -1;
    m_ui.channelSelector2->setCurrentIndex((std::max)(0, previousIndex));
}

bool CurvesTransformationProcess::isTypedIdentity(const CurvesTransformationParams& params) const
{
    // The identity curve leaves every sample as it is.
    return params.curve.isIdentity();
}

an::ui::ProcessBodyCoverage
CurvesTransformationProcess::typedProcessBodyCoverage(const CurvesTransformationParams& params) const
{
    // Every sample is the curve's, but for the identity curve, where the body leaves the destination as it started.
    return isTypedIdentity(params) ? an::ui::ProcessBodyCoverage::WritesPart
                                   : an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status CurvesTransformationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                an::ui::ProcessDestination& destination,
                                                                const an::ui::ProcessRun& run,
                                                                const CurvesTransformationParams& params,
                                                                an::ui::ExecutionScope& scope)
{
    // The identity curve the dialog opens with changes nothing: the destination is the source's copy.
    if (isTypedIdentity(params))
    {
        return an::Status::success();
    }
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "no engine session is lent to the curves transformation");
    }
    CurveOperation operation(scope.engine(), params.curve, !destination.holdsSource());
    const an::Result<an::ChannelScopeResult> scoped =
        an::ui::applyProcessChannelScope(scope.execution(), params.channel, source, destination, operation);
    if (!scoped.hasValue())
    {
        return scoped.status();
    }
    // The curved samples are the destination's; what the curve did is recorded with them.
    const StretchRecord record{.effect = kCurveEffect,
                               .operation = "an::applyChannelScope",
                               .detail = describeParameters(params),
                               .processingOperation = kProcessingOperation};
    return an::ui::processes::publishStretchResult(*session, scope.engine(), scope.execution(), source,
                                                   destination.asset().image(), QRect(), record, destination);
}

an::ui::ProcessStep
CurvesTransformationProcess::describeTypedProcessStep(const CurvesTransformationParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(), .description = describeParameters(params)};
}

CurvesTransformationParams CurvesTransformationProcess::getParams() const
{
    CurvesTransformationParams params;
    const QVariant scope = m_ui.channelSelector2->currentData();
    params.channel = scope.isValid() ? static_cast<an::ChannelScope>(scope.toInt()) : an::ChannelScope::Whole;
    params.channelLabel = m_ui.channelSelector2->currentText().toStdString();
    params.interpolationLabel = m_ui.interpolationSelector->currentText().toStdString();
    params.curve = m_curves_widget->getCurve();
    return params;
}

void CurvesTransformationProcess::previewControlStarted()
{
    if (m_ui.histogramButton->isChecked())
    {
        m_curves_widget->setHistogramEnabled(true);
        recountHistogram();
    }
    else
    {
        m_curves_widget->setHistogramEnabled(false);
    }
}

void CurvesTransformationProcess::previewControlClosed()
{
    m_curves_widget->setHistogramEnabled(false);
    m_HistogramCounts->count(nullptr);
}

void CurvesTransformationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();

    m_curves_widget->resetZoom();

    {
        const QSignalBlocker blocker(m_ui.channelSelector2);
        const int whole = m_ui.channelSelector2->findData(static_cast<int>(an::ChannelScope::Whole));
        m_ui.channelSelector2->setCurrentIndex((std::max)(0, whole));
    }

    resetCurve();

    settingsUpdated();
}

void CurvesTransformationProcess::resetCurve()
{
    const QVariant rule = m_ui.interpolationSelector->currentData();
    m_curves_widget->setCurve(an::ui::ToneCurveModel(
        {an::CurveControlPoint{.input = 0.0, .output = 0.0}, an::CurveControlPoint{.input = 1.0, .output = 1.0}},
        rule.isValid() ? static_cast<an::CurveInterpolation>(rule.toInt()) : an::CurveInterpolation::NaturalCubic));
}

bool CurvesTransformationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void CurvesTransformationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void CurvesTransformationProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(!m_curves_widget->curveIsIdentity());
}

void CurvesTransformationProcess::enableProcessControlsImpl(bool enabled)
{
    if (m_curves_widget->curveIsIdentity())
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_ui.interpolationSelector->setEnabled(enabled);
    m_ui.channelSelector2->setEnabled(enabled && isColourTarget());

    m_curves_widget->setEnabled(enabled);
}
