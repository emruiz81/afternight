// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view's work over the session: decode and a camera RAW frame's treatment, the one demosaic setting, the static
/// preview's calibration by its masters, statistics, the transactional write in the representation its intent states,
/// the exact turns and mirrors, and the overlay's star detection.

#include "controls/ImageViewFiles.h"

#include "app/PlateSolveTypes.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/CosmeticCorrection/CosmeticCorrection.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ObservationCards.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::ui
{

namespace
{

/// The pedestal the product's calibration settings state is in sixteen-bit codes; the preview calibrates fractions of
/// full scale.
constexpr double kPedestalCodesPerUnit = 65535.0;

/// A failure the view decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A failure that says what the view was doing, keeping the engine's failure as its cause.
[[nodiscard]] an::Status explained(an::Status cause, std::string message)
{
    const an::ErrorCode code = cause.code();
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)}, std::move(cause));
}

/// A path as the engine reads it.
[[nodiscard]] std::string enginePath(const QString& path)
{
    return path.toUtf8().toStdString();
}

/// Admits one step of a read or a write, for its purpose.
[[nodiscard]] an::Result<an::consumer::OperationScope> admitStep(const an::consumer::EngineSession& session,
                                                                 an::consumer::OperationPurpose purpose,
                                                                 const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        return refusal(an::ErrorCode::Cancelled, "the request was stopped");
    }
    return session.admit(an::consumer::OperationRequest{.purpose = purpose, .stopToken = stop});
}

/// Takes a decoded frame over as an engine image of its own representation: with no copy when its memory is the
/// engine's and in this host's byte order, and through one copy otherwise.
[[nodiscard]] an::Result<an::consumer::AnyImage> importFrame(const an::EngineContext& engine,
                                                             const an::ExecutionContext& execution,
                                                             an::RawFrame&& frame, an::ImageMetadata&& metadata,
                                                             bool planar = false)
{
    const std::optional<an::RawFrameDescriptor> frameDescriptor = frame.descriptor();
    const std::optional<an::ImageLayout> frameLayout = frame.layout();
    const std::optional<an::SampleType> resident =
        frameDescriptor.has_value() ? frameDescriptor->residentSampleType() : std::nullopt;
    const std::optional<an::ImageDescriptor> imageDescriptor =
        frameDescriptor.has_value() ? frameDescriptor->toImageDescriptor() : std::nullopt;
    if (!resident.has_value() || !frameLayout.has_value() || !imageDescriptor.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, "the file holds samples outside the eight this engine processes");
    }
    // An interleaved frame of several channels is adopted only when the caller does not need planes.
    const bool arranged = !planar || frameLayout->samplesPerPixel() == 1u;
    const bool adoptable = arranged && frame.checkAdoptable().ok();
    const bool foreignOrder = frameDescriptor->byteOrder() != an::ByteOrder::Native &&
                              frameDescriptor->byteOrder() != an::nativeByteOrder() &&
                              an::isByteOrderMeaningful(*resident);
    an::Result<an::consumer::AnyImage> imported = refusal(an::ErrorCode::Internal, "no representation was imported");
    const bool dispatched = an::dispatchSampleType(*resident, [&]<typename SampleT>() {
        if (adoptable && !foreignOrder)
        {
            an::Result<an::Image<SampleT>> adopted = an::Image<SampleT>::adopt(std::move(frame), std::move(metadata));
            imported = adopted.hasValue()
                           ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage(std::move(*adopted)))
                           : an::Result<an::consumer::AnyImage>(adopted.status());
            return;
        }
        an::Result<an::Image<SampleT>> copy =
            an::Image<SampleT>::create(engine, *imageDescriptor, an::StorageArrangement::Planar,
                                       an::StorageInitialization::Uninitialized, std::move(metadata));
        if (!copy.hasValue())
        {
            imported = copy.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = copy->write();
            if (!access.has_value())
            {
                imported = refusal(an::ErrorCode::NotInitialized, "the copy of the file's image holds nothing");
                return;
            }
            an::Status copied = an::repackImageSamples(execution, *resident, frame.bytes().data(), *frameLayout,
                                                       access->view().data(), access->view().layout());
            if (copied.ok() && foreignOrder)
            {
                copied = an::swapImageByteOrder(execution, *resident, access->view().data(), access->view().layout());
            }
            if (copied.failed())
            {
                imported = std::move(copied);
                return;
            }
        }
        frame.release();
        imported = an::consumer::AnyImage(std::move(*copy));
    });
    if (!dispatched)
    {
        return refusal(an::ErrorCode::Unsupported, "the file holds samples outside the eight this engine processes");
    }
    return imported;
}

/// Decodes a file into an engine image, through a named registry when one carries the read's RAW policy.
[[nodiscard]] an::Result<an::consumer::AnyImage> decodeFile(const an::consumer::EngineSession& session,
                                                            const QString& path, const an::CodecRegistry* registry,
                                                            an::consumer::OperationPurpose purpose,
                                                            const std::stop_token& stop)
{
    an::Result<an::consumer::OperationScope> scope = admitStep(session, purpose, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    // The request borrows the path for the call, so the string it views is kept here.
    const std::string file = enginePath(path);
    const an::CodecReadRequest request{.path = file};
    an::Result<an::CodecDecodeResult> decoded =
        registry != nullptr ? an::decodeImage(scope->engine(), scope->execution(), *registry, request)
                            : an::decodeImage(scope->engine(), scope->execution(), request);
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    return importFrame(scope->engine(), scope->execution(), std::move(decoded->frame), std::move(decoded->metadata));
}

/// The description of an image as Float32 fractions of full scale: the representation a preview calibrates in.
[[nodiscard]] an::Result<an::ImageDescriptor> fractionDescriptor(const an::ImageDescriptor& stored)
{
    an::ImageDescriptorRequest request = stored.toRequest();
    request.sampleType = an::SampleType::Float32;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the frame has no Float32 description");
    }
    return *described;
}

/// A Float32 copy of an image with every sample a fraction of full scale: an integer over its representation's
/// largest value, a floating sample as it is.
[[nodiscard]] an::Result<an::Image<float>> toFractions(const an::consumer::OperationScope& scope,
                                                       const an::consumer::AnyImage& image)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::Image<float>> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> stored = typed.descriptor();
            const auto view = typed.view();
            if (!stored.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
            }
            const an::Result<an::ImageDescriptor> described = fractionDescriptor(*stored);
            if (!described.hasValue())
            {
                return described.status();
            }
            an::Result<an::Image<float>> widened =
                an::Image<float>::create(scope.engine(), *described, an::StorageArrangement::Planar,
                                         an::StorageInitialization::Uninitialized, typed.metadata());
            if (!widened.hasValue())
            {
                return widened.status();
            }
            std::optional<an::ImageWriteAccess<float>> access = widened->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the widened frame holds nothing");
            }
            an::ConversionCounters counters;
            const an::Status converted = an::convertImageSamples(
                scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(), an::SampleType::Float32,
                access->view().data(), access->view().layout(), an::ConversionPolicy{}, counters);
            if (converted.failed())
            {
                return converted;
            }
            access.reset();
            return std::move(*widened);
        },
        image);
}

/// A frame the calibration reads, with the view it reads it through.
struct CalibrationFrame
{
    std::shared_ptr<const an::consumer::ImageAsset> asset;
    an::consumer::AssetReadLease lease;
    std::optional<an::CalibrationFrameView> view;
};

