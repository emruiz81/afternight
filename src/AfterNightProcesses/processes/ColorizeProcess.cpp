// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ColorizeProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace
{

/// A working plane of a colour image's CIE lightness, in the unit interval, which a colour image colorized from its
/// lightness is colorized from.
struct LightnessPlane
{
    std::variant<std::monostate, an::Image<float>, an::Image<double>> image;
    void* samples = nullptr;
    std::optional<an::ImageLayout> layout;
};

/// The CIE lightness of the source, in the working representation `working`, as the process framework reads a colour
/// image's CIE components.
[[nodiscard]] an::Result<LightnessPlane> lightnessOf(const an::ui::ExecutionScope& scope,
                                                     const an::ui::ProcessSource& source,
                                                     const an::ImageDescriptor& described, an::SampleType working)
{
    const an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(an::ui::processChannelScopeRequest(an::ChannelScope::CieLightness, described));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> planeDescribed = an::channelScopePlaneDescriptor(*plan, described);
    if (!planeDescribed.hasValue())
    {
        return planeDescribed.status();
    }
    const an::Result<an::ImageDescriptor> workingDescribed = an::describeSamplesAs(*planeDescribed, working);
    if (!workingDescribed.hasValue())
    {
        return workingDescribed.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> mask =
        source.validity() != nullptr ? source.validity()->view() : std::nullopt;
    LightnessPlane plane;
    an::Status made = an::Status::success();
    auto extract = [&]<typename WorkT>() {
        an::Result<an::Image<WorkT>> created =
            an::Image<WorkT>::create(scope.engine(), *workingDescribed, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Uninitialized, an::ImageMetadata{});
        if (!created.hasValue())
        {
            made = created.status();
            return;
        }
        an::Image<WorkT>& image = plane.image.template emplace<an::Image<WorkT>>(std::move(*created));
        const std::optional<an::ImageWriteAccess<WorkT>> access = image.write();
        if (!access.has_value())
        {
            made = an::Status::failure(an::ErrorCode::NotInitialized, "the lightness plane holds nothing");
            return;
        }
        plane.samples = access->view().data();
        plane.layout = access->view().layout();
        made = std::visit(
            [&](const auto& typed) -> an::Status {
                const auto view = typed.view();
                if (!view.has_value())
                {
                    return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
                }
                const an::ImageLayout sourceLayout = view->layout();
                an::ChannelScopeSource scoped;
                scoped.descriptor = &described;
                scoped.samples = view->data();
                scoped.layout = &sourceLayout;
                scoped.validity.descriptor = typed.metadata().semantics().validity();
                scoped.validity.mask = mask.has_value() ? &*mask : nullptr;
                if (*planeDescribed == *workingDescribed)
                {
                    const an::Result<an::ChannelScopeResult> extracted =
                        an::extractChannelScope(scope.execution(), *plan, scoped, plane.samples, *plane.layout);
                    return extracted.hasValue() ? an::Status::success() : extracted.status();
                }
                // The scope hands a component over in Float32 for every representation but Float64, so a lightness the
                // process works on in Float64 is widened from it.
                an::Result<an::Image<float>> narrow =
                    an::Image<float>::create(scope.engine(), *planeDescribed, an::StorageArrangement::Planar,
                                             an::StorageInitialization::Uninitialized, an::ImageMetadata{});
                if (!narrow.hasValue())
                {
                    return narrow.status();
                }
                const std::optional<an::ImageWriteAccess<float>> narrowAccess = narrow->write();
                if (!narrowAccess.has_value())
                {
                    return an::Status::failure(an::ErrorCode::NotInitialized, "the lightness plane holds nothing");
                }
                const an::Result<an::ChannelScopeResult> extracted = an::extractChannelScope(
                    scope.execution(), *plan, scoped, narrowAccess->view().data(), narrowAccess->view().layout());
                if (!extracted.hasValue())
                {
                    return extracted.status();
                }
                an::ConversionCounters counters;
                return an::convertImageSamples(scope.execution(), an::SampleType::Float32, narrowAccess->view().data(),
                                               narrowAccess->view().layout(), working, plane.samples, *plane.layout,
                                               an::ConversionPolicy{}, counters);
            },
            source.image());
    };
    if (working == an::SampleType::Float64)
    {
        extract.template operator()<double>();
    }
    else
    {
        extract.template operator()<float>();
    }
    if (made.failed())
    {
        return made;
    }
    return plane;
}

/// Writes an intensity plane into every channel of planar working samples of red, green and blue, as a colorization's
/// result starts.
[[nodiscard]] an::Status replicateIntensity(const an::ui::ExecutionScope& scope, an::SampleType working,
                                            const void* intensity, const an::ImageLayout& intensityLayout,
                                            const process_utils::NormalizedSamples& colour)
{
    const an::ImageLayout& layout = colour.layout();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), layout.extent().height(), 1u, scope.engine().limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "a plane of the colorized image cannot be described");
    }
    const std::optional<an::ImageLayout> plane = an::ImageLayout::createWithStrides(
        an::ImageLayoutRequest{.extent = *extent,
                               .elementSizeBytes = layout.elementSizeBytes(),
                               .samplesPerPixel = 1u,
                               .planeCount = 1u,
                               .rowAlignmentBytes = 1u},
        layout.rowStrideBytes(), layout.rowStrideBytes() * static_cast<std::uint64_t>(layout.extent().height()),
        scope.engine().limits());
    if (!plane.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "a plane of the colorized image cannot be laid out");
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        void* start =
            static_cast<std::byte*>(colour.samples()) + static_cast<std::size_t>(layout.planeStrideBytes()) * channel;
        if (an::Status copied =
                an::copyImageSamples(scope.execution(), working, intensity, intensityLayout, start, *plane);
            copied.failed())
        {
            return copied;
        }
    }
    return an::Status::success();
}

