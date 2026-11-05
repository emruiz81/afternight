// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "DarkStructureEnhanceProcess.h"

#include "controls/ProcessButtonIcons.h"
#include "processes/PointwiseProcessUtils.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <algorithm>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{

[[nodiscard]] const char* scaling_name_from_index(const int index)
{
    return index == 0 ? "3x3 Linear Interpolation" : "5x5 B3 Spline";
}

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A new grey Float32 mask of a source's extent, of the run's session, recorded as the dark-structure mask.
[[nodiscard]] an::Result<an::consumer::ImageAsset>
newMask(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& source, const an::ImageMetadata& metadata)
{
    const an::Result<an::ImageDescriptor> described =
        process_utils::derivedImageDescriptor(source, 1u, an::SampleType::Float32, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::ImageMetadata> derived =
        process_utils::derivedImageMetadata(metadata, *described, "dark-structure-mask");
    if (!derived.hasValue())
    {
        return derived.status();
    }
    return process_utils::createDerivedImage(scope, *described, std::move(*derived));
}

/// A grey mask repeated over a colour image's three channels, recorded as the mask is.
[[nodiscard]] an::Result<an::consumer::ImageAsset> repeatedOverColour(const an::ui::ExecutionScope& scope,
                                                                      const an::ImageDescriptor& source,
                                                                      const an::ImageMetadata& metadata,
                                                                      const an::consumer::ImageAsset& grey)
{
    const an::Result<an::ImageDescriptor> described =
        process_utils::derivedImageDescriptor(source, 3u, an::SampleType::Float32, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::ImageMetadata> derived =
        process_utils::derivedImageMetadata(metadata, *described, "dark-structure-mask");
    if (!derived.hasValue())
    {
        return derived.status();
    }
    an::Result<an::consumer::ImageAsset> colour =
        process_utils::createDerivedImage(scope, *described, std::move(*derived));
    if (!colour.hasValue())
    {
        return colour.status();
    }
    {
        an::Result<an::consumer::AssetReadLease> from = grey.read();
        an::Result<an::consumer::AssetWriteLease> to = colour->write();
        if (!from.hasValue() || !to.hasValue())
        {
            return !from.hasValue() ? from.status() : to.status();
        }
        const auto* image = std::get_if<an::Image<float>>(&from->image());
        const std::optional<an::ImageWriteAccess<float>> access = to->write<float>();
        if (image == nullptr || !access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mask cannot be repeated over the colour channels");
        }
        const auto view = image->view();
        if (!view.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mask holds nothing");
        }
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < described->extent().height(); ++y)
            {
                const auto in = view->row(0u, y);
                auto out = access->view().row(channel, y);
                if (!in.has_value() || !out.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "a row of the mask holds nothing");
                }
                std::copy(in->begin(), in->end(), out->begin());
            }
        }
    }
    return colour;
}

/// Runs `body` with the mask's one plane, writable.
template <typename BodyT> [[nodiscard]] an::Status withMaskPlane(an::consumer::ImageAsset& mask, BodyT&& body)
{
    an::Result<an::consumer::AssetWriteLease> lease = mask.write();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const std::optional<an::ImageWriteAccess<float>> access = lease->write<float>();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mask cannot be written");
    }
    const std::optional<an::PlaneView<float>> plane = access->view().plane(0u);
    if (!plane.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mask holds no plane");
    }
    return std::forward<BodyT>(body)(*plane);
}

} // namespace

namespace an::ui::processes::dark_structure_enhance_detail
{

an::MultiscaleScalingFunction scalingFromIndex(const int index) noexcept
{
    return index == 0 ? an::MultiscaleScalingFunction::LinearInterpolation3 : an::MultiscaleScalingFunction::BSpline5;
}

double midtonesFromAmount(const double amount) noexcept
{
    return 0.5 * amount + 0.5;
}

an::DarkStructureRequest buildRequest(const DarkStructureEnhanceParams& params)
{
    an::DarkStructureRequest request;
    request.removedLayers = static_cast<std::uint32_t>(params.layersToRemove);
    request.scaling = scalingFromIndex(params.scalingFunction);
    // The balance is kept in single precision.
    request.transfer.midtones = static_cast<double>(static_cast<float>(midtonesFromAmount(params.amount)));
    request.transfer.shadows = 0.0;
    request.transfer.highlights = 1.0;
    request.iterations = static_cast<std::uint32_t>(params.iterations);
    request.whiteLevel = 1.0;
    request.denoiseMask = true;
    return request;
}

std::string channelHistorySuffix(const std::string_view channelLabel)
{
    if (channelLabel.empty() || channelLabel == "RGB" || channelLabel == "Grayscale")
    {
        return std::string();
    }
    return " in channel: " + std::string(channelLabel);
}

} // namespace an::ui::processes::dark_structure_enhance_detail