/// The calibration view of a Float32 image under a lease.
[[nodiscard]] an::Result<an::CalibrationFrameView> calibrationViewOf(const an::consumer::AssetReadLease& lease)
{
    const an::Image<float>* typed = std::get_if<an::Image<float>>(&lease.image());
    if (typed == nullptr || !typed->view().has_value() || !typed->descriptor().has_value())
    {
        return refusal(an::ErrorCode::Internal, "a calibration frame is not a Float32 image");
    }
    const auto view = typed->view();
    return an::CalibrationFrameView::create(view->data(), view->layout().totalBytes(), view->layout(),
                                            *typed->descriptor());
}

/// Reads a master as Float32 fractions, from the view's cache or from its file.
[[nodiscard]] an::Result<std::shared_ptr<const an::consumer::ImageAsset>>
readMaster(const an::consumer::EngineSession& session, const QString& path, an::consumer::OperationPurpose purpose,
           const std::stop_token& stop, ViewMasterCache* masters)
{
    if (masters != nullptr)
    {
        if (std::shared_ptr<const an::consumer::ImageAsset> kept = masters->find(path))
        {
            return kept;
        }
    }
    an::Result<an::consumer::AnyImage> decoded = decodeFile(session, path, nullptr, purpose, stop);
    if (!decoded.hasValue())
    {
        return explained(decoded.status(), "the master '" + enginePath(path) + "' could not be read");
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, purpose, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::Image<float>> widened = toFractions(*scope, *decoded);
    if (!widened.hasValue())
    {
        return widened.status();
    }
    an::Result<an::consumer::ImageAsset> adopted =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*widened)));
    if (!adopted.hasValue())
    {
        return adopted.status();
    }
    auto master = std::make_shared<const an::consumer::ImageAsset>(std::move(*adopted));
    if (masters != nullptr)
    {
        masters->keep(path, master);
    }
    return master;
}

/// Opens a calibration frame: a lease on its asset and the view the calibration reads.
[[nodiscard]] an::Result<CalibrationFrame> openCalibrationFrame(std::shared_ptr<const an::consumer::ImageAsset> asset,
                                                                const std::stop_token& stop)
{
    CalibrationFrame frame;
    frame.asset = std::move(asset);
    an::Result<an::consumer::AssetReadLease> lease = frame.asset->read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    frame.lease = std::move(*lease);
    an::Result<an::CalibrationFrameView> view = calibrationViewOf(frame.lease);
    if (!view.hasValue())
    {
        return view.status();
    }
    frame.view = *view;
    return frame;
}

