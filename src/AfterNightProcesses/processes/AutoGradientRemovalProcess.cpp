// Native AfterNight process based on Siril AutoGradientRemoval 1.0.0 by
// Cyril Richard, 2026 (GPL-3.0-or-later).

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "AutoGradientRemovalProcess.h"

#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QStandardItemModel>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

using PreviewDisplayMode = AutoGradientRemovalProcessParams::PreviewDisplayMode;

[[nodiscard]] an::BackgroundCorrection correctionModeFromIndex(const int index)
{
    return index == 1 ? an::BackgroundCorrection::Divide : an::BackgroundCorrection::Subtract;
}

[[nodiscard]] const char* correctionModeName(const an::BackgroundCorrection mode)
{
    return mode == an::BackgroundCorrection::Divide ? "Division" : "Subtraction";
}

[[nodiscard]] const char* modelModeName(const an::AutoBackgroundModel model)
{
    return model == an::AutoBackgroundModel::Polynomial ? "Simplified polynomial" : "Multiscale";
}

[[nodiscard]] PreviewDisplayMode previewModeFromIndex(const int index)
{
    if (index == 1)
    {
        return PreviewDisplayMode::gradient_model;
    }
    if (index == 2)
    {
        return PreviewDisplayMode::protection_mask;
    }
    return PreviewDisplayMode::corrected;
}

void setComboItemEnabled(QComboBox* comboBox, const int index, const bool enabled)
{
    if (!comboBox)
    {
        return;
    }

    if (auto* model = qobject_cast<QStandardItemModel*>(comboBox->model()))
    {
        if (auto* item = model->item(index))
        {
            item->setEnabled(enabled);
        }
    }
}

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Float32 planes of the target's extent and channels, one per channel, which the estimate writes.
struct ModelPlanes
{
    an::Image<float> image;
    std::vector<an::PlaneView<float>> planes;
};

[[nodiscard]] an::Result<ModelPlanes> createModelPlanes(const an::EngineContext& engine,
                                                        const an::ImageDescriptor& described)
{
    const an::Result<an::ImageDescriptor> floatDescribed = an::describeSamplesAs(described, an::SampleType::Float32);
    if (!floatDescribed.hasValue())
    {
        return floatDescribed.status();
    }
    an::Result<an::Image<float>> created =
        an::Image<float>::create(engine, *floatDescribed, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, an::ImageMetadata{});
    if (!created.hasValue())
    {
        return created.status();
    }
    ModelPlanes made{.image = std::move(*created), .planes = {}};
    const std::optional<an::ImageWriteAccess<float>> access = made.image.write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the model planes hold nothing");
    }
    for (std::uint32_t channel = 0u; channel < described.extent().channelCount(); ++channel)
    {
        const std::optional<an::PlaneView<float>> plane = access->view().plane(channel);
        if (!plane.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a model plane holds nothing");
        }
        made.planes.push_back(*plane);
    }
    return made;
}

/// The key a preview of these settings keeps the image of a display under: every setting of the algorithm the images
/// depend on, written so it reads back exactly, and the display. The output policy is not written: the dialog never
/// changes it.
[[nodiscard]] std::string previewKeyOf(const an::AutoBackgroundRequest& algorithm, const PreviewDisplayMode display)
{
    const an::BackgroundApplyRequest& apply = algorithm.apply;
    return fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", static_cast<int>(display),
                       static_cast<int>(algorithm.model), algorithm.scale, algorithm.smoothness, algorithm.downsample,
                       algorithm.polynomialDegree, algorithm.protectStructures, algorithm.protectionThreshold,
                       algorithm.protectionAmount, algorithm.maxIterations, algorithm.convergence,
                       algorithm.lowRejection, algorithm.highRejection, static_cast<int>(apply.correction),
                       static_cast<int>(apply.level), apply.statedLevel, apply.guard, apply.evaluationStride,
                       apply.dither, apply.clamp.has_value(), apply.clamp.has_value() ? apply.clamp->lowest : 0.0,
                       apply.clamp.has_value() ? apply.clamp->highest : 0.0);
}

