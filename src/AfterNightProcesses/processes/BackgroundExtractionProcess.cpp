// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BackgroundExtractionProcess.h"
#include "controls/IAppContext.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
[[nodiscard]] an::BackgroundCorrection getBackgroundCorrectionTypeFromStr(const QString& str)
{
    if (str == "Division")
    {
        return an::BackgroundCorrection::Divide;
    }

    return an::BackgroundCorrection::Subtract;
}

[[nodiscard]] an::BackgroundModelKind getBackgroundInterpolationTypeFromStr(const QString& str)
{
    if (str == "Polynomial")
    {
        return an::BackgroundModelKind::Polynomial;
    }

    return an::BackgroundModelKind::RadialBasis;
}

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The session every Controls operation runs in, or null when the application has none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

/// How a sample's box is measured: over `kSampleExtent` pixels a side, every channel, whatever its median.
[[nodiscard]] an::BackgroundSampleRequest measurementRequest()
{
    an::BackgroundSampleRequest request;
    request.extent = an::ui::processes::background_extraction_detail::kSampleExtent;
    request.luminance = an::BackgroundLuminance::FirstChannel;
    request.rejectNonPositive = false;
    return request;
}

/// The fitted background, one Float32 plane per channel.
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
        return refusal(an::ErrorCode::NotInitialized, "the background holds nothing");
    }
    for (std::uint32_t channel = 0u; channel < described.extent().channelCount(); ++channel)
    {
        const std::optional<an::PlaneView<float>> plane = access->view().plane(channel);
        if (!plane.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a plane of the background holds nothing");
        }
        made.planes.push_back(*plane);
    }
    return made;
}

/// Copies the background, held to the unit interval, into working samples.
[[nodiscard]] an::Status copyPlanesInto(const an::ExecutionContext& execution, ModelPlanes& planes,
                                        an::SampleType workingType, void* samples, const an::ImageLayout& layout)
{
    const std::optional<an::ImageWriteAccess<float>> access = planes.image.write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the background holds nothing");
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

namespace an::ui::processes::background_extraction_detail
{

bool sampleFits(const BackgroundSamplePosition& position, std::uint32_t width, std::uint32_t height) noexcept
{
    const std::uint32_t half = kSampleExtent / 2u;
    return width >= kSampleExtent && height >= kSampleExtent && position.x >= half && position.y >= half &&
           position.x + half < width && position.y + half < height;
}

std::optional<std::size_t> nearestSample(const std::vector<BackgroundSamplePosition>& samples, double x, double y)
{
    std::optional<std::size_t> nearest;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const double dx = static_cast<double>(samples[index].x) - x;
        const double dy = static_cast<double>(samples[index].y) - y;
        const double distance = dx * dx + dy * dy;
        if (distance < best)
        {
            best = distance;
            nearest = index;
        }
    }
    return nearest;
}

an::Result<std::vector<BackgroundSamplePosition>> generateSamples(const an::ui::ExecutionScope& scope,
                                                                  const an::ui::ProcessSource& source,
                                                                  int samplesPerLine, double tolerance)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }

    // The grid is placed over the colour image's CIE lightness, or over the grey image itself, in fractions of full
    // scale.
    std::optional<an::consumer::ImageAsset> lightness;
    std::optional<an::consumer::AssetReadLease> lightnessLease;
    an::Result<process_utils::NormalizedCopy> placedOver = refusal(an::ErrorCode::NotInitialized, "nothing was read");
    if (process_utils::describesColourImage(*described))
    {
        an::Result<an::consumer::ImageAsset> extracted = process_utils::extractLightness(scope, source);
        if (!extracted.hasValue())
        {
            return extracted.status();
        }
        lightness.emplace(std::move(*extracted));
        an::Result<an::consumer::AssetReadLease> lease = lightness->read();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        lightnessLease.emplace(std::move(*lease));
        placedOver = process_utils::normalizedCopyOf(scope, an::ui::ProcessSource::of(*lightnessLease));
    }
    else
    {
        placedOver = process_utils::normalizedCopyOf(scope, source);
    }
    if (!placedOver.hasValue())
    {
        return placedOver.status();
    }
    const process_utils::NormalizedSamples& grey = placedOver->samples;
    const an::ImageLayout& layout = grey.layout();

    an::BackgroundSampleRequest request = measurementRequest();
    request.samplesPerLine = static_cast<std::uint32_t>((std::max)(samplesPerLine, 2));
    request.tolerance = tolerance;
    request.rejectNonPositive = true;
    const an::Result<an::BackgroundSampleGrid> grid =
        an::planBackgroundSampleGrid(request, layout.extent().width(), layout.extent().height());
    if (!grid.hasValue())
    {
        return grid.status();
    }
    std::vector<an::BackgroundSample> sites(grid->siteCount());
    const an::Result<an::BackgroundSampleResult> sampled =
        an::sampleImageBackground(scope.execution(), grey.sampleType(), grey.samples(), layout, request, sites);
    if (!sampled.hasValue())
    {
        return sampled.status();
    }
    std::vector<BackgroundSamplePosition> accepted;
    for (std::uint32_t index = 0u; index < sampled->acceptedCount; ++index)
    {
        accepted.push_back(BackgroundSamplePosition{.x = static_cast<std::uint32_t>(sites[index].x),
                                                    .y = static_cast<std::uint32_t>(sites[index].y)});
    }
    return accepted;
}

} // namespace an::ui::processes::background_extraction_detail