namespace detail = an::ui::processes::dark_structure_enhance_detail;

DarkStructureEnhanceProcess::DarkStructureEnhanceProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<DarkStructureEnhanceParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildDarkStructureEnhanceProcessUi(this);
    setProcessDescription(
        QStringLiteral("Enhance dark structures with a native starlet mask and masked midtones transformation."));

    INIT_SPINBOX_CTRL(layersControl, m_ui.layersValue, m_ui.layersSlider,
                      &DarkStructureEnhanceProcess::on_layersChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider,
                        &DarkStructureEnhanceProcess::on_amountChanged);
    INIT_SPINBOX_CTRL(iterationsControl, m_ui.iterationsValue, m_ui.iterationsSlider,
                      &DarkStructureEnhanceProcess::on_iterationsChanged);

    QObject::connect(m_ui.scalingFunctionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DarkStructureEnhanceProcess::on_scalingFunctionChanged);
    QObject::connect(m_ui.previewModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DarkStructureEnhanceProcess::on_previewModeChanged);
    QObject::connect(m_ui.preview, &QCheckBox::checkStateChanged, this,
                     &DarkStructureEnhanceProcess::on_previewChanged);
    QObject::connect(m_ui.createMaskImage, &QCheckBox::checkStateChanged, this,
                     &DarkStructureEnhanceProcess::on_createMaskImageChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(800, 800), "Preview: DarkStructureEnhance");

    initialize();
    updateUiState();
    settingsUpdated();
    refreshTheme();
}

std::string DarkStructureEnhanceProcess::getProcessNameStatic()
{
    return "DarkStructureEnhance";
}

bool DarkStructureEnhanceProcess::requiresExclusiveAccessStatic()
{
    return false;
}

DarkStructureEnhanceProcess::~DarkStructureEnhanceProcess() = default;

void DarkStructureEnhanceProcess::on_layersChanged([[maybe_unused]] const int value)
{
    settingsUpdated();
}

void DarkStructureEnhanceProcess::on_amountChanged([[maybe_unused]] const double value)
{
    settingsUpdated();
}

void DarkStructureEnhanceProcess::on_iterationsChanged([[maybe_unused]] const int value)
{
    settingsUpdated();
}

void DarkStructureEnhanceProcess::on_scalingFunctionChanged([[maybe_unused]] const int index)
{
    settingsUpdated();
}

void DarkStructureEnhanceProcess::on_previewModeChanged([[maybe_unused]] const int index)
{
    settingsUpdated();
}

void DarkStructureEnhanceProcess::on_previewChanged([[maybe_unused]] const Qt::CheckState state)
{
    updateUiState();
    settingsUpdated();
}

void DarkStructureEnhanceProcess::on_createMaskImageChanged([[maybe_unused]] const Qt::CheckState state)
{
    settingsUpdated();
}

