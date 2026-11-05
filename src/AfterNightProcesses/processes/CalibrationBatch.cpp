// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Calibration's run over the engine: masters read once and widened as each working representation needs them,
/// the dark's cosmetic maps made once, and each light read, calibrated, corrected and written through the view's file
/// helpers.

#include "stdafx.hpp"
#include "processes/CalibrationBatch.h"

#include "controls/ImageViewFiles.h"
#include "processes/ImageListProcessHelpers.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/CosmeticCorrection/CosmeticCorrection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>

#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui::processes
{

namespace
{

/// The codes a sixteen-bit sample spans, by which the window's pedestal is divided into a fraction of full scale.
constexpr double kPedestalCodesPerUnit = 65535.0;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A failure that says what the run was doing, keeping the engine's failure as its cause.
[[nodiscard]] an::Status explained(an::Status cause, std::string message)
{
    const an::ErrorCode code = cause.code();
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)}, std::move(cause));
}

[[nodiscard]] std::string utf8(const QString& text)
{
    return text.toUtf8().toStdString();
}

[[nodiscard]] an::Result<an::consumer::OperationScope> admitStep(const an::consumer::EngineSession& session,
                                                                 const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        return refusal(an::ErrorCode::Cancelled, "the calibration was stopped");
    }
    return session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop});
}

/// Whether a file's extension is one of FITS's, case aside.
[[nodiscard]] bool isFitsSuffix(const QString& suffix)
{
    const QString lower = suffix.toLower();
    return lower == QStringLiteral("fit") || lower == QStringLiteral("fits") || lower == QStringLiteral("fts");
}

/// The representation of an image, whichever it is.
[[nodiscard]] an::SampleType sampleTypeOf(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            return an::kSampleTypeOf<SampleT>;
        },
        image);
}

/// The description of an image, whichever it is.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// A copy of an image in a floating representation with every sample a fraction of full scale: an integer over its
/// representation's largest value, a floating sample as it is.
template <typename WorkT>
[[nodiscard]] an::Result<an::Image<WorkT>> fractionsOf(const an::consumer::OperationScope& scope,
                                                       const an::consumer::AnyImage& image)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::Image<WorkT>> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> stored = typed.descriptor();
            const auto view = typed.view();
            if (!stored.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
            }
            an::ImageDescriptorRequest request = stored->toRequest();
            request.sampleType = an::kSampleTypeOf<WorkT>;
            request.numericDomain = an::NumericDomain::NormalizedLinear;
            const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
            if (!described.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments, "the frame has no floating description");
            }
            an::Result<an::Image<WorkT>> widened =
                an::Image<WorkT>::create(scope.engine(), *described, an::StorageArrangement::Planar,
                                         an::StorageInitialization::Uninitialized, typed.metadata());
            if (!widened.hasValue())
            {
                return widened.status();
            }
            std::optional<an::ImageWriteAccess<WorkT>> access = widened->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the widened frame holds nothing");
            }
            an::ConversionCounters counters;
            const an::Status converted = an::convertImageSamples(
                scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(), an::kSampleTypeOf<WorkT>,
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

/// The calibration view of a floating image.
template <typename WorkT>
[[nodiscard]] an::Result<an::CalibrationFrameView> calibrationViewOf(const an::Image<WorkT>& image)
{
    const auto view = image.view();
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a calibration frame holds nothing");
    }
    return an::CalibrationFrameView::create(view->data(), view->layout().totalBytes(), view->layout(), *descriptor);
}

/// The masters widened to one working representation.
template <typename WorkT> struct WorkingMasters
{
    std::optional<an::Image<WorkT>> bias;
    std::optional<an::Image<WorkT>> dark;
    std::optional<an::Image<WorkT>> flat;
    bool made = false;
};

/// Reads a file as the view reads it, under a lease the caller keeps.
struct ReadFrame
{
    an::consumer::ImageAsset asset;
    an::consumer::AssetReadLease lease;
};