/// Copies Float32 planes, held to the unit interval, into working samples.
[[nodiscard]] an::Status copyPlanesInto(const an::ExecutionContext& execution, ModelPlanes& planes,
                                        an::SampleType workingType, void* samples, const an::ImageLayout& layout)
{
    const std::optional<an::ImageWriteAccess<float>> access = planes.image.write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the model planes hold nothing");
    }
    const an::ImageLayout planesLayout = access->view().layout();
    if (an::Status held =
            process_utils::scaleAndBiasInPlace(execution, an::SampleType::Float32, access->view().data(), planesLayout,
                                               1.0, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
        held.failed())
    {
        return held;
    }
    an::ConversionCounters counters;
    return an::convertImageSamples(execution, an::SampleType::Float32, access->view().data(), planesLayout, workingType,
                                   samples, layout, an::ConversionPolicy{}, counters);
}

} // namespace

AutoGradientRemovalProcess::AutoGradientRemovalProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<AutoGradientRemovalProcessParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildAutoGradientRemovalProcessUi(this);

    INIT_D_SPINBOX_CTRL(scaleControl, m_ui.scaleValue, m_ui.scaleSlider, &AutoGradientRemovalProcess::on_scaleChanged);
    INIT_D_SPINBOX_CTRL(smoothnessControl, m_ui.smoothnessValue, m_ui.smoothnessSlider,
                        &AutoGradientRemovalProcess::on_smoothnessChanged);
    INIT_D_SPINBOX_CTRL(protectionThresholdControl, m_ui.protectionThresholdValue, m_ui.protectionThresholdSlider,
                        &AutoGradientRemovalProcess::on_protectionThresholdChanged);
    INIT_D_SPINBOX_CTRL(protectionAmountControl, m_ui.protectionAmountValue, m_ui.protectionAmountSlider,
                        &AutoGradientRemovalProcess::on_protectionAmountChanged);
    INIT_SPINBOX_CTRL(polynomialDegreeControl, m_ui.polynomialDegreeValue, m_ui.polynomialDegreeSlider,
                      &AutoGradientRemovalProcess::on_polynomialDegreeChanged);

    QObject::connect(m_ui.downsampleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &AutoGradientRemovalProcess::on_downsampleChanged);
    QObject::connect(m_ui.structureProtection, &QCheckBox::checkStateChanged, this,
                     &AutoGradientRemovalProcess::on_structureProtectionChanged);
    QObject::connect(m_ui.simplifiedModel, &QCheckBox::checkStateChanged, this,
                     &AutoGradientRemovalProcess::on_simplifiedModelChanged);
    QObject::connect(m_ui.correctionModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &AutoGradientRemovalProcess::on_correctionModeChanged);
    QObject::connect(m_ui.previewDisplaySelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &AutoGradientRemovalProcess::on_previewDisplayChanged);
    QObject::connect(m_ui.previewButton, &QPushButton::released, this,
                     &AutoGradientRemovalProcess::on_previewButtonPushed);
    QObject::connect(m_ui.outputGradientModel, &QCheckBox::checkStateChanged, this,
                     &AutoGradientRemovalProcess::on_outputGradientChanged);
    QObject::connect(m_ui.outputProtectionMask, &QCheckBox::checkStateChanged, this,
                     &AutoGradientRemovalProcess::on_outputProtectionMaskChanged);
    QObject::connect(m_ui.previewCheckbox, &QCheckBox::checkStateChanged, this,
                     &AutoGradientRemovalProcess::on_previewCheckboxChanged);

    updateWindowTitle();
    setProcessDescription(tr("Model and remove large-scale gradients with a native port of Siril "
                             "AutoGradientRemoval."));

    initializeStandardProcess(
        m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.previewCheckbox, m_ui.buttonBox, m_ui.targetImageSelector,
        nullptr, {.supportPerChannel = false, .showWaitingSpinner = true, .previewAreaSelectionEnabled = false});

    m_FixedLayout = false;
    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);

    refreshTheme();
    initialize();
    enableProcessControls(true);
}

std::string AutoGradientRemovalProcess::getProcessNameStatic()
{
    return "Auto Gradient Removal";
}

bool AutoGradientRemovalProcess::requiresExclusiveAccessStatic()
{
    return true;
}

AutoGradientRemovalProcess::~AutoGradientRemovalProcess() = default;

