// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The calibration masters one workflow run reads, each once, and the engine's calibration of a light with them.

#include "stdafx.hpp"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"

#include "controls/ImageViewFiles.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/CosmeticCorrection/CosmeticCorrection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QFileInfo>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace core
{

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A failure that says what the set was doing, keeping the engine's failure as its cause.
[[nodiscard]] an::Status explained(an::Status cause, std::string message)
{
    if (cause.code() == an::ErrorCode::Cancelled || cause.code() == an::ErrorCode::DeadlineExceeded)
    {
        return cause;
    }
    const an::ErrorCode code = cause.code();
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)}, std::move(cause));
}

[[nodiscard]] std::string utf8(const QString& text)
{
    return text.toUtf8().toStdString();
}

/// The calibration view of a Float32 image.
[[nodiscard]] an::Result<an::CalibrationFrameView> calibrationViewOf(const an::Image<float>& image)
{
    const auto view = image.view();
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a calibration frame holds nothing");
    }
    return an::CalibrationFrameView::create(view->data(), view->layout().totalBytes(), view->layout(), *descriptor);
}

/// The views of the masters a calibration names.
[[nodiscard]] an::Result<an::CalibrationMasters> mastersOf(const LightCalibration& calibration)
{
    an::CalibrationMasters masters;
    for (auto [image, view] : {std::pair{calibration.bias, &masters.bias}, std::pair{calibration.dark, &masters.dark},
                               std::pair{calibration.flat, &masters.flat}})
    {
        if (image == nullptr)
        {
            continue;
        }
        an::Result<an::CalibrationFrameView> described = calibrationViewOf(*image);
        if (!described.hasValue())
        {
            return described.status();
        }
        *view = *described;
    }
    return masters;
}

/// Runs a prepared calibration of a Float32 frame into a new Float32 image with its transition stated.
[[nodiscard]] an::Result<an::Image<float>> runCalibration(const an::consumer::OperationScope& scope,
                                                          const an::CalibrationPlan& plan,
                                                          const an::CalibrationFrameView& light,
                                                          const an::CalibrationMasters& masters,
                                                          const an::Image<float>& source)
{
    an::Result<an::Image<float>> calibrated =
        an::Image<float>::createDestination(scope.engine(), light.descriptor(), an::StorageArrangement::Planar,
                                            an::StorageInitialization::Uninitialized, source);
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
            an::calibrateImage(scope.execution(), plan, light, masters, access->view().data(),
                               access->view().layout().totalBytes(), access->view().layout());
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::imageCalibrationTransition(plan, source.metadata().semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = calibrated->transition(*change); stated.failed())
    {
        return stated;
    }
    return std::move(*calibrated);
}

/// The key a calibration is shared under: its masters and every setting that changes what it does.
[[nodiscard]] QString calibrationKey(const WorkflowManifest::LightsCalibrationParams& params)
{
    const auto path = [](const std::optional<FrameId>& frame) {
        return frame.has_value() ? frame->canonicalPath() : QString();
    };
    const WorkflowManifest::CalibrationSettings& settings = params.settings;
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
        .arg(path(params.biasPath), path(params.darkPath), path(params.flatPath))
        .arg(settings.subtractBiasFromDark ? 1 : 0)
        .arg(settings.cosmeticCorrection ? 1 : 0)
        .arg(settings.sigmaHot, 0, 'g', 17)
        .arg(settings.sigmaCold, 0, 'g', 17)
        .arg(settings.darkOptimization ? 1 : 0)
        .arg(settings.darkMultiplicationFactor, 0, 'g', 17)
        .arg(settings.flatAutoLevel ? 1 : 0)
        .arg(settings.pedestal, 0, 'g', 17);
}

/// The cold and hot maps of one dark.
struct CosmeticMaps
{
    std::unique_ptr<an::ValidityMask> cold;
    std::unique_ptr<an::ValidityMask> hot;
    std::uint64_t coldCount = 0u;
    std::uint64_t hotCount = 0u;
};

/// Whether the dark-scale optimization refused what it was given, so the fixed scale serves the light instead: a light
/// that is not one channel or is smaller than the central square (`OutOfRange`), a square with no sample that means
/// something (`EmptySelection`), or a dark that does not vary over it or holds a sample that is not finite
/// (`InvalidData`).
[[nodiscard]] bool optimizationRefused(const an::ErrorCode code) noexcept
{
    return code == an::ErrorCode::OutOfRange || code == an::ErrorCode::EmptySelection ||
           code == an::ErrorCode::InvalidData;
}