namespace detail = an::ui::processes::background_extraction_detail;

BackgroundExtractionProcess::BackgroundExtractionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<BackgroundExtractionParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildBackgroundExtractionProcessUi(this);

    QObject::connect(m_ui.generateSamplesButton, &QPushButton::released, this,
                     &BackgroundExtractionProcess::on_generateSamplesButtonPushed);
    QObject::connect(m_ui.clearSamplesButton, &QPushButton::released, this,
                     &BackgroundExtractionProcess::on_clearSamplesButtonPushed);
    QObject::connect(m_ui.previewButton, &QPushButton::released, this,
                     &BackgroundExtractionProcess::on_previewButtonPushed);
    QObject::connect(m_ui.previewCheckbox, &QCheckBox::checkStateChanged, this,
                     &BackgroundExtractionProcess::on_previewCheckboxChanged);
    QObject::connect(m_ui.interpolationMethod, &QComboBox::currentTextChanged, this,
                     &BackgroundExtractionProcess::on_interpolationMethod_currentTextChanged);
    QObject::connect(m_ui.correctionMode, &QComboBox::currentTextChanged, this,
                     &BackgroundExtractionProcess::on_correctionMode_currentTextChanged);
    QObject::connect(m_ui.previewDisplaySelector, &QComboBox::currentTextChanged, this,
                     &BackgroundExtractionProcess::on_previewDisplay_currentTextChanged);

    INIT_SPINBOX_CTRL(samplesPerLineControl, m_ui.samplesPerLineValue, m_ui.samplesPerLineSlider,
                      &BackgroundExtractionProcess::on_samplesPerLineChanged);
    INIT_D_SPINBOX_CTRL(gridToleranceControl, m_ui.gridToleranceValue, m_ui.gridToleranceSlider,
                        &BackgroundExtractionProcess::on_gridToleranceChanged);
    INIT_D_SPINBOX_CTRL(smoothingControl, m_ui.smoothingValue, m_ui.smoothingSlider,
                        &BackgroundExtractionProcess::on_smoothingChanged);

    m_ui.interpolationMethod->addItem("Polynomial");
    m_ui.interpolationMethod->addItem("RBF");

    m_ui.correctionMode->addItem("Subtraction");
    m_ui.correctionMode->addItem("Division");

    updateWindowTitle();
    setProcessDescription(QStringLiteral(
        "Model and remove large-scale gradients using sampled background points and interpolation controls."));

    initializeStandardProcess(
        m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.previewCheckbox, m_ui.buttonBox, m_ui.targetImageSelector,
        nullptr, {.supportPerChannel = false, .showWaitingSpinner = false, .previewAreaSelectionEnabled = false});

    m_FixedLayout = false;
    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);

    refreshTheme();

    initialize();
    enableProcessControls(true);
}