[[nodiscard]] an::Result<ReadFrame> readFrame(const an::consumer::EngineSession& session, const QString& path,
                                              const std::stop_token& stop)
{
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = path}, stop, nullptr);
    if (!read.hasValue())
    {
        return read.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return ReadFrame{.asset = std::move(read->asset), .lease = std::move(*lease)};
}

} // namespace

CalibrationOutputFormat calibrationOutputFormatFromText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("tiff"))
    {
        return CalibrationOutputFormat::Tiff;
    }
    if (normalized == QStringLiteral("original"))
    {
        return CalibrationOutputFormat::Original;
    }
    return CalibrationOutputFormat::Fits;
}

QString calibrationOutputFormatText(const CalibrationOutputFormat format)
{
    switch (format)
    {
    case CalibrationOutputFormat::Tiff:
        return QStringLiteral("tiff");
    case CalibrationOutputFormat::Original:
        return QStringLiteral("original");
    case CalibrationOutputFormat::Fits:
        break;
    }
    return QStringLiteral("fits");
}

bool calibrationOutputPrecisionApplies(const CalibrationOutputFormat format) noexcept
{
    return format != CalibrationOutputFormat::Original;
}

QString calibrationOutputPath(const QString& source, const QString& directory, const CalibrationOutputOptions& options,
                              const std::size_t index)
{
    const QFileInfo info(source);
    const QString base = options.preserveOriginalName
                             ? info.completeBaseName()
                             : QStringLiteral("image_%1").arg(static_cast<qulonglong>(index), 4, 10, QLatin1Char('0'));
    const QString suffix = info.suffix();
    QString extension;
    switch (options.format)
    {
    case CalibrationOutputFormat::Original:
        extension = suffix.isEmpty() ? QStringLiteral(".fits") : QStringLiteral(".") + suffix;
        break;
    case CalibrationOutputFormat::Tiff:
        extension = QStringLiteral(".tiff");
        break;
    case CalibrationOutputFormat::Fits:
        extension = isFitsSuffix(suffix) ? QStringLiteral(".") + suffix : QStringLiteral(".fits");
        break;
    }
    return QDir(directory).filePath(base + options.filenameSuffix + extension);
}

an::SampleType calibrationOutputRepresentation(const CalibrationOutputOptions& options,
                                               const an::SampleType source) noexcept
{
    if (options.format == CalibrationOutputFormat::Original)
    {
        return source;
    }
    return options.use32BitFloat ? an::SampleType::Float32 : an::SampleType::Float64;
}

an::SampleType calibrationWorkingRepresentation(const CalibrationOutputOptions& options,
                                                const an::SampleType source) noexcept
{
    const an::SampleType output = calibrationOutputRepresentation(options, source);
    const bool wide =
        output == an::SampleType::Float64 || output == an::SampleType::UInt32 || output == an::SampleType::Int32;
    return wide ? an::SampleType::Float64 : an::SampleType::Float32;
}

bool calibrationBatchCalibrates(const CalibrationBatchSettings& settings) noexcept
{
    return !settings.biasPath.isEmpty() || !settings.darkPath.isEmpty() || !settings.flatPath.isEmpty() ||
           settings.pedestalCodes > 0.0;
}

struct CalibrationBatch::State
{
    const an::consumer::EngineSession* session = nullptr;
    CalibrationBatchSettings settings;
    std::optional<ReadFrame> bias;
    std::optional<ReadFrame> dark;
    std::optional<ReadFrame> flat;
    WorkingMasters<float> single;
    WorkingMasters<double> wide;
    std::optional<an::ValidityMask> coldMap;
    std::optional<an::ValidityMask> hotMap;
    std::uint64_t coldCount = 0u;
    std::uint64_t hotCount = 0u;

    template <typename WorkT> WorkingMasters<WorkT>& mastersFor() noexcept
    {
        if constexpr (std::is_same_v<WorkT, float>)
        {
            return single;
        }
        else
        {
            return wide;
        }
    }