/// The file name a warning names a master by.
[[nodiscard]] QString masterName(const std::optional<FrameId>& master)
{
    return master.has_value() ? QFileInfo(master->canonicalPath()).fileName() : QString();
}

} // namespace

struct CalibrationFallbackRecord
{
    /// The file names of the masters the calibration applies, as its warnings name them; empty where it applies none.
    QString bias;
    QString dark;
    QString flat;
    /// The fixed dark scale a light the optimization cannot serve is calibrated with.
    double fixedScale = 1.0;
    /// The lights whose dark scale was to be optimized and could not be, so the fixed scale was used.
    std::atomic<std::uint64_t> fixedScaleLights{0u};
};

an::Result<an::consumer::AnyImage> readWorkflowImage(const an::consumer::OperationScope& scope,
                                                     const an::CodecRegistry* registry, const QString& path)
{
    const std::string nativePath = utf8(path);
    const an::CodecReadRequest request{.path = nativePath};
    an::Result<an::CodecDecodeResult> decoded =
        registry != nullptr ? an::decodeImage(scope.engine(), scope.execution(), *registry, request)
                            : an::decodeImage(scope.engine(), scope.execution(), request);
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    return an::ui::importViewFrame(scope.engine(), scope.execution(), std::move(decoded->frame),
                                   std::move(decoded->metadata), true);
}

an::Result<an::Image<float>> workingFractions(const an::consumer::OperationScope& scope,
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
            an::ImageDescriptorRequest request = stored->toRequest();
            request.sampleType = an::SampleType::Float32;
            request.numericDomain = an::NumericDomain::NormalizedLinear;
            const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
            if (!described.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the frame has no description as fractions of full scale");
            }
            an::Result<an::Image<float>> widened =
                an::Image<float>::create(scope.engine(), *described, an::StorageArrangement::Planar,
                                         an::StorageInitialization::Uninitialized, typed.metadata());
            if (!widened.hasValue())
            {
                return widened.status();
            }
            {
                std::optional<an::ImageWriteAccess<float>> access = widened->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the widened frame holds nothing");
                }
                an::ConversionCounters counters;
                const an::Status converted =
                    an::convertImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                            an::SampleType::Float32, access->view().data(), access->view().layout(),
                                            an::ConversionPolicy{}, counters);
                if (converted.failed())
                {
                    return converted;
                }
            }
            return std::move(*widened);
        },
        image);
}

struct CalibrationMasterSet::State
{
    std::shared_ptr<const an::CodecRegistry> registry;
    std::map<QString, std::unique_ptr<an::Image<float>>> masters;
    std::map<QString, std::unique_ptr<an::Image<float>>> processedDarks;
    std::map<std::pair<const an::Image<float>*, QString>, CosmeticMaps> cosmetic;
    std::map<QString, std::shared_ptr<const LightCalibration>> calibrations;
    CalibrationMasterSetDiagnostics diagnostics;
};

CalibrationMasterSet::CalibrationMasterSet(std::shared_ptr<const an::CodecRegistry> registry)
    : m_state(std::make_unique<State>())
{
    m_state->registry = std::move(registry);
}

CalibrationMasterSet::CalibrationMasterSet(CalibrationMasterSet&&) noexcept = default;
CalibrationMasterSet& CalibrationMasterSet::operator=(CalibrationMasterSet&&) noexcept = default;
CalibrationMasterSet::~CalibrationMasterSet() = default;

an::Result<const an::Image<float>*> CalibrationMasterSet::master(const an::consumer::OperationScope& scope,
                                                                 const FrameId& master)
{
    if (!master.isValid())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a calibration master names no file");
    }
    const QString& key = master.canonicalPath();
    if (const auto found = m_state->masters.find(key); found != m_state->masters.end())
    {
        ++m_state->diagnostics.masterCacheHits;
        return found->second.get();
    }
    an::Result<an::consumer::AnyImage> read = readWorkflowImage(scope, m_state->registry.get(), key);
    if (!read.hasValue())
    {
        return explained(read.status(), "the calibration master '" + utf8(key) + "' could not be read");
    }
    an::Result<an::Image<float>> fractions = workingFractions(scope, *read);
    if (!fractions.hasValue())
    {
        return explained(fractions.status(), "the calibration master '" + utf8(key) + "' could not be widened");
    }
    ++m_state->diagnostics.masterReads;
    auto kept = std::make_unique<an::Image<float>>(std::move(*fractions));
    const an::Image<float>* result = kept.get();
    m_state->masters.emplace(key, std::move(kept));
    return result;
}