void AutoGradientRemovalProcess::on_scaleChanged([[maybe_unused]] const double value)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_smoothnessChanged([[maybe_unused]] const double value)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_downsampleChanged([[maybe_unused]] const int index)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_structureProtectionChanged([[maybe_unused]] const Qt::CheckState state)
{
    invalidatePreview();
    syncDependentUiState(!m_ExecutionInProgress);
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_protectionThresholdChanged([[maybe_unused]] const double value)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_protectionAmountChanged([[maybe_unused]] const double value)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_simplifiedModelChanged([[maybe_unused]] const Qt::CheckState state)
{
    invalidatePreview();
    syncDependentUiState(!m_ExecutionInProgress);
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_polynomialDegreeChanged([[maybe_unused]] const int value)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_correctionModeChanged([[maybe_unused]] const int index)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_previewDisplayChanged([[maybe_unused]] const int index)
{
    // A preview computed for these settings kept every image the display offers, so the one asked for shows at once,
    // with no new run; one not computed shows the target.
    if (isPreviewEnabled())
    {
        schedulePreviewProcessExecution();
    }
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_previewButtonPushed()
{
    if (m_ExecutionInProgress || !isPreviewEnabled() || m_previewComputed)
    {
        return;
    }
    if (!m_rt_preview_view_control)
    {
        createPreviewControl();
    }
    m_previewComputed = true;
    syncPreviewUiState(!m_ExecutionInProgress);
    schedulePreviewProcessExecution();
}

void AutoGradientRemovalProcess::on_outputGradientChanged([[maybe_unused]] const Qt::CheckState state)
{
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_outputProtectionMaskChanged([[maybe_unused]] const Qt::CheckState state)
{
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::on_previewCheckboxChanged([[maybe_unused]] const Qt::CheckState state)
{
    m_previewEnabled = m_ui.previewCheckbox->isChecked();
    syncPreviewUiState(!m_ExecutionInProgress);
    settingsUpdatedImpl();
}

void AutoGradientRemovalProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    invalidatePreview();
    if (m_rt_preview_view_control)
    {
        schedulePreviewProcessExecution();
    }
}

bool AutoGradientRemovalProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return ctrl && !ctrl->isSequence();
}

void AutoGradientRemovalProcess::initialize()
{
    const QSignalBlocker blockScaleValue(m_ui.scaleValue);
    const QSignalBlocker blockScaleSlider(m_ui.scaleSlider);
    const QSignalBlocker blockSmoothnessValue(m_ui.smoothnessValue);
    const QSignalBlocker blockSmoothnessSlider(m_ui.smoothnessSlider);
    const QSignalBlocker blockDownsample(m_ui.downsampleComboBox);
    const QSignalBlocker blockStructureProtection(m_ui.structureProtection);
    const QSignalBlocker blockThresholdValue(m_ui.protectionThresholdValue);
    const QSignalBlocker blockThresholdSlider(m_ui.protectionThresholdSlider);
    const QSignalBlocker blockAmountValue(m_ui.protectionAmountValue);
    const QSignalBlocker blockAmountSlider(m_ui.protectionAmountSlider);
    const QSignalBlocker blockSimplified(m_ui.simplifiedModel);
    const QSignalBlocker blockDegreeValue(m_ui.polynomialDegreeValue);
    const QSignalBlocker blockDegreeSlider(m_ui.polynomialDegreeSlider);
    const QSignalBlocker blockCorrectionMode(m_ui.correctionModeComboBox);
    const QSignalBlocker blockPreviewDisplay(m_ui.previewDisplaySelector);
    const QSignalBlocker blockOutputGradient(m_ui.outputGradientModel);
    const QSignalBlocker blockOutputProtection(m_ui.outputProtectionMask);

    scaleControl->setValue(5.0);
    smoothnessControl->setValue(1.0);
    m_ui.downsampleComboBox->setCurrentText(QStringLiteral("4"));
    m_ui.structureProtection->setChecked(true);
    protectionThresholdControl->setValue(0.05);
    protectionAmountControl->setValue(0.5);
    m_ui.simplifiedModel->setChecked(false);
    polynomialDegreeControl->setValue(2, false);
    m_ui.correctionModeComboBox->setCurrentIndex(0);
    m_ui.previewDisplaySelector->setCurrentIndex(0);
    m_ui.outputGradientModel->setChecked(false);
    m_ui.outputProtectionMask->setChecked(false);

    invalidatePreview();
    syncDependentUiState(!m_ExecutionInProgress);
    syncPreviewUiState(!m_ExecutionInProgress);

    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }
}

void AutoGradientRemovalProcess::invalidatePreview()
{
    m_previewComputed = false;
    // The images the last preview kept were made with the settings that changed.
    discardKeptPreviews();
    syncPreviewUiState(!m_ExecutionInProgress);
}

void AutoGradientRemovalProcess::syncDependentUiState(const bool controlsEnabled)
{
    const bool protectionEnabled = m_ui.structureProtection->isChecked();
    const bool simplifiedEnabled = m_ui.simplifiedModel->isChecked();
    const auto state = autoGradientRemovalDependentUiState(controlsEnabled, protectionEnabled, simplifiedEnabled);

    protectionThresholdControl->setEnabled(state.protectionParametersEnabled);
    protectionAmountControl->setEnabled(state.protectionParametersEnabled);
    m_ui.outputProtectionMask->setEnabled(state.protectionMaskOutputEnabled);
    setComboItemEnabled(m_ui.previewDisplaySelector, 2, state.protectionMaskPreviewEnabled);

    if (!protectionEnabled)
    {
        if (m_ui.outputProtectionMask->isChecked())
        {
            m_ui.outputProtectionMask->setChecked(false);
        }
        if (m_ui.previewDisplaySelector->currentIndex() == 2)
        {
            m_ui.previewDisplaySelector->setCurrentIndex(0);
        }
    }

    polynomialDegreeControl->setEnabled(state.polynomialDegreeEnabled);
}

void AutoGradientRemovalProcess::syncPreviewUiState(const bool controlsEnabled)
{
    const bool previewControlsEnabled = controlsEnabled && isPreviewEnabled();
    m_ui.previewDisplaySelector->setEnabled(previewControlsEnabled);
    m_ui.previewButton->setEnabled(previewControlsEnabled && !m_previewComputed);
}

bool AutoGradientRemovalProcess::isPreviewEnabled() const
{
    return m_previewEnabled && m_ui.previewCheckbox && m_ui.previewCheckbox->isChecked();
}

void AutoGradientRemovalProcess::enablePreview(const bool enabled)
{
    m_previewEnabled = enabled;
    if (m_ui.previewCheckbox)
    {
        const QSignalBlocker blockPreview(m_ui.previewCheckbox);
        m_ui.previewCheckbox->setChecked(enabled);
    }
    syncPreviewUiState(!m_ExecutionInProgress);
}

void AutoGradientRemovalProcess::previewControlClosed()
{
    invalidatePreview();
}

AutoGradientRemovalProcessParams AutoGradientRemovalProcess::getParams() const
{
    AutoGradientRemovalProcessParams params;
    params.algorithm.scale = scaleControl->getValue();
    params.algorithm.smoothness = smoothnessControl->getValue();
    params.algorithm.downsample =
        static_cast<std::uint32_t>((std::max)(m_ui.downsampleComboBox->currentText().toInt(), 1));
    params.algorithm.protectStructures = m_ui.structureProtection->isChecked();
    params.algorithm.protectionThreshold = protectionThresholdControl->getValue();
    params.algorithm.protectionAmount = protectionAmountControl->getValue();
    params.algorithm.model = m_ui.simplifiedModel->isChecked() ? an::AutoBackgroundModel::Polynomial
                                                               : an::AutoBackgroundModel::InpaintedLowpass;
    params.algorithm.polynomialDegree = static_cast<std::uint32_t>(polynomialDegreeControl->getValue());
    // Corrected in the unit interval the samples are read in, around the model's own median, and held to it.
    params.algorithm.apply.correction = correctionModeFromIndex(m_ui.correctionModeComboBox->currentIndex());
    params.algorithm.apply.level = an::BackgroundLevel::ModelMedian;
    params.algorithm.apply.clamp = an::KernelValueWindow{.lowest = 0.0, .highest = 1.0};
    params.previewDisplay = previewModeFromIndex(m_ui.previewDisplaySelector->currentIndex());
    params.previewComputed = m_previewComputed;
    params.outputGradientModel = m_ui.outputGradientModel->isChecked();
    params.outputProtectionMask = m_ui.outputProtectionMask->isChecked() && params.algorithm.protectStructures;
    params.targetName = m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string();
    return params;
}

an::Status AutoGradientRemovalProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                               an::ui::ProcessDestination& destination,
                                                               const an::ui::ProcessRun& run,
                                                               const AutoGradientRemovalProcessParams& params,
                                                               an::ui::ExecutionScope& scope)
{
    if (run.isForSequence())
    {
        return refusal(an::ErrorCode::Unsupported, "Auto Gradient Removal works on a still image, not a sequence");
    }
    // Until its button asks, the preview shows the target.
    if (run.isForPreview() && !params.previewComputed)
    {
        return an::Status::success();
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::EngineContext& engine = scope.engine();
    const an::ExecutionContext& execution = scope.execution();

    const an::Result<an::AutoBackgroundPlan> plan =
        an::planAutoBackground(params.algorithm, described->extent().width(), described->extent().height());
    if (!plan.hasValue())
    {
        return plan.status();
    }

    // The image a preview shows goes to the destination. A preview frame also makes the others, as the destination was
    // made, and keeps them beside it under the key of the display that shows each; a snapshot of the preview makes the
    // one it shows. With no protection the mask is not offered, and the model is shown in its place.
    const bool preview = run.isForPreview();
    const bool besideShown = run.kind == an::ui::ProcessRunKind::Preview;
    const bool protecting = params.algorithm.protectStructures;
    PreviewDisplayMode shown = preview ? params.previewDisplay : PreviewDisplayMode::corrected;
    if (shown == PreviewDisplayMode::protection_mask && !protecting)
    {
        shown = PreviewDisplayMode::gradient_model;
    }

    // What the one fit draws besides the correction: the images a preview makes, or the outputs an Apply opens.
    const bool keepModel = !preview && params.outputGradientModel;
    const bool keepProtection = !preview && params.outputProtectionMask && protecting;
    const bool drawModel = keepModel || (preview && (besideShown || shown == PreviewDisplayMode::gradient_model));
    const bool drawProtection =
        keepProtection || (preview && protecting && (besideShown || shown == PreviewDisplayMode::protection_mask));
    std::optional<ModelPlanes> models;
    std::optional<ModelPlanes> protection;
    if (drawModel)
    {
        an::Result<ModelPlanes> madeModels = createModelPlanes(engine, *described);
        if (!madeModels.hasValue())
        {
            return madeModels.status();
        }
        models.emplace(std::move(*madeModels));
    }
    if (drawProtection)
    {
        an::Result<ModelPlanes> madeProtection = createModelPlanes(engine, *described);
        if (!madeProtection.hasValue())
        {
            return madeProtection.status();
        }
        protection.emplace(std::move(*madeProtection));
    }

    const auto madeBeside = [&]() {
        return an::ui::ProcessDestination::forBody(scope.session(), engine, execution, source, run.coverage);
    };
    // A corrected image not shown is corrected beside the destination; one no frame keeps is corrected in the
    // destination, which the image shown then overwrites.
    std::optional<an::ui::ProcessDestination> correctedBeside;
    if (shown != PreviewDisplayMode::corrected && besideShown)
    {
        an::Result<an::ui::ProcessDestination> made = madeBeside();
        if (!made.hasValue())
        {
            return made.status();
        }
        correctedBeside.emplace(std::move(*made));
    }

    // The correction, in place, with the model and the protection drawn from the fit it reads.
    {
        an::Result<process_utils::NormalizedSamples> samples = process_utils::openNormalizedDestination(
            scope, source, correctedBeside.has_value() ? *correctedBeside : destination);
        if (!samples.hasValue())
        {
            return samples.status();
        }
        const an::KernelValidityView validity = process_utils::skippingNonFinite(samples->validity());
        const an::SpatialSelection selection{.validity = validity.descriptor, .mask = validity.mask};
        // A sample the validity rejects is left as it was.
        const an::Result<an::AutoBackgroundResult> removed = an::removeAndDrawImageAutoBackground(
            execution, samples->sampleType(), samples->samples(), samples->layout(), samples->samples(),
            samples->layout(), *plan,
            models.has_value() ? std::span<const an::PlaneView<float>>(models->planes)
                               : std::span<const an::PlaneView<float>>{},
            protection.has_value() ? std::span<const an::PlaneView<float>>(protection->planes)
                                   : std::span<const an::PlaneView<float>>{},
            selection);
        if (!removed.hasValue())
        {
            return removed.status();
        }
        if (an::Status committed = samples->commit(execution); committed.failed())
        {
            return committed;
        }
    }

    if (preview)
    {
        // Planes held to the unit interval, written into an image made as the destination was.
        const auto drawInto = [&](ModelPlanes& planes, an::ui::ProcessDestination& into) -> an::Status {
            an::Result<process_utils::NormalizedSamples> samples =
                process_utils::openNormalizedDestination(scope, source, into);
            if (!samples.hasValue())
            {
                return samples.status();
            }
            if (an::Status copied =
                    copyPlanesInto(execution, planes, samples->sampleType(), samples->samples(), samples->layout());
                copied.failed())
            {
                return copied;
            }
            return samples->commit(execution);
        };
        const auto keepBeside = [&](an::ui::ProcessDestination& made, const PreviewDisplayMode display) -> an::Status {
            an::Result<an::consumer::ImageAsset> image = made.release();
            if (!image.hasValue())
            {
                return image.status();
            }
            return destination.keepAlternative(previewKeyOf(params.algorithm, display), std::move(*image));
        };
        const auto drawShownOrBeside = [&](ModelPlanes& planes, const PreviewDisplayMode display) -> an::Status {
            if (shown == display)
            {
                return drawInto(planes, destination);
            }
            if (!besideShown)
            {
                return an::Status::success();
            }
            an::Result<an::ui::ProcessDestination> made = madeBeside();
            if (!made.hasValue())
            {
                return made.status();
            }
            if (an::Status drawn = drawInto(planes, *made); drawn.failed())
            {
                return drawn;
            }
            return keepBeside(*made, display);
        };
        if (models.has_value())
        {
            if (an::Status drawn = drawShownOrBeside(*models, PreviewDisplayMode::gradient_model); drawn.failed())
            {
                return drawn;
            }
            models.reset();
        }
        if (protection.has_value())
        {
            if (an::Status drawn = drawShownOrBeside(*protection, PreviewDisplayMode::protection_mask); drawn.failed())
            {
                return drawn;
            }
            protection.reset();
        }
        if (correctedBeside.has_value())
        {
            return keepBeside(*correctedBeside, PreviewDisplayMode::corrected);
        }
        return an::Status::success();
    }

    // The outputs an Apply keeps, each an image of the target's kind holding the unit interval.
    const QString target = QString::fromStdString(params.targetName);
    auto stage = [&](ModelPlanes& planes, const char* operation, const QString& name,
                     const std::string& history) -> an::Status {
        an::Result<an::ImageMetadata> metadata =
            process_utils::derivedImageMetadata(source.metadata(), *described, operation);
        if (!metadata.hasValue())
        {
            return metadata.status();
        }
        an::Result<an::consumer::ImageAsset> image =
            process_utils::createDerivedImage(scope, *described, std::move(*metadata));
        if (!image.hasValue())
        {
            return image.status();
        }
        {
            an::Result<an::consumer::AssetWriteLease> lease = image->write();
            if (!lease.hasValue())
            {
                return lease.status();
            }
            const an::Status written = lease->visitWrite([&](auto&& access) -> an::Status {
                const an::ImageLayout layout = access.view().layout();
                an::Result<process_utils::NormalizedSamples> output = process_utils::NormalizedSamples::open(
                    engine, execution, *described, access.view().data(), layout, an::KernelValidityView{});
                if (!output.hasValue())
                {
                    return output.status();
                }
                if (an::Status copied =
                        copyPlanesInto(execution, planes, output->sampleType(), output->samples(), output->layout());
                    copied.failed())
                {
                    return copied;
                }
                return output->commit(execution);
            });
            if (written.failed())
            {
                return written;
            }
        }
        m_StagedOutputs.stage(
            process_utils::StagedOutput{.asset = std::move(*image), .name = name, .history = history});
        return an::Status::success();
    };
    if (keepModel)
    {
        if (an::Status staged =
                stage(*models, "auto-gradient-removal-gradient", target + QStringLiteral(" - AGR Gradient"),
                      fmt::format("AutoGradientRemoval: gradient model created from '{}'", params.targetName));
            staged.failed())
        {
            return staged;
        }
    }
    if (keepProtection)
    {
        if (an::Status staged = stage(
                *protection, "auto-gradient-removal-protection-mask", target + QStringLiteral(" - AGR Protection Mask"),
                fmt::format("AutoGradientRemoval: protection mask created from '{}'", params.targetName));
            staged.failed())
        {
            return staged;
        }
    }
    // A frame too small for the factor asked for was worked at a smaller one, which the run warns of.
    if (plan->downsample() != params.algorithm.downsample)
    {
        const std::lock_guard lock(m_warningMutex);
        m_pendingWarningText = tr("The image is too small to be downsampled by %1; it was downsampled by %2.")
                                   .arg(params.algorithm.downsample)
                                   .arg(plan->downsample());
    }

    an::Result<an::ProvenanceEntry> entry =
        an::ProvenanceEntry::create("an::removeImageAutoBackground", describeTypedProcessStep(params).description);
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return destination.asset().transition(an::MetadataTransition{.record = *entry});
}

std::string AutoGradientRemovalProcess::typedPreviewResultKey(const AutoGradientRemovalProcessParams& params) const
{
    // A preview not computed shows the target, which nothing keeps.
    return params.previewComputed ? previewKeyOf(params.algorithm, params.previewDisplay) : std::string();
}

an::ui::ProcessStep
AutoGradientRemovalProcess::describeTypedProcessStep(const AutoGradientRemovalProcessParams& params) const
{
    const an::AutoBackgroundRequest& algorithm = params.algorithm;
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format(
            "Scale: {:.2f}, Smoothness: {:.2f}, Downsample: {}, Correction: {}, Model: {}{}, Structure "
            "protection: {}{}",
            algorithm.scale, algorithm.smoothness, algorithm.downsample, correctionModeName(algorithm.apply.correction),
            modelModeName(algorithm.model),
            algorithm.model == an::AutoBackgroundModel::Polynomial
                ? fmt::format(", Degree: {}", algorithm.polynomialDegree)
                : "",
            algorithm.protectStructures ? "enabled" : "disabled",
            algorithm.protectStructures ? fmt::format(" (threshold: {:.3f}, amount: {:.2f})",
                                                      algorithm.protectionThreshold, algorithm.protectionAmount)
                                        : "")};
}

void AutoGradientRemovalProcess::executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode == RTPreviewMode::NoPreview && !sequenceMode)
    {
        m_StagedOutputs.clear();
        const std::lock_guard lock(m_warningMutex);
        m_pendingWarningText.clear();
    }
}

void AutoGradientRemovalProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode != RTPreviewMode::NoPreview || sequenceMode)
    {
        return;
    }
    process_utils::openStagedOutputs(m_StagedOutputs.take(), m_image_view_control);
    QString warning;
    {
        const std::lock_guard lock(m_warningMutex);
        warning = std::move(m_pendingWarningText);
        m_pendingWarningText.clear();
    }
    if (!warning.isEmpty())
    {
        notifyWarnMessage(tr("Auto Gradient Removal"), warning);
    }
}

void AutoGradientRemovalProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(!m_ExecutionInProgress);
    }
    syncDependentUiState(!m_ExecutionInProgress);
    syncPreviewUiState(!m_ExecutionInProgress);
}

void AutoGradientRemovalProcess::enableProcessControlsImpl(const bool enabled)
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(enabled);
    }
    if (auto* closeButton = m_ui.buttonBox->button(QDialogButtonBox::Close))
    {
        closeButton->setEnabled(enabled);
    }
    if (auto* resetButton = m_ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        resetButton->setEnabled(enabled);
    }

    scaleControl->setEnabled(enabled);
    smoothnessControl->setEnabled(enabled);
    m_ui.downsampleComboBox->setEnabled(enabled);
    m_ui.structureProtection->setEnabled(enabled);
    m_ui.simplifiedModel->setEnabled(enabled);
    m_ui.correctionModeComboBox->setEnabled(enabled);
    m_ui.outputGradientModel->setEnabled(enabled);
    m_ui.previewCheckbox->setEnabled(enabled);
    m_ui.targetImageSelector->setEnabled(enabled);
    syncDependentUiState(enabled);
    syncPreviewUiState(enabled);
}

void AutoGradientRemovalProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    initialize();
    if (m_rt_preview_view_control)
    {
        schedulePreviewProcessExecution();
    }
}

void AutoGradientRemovalProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

ProgressDialogControl* AutoGradientRemovalProcess::getProgressDialogControl(const bool forSequence)
{
    auto* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
    progress->setProgressBarRange(0, 100);
    return progress;
}
