// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RangeSelectionProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Masks/RangeSelection.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <type_traits>
#include <variant>

namespace
{

/// The samples a selection reads, in fractions of full scale, and where its result is written: the same planar
/// floating layout, which may be the very samples it reads.
struct SelectionPlanes
{
    an::SampleType sampleType = an::SampleType::Float32;
    const void* from = nullptr;
    const an::ImageLayout* fromLayout = nullptr;
    an::KernelValidityView validity{};
    void* result = nullptr;
    const an::ImageLayout* resultLayout = nullptr;
};

/// Selects the range of `planes.from` into a zeroed working image, so a sample the validity rejects - or, under a rule
/// that admits every sample, one holding no number - is not selected; smooths it into `planes.result` by the exact
/// Gaussian, or copies it there; and complements it when asked - in that order.
[[nodiscard]] an::Status selectRange(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& workingDescribed,
                                     const SelectionPlanes& planes, const RangeSelectionProcessParams& params)
{
    const an::ExecutionContext& execution = scope.execution();
    an::Status made = an::Status::success();
    auto select = [&]<typename WorkT>() {
        an::Result<an::Image<WorkT>> scratch =
            an::Image<WorkT>::create(scope.engine(), workingDescribed, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Zeroed, an::ImageMetadata{});
        if (!scratch.hasValue())
        {
            made = scratch.status();
            return;
        }
        const std::optional<an::ImageWriteAccess<WorkT>> access = scratch->write();
        if (!access.has_value())
        {
            made = an::Status::failure(an::ErrorCode::NotInitialized, "the selection's working image holds nothing");
            return;
        }
        const an::ImageLayout scratchLayout = access->view().layout();
        an::RangeSelectionRequest request;
        request.lowerLimit = params.lowerLimit;
        request.upperLimit = params.upperLimit;
        request.fuzziness = params.fuzziness;
        request.screening = params.screening;
        request.invert = false;
        const an::Result<an::RangeSelectionResult> selected =
            an::rangeSelectImage(execution, planes.sampleType, planes.from, *planes.fromLayout, access->view().data(),
                                 scratchLayout, request, process_utils::skippingNonFinite(planes.validity));
        if (!selected.hasValue())
        {
            made = selected.status();
            return;
        }
        if (params.smoothness > 0.0)
        {
            const an::Result<an::GaussianPlan> plan =
                an::planGaussianBlur(an::GaussianRequest{.sigmaX = params.smoothness, .sigmaY = params.smoothness});
            if (!plan.hasValue())
            {
                made = plan.status();
                return;
            }
            const an::Result<an::SpatialFilterResult> blurred =
                an::blurImageGaussian(execution, planes.sampleType, access->view().data(), scratchLayout, planes.result,
                                      *planes.resultLayout, *plan);
            made = blurred.hasValue() ? an::Status::success() : blurred.status();
            return;
        }
        made = an::copyImageSamples(execution, planes.sampleType, access->view().data(), scratchLayout, planes.result,
                                    *planes.resultLayout);
    };
    if (planes.sampleType == an::SampleType::Float64)
    {
        select.template operator()<double>();
    }
    else
    {
        select.template operator()<float>();
    }
    if (made.failed() || !params.invert)
    {
        return made;
    }
    return process_utils::scaleAndBiasInPlace(execution, planes.sampleType, planes.result, *planes.resultLayout, -1.0,
                                              1.0);
}

} // namespace

RangeSelectionProcess::RangeSelectionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<RangeSelectionProcessParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildRangeSelectionProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Create a luminance range mask using lower/upper thresholds with fuzziness and smoothness controls."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, false);

    QObject::connect(m_ui.linkRangeLimits, &QCheckBox::checkStateChanged, this,
                     &RangeSelectionProcess::on_linkRangeLimitsClicked);

    QObject::connect(m_ui.screening, &QCheckBox::checkStateChanged, this, &RangeSelectionProcess::on_screeningClicked);
    QObject::connect(m_ui.lightness, &QCheckBox::checkStateChanged, this, &RangeSelectionProcess::on_lightnessClicked);
    QObject::connect(m_ui.invert, &QCheckBox::checkStateChanged, this, &RangeSelectionProcess::on_invertClicked);

    INIT_D_SPINBOX_CTRL(lowerLimitControl, m_ui.lowerLimitValue, m_ui.lowerLimitSlider,
                        &RangeSelectionProcess::on_lowerLimitChanged);
    INIT_D_SPINBOX_CTRL(upperLimitControl, m_ui.upperLimitValue, m_ui.upperLimitSlider,
                        &RangeSelectionProcess::on_upperLimitChanged);
    INIT_D_SPINBOX_CTRL(fuzzinessControl, m_ui.fuzzinessValue, m_ui.fuzzinessSlider,
                        &RangeSelectionProcess::on_fuzzinessChanged);
    INIT_D_SPINBOX_CTRL(smoothnessControl, m_ui.smoothnessValue, m_ui.smoothnessSlider,
                        &RangeSelectionProcess::on_smoothnessChanged);

    updateWindowTitle();

    m_CreatedViewControlsAreNeighbour = true;

    m_ShowWaitingSpinner = false;
    setExecutionAutostretchSupported(true);
    // The selection is a mask of its own; no mask of the target weighs it.
    m_SupportMasking = false;
    setupPreviewControl(false);

    refreshTheme();
}