/// Runs one calibration of an input by masters into a new Float32 image.
[[nodiscard]] an::Result<an::Image<float>> calibrateFrame(const an::consumer::OperationScope& scope,
                                                          const an::Image<float>& input,
                                                          const an::CalibrationFrameView& inputView,
                                                          const an::CalibrationMasters& masters,
                                                          const an::CalibrationRequest& request)
{
    an::Result<an::CalibrationPlan> plan = an::prepareImageCalibration(scope.execution(), inputView, masters, request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<float>> calibrated =
        an::Image<float>::createDestination(scope.engine(), inputView.descriptor(), an::StorageArrangement::Planar,
                                            an::StorageInitialization::Uninitialized, input);
    if (!calibrated.hasValue())
    {
        return calibrated.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = calibrated->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the calibrated frame holds nothing");
        }
        const an::Result<an::CalibrationResult> ran =
            an::calibrateImage(scope.execution(), *plan, inputView, masters, access->view().data(),
                               access->view().layout().totalBytes(), access->view().layout());
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::imageCalibrationTransition(*plan, input.metadata().semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = calibrated->transition(*change); stated.failed())
    {
        return stated;
    }
    return calibrated;
}

/// Replaces the defective pixels a dark names in a calibrated light, at the sigmas the settings state.
[[nodiscard]] an::Status correctDefects(const an::consumer::OperationScope& scope, const an::Image<float>& dark,
                                        an::Image<float>& light, const CalibrationPreviewSettings& settings)
{
    const auto darkView = dark.view();
    const std::optional<an::ImageDescriptor> lightDescriptor = light.descriptor();
    if (!darkView.has_value() || !lightDescriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a frame of the cosmetic correction holds nothing");
    }
    const an::CosmeticDetectionOptions options{.coldSigma = settings.sigmaCold, .hotSigma = settings.sigmaHot};
    an::Result<an::ValidityMask> cold = an::ValidityMask::create(scope.engine(), lightDescriptor->extent());
    an::Result<an::ValidityMask> hot = an::ValidityMask::create(scope.engine(), lightDescriptor->extent());
    if (!cold.hasValue())
    {
        return cold.status();
    }
    if (!hot.hasValue())
    {
        return hot.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> coldMap = cold->view();
    std::optional<an::ValidityMaskView<std::uint8_t>> hotMap = hot->view();
    if (!coldMap.has_value() || !hotMap.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the cosmetic maps hold nothing");
    }
    const an::Result<an::CosmeticScanResult> mapped = an::mapImageDeviantPixels(
        scope.execution(), an::SampleType::Float32, darkView->data(), darkView->layout(), options, &*coldMap, &*hotMap);
    if (!mapped.hasValue())
    {
        return mapped.status();
    }
    if (mapped->coldCount == 0u && mapped->hotCount == 0u)
    {
        return an::Status::success();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> coldRead = std::as_const(*cold).view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> hotRead = std::as_const(*hot).view();
    an::Result<an::Image<float>> corrected =
        an::Image<float>::createDestination(scope.engine(), *lightDescriptor, an::StorageArrangement::Planar,
                                            an::StorageInitialization::Uninitialized, light);
    if (!corrected.hasValue())
    {
        return corrected.status();
    }
    const auto lightView = light.view();
    {
        std::optional<an::ImageWriteAccess<float>> access = corrected->write();
        if (!access.has_value() || !lightView.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the corrected frame holds nothing");
        }
        an::CosmeticCorrectionRequest request;
        request.spacing = lightDescriptor->isMosaiced() ? an::CosmeticSiteSpacing::SameCfaFilter
                                                        : an::CosmeticSiteSpacing::EveryPixel;
        const an::Result<an::CosmeticCorrectionResult> fixed = an::correctImageDeviantPixels(
            scope.execution(), an::SampleType::Float32, lightView->data(), lightView->layout(), access->view().data(),
            access->view().layout(), coldRead.has_value() ? &*coldRead : nullptr,
            hotRead.has_value() ? &*hotRead : nullptr, request);
        if (!fixed.hasValue())
        {
            return fixed.status();
        }
    }
    light = std::move(*corrected);
    return an::Status::success();
}

/// Calibrates a light by the masters a view's preview names, in Float32 fractions of full scale.
[[nodiscard]] an::Result<an::consumer::AnyImage> calibrateLight(const an::consumer::EngineSession& session,
                                                                an::consumer::AnyImage&& light,
                                                                const ViewCalibration& calibration,
                                                                an::consumer::OperationPurpose purpose,
                                                                const std::stop_token& stop, ViewMasterCache* masters)
{
    std::optional<CalibrationFrame> bias;
    std::optional<CalibrationFrame> dark;
    std::optional<CalibrationFrame> flat;
    for (auto [path, frame] :
         {std::pair<const QString*, std::optional<CalibrationFrame>*>{&calibration.biasPath, &bias},
          std::pair<const QString*, std::optional<CalibrationFrame>*>{&calibration.darkPath, &dark},
          std::pair<const QString*, std::optional<CalibrationFrame>*>{&calibration.flatPath, &flat}})
    {
        if (path->isEmpty())
        {
            continue;
        }
        an::Result<std::shared_ptr<const an::consumer::ImageAsset>> master =
            readMaster(session, *path, purpose, stop, masters);
        if (!master.hasValue())
        {
            return master.status();
        }
        an::Result<CalibrationFrame> opened = openCalibrationFrame(std::move(*master), stop);
        if (!opened.hasValue())
        {
            return opened.status();
        }
        frame->emplace(std::move(*opened));
    }

    an::Result<an::consumer::OperationScope> scope = admitStep(session, purpose, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::Image<float>> widened = toFractions(*scope, light);
    if (!widened.hasValue())
    {
        return widened.status();
    }

    // The dark the light is calibrated by: the dark less its bias when it holds one, and the bias then subtracted from
    // the light too; with no bias in the dark the light is calibrated by the dark alone.
    std::optional<an::Image<float>> effectiveDark;
    std::optional<an::CalibrationFrameView> effectiveDarkView;
    bool useBias = bias.has_value();
    if (dark.has_value())
    {
        const auto* darkImage = std::get_if<an::Image<float>>(&dark->lease.image());
        if (calibration.subtractBiasFromDark && bias.has_value())
        {
            an::Result<an::Image<float>> lessBias =
                calibrateFrame(*scope, *darkImage, *dark->view, an::CalibrationMasters{.bias = bias->view},
                               an::CalibrationRequest{.flatNormalization = an::FlatNormalizationMode::None});
            if (!lessBias.hasValue())
            {
                return explained(lessBias.status(), "the bias could not be subtracted from the dark");
            }
            effectiveDark.emplace(std::move(*lessBias));
        }
        else
        {
            useBias = false;
            an::Result<an::Image<float>> copied = darkImage->clone();
            if (!copied.hasValue())
            {
                return copied.status();
            }
            effectiveDark.emplace(std::move(*copied));
        }
        const auto view = effectiveDark->view();
        an::Result<an::CalibrationFrameView> described = an::CalibrationFrameView::create(
            view->data(), view->layout().totalBytes(), view->layout(), *effectiveDark->descriptor());
        if (!described.hasValue())
        {
            return described.status();
        }
        effectiveDarkView = *described;
    }

    const auto lightView = widened->view();
    an::Result<an::CalibrationFrameView> lightFrame = an::CalibrationFrameView::create(
        lightView->data(), lightView->layout().totalBytes(), lightView->layout(), *widened->descriptor());
    if (!lightFrame.hasValue())
    {
        return lightFrame.status();
    }
    an::CalibrationMasters applied;
    if (useBias)
    {
        applied.bias = bias->view;
    }
    if (effectiveDarkView.has_value())
    {
        applied.dark = effectiveDarkView;
    }
    if (flat.has_value())
    {
        applied.flat = flat->view;
    }
    const CalibrationPreviewSettings& settings = calibration.settings;
    an::CalibrationRequest request;
    request.pedestal = settings.pedestal / kPedestalCodesPerUnit;
    request.darkScaling =
        settings.darkOptimization ? an::DarkScalingMode::OptimizeCentralRegion : an::DarkScalingMode::Fixed;
    request.darkScale = settings.darkMultiplicationFactor;
    request.flatNormalization =
        settings.flatAutoLevel ? an::FlatNormalizationMode::CentralMean : an::FlatNormalizationMode::None;
    an::Result<an::Image<float>> calibrated = calibrateFrame(*scope, *widened, *lightFrame, applied, request);
    if (!calibrated.hasValue())
    {
        return calibrated.status();
    }
    if (settings.cosmeticCorrection && effectiveDark.has_value())
    {
        if (an::Status corrected = correctDefects(*scope, *effectiveDark, *calibrated, settings); corrected.failed())
        {
            return explained(std::move(corrected), "the defective pixels could not be corrected");
        }
    }
    return an::consumer::AnyImage(std::move(*calibrated));
}

/// The colour description a mosaic is demosaiced into: three channels, no filter array.
[[nodiscard]] an::Result<an::ImageDescriptor> colourDescriptorOf(const an::ImageDescriptor& mosaic,
                                                                 const an::EngineLimits& limits)
{
    an::ImageDescriptorRequest request = mosaic.toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(request.extent.width(), request.extent.height(), 3u, limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the colour image has no extent");
    }
    request.extent = *extent;
    request.channelSchema = an::ChannelSchema::Rgb;
    request.cfa = an::CfaDescriptor{};
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the mosaic has no colour description");
    }
    return *described;
}

/// The white level a mosaic's samples read at full exposure, which every threshold of the demosaic is derived from: an
/// integer's largest code, and one for a floating sample stated as a fraction of full scale. A floating mosaic in a
/// physical or sensor-code domain has no full scale. One whose meaningful finite samples lie inside zero to one but
/// for a few - no more than a quarter of them above one - is a fraction of full scale, as a light calibrated into
/// fractions is, and is demosaiced with a white level of one whatever its brightest sample: a light divided by a flat
/// whose dark border reads nearly zero holds samples in the hundreds of thousands there, and as its white level they
/// made the rest of the frame read as nothing, below the demosaic's smallest threshold. Any other floating mosaic's
/// brightest meaningful finite sample stands for full scale. The quarter, rather than the half a median tells, is
/// what keeps sensor codes over a dark sky apart: a calibrated light in codes whose sky sits under one code has its
/// noise - several codes wide - carry far more than a quarter of its samples above one, where fractions of full
/// scale have next to none there.
template <typename SampleT>
[[nodiscard]] an::Result<double> whiteLevelOf(const an::ExecutionContext& execution, const an::Image<SampleT>& mosaic,
                                              const an::ImageView<const SampleT>& view,
                                              const an::ValidityMaskView<const std::uint8_t>* mask)
{
    if constexpr (!std::is_floating_point_v<SampleT>)
    {
        return static_cast<double>((std::numeric_limits<SampleT>::max)());
    }
    else
    {
        const std::optional<an::ImageDescriptor> stored = mosaic.descriptor();
        if (!stored.has_value() || (stored->numericDomain() != an::NumericDomain::PhysicalScaled &&
                                    stored->numericDomain() != an::NumericDomain::RawSensorCode))
        {
            return 1.0;
        }
        // The brightest sample and the upper quartile are measured among the finite ones the image's rule admits; they
        // do not restate which samples mean something.
        const an::ValidityDescriptor validity = mosaic.metadata().semantics().validity();
        double brightest = 0.0;
        bool fractions = false;
        if (validity.isAllValid())
        {
            an::StatisticsSelection selection;
            selection.validity =
                an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly})
                    .value_or(validity);
            constexpr std::array<double, 1u> kUpperQuartile{0.75};
            std::array<double, 1u> upperQuartile{};
            const an::Result<an::RobustStatistics> measured = an::computeImageRobustStatistics(
                execution, an::kSampleTypeOf<SampleT>, view.data(), view.layout(), 0u, selection,
                an::RobustStatisticsRequest{.quantiles = kUpperQuartile}, upperQuartile);
            if (!measured.hasValue())
            {
                // A mosaic with no finite sample has no brightest one, and nothing for a white level to clip.
                if (measured.status().code() == an::ErrorCode::EmptySelection)
                {
                    return 1.0;
                }
                return measured.status();
            }
            brightest = measured->maximum;
            fractions = upperQuartile.front() <= 1.0;
        }
        else
        {
            // Another rule may keep a non-finite sample among those that mean something, and the statistics refuse a
            // selection holding one, so the plane is read here: a sample counts when the rule, its mask and its
            // finiteness all admit it. Their upper quartile is at most one when no more than a quarter of them lie
            // above one.
            std::uint64_t counted = 0u;
            std::uint64_t aboveOne = 0u;
            for (std::uint32_t y = 0u; y < view.height(); ++y)
            {
                if (an::Status stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
                const std::optional<std::span<const SampleT>> row = view.row(0u, y);
                if (!row.has_value())
                {
                    return refusal(an::ErrorCode::Internal, "the mosaic's row could not be read");
                }
                for (std::uint32_t x = 0u; x < view.width(); ++x)
                {
                    const SampleT sample = (*row)[x];
                    if (!std::isfinite(sample) || !an::sampleIsValidByValue(validity, sample) ||
                        (mask != nullptr && !mask->test(0u, x, y).value_or(false)))
                    {
                        continue;
                    }
                    brightest = (std::max)(brightest, static_cast<double>(sample));
                    ++counted;
                    if (static_cast<double>(sample) > 1.0)
                    {
                        ++aboveOne;
                    }
                }
            }
            fractions = aboveOne <= counted / 4u;
        }
        return !fractions && std::isfinite(brightest) && brightest > 1.0
                   ? (std::min)(brightest, static_cast<double>((std::numeric_limits<float>::max)()))
                   : 1.0;
    }
}

/// Writes the answer a colour image's mask gives into its samples: every pixel the mask clears made non-finite in each
/// colour. An integer image cannot hold a non-finite sample, so one whose mask clears any pixel is refused; under a
/// rule that only non-finite samples mean nothing, an integer mosaic's mask clears none.
template <typename SampleT>
[[nodiscard]] an::Status carryMaskAsNonFinite(const an::ExecutionContext& execution, an::Image<SampleT>& colour,
                                              const an::ValidityMaskView<std::uint8_t>& mask)
{
    std::optional<an::ImageWriteAccess<SampleT>> access = colour.write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the colour image holds nothing");
    }
    const auto view = access->view();
    for (std::uint32_t y = 0u; y < view.height(); ++y)
    {
        if (an::Status stopped = execution.checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        for (std::uint32_t x = 0u; x < view.width(); ++x)
        {
            if (mask.test(0u, x, y).value_or(false))
            {
                continue;
            }
            if constexpr (!std::is_floating_point_v<SampleT>)
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "an integer colour image cannot hold the pixels its mosaic left meaning nothing");
            }
            else
            {
                for (std::uint32_t channel = 0u; channel < view.channelCount(); ++channel)
                {
                    const std::optional<std::span<SampleT>> row = view.row(channel, y);
                    if (!row.has_value())
                    {
                        return refusal(an::ErrorCode::Internal, "the colour image's row could not be written");
                    }
                    (*row)[x] = std::numeric_limits<SampleT>::quiet_NaN();
                }
            }
        }
    }
    return an::Status::success();
}