bool DarkStructureEnhanceProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void DarkStructureEnhanceProcess::enablePreview(const bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status DarkStructureEnhanceProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                     const an::ChannelScopeOutput& output,
                                                                     const an::ui::ProcessRun& run,
                                                                     const DarkStructureEnhanceParams& params,
                                                                     an::ui::ExecutionScope& scope)
{
    const an::Result<an::DarkStructurePlan> plan = an::planDarkStructure(detail::buildRequest(params));
    if (!plan.hasValue())
    {
        return plan.status();
    }

    // The plane in fractions of full scale, darkened into a working image and copied back, since the enhancement reads
    // and writes different samples.
    an::Result<process_utils::NormalizedSamples> samples = process_utils::NormalizedSamples::open(
        scope.engine(), scope.execution(), *output.descriptor, output.samples, *output.layout, input.validity);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    const an::Result<an::ImageDescriptor> workingDescribed =
        an::describeSamplesAs(*output.descriptor, samples->sampleType());
    if (!workingDescribed.hasValue())
    {
        return workingDescribed.status();
    }

    // An Apply that keeps the mask writes it into an image of its own while it enhances.
    std::optional<an::consumer::ImageAsset> mask;
    if (params.createMaskImage && !run.isForPreview() && !run.isForSequence())
    {
        an::Result<an::consumer::ImageAsset> made =
            newMask(scope, *output.descriptor, params.sourceMetadata ? *params.sourceMetadata : an::ImageMetadata{});
        if (!made.hasValue())
        {
            return made.status();
        }
        mask.emplace(std::move(*made));
    }

    // A sample that holds no number is left as it was and takes no part in the stretch.
    const an::KernelValidityView validity = process_utils::skippingNonFinite(samples->validity());
    const an::SpatialSelection selection{.validity = validity.descriptor, .mask = validity.mask};
    an::Status enhanced = an::Status::success();
    auto enhance = [&]<typename WorkT>() {
        an::Result<an::Image<WorkT>> scratch =
            an::Image<WorkT>::create(scope.engine(), *workingDescribed, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Uninitialized, an::ImageMetadata{});
        if (!scratch.hasValue())
        {
            enhanced = scratch.status();
            return;
        }
        const std::optional<an::ImageWriteAccess<WorkT>> access = scratch->write();
        if (!access.has_value())
        {
            enhanced = refusal(an::ErrorCode::NotInitialized, "the enhancement's working image holds nothing");
            return;
        }
        const an::ImageLayout scratchLayout = access->view().layout();
        // The enhancement leaves a sample the validity rejects untouched, so the working image starts as the plane.
        enhanced = an::copyImageSamples(scope.execution(), samples->sampleType(), samples->samples(), samples->layout(),
                                        access->view().data(), scratchLayout);
        if (enhanced.failed())
        {
            return;
        }
        auto enhanceInto = [&](const an::PlaneView<float>* maskPlane) -> an::Status {
            const an::Result<an::DarkStructureResult> result = an::enhanceImageDarkStructures(
                scope.engine(), scope.execution(), samples->sampleType(), samples->samples(), samples->layout(),
                access->view().data(), scratchLayout, *plan, maskPlane, selection);
            return result.hasValue() ? an::Status::success() : result.status();
        };
        enhanced = mask.has_value()
                       ? withMaskPlane(*mask, [&](const an::PlaneView<float>& plane) { return enhanceInto(&plane); })
                       : enhanceInto(nullptr);
        if (enhanced.failed())
        {
            return;
        }
        enhanced = an::copyImageSamples(scope.execution(), samples->sampleType(), access->view().data(), scratchLayout,
                                        samples->samples(), samples->layout());
    };
    if (samples->sampleType() == an::SampleType::Float64)
    {
        enhance.template operator()<double>();
    }
    else
    {
        enhance.template operator()<float>();
    }
    if (enhanced.failed())
    {
        return enhanced;
    }
    if (an::Status committed = samples->commit(scope.execution()); committed.failed())
    {
        return committed;
    }
    if (mask.has_value())
    {
        const QString target = QString::fromStdString(params.targetName);
        m_StagedOutputs.stage(process_utils::StagedOutput{
            .asset = std::move(*mask),
            .name = target + QStringLiteral(" - DarkStructureEnhance Mask"),
            .history = fmt::format("DarkStructureEnhance: mask created from '{}'", params.targetName)});
    }
    return an::Status::success();
}