std::string RangeSelectionProcess::getProcessNameStatic()
{
    return "Range Selection";
}

bool RangeSelectionProcess::requiresExclusiveAccessStatic()
{
    return false;
}

RangeSelectionProcess::~RangeSelectionProcess() {}

void RangeSelectionProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void RangeSelectionProcess::on_lowerLimitChanged(double value)
{
    checkLinkRangeLimits();

    if (value >= upperLimitControl->getValue())
    {
        upperLimitControl->setValue(value);
    }

    if (m_ui.linkRangeLimits->isChecked())
    {
        if (value + m_CurrentLinkedLimitsOffset > 1.0)
        {
            lowerLimitControl->setValue(1.0 - m_CurrentLinkedLimitsOffset);
        }
        else
        {
            upperLimitControl->setValue(value + m_CurrentLinkedLimitsOffset);
        }
    }

    settingsUpdated();
}

void RangeSelectionProcess::on_upperLimitChanged(double value)
{
    checkLinkRangeLimits();

    if (value <= lowerLimitControl->getValue())
    {
        lowerLimitControl->setValue(value);
    }

    if (m_ui.linkRangeLimits->isChecked())
    {
        if (value - m_CurrentLinkedLimitsOffset < 0.0)
        {
            upperLimitControl->setValue(0.0 + m_CurrentLinkedLimitsOffset);
        }
        else
        {
            lowerLimitControl->setValue(value - m_CurrentLinkedLimitsOffset);
        }
    }

    settingsUpdated();
}

void RangeSelectionProcess::on_linkRangeLimitsClicked(int state)
{
    if (state == Qt::Checked)
    {
        m_CurrentLinkedLimitsOffset = upperLimitControl->getValue() - lowerLimitControl->getValue();
    }
    else
    {
        m_CurrentLinkedLimitsOffset = 0.0;
    }

    settingsUpdated();
}

void RangeSelectionProcess::on_fuzzinessChanged(double value)
{
    settingsUpdated();
}

void RangeSelectionProcess::on_smoothnessChanged(double value)
{
    settingsUpdated();
}

void RangeSelectionProcess::on_screeningClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void RangeSelectionProcess::on_lightnessClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void RangeSelectionProcess::on_invertClicked(Qt::CheckState state)
{
    settingsUpdated();
}