    /// The masters in a working representation, widened the first time a light needs them.
    template <typename WorkT>
    [[nodiscard]] an::Result<WorkingMasters<WorkT>*> widenedMasters(const an::consumer::OperationScope& scope)
    {
        WorkingMasters<WorkT>& masters = mastersFor<WorkT>();
        if (masters.made)
        {
            return &masters;
        }
        for (auto [source, target] :
             {std::pair{&bias, &masters.bias}, std::pair{&dark, &masters.dark}, std::pair{&flat, &masters.flat}})
        {
            if (!source->has_value())
            {
                continue;
            }
            an::Result<an::Image<WorkT>> widened = fractionsOf<WorkT>(scope, (*source)->lease.image());
            if (!widened.hasValue())
            {
                return widened.status();
            }
            target->emplace(std::move(*widened));
        }
        masters.made = true;
        return &masters;
    }
};

CalibrationBatch::CalibrationBatch(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

CalibrationBatch::CalibrationBatch(CalibrationBatch&&) noexcept = default;
CalibrationBatch& CalibrationBatch::operator=(CalibrationBatch&&) noexcept = default;
CalibrationBatch::~CalibrationBatch() = default;

an::Result<CalibrationBatch> CalibrationBatch::open(const an::consumer::EngineSession& session,
                                                    const CalibrationBatchSettings& settings,
                                                    const std::vector<ImageListProcessHelpers::ImageListEntry>& lights,
                                                    std::stop_token stop)
{
    if (lights.empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "No image selected");
    }
    if (!calibrationBatchCalibrates(settings))
    {
        return refusal(an::ErrorCode::InvalidArgument, "The calibration names no master and no pedestal");
    }
    if (!ImageListProcessHelpers::validateImageListSizes(lights))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "Some images in the list have different size");
    }
    if (!ImageListProcessHelpers::validateImageListColorDepth(lights))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "Some images in the list have different color depth");
    }

    auto state = std::make_unique<State>();
    state->session = &session;
    state->settings = settings;
    for (auto [path, frame, role] :
         {std::tuple{&settings.biasPath, &state->bias, "bias"}, std::tuple{&settings.darkPath, &state->dark, "dark"},
          std::tuple{&settings.flatPath, &state->flat, "flat"}})
    {
        if (path->isEmpty())
        {
            continue;
        }
        an::Result<ReadFrame> read = readFrame(session, *path, stop);
        if (!read.hasValue())
        {
            if (read.status().code() == an::ErrorCode::Cancelled)
            {
                return read.status();
            }
            return explained(read.status(),
                             std::string("Could NOT load calibration ") + role + " frame '" + utf8(*path) + "'");
        }
        if (an::Status fits =
                ImageListProcessHelpers::checkCalibrationMaster(descriptorOf(read->lease.image()), lights, role);
            fits.failed())
        {
            return fits;
        }
        LOG_INFO("Image Calibration: using {} master '{}'", role, utf8(*path));
        frame->emplace(std::move(*read));
    }

    if (settings.cosmeticCorrection && state->dark.has_value() && !settings.coldSigma.has_value() &&
        !settings.hotSigma.has_value())
    {
        LOG_WARNING("Image Calibration: cosmetic correction is on but neither the cold nor the hot sigma is checked, "
                    "so no pixel is corrected");
    }
    else if (settings.cosmeticCorrection && state->dark.has_value())
    {
        an::Result<an::consumer::OperationScope> scope = admitStep(session, stop);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<WorkingMasters<float>*> masters = state->widenedMasters<float>(*scope);
        if (!masters.hasValue())
        {
            return masters.status();
        }
        const an::Image<float>& dark = *(*masters)->dark;
        const auto darkView = dark.view();
        const std::optional<an::ImageDescriptor> darkDescriptor = dark.descriptor();
        if (!darkView.has_value() || !darkDescriptor.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the dark holds nothing");
        }
        // A map exists exactly when its sigma is stated, as the engine requires.
        std::optional<an::ValidityMask> cold;
        std::optional<an::ValidityMask> hot;
        std::optional<an::ValidityMaskView<std::uint8_t>> coldView;
        std::optional<an::ValidityMaskView<std::uint8_t>> hotView;
        for (const auto& [sigma, map, mapView] : {std::tuple{settings.coldSigma.has_value(), &cold, &coldView},
                                                  std::tuple{settings.hotSigma.has_value(), &hot, &hotView}})
        {
            if (!sigma)
            {
                continue;
            }
            an::Result<an::ValidityMask> made = an::ValidityMask::create(scope->engine(), darkDescriptor->extent());
            if (!made.hasValue())
            {
                return made.status();
            }
            map->emplace(std::move(*made));
            *mapView = (*map)->view();
            if (!mapView->has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the cosmetic maps hold nothing");
            }
        }
        const an::CosmeticDetectionOptions options{.coldSigma = settings.coldSigma, .hotSigma = settings.hotSigma};
        const an::Result<an::CosmeticScanResult> mapped = an::mapImageDeviantPixels(
            scope->execution(), an::SampleType::Float32, darkView->data(), darkView->layout(), options,
            coldView.has_value() ? &*coldView : nullptr, hotView.has_value() ? &*hotView : nullptr);
        if (!mapped.hasValue())
        {
            return explained(mapped.status(), "the dark's deviant pixels could not be mapped");
        }
        state->coldCount = settings.coldSigma.has_value() ? mapped->coldCount : 0u;
        state->hotCount = settings.hotSigma.has_value() ? mapped->hotCount : 0u;
        state->coldMap = std::move(cold);
        state->hotMap = std::move(hot);
        LOG_INFO("Image Calibration: the dark marks {} cold and {} hot pixels", mapped->coldCount, mapped->hotCount);
    }

    if (!settings.outputDirectory.isEmpty())
    {
        QDir().mkpath(settings.outputDirectory);
    }
    return CalibrationBatch(std::move(state));
}