/// A colour image a demosaic made, the mask saying which of its samples mean something when the mosaic stated a rule
/// other than every sample meaning something, and the method that ran.
struct Demosaiced
{
    an::consumer::AnyImage image;
    std::optional<an::ValidityMask> validity;
    an::DemosaicMethod method = an::DemosaicMethod::Rcd;
};

/// Demosaics a typed mosaic under an admitted operation. The mosaic's validity - its rule, and its mask when the rule
/// names one - reaches the engine, and a colour pixel estimated from a sample that means nothing is marked so in the
/// mask the result carries. With `nonFiniteCarriesValidity`, a mosaic whose rule is that its non-finite samples mean
/// nothing makes a colour image under the same rule instead: every colour pixel the mask would clear is written
/// non-finite in each colour, so the answer travels in the samples and no mask is returned.
template <typename SampleT>
[[nodiscard]] an::Result<Demosaiced>
demosaicTyped(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& mosaic,
              const an::ValidityMask* validity, an::DemosaicMethod method,
              const std::optional<an::CfaDescriptor>& pattern = std::nullopt, bool nonFiniteCarriesValidity = false)
{
    const std::optional<an::ImageDescriptor> stored = mosaic.descriptor();
    const auto view = mosaic.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mosaic holds nothing");
    }
    const an::CfaDescriptor filters = pattern.value_or(stored->cfa());
    if (!filters.isMosaiced() || stored->extent().channelCount() != 1u)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image is not a mosaic");
    }
    // The product's one rule: over a mosaic of the other shape - an X-Trans table - the chosen method's counterpart.
    const an::DemosaicMethod applied = demosaicMethodForMosaic(method, filters);
    const an::ValidityDescriptor rule = mosaic.metadata().semantics().validity();
    std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask;
    if (rule.rule() == an::ValidityRule::ExplicitMask)
    {
        if (validity == nullptr)
        {
            return refusal(
                an::ErrorCode::IncompatibleArguments,
                "the mosaic states that a mask decides which of its samples mean something, and carries none");
        }
        sourceMask = validity->view();
        if (!sourceMask.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mosaic's mask holds nothing");
        }
    }
    const an::Result<double> whiteLevel =
        whiteLevelOf(execution, mosaic, *view, sourceMask.has_value() ? &*sourceMask : nullptr);
    if (!whiteLevel.hasValue())
    {
        return whiteLevel.status();
    }
    const an::Result<an::DemosaicPlan> plan =
        an::planDemosaic(an::DemosaicRequest{.method = applied, .domain = {.whiteLevel = *whiteLevel}}, filters);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> colour = colourDescriptorOf(*stored, engine.limits());
    if (!colour.hasValue())
    {
        return colour.status();
    }
    an::Result<an::Image<SampleT>> result = an::Image<SampleT>::createDestination(
        engine, *colour, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, mosaic);
    if (!result.hasValue())
    {
        return result.status();
    }
    // A mosaic every sample of which means something makes a colour image of which every sample does; any other rule
    // makes a per-pixel answer, which only a mask - one plane, shared by the three colours - can carry.
    std::optional<an::ValidityMask> produced;
    std::optional<an::ValidityMaskView<std::uint8_t>> producedView;
    if (!rule.isAllValid())
    {
        const std::optional<an::ImageExtent> plane =
            an::ImageExtent::create(stored->extent().width(), stored->extent().height(), 1u, engine.limits());
        if (!plane.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the colour image's mask has no extent");
        }
        an::Result<an::ValidityMask> made = an::ValidityMask::create(engine, *plane);
        if (!made.hasValue())
        {
            return made.status();
        }
        produced.emplace(std::move(*made));
        producedView = produced->view();
        if (!producedView.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the colour image's mask holds nothing");
        }
    }
    an::DemosaicSelection selection;
    selection.validity = rule;
    selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
    selection.produced = producedView.has_value() ? &*producedView : nullptr;
    an::Result<an::DemosaicResult> ran = refusal(an::ErrorCode::Internal, "the demosaic did not run");
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the colour image holds nothing");
        }
        ran = an::demosaicImage(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                access->view().data(), access->view().layout(), *plan, selection);
    }
    if (!ran.hasValue())
    {
        return ran.status();
    }
    an::ImageSemanticsRequest meaning = mosaic.metadata().semantics().toRequest();
    meaning.validity = ran->outputValidity;
    if (nonFiniteCarriesValidity && rule.rule() == an::ValidityRule::FiniteOnly && producedView.has_value())
    {
        if (an::Status carried = carryMaskAsNonFinite(execution, *result, *producedView); carried.failed())
        {
            return carried;
        }
        meaning.validity = rule;
        produced.reset();
    }
    meaning.processingState.demosaiced = true;
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        an::kDemosaicEffect,
        an::OperationOutcome{.operation = "an::demosaicImage",
                             .detail = std::string("the view's demosaic, ") + an::demosaicMethodName(applied),
                             .semantics = *semantics});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = result->transition(*change); stated.failed())
    {
        return stated;
    }
    return Demosaiced{
        .image = an::consumer::AnyImage(std::move(*result)), .validity = std::move(produced), .method = applied};
}