std::string BackgroundExtractionProcess::getProcessNameStatic()
{
    return "Background Extraction";
}

bool BackgroundExtractionProcess::requiresExclusiveAccessStatic()
{
    return true;
}

size_t BackgroundExtractionProcess::getSampleRadius() const
{
    return detail::kSampleExtent;
}

void BackgroundExtractionProcess::addSampleSelection(const ImageSampleSelection& selection)
{
    const std::optional<an::ImageDescriptor> described =
        m_image_view_control != nullptr ? m_image_view_control->imageDescriptor() : std::nullopt;
    const BackgroundSamplePosition position{.x = static_cast<std::uint32_t>((std::max)(selection.pos.x(), 0)),
                                            .y = static_cast<std::uint32_t>((std::max)(selection.pos.y(), 0))};
    // A sample whose box leaves the image measures nothing, and is not kept.
    if (described.has_value() && selection.pos.x() >= 0 && selection.pos.y() >= 0 &&
        detail::sampleFits(position, described->extent().width(), described->extent().height()))
    {
        m_samples.push_back(position);
    }
    else
    {
        syncSampleSelectionOverlay();
    }
    invalidatePreview();
    updateActionButtons();
}

void BackgroundExtractionProcess::removeSampleSelection(const ImageSampleSelection& selection)
{
    if (const std::optional<std::size_t> nearest = detail::nearestSample(
            m_samples, static_cast<double>(selection.pos.x()), static_cast<double>(selection.pos.y()));
        nearest.has_value())
    {
        m_samples.erase(m_samples.begin() + static_cast<std::ptrdiff_t>(*nearest));
    }
    invalidatePreview();
    updateActionButtons();
}

void BackgroundExtractionProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);

    clearSamples();
    if (m_rt_preview_view_control)
    {
        schedulePreviewProcessExecution();
    }
}

const std::vector<BackgroundSamplePosition>& BackgroundExtractionProcess::samplePositions() const noexcept
{
    return m_samples;
}

bool BackgroundExtractionProcess::isGeneratingSamples() const noexcept
{
    return m_generating;
}

BackgroundExtractionProcess::~BackgroundExtractionProcess() = default;

void BackgroundExtractionProcess::on_samplesPerLineChanged(int value)
{
    settingsUpdatedImpl();
}

void BackgroundExtractionProcess::on_gridToleranceChanged(double value)
{
    settingsUpdatedImpl();
}

void BackgroundExtractionProcess::on_smoothingChanged(double value)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void BackgroundExtractionProcess::on_generateSamplesButtonPushed()
{
    const an::consumer::EngineSession* session = currentSession();
    const std::shared_ptr<const an::consumer::ImageAsset> target =
        m_image_view_control != nullptr && m_image_view_control->hasImage() ? m_image_view_control->document().asset()
                                                                            : nullptr;
    if (session == nullptr || !target)
    {
        return;
    }

    // The grid is placed on a worker and lands here, unless another request, a cleared list or another target came
    // first.
    const std::uint64_t generation = ++m_generateGeneration;
    m_generating = true;
    updateActionButtons(!m_ExecutionInProgress);
    const int samplesPerLine = samplesPerLineControl->getValue();
    const double tolerance = gridToleranceControl->getValue();
    QPointer<BackgroundExtractionProcess> guard(this);
    // The window's stop reaches the lease and the operation: at the application's exit the grid ends at its next stop
    // point rather than being waited for.
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, target, samplesPerLine, tolerance, generation, guard, stop]() {
        auto place = [&]() -> an::Result<std::vector<BackgroundSamplePosition>> {
            // The target is leased before the grid asks to be admitted: an edit of the view holds its writer while it
            // waits to be admitted.
            an::Result<an::consumer::AssetReadLease> lease = target->read(stop);
            if (!lease.hasValue())
            {
                return lease.status();
            }
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session, an::ui::ExecutionRequest{.stopToken = stop}, an::ui::ExecutionObserver{});
            if (!scope.hasValue())
            {
                return scope.status();
            }
            an::Result<std::vector<BackgroundSamplePosition>> placed =
                detail::generateSamples(*scope, an::ui::ProcessSource::of(*lease), samplesPerLine, tolerance);
            scope->finish(placed.hasValue() ? an::Status::success() : placed.status());
            return placed;
        };
        auto placed = std::make_shared<an::Result<std::vector<BackgroundSamplePosition>>>(place());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, generation, placed]() {
                if (guard)
                {
                    guard->landGeneratedSamples(generation, std::move(*placed));
                }
            },
            Qt::QueuedConnection);
    });
}