/// What the colorized result is: the source's description as red, green and blue, with no filter array.
[[nodiscard]] an::Result<an::ImageDescriptor> colourDescriptorOf(const an::ImageDescriptor& described,
                                                                 const an::EngineLimits& limits)
{
    an::ImageDescriptorRequest request = described.toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(described.extent().width(), described.extent().height(), 3u, limits);
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the colorized image cannot be described");
    }
    request.extent = *extent;
    request.channelSchema = an::ChannelSchema::Rgb;
    request.cfa = an::CfaDescriptor{};
    const std::optional<an::ImageDescriptor> colour = an::ImageDescriptor::create(request);
    if (!colour.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the colorized image cannot be described");
    }
    return *colour;
}

/// Whether a target view holds red, green and blue alone: the colorization reads a colour image's lightness only from
/// those, and refuses an image with alpha.
[[nodiscard]] bool holdsRedGreenAndBlue(const ImageViewControl* target)
{
    if (target == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = target->imageDescriptor();
    return described.has_value() && described->channelSchema() == an::ChannelSchema::Rgb;
}

} // namespace

ColorizeProcess::ColorizeProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<ColorizeParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildColorizeProcessUi(this);
    setProcessDescription(QStringLiteral("Apply controlled hue and saturation colorization using HSV or HSI methods."));

    QObject::connect(m_ui.colorizeMethod, &QComboBox::currentTextChanged, this,
                     &ColorizeProcess::on_method_currentTextChanged);
    QObject::connect(m_ui.useLightness, &QCheckBox::checkStateChanged, this, &ColorizeProcess::on_useLightness_clicked);

    updateWindowTitle();

    INIT_D_SPINBOX_CTRL(hueControl, m_ui.hueValue, m_ui.hueSlider, &ColorizeProcess::on_hueChanged);
    INIT_D_SPINBOX_CTRL(saturationControl, m_ui.saturationValue, m_ui.saturationSlider,
                        &ColorizeProcess::on_saturationChanged);

    m_ui.colorizeMethod->addItem("HSV", 0);
    m_ui.colorizeMethod->addItem("HSI", 1);
    m_ui.colorizeMethod->setCurrentIndex(0);

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);
    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, false);

    m_ui.useLightness->setEnabled(holdsRedGreenAndBlue(m_image_view_control));

    setupPreviewControl(false);
    settingsUpdated();

    refreshTheme();
}

std::string ColorizeProcess::getProcessNameStatic()
{
    return "Colorize";
}