/// What the view's turn or mirror does to an image's metadata: the engine's geometry effect, with the validity rule
/// kept, because every destination sample is one source sample and a mask turns with them.
inline constexpr an::OperationEffect kViewOrientationEffect{.description = an::DescriptionEffect::Restates,
                                                            .solution = an::SolutionEffect::Invalidates,
                                                            .codecMetadata = an::CodecMetadataEffect::Drops,
                                                            .outputIntent = an::OutputIntentEffect::Withdraws,
                                                            .validity = {.policy = an::ValidityPolicy::Preserve}};

/// The name a turn or mirror is recorded under in an image's lineage.
[[nodiscard]] const char* orientationLineage(an::ImageOrientation orientation) noexcept
{
    switch (orientation)
    {
    case an::ImageOrientation::MirrorHorizontal:
        return "the view's horizontal mirror";
    case an::ImageOrientation::MirrorVertical:
        return "the view's vertical mirror";
    case an::ImageOrientation::Rotate180:
        return "the view's half turn";
    case an::ImageOrientation::Rotate90Clockwise:
        return "the view's quarter turn right";
    case an::ImageOrientation::Rotate270Clockwise:
        return "the view's quarter turn left";
    case an::ImageOrientation::Identity:
    case an::ImageOrientation::Transpose:
    case an::ImageOrientation::Transverse:
        break;
    }
    return "the view's orientation";
}

/// What turning a camera RAW frame by the orientation its camera recorded does when the frame is opened: every sample
/// is one of the decoded frame's, so its validity rule stays, and the cards the camera wrote still describe the frame
/// it took, so they stay too.
inline constexpr an::OperationEffect kRawOrientationEffect{.description = an::DescriptionEffect::Restates,
                                                           .solution = an::SolutionEffect::Invalidates,
                                                           .codecMetadata = an::CodecMetadataEffect::Preserves,
                                                           .outputIntent = an::OutputIntentEffect::Withdraws,
                                                           .validity = {.policy = an::ValidityPolicy::Preserve}};

/// What changing the representation an image is written in does to the copy written: the samples are written again and
/// the description is restated; nothing else about the image changes.
inline constexpr an::OperationEffect kWriteRepresentationEffect{.samples = an::SampleEffect::Writes,
                                                                .description = an::DescriptionEffect::Restates};

/// Turns or mirrors a typed image, and its explicit mask, under an admitted operation, recording the change with the
/// effect and the lineage the caller states.
template <typename SampleT>
[[nodiscard]] an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>>
orientTyped(const an::consumer::OperationScope& scope, const an::Image<SampleT>& image,
            const an::ValidityMask* validity, an::ImageOrientation orientation, const an::OperationEffect& effect,
            const char* lineage)
{
    using Oriented = std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>;
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::Result<an::GeometryPlan> plan = an::planOrientation(*stored, orientation);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<SampleT>> result =
        an::Image<SampleT>::createDestination(scope.engine(), plan->destinationDescriptor(),
                                              an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, image);
    if (!result.hasValue())
    {
        return result.status();
    }
    an::Result<an::ValidityMask> written =
        an::ValidityMask::create(scope.engine(), plan->destinationDescriptor().extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the turned image's mask holds nothing");
    }
    const an::ValidityDescriptor rule = image.metadata().semantics().validity();
    std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask;
    if (validity != nullptr)
    {
        sourceMask = validity->view();
    }
    an::GeometrySelection selection;
    selection.validity = rule;
    selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the turned image holds nothing");
        }
        const an::Result<an::GeometryResult> ran =
            an::transformImageGeometry(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout(), *plan, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    an::Result<an::MetadataTransition> change = an::operationTransition(
        effect, an::OperationOutcome{.operation = "an::transformImageGeometry", .detail = lineage});
    if (!change.hasValue())
    {
        return change.status();
    }
    // A turned or mirrored frame is still that frame: the cards that described its layout go, and its observing context
    // - optics, site, pointing, camera settings and time - stays, as the engine carries a derived product's.
    if (change->clearCodecMetadata)
    {
        an::Result<an::CodecMetadata> observing = derivedFrameCards(image.metadata().codec());
        if (!observing.hasValue())
        {
            return observing.status();
        }
        if (!observing->cards().empty())
        {
            change->clearCodecMetadata = false;
            change->codec = std::move(*observing);
        }
    }
    if (an::Status stated = result->transition(*change); stated.failed())
    {
        return stated;
    }
    // The mask the operation wrote is the turned validity only where the source stated one; an image every sample of
    // which is valid keeps that rule and carries no mask.
    std::optional<an::ValidityMask> turnedMask;
    if (validity != nullptr)
    {
        turnedMask.emplace(std::move(*written));
    }
    return Oriented{an::consumer::AnyImage(std::move(*result)), std::move(turnedMask)};
}

/// Whether an image is a mosaic, by its description.
[[nodiscard]] bool isMosaic(const an::consumer::AnyImage& image) noexcept
{
    return std::visit(
        [](const auto& typed) {
            const std::optional<an::ImageDescriptor> stored = typed.descriptor();
            return stored.has_value() && stored->isMosaiced();
        },
        image);
}

/// The name a mosaic balance is recorded under in an image's lineage.
[[nodiscard]] const char* mosaicBalanceLineage(an::CfaWhiteBalanceMode mode) noexcept
{
    switch (mode)
    {
    case an::CfaWhiteBalanceMode::None:
        return "the RAW open's filter-array balance, equal gains";
    case an::CfaWhiteBalanceMode::Daylight:
        return "the RAW open's filter-array balance, daylight";
    case an::CfaWhiteBalanceMode::Camera:
        return "the RAW open's filter-array balance, as shot";
    case an::CfaWhiteBalanceMode::Automatic:
        return "the RAW open's filter-array balance, automatic";
    case an::CfaWhiteBalanceMode::Custom:
        return "the RAW open's filter-array balance, custom";
    }
    return "the RAW open's filter-array balance";
}

/// Balances a camera mosaic's black-subtracted codes: each filter's gain from the mode, the codes stretched so a code
/// at the white level reads 65535, and every product truncated.
[[nodiscard]] an::Result<an::consumer::AnyImage> balanceCameraMosaic(const an::consumer::OperationScope& scope,
                                                                     const an::Image<std::uint16_t>& mosaic,
                                                                     const an::RawCameraMetadata& camera,
                                                                     const ViewRawTreatment& treatment)
{
    const std::optional<an::ImageDescriptor> stored = mosaic.descriptor();
    const auto view = mosaic.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mosaic holds nothing");
    }
    an::CfaWhiteBalanceRequest request{
        .mode = treatment.mosaicBalance,
        .custom = {treatment.customGains[0], treatment.customGains[1], treatment.customGains[2]}};
    std::optional<an::CfaGreyWorld> measured;
    if (request.mode == an::CfaWhiteBalanceMode::Automatic)
    {
        const an::Result<an::CfaChannelValues> clip = an::cfaGreyWorldClipLevels(camera);
        if (!clip.hasValue())
        {
            return clip.status();
        }
        const an::Result<an::CfaGreyWorld> grey =
            an::measureCfaGreyWorld(scope.execution(), an::SampleType::UInt16, view->data(), view->layout(),
                                    stored->cfa(), an::CfaGreyWorldRequest{.clippedAbove = *clip});
        if (!grey.hasValue())
        {
            return grey.status();
        }
        measured = *grey;
        request.measured = &*measured;
    }
    const an::Result<an::CfaWhiteBalance> balance = an::resolveCfaWhiteBalance(camera, request);
    if (!balance.hasValue())
    {
        return balance.status();
    }
    an::Result<an::Image<std::uint16_t>> result = an::Image<std::uint16_t>::createDestination(
        scope.engine(), *stored, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, mosaic);
    if (!result.hasValue())
    {
        return result.status();
    }
    {
        std::optional<an::ImageWriteAccess<std::uint16_t>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the balanced mosaic holds nothing");
        }
        const an::Result<an::CfaChannelResult> gained = an::applyCfaChannelGains(
            scope.execution(), an::SampleType::UInt16, view->data(), view->layout(), stored->cfa(),
            access->view().data(), access->view().layout(), balance->multipliers(),
            an::ConversionPolicy{.rounding = an::RoundingMode::TowardZero, .domain = an::DomainMapping::CodeValue});
        if (!gained.hasValue())
        {
            return gained.status();
        }
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        an::kCfaChannelGainsEffect, an::OperationOutcome{.operation = "an::applyCfaChannelGains",
                                                         .detail = mosaicBalanceLineage(treatment.mosaicBalance),
                                                         .semantics = mosaic.metadata().semantics()});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = result->transition(*change); stated.failed())
    {
        return stated;
    }
    return an::consumer::AnyImage(std::move(*result));
}