void BackgroundExtractionProcess::landGeneratedSamples(std::uint64_t generation,
                                                       an::Result<std::vector<BackgroundSamplePosition>>&& samples)
{
    if (generation != m_generateGeneration)
    {
        return;
    }
    m_generating = false;
    if (!samples.hasValue())
    {
        notifyErrorMessageAsync(QString::fromStdString(getProcessNameStatic()),
                                QString::fromUtf8(samples.status().message()));
        updateActionButtons(!m_ExecutionInProgress);
        return;
    }
    m_samples = std::move(*samples);
    syncSampleSelectionOverlay();
    invalidatePreview();
    updateActionButtons(!m_ExecutionInProgress);
}

void BackgroundExtractionProcess::on_clearSamplesButtonPushed()
{
    clearSamples();
}

void BackgroundExtractionProcess::on_previewButtonPushed()
{
    if (m_ExecutionInProgress || !m_previewEnabled || m_samples.empty())
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

void BackgroundExtractionProcess::on_previewCheckboxChanged([[maybe_unused]] Qt::CheckState state)
{
    syncPreviewUiState(!m_ExecutionInProgress);
    updateActionButtons(!m_ExecutionInProgress);
}

void BackgroundExtractionProcess::on_interpolationMethod_currentTextChanged(const QString& text)
{
    if (text == "Polynomial")
    {
        m_ui.degreeOrder->setEnabled(true);
        smoothingControl->setEnabled(false);
    }
    else
    {
        m_ui.degreeOrder->setEnabled(false);
        smoothingControl->setEnabled(true);
    }

    invalidatePreview();
    settingsUpdatedImpl();
}

void BackgroundExtractionProcess::on_correctionMode_currentTextChanged(const QString& text)
{
    invalidatePreview();
    settingsUpdatedImpl();
}

void BackgroundExtractionProcess::on_previewDisplay_currentTextChanged(const QString& text)
{
    // A preview already computed for these settings shows the other image at once.
    if (m_previewComputed && m_rt_preview_view_control)
    {
        schedulePreviewProcessExecution();
    }
    settingsUpdatedImpl();
}

void BackgroundExtractionProcess::initialize()
{
    samplesPerLineControl->setValue(20);
    gridToleranceControl->setValue(2.0);
    smoothingControl->setValue(0.5);

    m_ui.addDither->setChecked(false);
    m_ui.outputBackgroundModel->setChecked(false);
    m_ui.previewDisplaySelector->setCurrentIndex(0);

    m_ui.interpolationMethod->setCurrentText("RBF");
    m_ui.correctionMode->setCurrentText("Subtraction");

    m_ui.clearSamplesButton->setEnabled(false);
    m_ui.degreeOrder->setEnabled(false);
    m_ui.previewButton->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    invalidatePreview();
}

void BackgroundExtractionProcess::clearSamples()
{
    m_samples.clear();
    ++m_generateGeneration;
    m_generating = false;
    if (m_sampleSelectionController)
    {
        m_sampleSelectionController->resetSampleSelection();
    }

    invalidatePreview();
    updateActionButtons();
}

void BackgroundExtractionProcess::syncSampleSelectionOverlay()
{
    if (!m_sampleSelectionController)
    {
        return;
    }

    auto sampleSelection = std::make_shared<ImageSampleSelectionList>();
    for (const BackgroundSamplePosition& sample : m_samples)
    {
        ImageSampleSelection dstSample;
        dstSample.pos.setX(static_cast<int>(sample.x));
        dstSample.pos.setY(static_cast<int>(sample.y));
        dstSample.size = detail::kSampleExtent;
        sampleSelection->push_back(std::move(dstSample));
    }

    m_sampleSelectionController->updateSampleSelection(sampleSelection);
}

void BackgroundExtractionProcess::updateActionButtons(bool controlsEnabled)
{
    const bool hasSamples = !m_samples.empty();
    const bool previewControlsEnabled = controlsEnabled && m_previewEnabled;

    m_ui.generateSamplesButton->setEnabled(controlsEnabled && !m_generating);
    m_ui.clearSamplesButton->setEnabled(controlsEnabled && hasSamples);
    m_ui.previewButton->setEnabled(previewControlsEnabled && hasSamples);

    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(controlsEnabled && hasSamples);
    }
}