an::Status DarkStructureEnhanceProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                an::ui::ProcessDestination& destination,
                                                                const an::ui::ProcessRun& run,
                                                                const DarkStructureEnhanceParams& params,
                                                                an::ui::ExecutionScope& scope)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }

    // A preview of the mask shows the mask the selected channel or component would be enhanced through, grey.
    if (run.isForPreview() && params.previewMode == DarkStructureEnhancePreviewMode::GeneratedMask)
    {
        const an::Result<an::DarkStructurePlan> plan = an::planDarkStructure(detail::buildRequest(params));
        if (!plan.hasValue())
        {
            return plan.status();
        }
        an::Result<an::consumer::ImageAsset> mask = newMask(scope, *described, source.metadata());
        if (!mask.hasValue())
        {
            return mask.status();
        }
        const std::optional<an::ValidityMaskView<const std::uint8_t>> validityMask =
            source.validity() != nullptr ? source.validity()->view() : std::nullopt;
        // As the Apply builds it: a sample that holds no number takes no part in the mask.
        an::KernelValidityView sourceValidity;
        sourceValidity.descriptor = source.metadata().semantics().validity();
        sourceValidity.mask = validityMask.has_value() ? &*validityMask : nullptr;
        const an::KernelValidityView validity = process_utils::skippingNonFinite(sourceValidity);
        const an::SpatialSelection selection{.validity = validity.descriptor, .mask = validity.mask};
        const an::ChannelScope channel = an::ui::effectiveProcessChannel(run.channel, *described);
        const an::Status built = withMaskPlane(*mask, [&](const an::PlaneView<float>& plane) -> an::Status {
            return std::visit(
                [&](const auto& typed) -> an::Status {
                    const auto view = typed.view();
                    if (!view.has_value())
                    {
                        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
                    }
                    const an::ImageLayout sourceLayout = view->layout();
                    if (channel == an::ChannelScope::Whole)
                    {
                        const an::Result<an::DarkStructureResult> result =
                            an::buildImageDarkStructureMask(scope.engine(), scope.execution(), described->sampleType(),
                                                            view->data(), sourceLayout, *plan, plane, selection);
                        return result.hasValue() ? an::Status::success() : result.status();
                    }
                    // One channel or component: the plane the selector names, as the channel scope hands it over,
                    // from the samples read as fractions of full scale where the image states no full scale.
                    const std::optional<an::ImageDescriptor> reading = an::ui::componentReadingOf(*described);
                    if (!reading.has_value())
                    {
                        return refusal(an::ErrorCode::InvalidArgument,
                                       "the image cannot be read as fractions of full scale");
                    }
                    const an::Result<an::ChannelScopePlan> scoped =
                        an::planChannelScope(an::ui::processChannelScopeRequest(channel, *reading));
                    if (!scoped.hasValue())
                    {
                        return scoped.status();
                    }
                    const an::Result<an::ImageDescriptor> planeDescribed =
                        an::channelScopePlaneDescriptor(*scoped, *reading);
                    if (!planeDescribed.hasValue())
                    {
                        return planeDescribed.status();
                    }
                    an::Result<an::ImageMetadata> planeMetadata =
                        process_utils::derivedImageMetadata(source.metadata(), *planeDescribed, std::string());
                    if (!planeMetadata.hasValue())
                    {
                        return planeMetadata.status();
                    }
                    an::Result<an::consumer::ImageAsset> component =
                        process_utils::createDerivedImage(scope, *planeDescribed, std::move(*planeMetadata));
                    if (!component.hasValue())
                    {
                        return component.status();
                    }
                    an::Result<an::consumer::AssetWriteLease> lease = component->write();
                    if (!lease.hasValue())
                    {
                        return lease.status();
                    }
                    an::ChannelScopeSource from;
                    from.descriptor = &*reading;
                    from.samples = view->data();
                    from.layout = &sourceLayout;
                    from.validity.descriptor = selection.validity;
                    from.validity.mask = selection.mask;
                    return lease->visitWrite([&](auto&& access) -> an::Status {
                        const an::ImageLayout componentLayout = access.view().layout();
                        const an::Result<an::ChannelScopeResult> extracted = an::extractChannelScope(
                            scope.execution(), *scoped, from, access.view().data(), componentLayout);
                        if (!extracted.hasValue())
                        {
                            return extracted.status();
                        }
                        const an::Result<an::DarkStructureResult> result = an::buildImageDarkStructureMask(
                            scope.engine(), scope.execution(), planeDescribed->sampleType(), access.view().data(),
                            componentLayout, *plan, plane);
                        return result.hasValue() ? an::Status::success() : result.status();
                    });
                },
                source.image());
        });
        if (built.failed())
        {
            return built;
        }
        // A colour image's mask is shown over its three channels, as the colour image it stands for, which a mask of
        // the target can weigh.
        if (process_utils::describesColourImage(*described) && described->extent().channelCount() == 3u)
        {
            an::Result<an::consumer::ImageAsset> colour =
                repeatedOverColour(scope, *described, source.metadata(), *mask);
            if (!colour.hasValue())
            {
                return colour.status();
            }
            return destination.replace(std::move(*colour));
        }
        return destination.replace(std::move(*mask));
    }

    if (!run.isForPreview())
    {
        m_StagedOutputs.clear();
    }
    // The mask an Apply keeps derives its record from the image the run reads.
    DarkStructureEnhanceParams withSource = params;
    withSource.sourceMetadata = std::make_shared<const an::ImageMetadata>(source.metadata());
    if (an::Status ran = executeTypedProcessUsingChannels(source, destination, run, withSource, scope); ran.failed())
    {
        return ran;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    return process_utils::recordNonlinearResult(
        scope, destination,
        process_utils::NonlinearResultRecord{
            .stateOperation = "dark-structure-enhance",
            .engineOperation = "an::enhanceImageDarkStructures",
            .detail = fmt::format("layers {}, {}, midtones {}, iterations {}", params.layersToRemove,
                                  scaling_name_from_index(params.scalingFunction),
                                  detail::midtonesFromAmount(params.amount), params.iterations)});
}