/// Treats a decoded camera RAW frame as a read asks: a mosaic balanced by the filter-array step, then the frame turned
/// by the orientation its camera recorded. A frame that states no camera record is returned as it is.
[[nodiscard]] an::Result<an::consumer::AnyImage>
treatCameraFrame(const an::consumer::EngineSession& session, an::consumer::AnyImage&& image,
                 const ViewRawTreatment& treatment, an::consumer::OperationPurpose purpose, const std::stop_token& stop)
{
    const std::optional<an::RawCameraMetadata> camera =
        std::visit([](const auto& typed) { return typed.metadata().acquisition().rawCamera(); }, image);
    if (!camera.has_value())
    {
        return std::move(image);
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, purpose, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    if (const auto* mosaic = std::get_if<an::Image<std::uint16_t>>(&image);
        mosaic != nullptr && mosaic->descriptor().has_value() && mosaic->descriptor()->isMosaiced())
    {
        an::Result<an::consumer::AnyImage> balanced = balanceCameraMosaic(*scope, *mosaic, *camera, treatment);
        if (!balanced.hasValue())
        {
            return balanced.status();
        }
        image = std::move(*balanced);
    }
    if (treatment.autoRotate)
    {
        const std::optional<an::ImageOrientation> orientation = an::imageOrientationFromRawFlip(camera->orientation);
        if (orientation.has_value() && *orientation != an::ImageOrientation::Identity)
        {
            an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>> oriented = std::visit(
                [&](const auto& typed) {
                    return orientTyped(*scope, typed, nullptr, *orientation, kRawOrientationEffect,
                                       "the RAW open's turn by the camera's recorded orientation");
                },
                image);
            if (!oriented.hasValue())
            {
                return oriented.status();
            }
            image = std::move(oriented->first);
        }
    }
    return std::move(image);
}

/// The description a copy written in another representation states: the image's own, with its samples re-represented.
/// A floating image in a physical or sensor-code domain has no full scale and no integer description, so it is
/// described as a fraction of full scale first, which is how the view shows it.
[[nodiscard]] an::Result<an::ImageDescriptor> writtenDescriptor(const an::ImageDescriptor& stored,
                                                                an::SampleType target)
{
    an::ImageDescriptor source = stored;
    const bool floating =
        stored.sampleType() == an::SampleType::Float32 || stored.sampleType() == an::SampleType::Float64;
    const bool targetFloating = target == an::SampleType::Float32 || target == an::SampleType::Float64;
    if (floating && !targetFloating &&
        (stored.numericDomain() == an::NumericDomain::PhysicalScaled ||
         stored.numericDomain() == an::NumericDomain::RawSensorCode))
    {
        an::ImageDescriptorRequest restated = stored.toRequest();
        restated.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> normalized = an::ImageDescriptor::create(restated);
        if (!normalized.has_value())
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "the floating image has no description as a fraction of full scale");
        }
        source = *normalized;
    }
    return an::describeSamplesAs(source, target);
}

/// A copy of a typed image in another representation, converted at corresponding full scales to the nearest value, with
/// the conversion recorded in its lineage.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage> convertForWrite(const an::EngineContext& engine,
                                                                 const an::ExecutionContext& execution,
                                                                 const an::Image<SampleT>& image, an::SampleType target)
{
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::Result<an::ImageDescriptor> described = writtenDescriptor(*stored, target);
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::consumer::AnyImage> converted = refusal(an::ErrorCode::Internal, "no representation was converted");
    const bool dispatched = an::dispatchSampleType(target, [&]<typename TargetT>() {
        an::Result<an::Image<TargetT>> copy = an::Image<TargetT>::createDestination(
            engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
        if (!copy.hasValue())
        {
            converted = copy.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<TargetT>> access = copy->write();
            if (!access.has_value())
            {
                converted = refusal(an::ErrorCode::NotInitialized, "the converted copy holds nothing");
                return;
            }
            an::ConversionCounters counters;
            const an::Status done = an::convertImageSamples(
                execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(), an::kSampleTypeOf<TargetT>,
                access->view().data(), access->view().layout(), an::ConversionPolicy{}, counters);
            if (done.failed())
            {
                converted = done;
                return;
            }
        }
        const an::Result<an::MetadataTransition> change = an::operationTransition(
            kWriteRepresentationEffect,
            an::OperationOutcome{
                .operation = "an::convertImageSamples",
                .detail = std::string("the write's representation, ") + an::sampleTypeName(an::kSampleTypeOf<SampleT>) +
                          " to " + an::sampleTypeName(an::kSampleTypeOf<TargetT>) + ", nearest value at full scale"});
        if (!change.hasValue())
        {
            converted = change.status();
            return;
        }
        if (an::Status recorded = copy->transition(*change); recorded.failed())
        {
            converted = std::move(recorded);
            return;
        }
        converted = an::consumer::AnyImage(std::move(*copy));
    });
    if (!dispatched)
    {
        return refusal(an::ErrorCode::InvalidArgument, "the intent states a representation outside the eight");
    }
    return converted;
}

/// Writes a typed image whose representation is the intent's through the output transaction.
template <typename SampleT>
[[nodiscard]] an::Result<an::OutputReport> writeTyped(const an::consumer::OperationScope& scope,
                                                      const an::Image<SampleT>& image, const std::string& destination,
                                                      an::CodecFormat format, const an::OutputIntent& intent)
{
    an::Result<an::RawFrame> frame = an::borrowImageFrame(image);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    an::OutputRequest request;
    request.destinationPath = destination;
    request.format = format;
    request.intent = &intent;
    request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
    return an::writeImage(scope.engine(), scope.execution(), request, *frame, image.metadata());
}

/// The container's own name, for a message.
[[nodiscard]] const char* containerName(an::CodecFormat format) noexcept
{
    switch (format)
    {
    case an::CodecFormat::Fits:
        return "FITS";
    case an::CodecFormat::Png:
        return "PNG";
    case an::CodecFormat::Jpeg:
        return "JPEG";
    default:
        break;
    }
    return "this container";
}

} // namespace

DemosaicChoice demosaicMethodForSetting(const QString& key) noexcept
{
    return demosaicMethodFromSettingsKey(key);
}

bool containerHoldsFilterArray(an::CodecFormat format, an::CfaPattern pattern) noexcept
{
    if (format == an::CodecFormat::Fits)
    {
        // FITS states a mosaic's filters in its one two-by-two pattern card, and its codec refuses a tabulated mosaic.
        return !an::cfaPatternIsTabulated(pattern);
    }
    return format == an::CodecFormat::Xisf || format == an::CodecFormat::Tiff;
}