std::uint64_t CalibrationBatch::coldPixels() const noexcept
{
    return m_state->coldCount;
}

std::uint64_t CalibrationBatch::hotPixels() const noexcept
{
    return m_state->hotCount;
}

namespace
{

/// The light calibrated in its working representation, and what the calibration decided.
template <typename WorkT> struct CalibratedLight
{
    an::Image<WorkT> image;
    std::optional<double> darkScale;
    bool darkScaleFixedInstead = false;
};

/// Calibrates a widened light by widened masters, falling back to the fixed dark scale for a light the optimization
/// cannot serve - a colour light, or one smaller than the central square.
template <typename WorkT>
[[nodiscard]] an::Result<CalibratedLight<WorkT>>
calibrateWidened(const an::consumer::OperationScope& scope, const an::Image<WorkT>& light,
                 const WorkingMasters<WorkT>& masters, const CalibrationBatchSettings& settings)
{
    an::Result<an::CalibrationFrameView> lightView = calibrationViewOf(light);
    if (!lightView.hasValue())
    {
        return lightView.status();
    }
    an::CalibrationMasters applied;
    for (auto [image, view] : {std::pair{&masters.bias, &applied.bias}, std::pair{&masters.dark, &applied.dark},
                               std::pair{&masters.flat, &applied.flat}})
    {
        if (!image->has_value())
        {
            continue;
        }
        an::Result<an::CalibrationFrameView> described = calibrationViewOf(**image);
        if (!described.hasValue())
        {
            return described.status();
        }
        *view = *described;
    }
    an::CalibrationRequest request;
    request.pedestal = settings.pedestalCodes / kPedestalCodesPerUnit;
    request.darkScaling = settings.darkOptimization && applied.dark.has_value()
                              ? an::DarkScalingMode::OptimizeCentralRegion
                              : an::DarkScalingMode::Fixed;
    request.darkScale = settings.darkMultiplicationFactor;
    request.flatNormalization =
        settings.normalizeFlats ? an::FlatNormalizationMode::CentralMean : an::FlatNormalizationMode::None;

    bool fixedInstead = false;
    an::Result<an::CalibrationPlan> plan = an::prepareImageCalibration(scope.execution(), *lightView, applied, request);
    if (!plan.hasValue() && request.darkScaling == an::DarkScalingMode::OptimizeCentralRegion &&
        plan.status().code() == an::ErrorCode::OutOfRange)
    {
        request.darkScaling = an::DarkScalingMode::Fixed;
        fixedInstead = true;
        plan = an::prepareImageCalibration(scope.execution(), *lightView, applied, request);
    }
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<WorkT>> calibrated =
        an::Image<WorkT>::createDestination(scope.engine(), lightView->descriptor(), an::StorageArrangement::Planar,
                                            an::StorageInitialization::Uninitialized, light);
    if (!calibrated.hasValue())
    {
        return calibrated.status();
    }
    {
        std::optional<an::ImageWriteAccess<WorkT>> access = calibrated->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the calibrated light holds nothing");
        }
        const an::Result<an::CalibrationResult> ran =
            an::calibrateImage(scope.execution(), *plan, *lightView, applied, access->view().data(),
                               access->view().layout().totalBytes(), access->view().layout());
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::imageCalibrationTransition(*plan, light.metadata().semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = calibrated->transition(*change); stated.failed())
    {
        return stated;
    }
    CalibratedLight<WorkT> result{.image = std::move(*calibrated)};
    if (applied.dark.has_value())
    {
        result.darkScale = plan->effectiveDarkScale();
    }
    result.darkScaleFixedInstead = fixedInstead;
    return result;
}

/// Replaces the pixels the dark's maps mark in a calibrated light, each by its neighbours under the same filter.
template <typename WorkT>
[[nodiscard]] an::Result<std::uint64_t>
correctMarked(const an::consumer::OperationScope& scope, an::Image<WorkT>& light,
              const std::optional<an::ValidityMask>& coldMap, const std::optional<an::ValidityMask>& hotMap)
{
    const std::optional<an::ImageDescriptor> descriptor = light.descriptor();
    const auto lightView = light.view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> cold =
        coldMap.has_value() ? coldMap->view() : std::nullopt;
    const std::optional<an::ValidityMaskView<const std::uint8_t>> hot =
        hotMap.has_value() ? hotMap->view() : std::nullopt;
    if (!descriptor.has_value() || !lightView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the calibrated light holds nothing");
    }
    an::Result<an::Image<WorkT>> corrected = an::Image<WorkT>::createDestination(
        scope.engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, light);
    if (!corrected.hasValue())
    {
        return corrected.status();
    }
    std::uint64_t replaced = 0u;
    {
        std::optional<an::ImageWriteAccess<WorkT>> access = corrected->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the corrected light holds nothing");
        }
        an::CosmeticCorrectionRequest request;
        request.spacing =
            descriptor->isMosaiced() ? an::CosmeticSiteSpacing::SameCfaFilter : an::CosmeticSiteSpacing::EveryPixel;
        const an::Result<an::CosmeticCorrectionResult> fixed = an::correctImageDeviantPixels(
            scope.execution(), an::kSampleTypeOf<WorkT>, lightView->data(), lightView->layout(), access->view().data(),
            access->view().layout(), cold.has_value() ? &*cold : nullptr, hot.has_value() ? &*hot : nullptr, request);
        if (!fixed.hasValue())
        {
            return fixed.status();
        }
        replaced = fixed->coldReplaced + fixed->hotReplaced;
    }
    light = std::move(*corrected);
    return replaced;
}

} // namespace