void BackgroundExtractionProcess::invalidatePreview()
{
    m_previewComputed = false;
    if (m_ui.previewButton != nullptr)
    {
        updateActionButtons(!m_ExecutionInProgress);
    }
}

void BackgroundExtractionProcess::syncPreviewUiState(const bool controlsEnabled)
{
    m_ui.previewDisplaySelector->setEnabled(controlsEnabled && m_previewEnabled);
}

void BackgroundExtractionProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

bool BackgroundExtractionProcess::isPreviewEnabled() const
{
    return m_previewEnabled;
}

void BackgroundExtractionProcess::enablePreview(bool enabled)
{
    m_previewEnabled = enabled;
}

void BackgroundExtractionProcess::previewControlStarted()
{
    if (m_sampleSelectionController)
    {
        m_sampleSelectionController->destroy();
        m_sampleSelectionController.reset();
    }

    if (m_rt_preview_view_control)
    {
        m_sampleSelectionController =
            std::make_unique<ImageSampleSelectionController>(m_rt_preview_view_control->getScrollArea(), this);
        m_sampleSelectionController->setListenerEnabled(true);
        syncSampleSelectionOverlay();
    }
}

void BackgroundExtractionProcess::previewControlClosed()
{
    if (m_sampleSelectionController)
    {
        m_sampleSelectionController->destroy();
        m_sampleSelectionController.reset();
    }

    invalidatePreview();
}

BackgroundExtractionParams BackgroundExtractionProcess::getParams() const
{
    BackgroundExtractionParams params;
    params.samples = m_samples;
    params.smoothing = smoothingControl->getValue();
    params.degreeOrder = m_ui.degreeOrder->value();
    params.addDither = m_ui.addDither->isChecked();
    params.outputBackgroundModel = m_ui.outputBackgroundModel->isChecked();
    params.interpolationType = getBackgroundInterpolationTypeFromStr(m_ui.interpolationMethod->currentText());
    params.correctionType = getBackgroundCorrectionTypeFromStr(m_ui.correctionMode->currentText());
    params.previewDisplay = m_ui.previewDisplaySelector->currentIndex() == 1
                                ? BackgroundExtractionParams::PreviewDisplayMode::gradient_model
                                : BackgroundExtractionParams::PreviewDisplayMode::corrected;
    params.previewComputed = m_previewComputed;
    params.samplesPerLine = samplesPerLineControl->getValue();
    params.gridTolerance = gridToleranceControl->getValue();
    params.targetName = m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string();
    return params;
}