QString viewCalibrationKey(const ViewCalibration& calibration)
{
    const CalibrationPreviewSettings& settings = calibration.settings;
    return QStringLiteral("bias=%1|dark=%2|flat=%3|lessBias=%4|cosmetic=%5,%6,%7|darkOpt=%8|darkScale=%9|flatAuto=%10|"
                          "pedestal=%11")
        .arg(calibration.biasPath, calibration.darkPath, calibration.flatPath)
        .arg(calibration.subtractBiasFromDark ? 1 : 0)
        .arg(settings.cosmeticCorrection ? 1 : 0)
        .arg(settings.sigmaCold, 0, 'g', 17)
        .arg(settings.sigmaHot, 0, 'g', 17)
        .arg(settings.darkOptimization ? 1 : 0)
        .arg(settings.darkMultiplicationFactor, 0, 'g', 17)
        .arg(settings.flatAutoLevel ? 1 : 0)
        .arg(settings.pedestal, 0, 'g', 17);
}

ViewMasterCache::ViewMasterCache(std::size_t capacity) : m_capacity(capacity) {}

std::shared_ptr<const an::consumer::ImageAsset> ViewMasterCache::find(const QString& path)
{
    std::lock_guard lock(m_mutex);
    for (auto entry = m_order.begin(); entry != m_order.end(); ++entry)
    {
        if (entry->first == path)
        {
            m_order.splice(m_order.begin(), m_order, entry);
            return m_order.front().second;
        }
    }
    return nullptr;
}

void ViewMasterCache::keep(const QString& path, std::shared_ptr<const an::consumer::ImageAsset> master)
{
    std::lock_guard lock(m_mutex);
    m_order.remove_if([&path](const Entry& entry) { return entry.first == path; });
    if (m_capacity == 0u)
    {
        return;
    }
    while (m_order.size() >= m_capacity)
    {
        m_order.pop_back();
    }
    m_order.emplace_front(path, std::move(master));
}

void ViewMasterCache::clear()
{
    std::lock_guard lock(m_mutex);
    m_order.clear();
}

std::size_t ViewMasterCache::size() const
{
    std::lock_guard lock(m_mutex);
    return m_order.size();
}

an::Result<ViewReadResult> readViewImage(const an::consumer::EngineSession& session, const ViewReadRequest& request,
                                         std::stop_token stop, ViewMasterCache* masters)
{
    const an::consumer::OperationPurpose purpose = request.calibration.has_value()
                                                       ? an::consumer::OperationPurpose::Preview
                                                       : an::consumer::OperationPurpose::Apply;
    an::Result<an::consumer::AnyImage> decoded = decodeFile(session, request.path, request.registry, purpose, stop);
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    ViewReadResult result;
    an::consumer::AnyImage image = std::move(*decoded);
    if (request.raw.has_value())
    {
        an::Result<an::consumer::AnyImage> treated =
            treatCameraFrame(session, std::move(image), *request.raw, purpose, stop);
        if (!treated.hasValue())
        {
            return treated.status();
        }
        image = std::move(*treated);
    }
    if (request.calibration.has_value())
    {
        an::Result<an::consumer::AnyImage> calibrated =
            calibrateLight(session, std::move(image), *request.calibration, purpose, stop, masters);
        if (!calibrated.hasValue())
        {
            return calibrated.status();
        }
        image = std::move(*calibrated);
        result.calibrated = true;
    }
    std::optional<an::ValidityMask> validity;
    if (isMosaic(image))
    {
        if (request.demosaic.has_value())
        {
            an::Result<an::consumer::OperationScope> scope = admitStep(session, purpose, stop);
            if (!scope.hasValue())
            {
                return scope.status();
            }
            // A decoded, treated or calibrated frame carries no mask of its own.
            an::Result<Demosaiced> colour = std::visit(
                [&](const auto& typed) {
                    return demosaicTyped(scope->engine(), scope->execution(), typed, nullptr, *request.demosaic);
                },
                image);
            if (!colour.hasValue())
            {
                return colour.status();
            }
            image = std::move(colour->image);
            validity = std::move(colour->validity);
            result.demosaiced = colour->method;
        }
        else
        {
            result.mosaic = true;
        }
    }
    an::Result<an::consumer::ImageAsset> adopted =
        an::consumer::ImageAsset::adopt(session, std::move(image), std::move(validity));
    if (!adopted.hasValue())
    {
        return adopted.status();
    }
    result.asset = std::move(*adopted);
    return result;
}

an::Result<an::consumer::ImageAsset> demosaicViewImage(const an::consumer::EngineSession& session,
                                                       const an::consumer::ImageAsset& mosaic,
                                                       an::DemosaicMethod method, std::stop_token stop)
{
    an::Result<an::consumer::AssetReadLease> lease = mosaic.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, an::consumer::OperationPurpose::Apply, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<Demosaiced> colour = lease->visit([&](const auto& typed) {
        return demosaicTyped(scope->engine(), scope->execution(), typed, lease->validity(), method);
    });
    if (!colour.hasValue())
    {
        return colour.status();
    }
    scope->end();
    lease->release();
    return an::consumer::ImageAsset::adopt(session, std::move(colour->image), std::move(colour->validity));
}

an::Result<an::consumer::AnyImage> importViewFrame(const an::EngineContext& engine,
                                                   const an::ExecutionContext& execution, an::RawFrame&& frame,
                                                   an::ImageMetadata&& metadata, bool planar)
{
    return importFrame(engine, execution, std::move(frame), std::move(metadata), planar);
}

an::Result<an::consumer::AnyImage> demosaicViewFrame(const an::EngineContext& engine,
                                                     const an::ExecutionContext& execution,
                                                     const an::consumer::AnyImage& mosaic, an::DemosaicMethod method,
                                                     const std::optional<an::CfaDescriptor>& pattern)
{
    const an::ValidityRule rule =
        std::visit([](const auto& typed) { return typed.metadata().semantics().validity().rule(); }, mosaic);
    if (rule != an::ValidityRule::AllValid && rule != an::ValidityRule::FiniteOnly)
    {
        // Any other rule's answer is a mask, which an image alone has nowhere to carry. A light calibrated in memory
        // calls only its non-finite samples meaningless, and that answer travels in the colour image's samples.
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the mosaic states which of its samples mean something, and a demosaic that returns the "
                       "colour image alone cannot carry the mask that answers for it");
    }
    an::Result<Demosaiced> colour = std::visit(
        [&](const auto& typed) { return demosaicTyped(engine, execution, typed, nullptr, method, pattern, true); },
        mosaic);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    return std::move(colour->image);
}

an::Result<ViewDemosaic> demosaicViewFrameWithValidity(const an::EngineContext& engine,
                                                       const an::ExecutionContext& execution,
                                                       const an::consumer::AnyImage& mosaic,
                                                       const an::ValidityMask* validity, an::DemosaicMethod method,
                                                       const std::optional<an::CfaDescriptor>& pattern)
{
    an::Result<Demosaiced> colour = std::visit(
        [&](const auto& typed) { return demosaicTyped(engine, execution, typed, validity, method, pattern); }, mosaic);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    return ViewDemosaic{.image = std::move(colour->image), .validity = std::move(colour->validity)};
}

an::Result<an::consumer::AnyImage> convertViewFrame(const an::EngineContext& engine,
                                                    const an::ExecutionContext& execution,
                                                    const an::consumer::AnyImage& image, an::SampleType target)
{
    return std::visit([&](const auto& typed) { return convertForWrite(engine, execution, typed, target); }, image);
}