an::Result<CalibrationFrameOutcome> CalibrationBatch::calibrate(const QString& light, const std::size_t index,
                                                                std::stop_token stop)
{
    State& state = *m_state;
    const an::consumer::EngineSession& session = *state.session;
    std::optional<ReadFrame> read;
    {
        an::Result<ReadFrame> opened = readFrame(session, light, stop);
        if (!opened.hasValue())
        {
            return opened.status();
        }
        read.emplace(std::move(*opened));
    }
    const an::SampleType source = sampleTypeOf(read->lease.image());
    const an::SampleType working = calibrationWorkingRepresentation(state.settings.output, source);
    CalibrationFrameOutcome outcome;
    outcome.output = calibrationOutputPath(light, state.settings.outputDirectory, state.settings.output, index);

    an::Result<an::consumer::ImageAsset> calibrated = [&]() -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::consumer::OperationScope> scope = admitStep(session, stop);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        const auto run = [&]<typename WorkT>() -> an::Result<an::consumer::ImageAsset> {
            an::Result<WorkingMasters<WorkT>*> masters = state.widenedMasters<WorkT>(*scope);
            if (!masters.hasValue())
            {
                return masters.status();
            }
            an::Result<an::Image<WorkT>> widened = fractionsOf<WorkT>(*scope, read->lease.image());
            if (!widened.hasValue())
            {
                return widened.status();
            }
            an::Result<CalibratedLight<WorkT>> done = calibrateWidened(*scope, *widened, **masters, state.settings);
            if (!done.hasValue())
            {
                return done.status();
            }
            outcome.darkScale = done->darkScale;
            outcome.darkScaleFixedInstead = done->darkScaleFixedInstead;
            if (state.coldMap.has_value() || state.hotMap.has_value())
            {
                an::Result<std::uint64_t> replaced = correctMarked(*scope, done->image, state.coldMap, state.hotMap);
                if (!replaced.hasValue())
                {
                    return explained(replaced.status(), "the defective pixels could not be corrected");
                }
                outcome.corrected = *replaced;
            }
            return an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(done->image)));
        };
        return working == an::SampleType::Float64 ? run.template operator()<double>()
                                                  : run.template operator()<float>();
    }();
    // The light's own lease is let go before the write, which reads only the calibrated image.
    read.reset();
    if (!calibrated.hasValue())
    {
        return calibrated.status();
    }
    if (outcome.darkScaleFixedInstead)
    {
        LOG_WARNING("Image Calibration: the dark scale of '{}' could not be optimized; the fixed scale {} was used",
                    utf8(light), state.settings.darkMultiplicationFactor);
    }

    const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
        an::OutputIntentRequest{.sampleType = calibrationOutputRepresentation(state.settings.output, source)});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    if (an::Status written = an::ui::writeViewImage(session, *calibrated, outcome.output, *intent, stop);
        written.failed())
    {
        return written;
    }
    return outcome;
}