an::Result<std::shared_ptr<const LightCalibration>>
CalibrationMasterSet::prepare(const an::consumer::OperationScope& scope,
                              const WorkflowManifest::LightsCalibrationParams& params)
{
    const QString key = calibrationKey(params);
    if (const auto found = m_state->calibrations.find(key); found != m_state->calibrations.end())
    {
        return found->second;
    }
    const WorkflowManifest::CalibrationSettings& settings = params.settings;
    auto calibration = std::make_shared<LightCalibration>();
    calibration->plans = std::make_shared<CalibrationPlanCache>();
    for (auto [path, target] :
         {std::pair{&params.biasPath, &calibration->bias}, std::pair{&params.darkPath, &calibration->dark},
          std::pair{&params.flatPath, &calibration->flat}})
    {
        if (!path->has_value())
        {
            continue;
        }
        an::Result<const an::Image<float>*> read = master(scope, **path);
        if (!read.hasValue())
        {
            return read.status();
        }
        *target = *read;
    }

    if (calibration->dark != nullptr && calibration->bias != nullptr)
    {
        if (settings.subtractBiasFromDark)
        {
            // CCD mode: the dark was taken without its bias. It is subtracted from the dark once and from the light.
            const QString pair = params.darkPath->canonicalPath() + QLatin1Char('|') + params.biasPath->canonicalPath();
            if (const auto found = m_state->processedDarks.find(pair); found != m_state->processedDarks.end())
            {
                ++m_state->diagnostics.processedDarkCacheHits;
                calibration->dark = found->second.get();
            }
            else
            {
                an::Result<an::CalibrationFrameView> darkView = calibrationViewOf(*calibration->dark);
                an::Result<an::CalibrationFrameView> biasView = calibrationViewOf(*calibration->bias);
                if (!darkView.hasValue() || !biasView.hasValue())
                {
                    return !darkView.hasValue() ? darkView.status() : biasView.status();
                }
                an::CalibrationMasters bias;
                bias.bias = *biasView;
                an::CalibrationRequest request;
                request.flatNormalization = an::FlatNormalizationMode::None;
                an::Result<an::CalibrationPlan> plan =
                    an::prepareImageCalibration(scope.execution(), *darkView, bias, request);
                if (!plan.hasValue())
                {
                    return explained(plan.status(), "the bias '" + utf8(params.biasPath->canonicalPath()) +
                                                        "' could not be subtracted from the dark '" +
                                                        utf8(params.darkPath->canonicalPath()) + "'");
                }
                an::Result<an::Image<float>> processed =
                    runCalibration(scope, *plan, *darkView, bias, *calibration->dark);
                if (!processed.hasValue())
                {
                    return explained(processed.status(), "the bias '" + utf8(params.biasPath->canonicalPath()) +
                                                             "' could not be subtracted from the dark '" +
                                                             utf8(params.darkPath->canonicalPath()) + "'");
                }
                ++m_state->diagnostics.processedDarkSubtractions;
                auto kept = std::make_unique<an::Image<float>>(std::move(*processed));
                calibration->dark = kept.get();
                m_state->processedDarks.emplace(pair, std::move(kept));
            }
        }
        else
        {
            // Standard mode: the dark holds the bias already, so subtracting the bias too would remove it twice.
            calibration->bias = nullptr;
        }
    }

    calibration->request.pedestal = settings.pedestal > 0.0 ? settings.pedestal / kPedestalCodesPerUnit : 0.0;
    calibration->request.darkScaling = settings.darkOptimization && calibration->dark != nullptr
                                           ? an::DarkScalingMode::OptimizeCentralRegion
                                           : an::DarkScalingMode::Fixed;
    calibration->request.darkScale = settings.darkMultiplicationFactor;
    calibration->request.flatNormalization =
        settings.flatAutoLevel ? an::FlatNormalizationMode::CentralMean : an::FlatNormalizationMode::None;

    calibration->fallbacks = std::make_shared<CalibrationFallbackRecord>();
    calibration->fallbacks->bias = calibration->bias != nullptr ? masterName(params.biasPath) : QString();
    calibration->fallbacks->dark = calibration->dark != nullptr ? masterName(params.darkPath) : QString();
    calibration->fallbacks->flat = calibration->flat != nullptr ? masterName(params.flatPath) : QString();
    calibration->fallbacks->fixedScale = settings.darkMultiplicationFactor;

    if (settings.cosmeticCorrection && calibration->dark == nullptr)
    {
        calibration->cosmeticWithoutDark = true;
    }
    else if (settings.cosmeticCorrection)
    {
        const QString mapsKey =
            QStringLiteral("%1|%2").arg(settings.sigmaCold, 0, 'g', 17).arg(settings.sigmaHot, 0, 'g', 17);
        auto& maps = m_state->cosmetic[std::pair{calibration->dark, mapsKey}];
        if (!maps.cold && !maps.hot)
        {
            const auto darkView = calibration->dark->view();
            const std::optional<an::ImageDescriptor> descriptor = calibration->dark->descriptor();
            if (!darkView.has_value() || !descriptor.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the dark holds nothing");
            }
            an::Result<an::ValidityMask> cold = an::ValidityMask::create(scope.engine(), descriptor->extent());
            an::Result<an::ValidityMask> hot = an::ValidityMask::create(scope.engine(), descriptor->extent());
            if (!cold.hasValue() || !hot.hasValue())
            {
                return !cold.hasValue() ? cold.status() : hot.status();
            }
            std::optional<an::ValidityMaskView<std::uint8_t>> coldView = cold->view();
            std::optional<an::ValidityMaskView<std::uint8_t>> hotView = hot->view();
            if (!coldView.has_value() || !hotView.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the cosmetic maps hold nothing");
            }
            const an::CosmeticDetectionOptions options{.coldSigma = settings.sigmaCold, .hotSigma = settings.sigmaHot};
            const an::Result<an::CosmeticScanResult> mapped =
                an::mapImageDeviantPixels(scope.execution(), an::SampleType::Float32, darkView->data(),
                                          darkView->layout(), options, &*coldView, &*hotView);
            if (!mapped.hasValue())
            {
                return explained(mapped.status(), "the dark's deviant pixels could not be mapped");
            }
            ++m_state->diagnostics.cosmeticMaps;
            maps.cold = std::make_unique<an::ValidityMask>(std::move(*cold));
            maps.hot = std::make_unique<an::ValidityMask>(std::move(*hot));
            maps.coldCount = mapped->coldCount;
            maps.hotCount = mapped->hotCount;
        }
        calibration->coldMap = maps.cold.get();
        calibration->hotMap = maps.hot.get();
        calibration->coldPixels = maps.coldCount;
        calibration->hotPixels = maps.hotCount;
    }

    std::shared_ptr<const LightCalibration> shared = std::move(calibration);
    m_state->calibrations.emplace(key, shared);
    return shared;
}