bool ColorizeProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void ColorizeProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void ColorizeProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    m_ui.useLightness->setEnabled(holdsRedGreenAndBlue(m_image_view_control));
    if (!m_ui.useLightness->isEnabled())
    {
        m_ui.useLightness->setChecked(false);
    }
}

void ColorizeProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    m_ui.useLightness->setEnabled(holdsRedGreenAndBlue(m_image_view_control));
    if (!m_ui.useLightness->isEnabled())
    {
        m_ui.useLightness->setChecked(false);
    }
}

ColorizeProcess::~ColorizeProcess() {}

void ColorizeProcess::on_hueChanged(int value)
{
    settingsUpdated();
}

void ColorizeProcess::on_saturationChanged(int value)
{
    settingsUpdated();
}

void ColorizeProcess::on_method_currentTextChanged(const QString& str)
{
    settingsUpdated();
}

void ColorizeProcess::on_useLightness_clicked(Qt::CheckState state)
{
    settingsUpdated();
}

bool ColorizeProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void ColorizeProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status ColorizeProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                    an::ui::ProcessDestination& destination,
                                                    const an::ui::ProcessRun& run, const ColorizeParams& params,
                                                    an::ui::ExecutionScope& scope)
{
    const an::ExecutionContext& execution = scope.execution();
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (described->channelSchema() != an::ChannelSchema::Mono && described->channelSchema() != an::ChannelSchema::Rgb)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "the colorization needs a grey image or an image of red, green and blue");
    }
    const bool colour = described->channelSchema() == an::ChannelSchema::Rgb;
    const bool fromLightness = params.useLightness && colour;
    const an::ColorizationParameters colorization{.hueDegrees = params.hue,
                                                  .saturation = params.saturation,
                                                  .model = params.method == 1 ? an::ColorizationModel::Hsi
                                                                              : an::ColorizationModel::Hsv};

    if (colour && !fromLightness)
    {
        // A colour image is colorized channel by channel in place: the destination is its copy, so a pixel the
        // colorization leaves - one holding no number - is the source's.
        an::Result<process_utils::NormalizedSamples> samples =
            process_utils::openNormalizedDestination(scope, source, destination);
        if (!samples.hasValue())
        {
            return samples.status();
        }
        const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(execution);
        if (!skipping.hasValue())
        {
            return skipping.status();
        }
        const an::Result<an::ColorAdjustmentResult> colorized =
            an::colorizeImage(execution, samples->sampleType(), samples->samples(), samples->layout(),
                              samples->samples(), samples->layout(), colorization, *skipping);
        if (!colorized.hasValue())
        {
            return colorized.status();
        }
        if (an::Status committed = samples->commit(execution); committed.failed())
        {
            return committed;
        }
    }
    else
    {
        // A grey image, or a colour image's CIE lightness, becomes red, green and blue: a new image of the source's
        // representation whose three channels start as the intensity, so a pixel the colorization leaves keeps it.
        const an::SampleType working = process_utils::normalizedWorkingType(described->sampleType());
        std::optional<LightnessPlane> lightness;
        an::Result<process_utils::NormalizedSamples> intensity = process_utils::NormalizedSamples();
        if (fromLightness)
        {
            // A file's floating or sensor-code image has no full scale; its lightness is taken from its samples read
            // as fractions of full scale, as the view shows them.
            const std::optional<an::ImageDescriptor> reading = an::ui::componentReadingOf(*described);
            if (!reading.has_value())
            {
                return an::Status::failure(an::ErrorCode::InvalidArgument,
                                           "the image cannot be read as fractions of full scale");
            }
            an::Result<LightnessPlane> extracted = lightnessOf(scope, source, *reading, working);
            if (!extracted.hasValue())
            {
                return extracted.status();
            }
            lightness.emplace(std::move(*extracted));
        }
        else
        {
            intensity = process_utils::openNormalizedDestination(scope, source, destination);
            if (!intensity.hasValue())
            {
                return intensity.status();
            }
        }
        const void* from = lightness.has_value() ? lightness->samples : intensity->samples();
        const an::ImageLayout& fromLayout = lightness.has_value() ? *lightness->layout : intensity->layout();
        // The lightness hands a pixel it had no colour for over as not-a-number, under a rule that says so; the
        // intensity's own samples are judged with their rule.
        an::KernelValidityView validity = process_utils::skippingNonFinite(an::KernelValidityView{});
        if (!lightness.has_value())
        {
            const an::Result<an::KernelValidityView> skipping = intensity->skippingNonFinite(execution);
            if (!skipping.hasValue())
            {
                return skipping.status();
            }
            validity = *skipping;
        }

        const an::Result<an::ImageDescriptor> colourDescribed = colourDescriptorOf(*described, scope.engine().limits());
        if (!colourDescribed.hasValue())
        {
            return colourDescribed.status();
        }
        an::Result<an::consumer::AnyImage> result = std::visit(
            [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
                using ImageT = std::remove_cvref_t<decltype(typed)>;
                using SampleT = typename ImageT::Sample;
                an::Result<ImageT> created =
                    ImageT::create(scope.engine(), *colourDescribed, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, typed.metadata());
                if (!created.hasValue())
                {
                    return created.status();
                }
                {
                    const std::optional<an::ImageWriteAccess<SampleT>> access = created->write();
                    if (!access.has_value())
                    {
                        return an::Status::failure(an::ErrorCode::NotInitialized, "the colorized image holds nothing");
                    }
                    an::Result<process_utils::NormalizedSamples> colourSamples = process_utils::NormalizedSamples::open(
                        scope.engine(), execution, *colourDescribed, access->view().data(), access->view().layout(),
                        an::KernelValidityView{});
                    if (!colourSamples.hasValue())
                    {
                        return colourSamples.status();
                    }
                    if (an::Status started = replicateIntensity(scope, working, from, fromLayout, *colourSamples);
                        started.failed())
                    {
                        return started;
                    }
                    const an::Result<an::ColorAdjustmentResult> colorized =
                        an::colorizeImage(execution, working, from, fromLayout, colourSamples->samples(),
                                          colourSamples->layout(), colorization, validity);
                    if (!colorized.hasValue())
                    {
                        return colorized.status();
                    }
                    if (an::Status committed = colourSamples->commit(execution); committed.failed())
                    {
                        return committed;
                    }
                }
                return an::consumer::AnyImage(std::move(*created));
            },
            source.image());
        if (!result.hasValue())
        {
            return result.status();
        }
        std::optional<an::ValidityMask> mask;
        if (source.validity() != nullptr)
        {
            an::Result<an::ValidityMask> cloned = source.validity()->clone();
            if (!cloned.hasValue())
            {
                return cloned.status();
            }
            mask.emplace(std::move(*cloned));
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(scope.session(), std::move(*result), std::move(mask));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        if (an::Status replaced = destination.replace(std::move(*asset)); replaced.failed())
        {
            return replaced;
        }
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    return process_utils::recordNonlinearResult(
        destination,
        process_utils::NonlinearResultRecord{
            .stateOperation = "colorize",
            .engineOperation = "an::colorizeImage",
            .detail = fmt::format("hue {}, saturation {}, the {} model{}", params.hue, params.saturation,
                                  params.method == 1 ? "HSI" : "HSV", fromLightness ? ", from CIE L*" : "")});
}

an::ui::ProcessStep ColorizeProcess::describeTypedProcessStep(const ColorizeParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Colorize hue to {} and saturation to {}", params.hue, params.saturation)};
}

void ColorizeProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    hueControl->setValue(0.0);
    saturationControl->setValue(1.0);

    m_ui.colorizeMethod->setCurrentIndex(0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void ColorizeProcess::settingsUpdatedImpl()
{
    /*if (hueControl->getValue() == 0 && saturationControl->getValue() == 0)
    {
        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else*/
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void ColorizeProcess::enableProcessControlsImpl(bool enabled)
{
    /* if (hueControl->getValue() == 0 && saturationControl->getValue() == 0)
     {
         buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
     }
     else*/
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    hueControl->setEnabled(enabled);
    saturationControl->setEnabled(enabled);
}

bool ColorizeProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return true;
}

ColorizeParams ColorizeProcess::getParams() const
{
    ColorizeParams params;
    params.hue = hueControl->getValue();
    params.saturation = saturationControl->getValue();
    params.method = m_ui.colorizeMethod->currentIndex();
    params.useLightness = m_ui.useLightness->isEnabled() && m_ui.useLightness->isChecked();

    return params;
}