an::Result<ViewImageStatistics> measureViewImage(const an::consumer::EngineSession& session,
                                                 const an::consumer::ImageAsset& image, std::stop_token stop)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, an::consumer::OperationPurpose::Preview, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    if (const an::ValidityMask* mask = lease->validity(); mask != nullptr)
    {
        maskView = mask->view();
    }
    return lease->visit([&](const auto& typed) -> an::Result<ViewImageStatistics> {
        using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
        const auto view = typed.view();
        const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
        if (!view.has_value() || !descriptor.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
        }
        const std::uint32_t stored = view->channelCount();
        const std::uint32_t colour = descriptor->channelSchema() == an::ChannelSchema::Rgba ? 3u : stored;
        ViewImageStatistics statistics;
        statistics.channels = (std::min)(colour, 3u);
        an::StatisticsSelection selection;
        selection.validity = typed.metadata().semantics().validity();
        selection.mask = maskView.has_value() ? &*maskView : nullptr;
        for (std::uint32_t channel = 0u; channel < statistics.channels; ++channel)
        {
            const an::Result<an::BasicStatistics> basic = an::computeImageBasicStatistics(
                scope->execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(), channel, selection);
            if (!basic.hasValue())
            {
                return basic.status();
            }
            const an::Result<an::RobustStatistics> robust =
                an::computeImageRobustStatistics(scope->execution(), an::kSampleTypeOf<SampleT>, view->data(),
                                                 view->layout(), channel, selection, an::RobustStatisticsRequest{});
            if (!robust.hasValue())
            {
                return robust.status();
            }
            statistics.channel[channel] =
                ViewChannelStatistics{.minimum = basic->minimum,
                                      .maximum = basic->maximum,
                                      .mean = basic->mean,
                                      .median = robust->median,
                                      .standardDeviation = basic->populationStandardDeviation};
        }
        return statistics;
    });
}

an::Result<an::CodecMetadata> derivedFrameCards(const an::CodecMetadata& frame)
{
    // The light the frame holds, as it states it, ahead of its observing context.
    constexpr std::array<std::string_view, 5u> kIntegrationKeywords{"EXPTIME", "TOTEXP", "LIVETIME", "STACKCNT",
                                                                    "NCOMBINE"};
    std::vector<an::CodecCard> light;
    for (const an::CodecCard& card : frame.cards())
    {
        if (card.origin() == an::CodecNamespace::Fits &&
            std::find(kIntegrationKeywords.begin(), kIntegrationKeywords.end(), card.keyword()) !=
                kIntegrationKeywords.end())
        {
            light.push_back(card);
        }
    }
    an::Result<an::CodecMetadata> kept = an::CodecMetadata::create(an::SourceEncoding{}, std::move(light));
    if (!kept.hasValue())
    {
        return kept.status();
    }
    return an::withObservationCards(*kept, frame);
}

an::Status writeViewImage(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image,
                          const QString& path, const an::OutputIntent& intent, std::stop_token stop)
{
    const std::string destination = enginePath(path);
    const an::CodecFormat format = an::codecFormatFromPath(destination);
    if (format == an::CodecFormat::Unknown)
    {
        return refusal(an::ErrorCode::Unsupported, "no container is named by '" + destination + "'");
    }
    an::Result<an::consumer::AssetReadLease> lease = image.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const std::optional<an::CfaPattern> mosaic = lease->visit([](const auto& typed) -> std::optional<an::CfaPattern> {
        const std::optional<an::ImageDescriptor> stored = typed.descriptor();
        if (!stored.has_value() || !stored->isMosaiced())
        {
            return std::nullopt;
        }
        return stored->cfa().pattern();
    });
    if (mosaic.has_value() && !containerHoldsFilterArray(format, *mosaic))
    {
        // Decided before any work: the container's codec would refuse the mosaic only after the conversion.
        if (an::cfaPatternIsTabulated(*mosaic))
        {
            return refusal(an::ErrorCode::Unsupported,
                           std::string("an X-Trans mosaic cannot be written as ") + containerName(format) +
                               ", which cannot state its six-by-six filter array; write it as XISF or TIFF, or "
                               "demosaic it first");
        }
        return refusal(an::ErrorCode::Unsupported,
                       std::string("a colour filter array mosaic cannot be written as ") + containerName(format) +
                           ", which cannot state its filter array; write it as FITS, XISF or TIFF, or demosaic it "
                           "first");
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, an::consumer::OperationPurpose::Apply, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const an::Result<an::OutputReport> written = lease->visit([&](const auto& typed) -> an::Result<an::OutputReport> {
        using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
        if (an::kSampleTypeOf<SampleT> == intent.sampleType())
        {
            return writeTyped(*scope, typed, destination, format, intent);
        }
        an::Result<an::consumer::AnyImage> converted =
            convertForWrite(scope->engine(), scope->execution(), typed, intent.sampleType());
        if (!converted.hasValue())
        {
            return converted.status();
        }
        return std::visit([&](const auto& copy) { return writeTyped(*scope, copy, destination, format, intent); },
                          *converted);
    });
    if (!written.hasValue())
    {
        return written.status();
    }
    LOG_INFO("Image view: wrote {} ({} bytes)", destination, written->bytesWritten);
    return an::Status::success();
}

an::Result<an::consumer::ImageAsset> orientViewImage(const an::consumer::EngineSession& session,
                                                     const an::consumer::AnyImage& image,
                                                     const an::ValidityMask* validity, an::ImageOrientation orientation,
                                                     std::stop_token stop)
{
    if (!an::isImageOrientation(orientation))
    {
        return refusal(an::ErrorCode::InvalidArgument, "no orientation is named");
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, an::consumer::OperationPurpose::Apply, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>> oriented = std::visit(
        [&](const auto& typed) {
            return orientTyped(*scope, typed, validity, orientation, kViewOrientationEffect,
                               orientationLineage(orientation));
        },
        image);
    if (!oriented.hasValue())
    {
        return oriented.status();
    }
    scope->end();
    return an::consumer::ImageAsset::adopt(session, std::move(oriented->first), std::move(oriented->second));
}

std::optional<an::WcsSolution> viewImageSolution(const an::ImageMetadata& metadata, std::uint32_t width,
                                                 std::uint32_t height)
{
    if (width == 0u || height == 0u)
    {
        return std::nullopt;
    }
    an::Result<an::WcsSolution> parsed = an::ui::readImageWcsSolution(metadata.codec().cards(), width, height);
    if (!parsed.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*parsed);
}

an::Result<an::StarDetectionChannelResult> detectViewOverlayStars(const an::consumer::EngineSession& session,
                                                                  const an::consumer::ImageAsset& image,
                                                                  const an::WcsSolution& solution, std::stop_token stop)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, an::consumer::OperationPurpose::Preview, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    if (const an::ValidityMask* mask = lease->validity(); mask != nullptr)
    {
        maskView = mask->view();
    }
    return lease->visit([&](const auto& typed) -> an::Result<an::StarDetectionChannelResult> {
        const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
        const auto view = typed.view();
        if (!descriptor.has_value() || !view.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
        }
        // The frame view borrows its layout, so the layout it is given outlives the detection.
        const an::ImageLayout layout = view->layout();
        an::KernelValidityView validity;
        validity.descriptor = typed.metadata().semantics().validity();
        validity.mask = maskView.has_value() ? &*maskView : nullptr;
        an::Result<an::StarFrameView> frame =
            an::StarFrameView::create(view->data(), layout.totalBytes(), layout, *descriptor, validity);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        an::StarDetectionRequest request;
        request.searchRadius = 5u;
        request.noiseSigma = 1.0;
        request.minimumRoundness = 0.4;
        request.model = an::PsfModel::Gaussian;
        request.maximumStars = kViewOverlayMaximumStars;
        return an::detectStarsByChannelPolicy(scope->execution(), *frame, request,
                                              an::greenThenRedChannelPolicy(*descriptor), &solution);
    });
}

} // namespace an::ui