bool RangeSelectionProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void RangeSelectionProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status RangeSelectionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                          an::ui::ProcessDestination& destination,
                                                          const an::ui::ProcessRun& run,
                                                          const RangeSelectionProcessParams& params,
                                                          an::ui::ExecutionScope& scope)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const process_utils::NonlinearResultRecord record{
        .stateOperation = "range-selection",
        .engineOperation = "an::rangeSelectImage",
        .detail = fmt::format("{} to {}, fuzziness {}, smoothness {}{}{}{}", params.lowerLimit, params.upperLimit,
                              params.fuzziness, params.smoothness, params.screening ? ", screening" : "",
                              params.invert ? ", inverted" : "", params.lightness ? ", on the CIE lightness" : ""),
        .restatedValidity = an::ValidityDescriptor::allValid()};

    // A colour image's CIE lightness becomes the mask, selected in place: the selection reads it into a working image
    // before it is overwritten.
    if (params.lightness && process_utils::describesColourImage(*described))
    {
        an::Result<an::consumer::ImageAsset> lightness =
            process_utils::extractLightness(scope, source, record.stateOperation);
        if (!lightness.hasValue())
        {
            return lightness.status();
        }
        {
            an::Result<an::consumer::AssetWriteLease> lease = lightness->write();
            if (!lease.hasValue())
            {
                return lease.status();
            }
            const std::optional<an::ImageDescriptor> planeDescribed = lightness->descriptor();
            // A shared mask of the source says which pixels' lightness means something; a value rule is the source's
            // samples' business, and a lightness holding no number is not selected either way.
            const std::optional<an::ValidityMaskView<const std::uint8_t>> mask =
                source.validity() != nullptr ? source.validity()->view() : std::nullopt;
            an::KernelValidityView validity;
            if (source.metadata().semantics().validity().rule() == an::ValidityRule::ExplicitMask && mask.has_value() &&
                mask->planeCount() == 1u)
            {
                validity.descriptor = source.metadata().semantics().validity();
                validity.mask = &*mask;
            }
            // An integer lightness is selected in fractions of full scale, as the image itself is.
            const an::Status selected = lease->visitWrite([&](auto&& access) -> an::Status {
                const an::ImageLayout layout = access.view().layout();
                an::Result<process_utils::NormalizedSamples> plane = process_utils::NormalizedSamples::open(
                    scope.engine(), scope.execution(), *planeDescribed, access.view().data(), layout, validity);
                if (!plane.hasValue())
                {
                    return plane.status();
                }
                const an::Result<an::ImageDescriptor> workingDescribed =
                    an::describeSamplesAs(*planeDescribed, plane->sampleType());
                if (!workingDescribed.hasValue())
                {
                    return workingDescribed.status();
                }
                if (an::Status done = selectRange(scope, *workingDescribed,
                                                  SelectionPlanes{.sampleType = plane->sampleType(),
                                                                  .from = plane->samples(),
                                                                  .fromLayout = &plane->layout(),
                                                                  .validity = plane->validity(),
                                                                  .result = plane->samples(),
                                                                  .resultLayout = &plane->layout()},
                                                  params);
                    done.failed())
                {
                    return done;
                }
                return plane->commit(scope.execution());
            });
            if (selected.failed())
            {
                return selected;
            }
        }
        if (an::Status replaced = destination.replace(std::move(*lightness)); replaced.failed())
        {
            return replaced;
        }
        return run.isForPreview() ? an::Status::success() : process_utils::recordNonlinearResult(destination, record);
    }

    // Otherwise the image itself, in fractions of full scale: the mask has the image's channels and representation.
    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    const an::Result<an::ImageDescriptor> workingDescribed = an::describeSamplesAs(*described, samples->sampleType());
    if (!workingDescribed.hasValue())
    {
        return workingDescribed.status();
    }
    const an::Status selected = selectRange(scope, *workingDescribed,
                                            SelectionPlanes{.sampleType = samples->sampleType(),
                                                            .from = samples->samples(),
                                                            .fromLayout = &samples->layout(),
                                                            .validity = samples->validity(),
                                                            .result = samples->samples(),
                                                            .resultLayout = &samples->layout()},
                                            params);
    if (selected.failed())
    {
        return selected;
    }
    if (an::Status committed = samples->commit(scope.execution()); committed.failed())
    {
        return committed;
    }
    return run.isForPreview() ? an::Status::success() : process_utils::recordNonlinearResult(destination, record);
}

void RangeSelectionProcess::resetSettings()
{
    m_ui.linkRangeLimits->setChecked(false);
    lowerLimitControl->setValue(0.0);
    upperLimitControl->setValue(1.0);

    fuzzinessControl->setValue(0.0);
    smoothnessControl->setValue(0.0);

    m_ui.screening->setChecked(false);
    m_ui.lightness->setChecked(true);
    m_ui.invert->setChecked(false);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void RangeSelectionProcess::settingsUpdatedImpl() {}

void RangeSelectionProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_ui.linkRangeLimits->setEnabled(enabled);
    lowerLimitControl->setEnabled(enabled);
    upperLimitControl->setEnabled(enabled);

    fuzzinessControl->setEnabled(enabled);
    smoothnessControl->setEnabled(enabled);

    m_ui.screening->setEnabled(enabled);
    m_ui.lightness->setEnabled(enabled);
    m_ui.invert->setEnabled(enabled);
}

void RangeSelectionProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                        bool& executeInBackground, std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = true;
    executeInBackground = true;

    newImageName = m_image_view_control->getUniqueName() + " - MASK";
}

void RangeSelectionProcess::checkLinkRangeLimits()
{
    if (lowerLimitControl->getValue() == 0.0 && upperLimitControl->getValue() == 1.0)
    {
        m_ui.linkRangeLimits->setEnabled(false);
        m_CurrentLinkedLimitsOffset = 0.0;
    }
    else
    {
        m_ui.linkRangeLimits->setEnabled(true);
    }
}

RangeSelectionProcessParams RangeSelectionProcess::getParams() const
{
    RangeSelectionProcessParams params;
    params.linkRangeLimits = m_ui.linkRangeLimits->isChecked();
    params.screening = m_ui.screening->isChecked();
    params.lightness = m_ui.lightness->isChecked();
    params.invert = m_ui.invert->isChecked();

    params.lowerLimit = lowerLimitControl->getValue();
    params.upperLimit = upperLimitControl->getValue();
    params.fuzziness = fuzzinessControl->getValue();
    params.smoothness = smoothnessControl->getValue();

    return params;
}