an::Status BackgroundExtractionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                an::ui::ProcessDestination& destination,
                                                                const an::ui::ProcessRun& run,
                                                                const BackgroundExtractionParams& params,
                                                                an::ui::ExecutionScope& scope)
{
    // Until its button asks, the preview shows the target.
    if (run.isForPreview() && (!params.previewComputed || params.samples.empty()))
    {
        return an::Status::success();
    }
    if (params.samples.empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "place background samples before extracting the background");
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::ExecutionContext& execution = scope.execution();
    const std::uint32_t width = described->extent().width();
    const std::uint32_t height = described->extent().height();
    const std::uint32_t channels = described->extent().channelCount();

    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    const an::KernelValidityView validity = process_utils::skippingNonFinite(samples->validity());
    const an::SpatialSelection selection{.validity = validity.descriptor, .mask = validity.mask};

    // Each sample's median in every channel, measured over this run's samples.
    std::vector<an::BackgroundSample> sites;
    sites.reserve(params.samples.size());
    for (const BackgroundSamplePosition& position : params.samples)
    {
        const an::Result<an::BackgroundSample> measured =
            an::measureImageBackgroundSample(execution, samples->sampleType(), samples->samples(), samples->layout(),
                                             measurementRequest(), position.x, position.y, selection);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        sites.push_back(*measured);
    }

    // One surface per channel, fitted through its medians.
    const an::BackgroundFitRequest fit{.kind = params.interpolationType,
                                       .degree = static_cast<std::uint32_t>((std::max)(params.degreeOrder, 1)),
                                       .smoothing = params.smoothing};
    const an::Result<std::uint32_t> terms = an::backgroundModelTermCount(fit, static_cast<std::uint32_t>(sites.size()));
    if (!terms.hasValue())
    {
        return terms.status();
    }
    std::vector<std::vector<double>> coefficients(channels, std::vector<double>(*terms, 0.0));
    std::vector<an::BackgroundModelView> models;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const an::Result<an::BackgroundFitResult> fitted =
            an::fitBackgroundModel(execution, fit, sites, channel, width, height, coefficients[channel]);
        if (!fitted.hasValue())
        {
            return fitted.status();
        }
        // A polynomial the samples do not determine is not the surface asked for.
        if (fit.kind == an::BackgroundModelKind::Polynomial && fitted->droppedTerms > 0u)
        {
            return refusal(an::ErrorCode::InvalidArgument,
                           fmt::format("a polynomial of degree {} needs samples that determine its {} terms; the {} "
                                       "samples placed determine {} of them",
                                       fit.degree, fitted->termCount, sites.size(),
                                       fitted->termCount - fitted->droppedTerms));
        }
        models.push_back(an::BackgroundModelView{
            .fit = fit, .sites = sites, .coefficients = coefficients[channel], .width = width, .height = height});
    }

    // The surface itself, for a preview of it or the image an Apply keeps.
    const bool previewModel =
        run.isForPreview() && params.previewDisplay == BackgroundExtractionParams::PreviewDisplayMode::gradient_model;
    const bool keepModel = !run.isForPreview() && !run.isForSequence() && params.outputBackgroundModel;
    std::optional<ModelPlanes> surface;
    if (previewModel || keepModel)
    {
        an::Result<ModelPlanes> made = createModelPlanes(scope.engine(), *described);
        if (!made.hasValue())
        {
            return made.status();
        }
        surface.emplace(std::move(*made));
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const an::Result<an::BackgroundEvaluationResult> evaluated =
                an::evaluateBackgroundModel(execution, models[channel], surface->planes[channel]);
            if (!evaluated.hasValue())
            {
                return evaluated.status();
            }
        }
    }
    if (previewModel)
    {
        if (an::Status copied =
                copyPlanesInto(execution, *surface, samples->sampleType(), samples->samples(), samples->layout());
            copied.failed())
        {
            return copied;
        }
        return samples->commit(execution);
    }

    // The correction, in place: a subtraction puts back the mean of the samples' medians over every channel, a division
    // keeps each channel's own mean, and the result is held to the unit interval.
    an::BackgroundApplyRequest apply;
    apply.correction = params.correctionType;
    apply.level = params.correctionType == an::BackgroundCorrection::Divide ? an::BackgroundLevel::SourceMean
                                                                            : an::BackgroundLevel::SampleMean;
    apply.dither = params.addDither ? 1.0e-4 : 0.0;
    apply.clamp = an::KernelValueWindow{.lowest = 0.0, .highest = 1.0};
    const an::Result<an::BackgroundApplyResult> applied =
        an::applyImageBackground(execution, samples->sampleType(), samples->samples(), samples->layout(),
                                 samples->samples(), samples->layout(), apply, models, selection);
    if (!applied.hasValue())
    {
        return applied.status();
    }
    if (an::Status committed = samples->commit(execution); committed.failed())
    {
        return committed;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }

    if (keepModel)
    {
        an::Result<an::ImageMetadata> metadata =
            process_utils::derivedImageMetadata(source.metadata(), *described, "background-extraction-gradient");
        if (!metadata.hasValue())
        {
            return metadata.status();
        }
        an::Result<an::consumer::ImageAsset> gradient =
            process_utils::createDerivedImage(scope, *described, std::move(*metadata));
        if (!gradient.hasValue())
        {
            return gradient.status();
        }
        {
            an::Result<an::consumer::AssetWriteLease> lease = gradient->write();
            if (!lease.hasValue())
            {
                return lease.status();
            }
            const an::Status written = lease->visitWrite([&](auto&& access) -> an::Status {
                const an::ImageLayout layout = access.view().layout();
                an::Result<process_utils::NormalizedSamples> output = process_utils::NormalizedSamples::open(
                    scope.engine(), execution, *described, access.view().data(), layout, an::KernelValidityView{});
                if (!output.hasValue())
                {
                    return output.status();
                }
                if (an::Status copied =
                        copyPlanesInto(execution, *surface, output->sampleType(), output->samples(), output->layout());
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
        m_StagedOutputs.stage(process_utils::StagedOutput{
            .asset = std::move(*gradient),
            .name = QString::fromStdString(params.targetName + " - GRADIENT"),
            .history = fmt::format("BackgroundExtractionProcess: Image gradient created from {}", params.targetName)});
    }

    an::Result<an::ProvenanceEntry> entry =
        an::ProvenanceEntry::create("an::applyImageBackground", describeTypedProcessStep(params).description);
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return destination.asset().transition(an::MetadataTransition{.record = *entry});
}

an::ui::ProcessStep
BackgroundExtractionProcess::describeTypedProcessStep(const BackgroundExtractionParams& params) const
{
    const bool polynomial = params.interpolationType == an::BackgroundModelKind::Polynomial;
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description =
            fmt::format("Samples per Line: {}, Grid Tolerance: {}, Correction: {}, Interpolation: {}{}",
                        params.samplesPerLine, params.gridTolerance,
                        params.correctionType == an::BackgroundCorrection::Divide ? "Division" : "Subtraction",
                        polynomial ? "Polynomial" : "RBF",
                        polynomial ? fmt::format(", Degree Order: {}", params.degreeOrder)
                                   : fmt::format(", Smoothing: {}", params.smoothing))};
}

void BackgroundExtractionProcess::executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode == RTPreviewMode::NoPreview && !sequenceMode)
    {
        m_StagedOutputs.clear();
    }
}

void BackgroundExtractionProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode != RTPreviewMode::NoPreview || sequenceMode)
    {
        return;
    }
    process_utils::openStagedOutputs(m_StagedOutputs.take(), m_image_view_control);
}

void BackgroundExtractionProcess::settingsUpdatedImpl()
{
    updateActionButtons(!m_ExecutionInProgress);
    syncPreviewUiState(!m_ExecutionInProgress);
}

void BackgroundExtractionProcess::enableProcessControlsImpl(bool enabled)
{
    updateActionButtons(enabled);

    if (auto* closeButton = m_ui.buttonBox->button(QDialogButtonBox::Close))
    {
        closeButton->setEnabled(enabled);
    }
    if (auto* resetButton = m_ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        resetButton->setEnabled(enabled);
    }

    samplesPerLineControl->setEnabled(enabled);
    gridToleranceControl->setEnabled(enabled);
    smoothingControl->setEnabled(enabled && m_ui.interpolationMethod->currentText() != "Polynomial");
    m_ui.interpolationMethod->setEnabled(enabled);
    m_ui.correctionMode->setEnabled(enabled);
    m_ui.previewCheckbox->setEnabled(enabled);
    syncPreviewUiState(enabled);
    m_ui.degreeOrder->setEnabled(enabled && m_ui.interpolationMethod->currentText() == "Polynomial");
    m_ui.outputBackgroundModel->setEnabled(enabled);
    m_ui.addDither->setEnabled(enabled);
    m_ui.targetImageSelector->setEnabled(enabled);
}

void BackgroundExtractionProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    clearSamples();
    initialize();
    if (m_rt_preview_view_control)
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* BackgroundExtractionProcess::getProgressDialogControl(bool forSequence)
{
    auto* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
    progress->setProgressBarRange(0, 100);
    return progress;
}