an::Result<CosmeticEstimate> estimateCosmeticDefects(const an::consumer::EngineSession& session,
                                                     const QString& darkPath, const std::optional<double> coldSigma,
                                                     const std::optional<double> hotSigma, std::stop_token stop)
{
    an::Result<ReadFrame> dark = readFrame(session, darkPath, stop);
    if (!dark.hasValue())
    {
        if (dark.status().code() == an::ErrorCode::Cancelled)
        {
            return dark.status();
        }
        return explained(dark.status(), "Could NOT load dark frame for estimation");
    }
    an::Result<an::consumer::OperationScope> scope = admitStep(session, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::Image<float>> widened = fractionsOf<float>(*scope, dark->lease.image());
    if (!widened.hasValue())
    {
        return widened.status();
    }
    const auto view = widened->view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the dark holds nothing");
    }
    const an::Result<an::CosmeticScanResult> scanned =
        an::scanImageDeviantPixels(scope->execution(), an::SampleType::Float32, view->data(), view->layout(),
                                   an::CosmeticDetectionOptions{.coldSigma = coldSigma, .hotSigma = hotSigma});
    if (!scanned.hasValue())
    {
        return scanned.status();
    }
    return CosmeticEstimate{.cold = scanned->coldCount, .hot = scanned->hotCount};
}

} // namespace an::ui::processes