const CalibrationMasterSetDiagnostics& CalibrationMasterSet::diagnostics() const noexcept
{
    return m_state->diagnostics;
}

QStringList CalibrationMasterSet::fallbackWarnings() const
{
    QStringList warnings;
    for (const auto& [key, calibration] : m_state->calibrations)
    {
        if (!calibration->calibrates() || calibration->fallbacks == nullptr)
        {
            continue;
        }
        const CalibrationFallbackRecord& record = *calibration->fallbacks;
        if (calibration->cosmeticWithoutDark)
        {
            QStringList masters;
            if (!record.bias.isEmpty())
            {
                masters << QStringLiteral("the bias '%1'").arg(record.bias);
            }
            if (!record.flat.isEmpty())
            {
                masters << QStringLiteral("the flat '%1'").arg(record.flat);
            }
            const QString calibratedWith =
                masters.isEmpty() ? QStringLiteral("a pedestal alone") : masters.join(QStringLiteral(" and "));
            warnings << QStringLiteral("Cosmetic correction is on for the lights calibrated with %1, which have no "
                                       "dark master to find defective pixels in, so none is corrected.")
                            .arg(calibratedWith);
        }
        if (record.fixedScaleLights.load(std::memory_order_relaxed) > 0u)
        {
            warnings << QStringLiteral("The dark scale of the lights calibrated with the dark '%1' could not be "
                                       "optimized - the optimization needs a one-channel light of at least %2 x %2 "
                                       "pixels and a dark that varies over its central square - so the fixed factor "
                                       "%3 was used.")
                            .arg(record.dark)
                            .arg(an::kCalibrationDarkOptimizationSide)
                            .arg(record.fixedScale, 0, 'g', 6);
        }
    }
    return warnings;
}

struct CalibrationPlanCache
{
    std::mutex mutex;
    std::vector<std::pair<an::ImageDescriptor, an::CalibrationPlan>> plans;
};