an::ui::ProcessStep
DarkStructureEnhanceProcess::describeTypedProcessStep(const DarkStructureEnhanceParams& params) const
{
    std::string description = fmt::format("Layers: {}, Scaling: {}, Amount: {:.2f} (midtones {:.2f}), Iterations: {}",
                                          params.layersToRemove, scaling_name_from_index(params.scalingFunction),
                                          params.amount, detail::midtonesFromAmount(params.amount), params.iterations);
    if (process_utils::holdsColourImage(m_image_view_control))
    {
        description += detail::channelHistorySuffix(an::ui::processChannelLabel(selectedChannelScope()).toStdString());
    }
    return an::ui::ProcessStep{.name = getProcessName(), .description = std::move(description)};
}

void DarkStructureEnhanceProcess::executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode == RTPreviewMode::NoPreview && !sequenceMode)
    {
        m_StagedOutputs.clear();
    }
}

void DarkStructureEnhanceProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode != RTPreviewMode::NoPreview || sequenceMode)
    {
        return;
    }
    std::vector<process_utils::StagedOutput> outputs = m_StagedOutputs.take();
    if (outputs.empty() && getParams().createMaskImage)
    {
        notifyWarnMessage("DarkStructureEnhance", "Mask output was not available.");
        return;
    }
    process_utils::openStagedOutputs(std::move(outputs), m_image_view_control);
}

void DarkStructureEnhanceProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void DarkStructureEnhanceProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void DarkStructureEnhanceProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }
}

void DarkStructureEnhanceProcess::enableProcessControlsImpl(const bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    layersControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);
    iterationsControl->setEnabled(enabled);
    m_ui.scalingFunctionComboBox->setEnabled(enabled);
    m_ui.preview->setEnabled(enabled);
    m_ui.previewModeComboBox->setEnabled(enabled && m_ui.preview->isChecked());
    m_ui.createMaskImage->setEnabled(enabled);
}

ProgressDialogControl* DarkStructureEnhanceProcess::getProgressDialogControl(const bool forSequence)
{
    auto* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 0);
    return progress;
}

DarkStructureEnhanceParams DarkStructureEnhanceProcess::getParams() const
{
    DarkStructureEnhanceParams params;
    params.layersToRemove = layersControl->getValue();
    params.scalingFunction = m_ui.scalingFunctionComboBox->currentIndex();
    params.amount = amountControl->getValue();
    params.iterations = iterationsControl->getValue();
    params.previewMode = m_ui.previewModeComboBox->currentIndex() == 1
                             ? DarkStructureEnhancePreviewMode::GeneratedMask
                             : DarkStructureEnhancePreviewMode::ProcessedImage;
    params.createMaskImage = m_ui.createMaskImage->isChecked();
    params.targetName = m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string();
    return params;
}

void DarkStructureEnhanceProcess::initialize()
{
    const QSignalBlocker blockLayersValue(m_ui.layersValue);
    const QSignalBlocker blockLayersSlider(m_ui.layersSlider);
    const QSignalBlocker blockScaling(m_ui.scalingFunctionComboBox);
    const QSignalBlocker blockAmountValue(m_ui.amountValue);
    const QSignalBlocker blockAmountSlider(m_ui.amountSlider);
    const QSignalBlocker blockIterationsValue(m_ui.iterationsValue);
    const QSignalBlocker blockIterationsSlider(m_ui.iterationsSlider);
    const QSignalBlocker blockPreviewMode(m_ui.previewModeComboBox);
    const QSignalBlocker blockMaskOutput(m_ui.createMaskImage);

    layersControl->setValue(8, false);
    m_ui.scalingFunctionComboBox->setCurrentIndex(1);
    amountControl->setValue(0.40);
    iterationsControl->setValue(1, false);
    m_ui.previewModeComboBox->setCurrentIndex(0);
    m_ui.createMaskImage->setChecked(false);
    updateUiState();
}

void DarkStructureEnhanceProcess::updateUiState()
{
    m_ui.previewModeComboBox->setEnabled(m_ui.preview->isChecked());
}