namespace
{

/// The plan a light is calibrated with: the one its calibration already prepared for its description when the dark
/// rule reads no light's samples, prepared from the description and kept on first use; otherwise prepared from the
/// light itself. A description the shared preparation refuses is prepared from the light, as before.
[[nodiscard]] an::Result<an::CalibrationPlan> calibrationPlanFor(const an::consumer::OperationScope& scope,
                                                                 const LightCalibration& calibration,
                                                                 const an::CalibrationFrameView& light,
                                                                 const an::CalibrationMasters& masters,
                                                                 const an::CalibrationRequest& request)
{
    if (calibration.plans != nullptr && request.darkScaling != an::DarkScalingMode::OptimizeCentralRegion)
    {
        CalibrationPlanCache& cache = *calibration.plans;
        const std::lock_guard lock(cache.mutex);
        for (const auto& [description, plan] : cache.plans)
        {
            if (description == light.descriptor())
            {
                return plan;
            }
        }
        an::Result<an::CalibrationPlan> shared =
            an::prepareImageCalibration(scope.execution(), light.descriptor(), masters, request);
        if (shared.hasValue())
        {
            cache.plans.emplace_back(light.descriptor(), *shared);
            return shared;
        }
        if (shared.status().isCancellation())
        {
            return shared;
        }
    }
    return an::prepareImageCalibration(scope.execution(), light, masters, request);
}

} // namespace

an::Result<CalibratedLight> calibrateLight(const an::consumer::OperationScope& scope,
                                           const LightCalibration& calibration, const an::Image<float>& light)
{
    an::Result<an::CalibrationFrameView> lightView = calibrationViewOf(light);
    if (!lightView.hasValue())
    {
        return lightView.status();
    }
    an::Result<an::CalibrationMasters> masters = mastersOf(calibration);
    if (!masters.hasValue())
    {
        return masters.status();
    }
    an::CalibrationRequest request = calibration.request;
    bool fixedInstead = false;
    an::Result<an::CalibrationPlan> plan = calibrationPlanFor(scope, calibration, *lightView, *masters, request);
    if (!plan.hasValue() && request.darkScaling == an::DarkScalingMode::OptimizeCentralRegion &&
        optimizationRefused(plan.status().code()))
    {
        request.darkScaling = an::DarkScalingMode::Fixed;
        fixedInstead = true;
        plan = calibrationPlanFor(scope, calibration, *lightView, *masters, request);
    }
    if (!plan.hasValue())
    {
        return plan.status();
    }
    if (fixedInstead && calibration.fallbacks != nullptr)
    {
        calibration.fallbacks->fixedScaleLights.fetch_add(1u, std::memory_order_relaxed);
    }
    an::Result<an::Image<float>> calibrated = runCalibration(scope, *plan, *lightView, *masters, light);
    if (!calibrated.hasValue())
    {
        return calibrated.status();
    }
    CalibratedLight result{.image = std::move(*calibrated)};
    if (calibration.dark != nullptr)
    {
        result.darkScale = plan->effectiveDarkScale();
    }
    result.darkScaleFixedInstead = fixedInstead;
    if (calibration.coldMap == nullptr && calibration.hotMap == nullptr)
    {
        return result;
    }

    const std::optional<an::ImageDescriptor> descriptor = result.image.descriptor();
    const auto calibratedView = result.image.view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> cold =
        calibration.coldMap != nullptr ? std::as_const(*calibration.coldMap).view() : std::nullopt;
    const std::optional<an::ValidityMaskView<const std::uint8_t>> hot =
        calibration.hotMap != nullptr ? std::as_const(*calibration.hotMap).view() : std::nullopt;
    if (!descriptor.has_value() || !calibratedView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the calibrated light holds nothing");
    }
    an::Result<an::Image<float>> corrected =
        an::Image<float>::createDestination(scope.engine(), *descriptor, an::StorageArrangement::Planar,
                                            an::StorageInitialization::Uninitialized, result.image);
    if (!corrected.hasValue())
    {
        return corrected.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = corrected->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the corrected light holds nothing");
        }
        an::CosmeticCorrectionRequest correction;
        correction.spacing =
            descriptor->isMosaiced() ? an::CosmeticSiteSpacing::SameCfaFilter : an::CosmeticSiteSpacing::EveryPixel;
        const an::Result<an::CosmeticCorrectionResult> fixed = an::correctImageDeviantPixels(
            scope.execution(), an::SampleType::Float32, calibratedView->data(), calibratedView->layout(),
            access->view().data(), access->view().layout(), cold.has_value() ? &*cold : nullptr,
            hot.has_value() ? &*hot : nullptr, correction);
        if (!fixed.hasValue())
        {
            return explained(fixed.status(), "the defective pixels could not be corrected");
        }
        result.corrected = fixed->coldReplaced + fixed->hotReplaced;
    }
    result.image = std::move(*corrected);
    return result;
}

} // namespace core
