// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "IntegrationOrchestrationProbe.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The probe's implementation, written the way a consumer writes an orchestrator.
//
// Four habits in here are deliberate. Each is either something the engine made easy or something it
// made the caller responsible for, and the second kind is what the friction record is made of.
//
// **Every stage states its effect before it runs and reports its outcome after.** The declaration
// comes from the operation - `pointwiseProgramEffect` for a chain, `kBasicStatisticsEffect` for a
// reduction - and the outcome carries whatever the declaration says is missing. The transition that
// falls out of the two is what the artifact is given, so nothing here decides for itself what an
// operation did to an image's meaning.
//
// **A stage that changes what a frame means in pipeline terms states that separately.** A subtract
// chain does not know that the image it subtracted was a master dark, so the declaration it returns
// preserves the meaning, and the workflow states the calibration itself in a second transition.
// That is correct - an operation should not guess - and it is two transitions per stage.
//
// **Metadata is carried forward by the caller.** An operation writes into a destination the caller
// allocated, and a freshly allocated image carries no metadata at all, so a chain of six stages
// would end with an empty lineage unless every stage seeds its destination from its primary input.
// `runChain` does that in one place; a consumer that forgets loses the history with no symptom.
//
// **An artifact key pairs an identity the probe maintains with the two generations an image
// carries.** The generations alone are not enough: they start at zero for every image, so two
// unrelated frames compare equal by their numbers, which the image model says in as many words. The
// first version of this probe left the identity out and silently reused a stale artifact after its
// input had changed.

namespace probe
{
namespace
{

/// The frames are small on purpose: this probe is about API shape, and a large frame would make it
/// slow without making it say anything more.
constexpr std::uint32_t kFrameWidth = 24u;

/// The frame height. See `kFrameWidth`.
constexpr std::uint32_t kFrameHeight = 16u;

/// How many stages the workflow runs, counted from the enumeration rather than restated.
constexpr std::uint32_t kStageCount = static_cast<std::uint32_t>(Stage::StateOutput) + 1u;

/// The failure this probe reports when the engine hands back nothing where it must hand back
/// something.
///
/// Named rather than bare, because a probe whose refusals all read "this has not been initialised
/// yet" cannot tell its author which call site gave up.
///
/// @param where the probe call site that could not continue.
/// @return `ErrorCode::NotInitialized`, naming that site.
[[nodiscard]] an::Status notInitialized(const char* where)
{
    return an::Status::failure(an::ErrorCode::NotInitialized,
                               an::StatusDetail{.message = std::string("the probe could not continue at ") + where,
                                                .operation = where,
                                                .domain = an::ErrorDomain::Image});
}

/// The description every frame in this workflow has.
///
/// @return a one-channel Float32 frame with no colorimetric claim, which is what an uncalibrated
///         monochrome sensor frame is.
[[nodiscard]] an::Result<an::ImageDescriptor> frameDescriptor()
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kFrameWidth, kFrameHeight, 1u, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument);
    }
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = an::SampleType::Float32,
                                             .channelSchema = an::ChannelSchema::Mono,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = an::CfaDescriptor{},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument);
    }
    return *descriptor;
}

/// An execution context for one operation of the workflow.
///
/// Every operation builds one and lets it die before the next is started. That is not tidiness: an
/// execution context takes a share of the engine context's thread budget for as long as it exists,
/// and one built with the default policy asks for the whole width, so a second live context is
/// refused with `ErrorCode::Busy`.
///
/// @param engine the context the operation runs in.
/// @param stopToken the workflow's stop token.
/// @param progress where the engine reports its own progress, or null.
/// @return the context, or whatever starting an operation reported.
[[nodiscard]] an::Result<an::ExecutionContext> startOperation(const an::EngineContext& engine,
                                                              std::stop_token stopToken, an::ProgressSink* progress)
{
    an::ExecutionOptions options;
    options.stopToken = std::move(stopToken);
    options.progressSink = progress;
    return an::ExecutionContext::create(engine, options);
}

/// A synthesized frame whose samples are a reproducible function of the pixel index.
///
/// @param engine the context the samples come from.
/// @param base the value every sample starts from.
/// @param slope how much each pixel index adds.
/// @return the frame, or whatever allocating or describing it reported.
[[nodiscard]] an::Result<an::Image<float>> synthesizeFrame(const an::EngineContext& engine, double base, double slope)
{
    const an::Result<an::ImageDescriptor> descriptor = frameDescriptor();
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }
    an::Result<an::Image<float>> image = an::Image<float>::create(engine, *descriptor, an::StorageArrangement::Planar,
                                                                  an::StorageInitialization::Zeroed);
    if (!image.hasValue())
    {
        return image.status();
    }
    {
        const std::optional<an::ImageWriteAccess<float>> access = image->write();
        if (!access.has_value())
        {
            return notInitialized("synthesizeFrame::write");
        }
        const std::optional<an::PlaneView<float>> plane = access->view().plane(0u);
        if (!plane.has_value())
        {
            return notInitialized("synthesizeFrame::plane");
        }
        for (std::uint32_t y = 0u; y < kFrameHeight; ++y)
        {
            const std::optional<std::span<float>> row = plane->row(y);
            if (!row.has_value())
            {
                return notInitialized("synthesizeFrame::row");
            }
            for (std::uint32_t x = 0u; x < kFrameWidth; ++x)
            {
                const double index = static_cast<double>(y) * static_cast<double>(kFrameWidth) + static_cast<double>(x);
                (*row)[x] = static_cast<float>(base + slope * index);
            }
        }
    }
    return std::move(*image);
}

/// The meaning a frame carries after a named calibration step.
///
/// @param darkSubtracted whether a dark frame has been subtracted.
/// @param flatFielded whether a flat has been divided out.
/// @param normalized whether the frame has been scaled onto a common level.
/// @return the semantics, or whatever building them reported.
[[nodiscard]] an::Result<an::ImageSemantics> calibratedSemantics(bool darkSubtracted, bool flatFielded, bool normalized)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.darkSubtracted = darkSubtracted;
    request.processingState.flatFielded = flatFielded;
    request.processingState.normalized = normalized;
    return an::ImageSemantics::create(request);
}

/// The shape every pointwise chain in this workflow is built against.
///
/// @return a one-channel Float32 shape in the normalized linear domain.
[[nodiscard]] an::PointwisePlanShape chainShape() noexcept
{
    an::PointwisePlanShape shape;
    shape.channelCount = 1u;
    return shape;
}

/// States a fact about a frame that no single operation could have known.
///
/// The second transition every calibration stage needs. A subtract chain preserves the meaning of
/// what it read, because subtracting one image from another says nothing about what either was; the
/// workflow is what knows that the subtrahend was a master dark.
///
/// @param image the frame to state it about.
/// @param semantics what the frame now means.
/// @param operation what the workflow did, for the lineage.
/// @param detail anything worth recording beside it.
/// @return success, or whatever the transition refused.
[[nodiscard]] an::Status statePipelineMeaning(an::Image<float>& image, const an::ImageSemantics& semantics,
                                              std::string operation, std::string detail)
{
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(std::move(operation), std::move(detail));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return image.transition(an::MetadataTransition{.semantics = semantics, .record = std::move(*entry)});
}

} // namespace

const char* stageName(Stage stage) noexcept
{
    switch (stage)
    {
    case Stage::MasterDark:
        return "MasterDark";
    case Stage::Calibrate:
        return "Calibrate";
    case Stage::DefectMask:
        return "DefectMask";
    case Stage::Normalize:
        return "Normalize";
    case Stage::Integrate:
        return "Integrate";
    case Stage::StateOutput:
        return "StateOutput";
    }
    return "UnknownStage";
}

Workflow::Workflow(const an::EngineContext& engine, std::vector<an::Image<float>> lights,
                   std::vector<an::Image<float>> darks, an::Image<float> flat) noexcept
    : m_engine(&engine), m_lights(std::move(lights)), m_darks(std::move(darks)), m_flat(std::move(flat))
{
}

an::Result<Workflow> Workflow::create(const an::EngineContext& engine, std::uint32_t lightCount,
                                      std::uint32_t darkCount)
{
    if (lightCount < 2u || darkCount < 2u)
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument);
    }

    std::vector<an::Image<float>> lights;
    lights.reserve(lightCount);
    for (std::uint32_t index = 0u; index < lightCount; ++index)
    {
        an::Result<an::Image<float>> light = synthesizeFrame(engine, 0.20 + 0.01 * static_cast<double>(index), 0.0009);
        if (!light.hasValue())
        {
            return light.status();
        }
        lights.push_back(std::move(*light));
    }

    std::vector<an::Image<float>> darks;
    darks.reserve(darkCount);
    for (std::uint32_t index = 0u; index < darkCount; ++index)
    {
        an::Result<an::Image<float>> dark = synthesizeFrame(engine, 0.02 + 0.002 * static_cast<double>(index), 0.0);
        if (!dark.hasValue())
        {
            return dark.status();
        }
        darks.push_back(std::move(*dark));
    }

    an::Result<an::Image<float>> flat = synthesizeFrame(engine, 1.0, 0.0);
    if (!flat.hasValue())
    {
        return flat.status();
    }

    return Workflow{engine, std::move(lights), std::move(darks), std::move(*flat)};
}

an::Status Workflow::touchReferenceLight()
{
    if (m_lights.empty())
    {
        return notInitialized("Workflow::touchReferenceLight");
    }
    const std::optional<an::ImageWriteAccess<float>> access = m_lights.front().write();
    if (!access.has_value())
    {
        return notInitialized("Workflow::touchReferenceLight");
    }
    return an::Status::success();
}

std::size_t Workflow::cachedArtifactCount() const noexcept
{
    return m_cache.size();
}

FrameRef Workflow::cachedFrame(const ArtifactKey& key) const noexcept
{
    for (const CacheEntry& entry : m_cache)
    {
        if (entry.key == key)
        {
            return FrameRef{.identity = entry.identity, .image = &entry.image};
        }
    }
    return FrameRef{};
}

FrameRef Workflow::store(const ArtifactKey& key, an::Image<float>&& image)
{
    for (CacheEntry& entry : m_cache)
    {
        if (entry.key == key)
        {
            entry.image = std::move(image);
            return FrameRef{.identity = entry.identity, .image = &entry.image};
        }
    }
    ++m_nextIdentity;
    m_cache.push_back(CacheEntry{.key = key, .identity = m_nextIdentity, .image = std::move(image)});
    return FrameRef{.identity = m_cache.back().identity, .image = &m_cache.back().image};
}

ArtifactKey Workflow::makeKey(Stage stage, std::string parameters, std::span<const FrameRef> inputs)
{
    ArtifactKey key;
    key.stage = stage;
    key.parameters = std::move(parameters);
    key.inputs.reserve(inputs.size());
    for (const FrameRef& input : inputs)
    {
        key.inputs.push_back(InputRevision{.identity = input.identity,
                                           .pixelGeneration = input.image->pixelGeneration(),
                                           .semanticGeneration = input.image->semanticGeneration()});
    }
    return key;
}

an::Result<an::Image<float>> Workflow::runChain(std::stop_token stopToken, an::ProgressSink* progress,
                                                const an::PointwiseProgram& program, std::span<const FrameRef> sources,
                                                const an::ImageDescriptor& descriptor,
                                                const std::optional<an::ImageSemantics>& semantics,
                                                std::string operation, std::string detail)
{
    if (sources.empty() || sources.front().image == nullptr)
    {
        return notInitialized("Workflow::runChain::sources");
    }

    // The destination is seeded from the primary input's metadata. An operation writes samples into
    // a buffer the caller allocated, and a freshly allocated image carries no acquisition record, no
    // lineage and no solution; a stage that forgets produces a correct picture with no history, and
    // nothing downstream can tell. `createDestination` is the engine naming that, so forgetting is a
    // different call rather than a missing argument.
    an::Result<an::Image<float>> destination =
        an::Image<float>::createDestination(*m_engine, descriptor, an::StorageArrangement::Planar,
                                            an::StorageInitialization::Zeroed, *sources.front().image);
    if (!destination.hasValue())
    {
        return destination.status();
    }

    std::vector<an::ImageLayout> layouts;
    std::vector<an::PointwiseImageView> bound;
    layouts.reserve(sources.size());
    bound.reserve(sources.size());
    for (const FrameRef& source : sources)
    {
        const std::optional<an::ImageLayout> layout = source.image->layout();
        if (!layout.has_value())
        {
            return notInitialized("Workflow::runChain::sourceLayout");
        }
        layouts.push_back(*layout);
    }
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        bound.push_back(
            an::PointwiseImageView{.samples = sources[index].image->storage().data(), .layout = &layouts[index]});
    }

    {
        an::Result<an::ExecutionContext> execution = startOperation(*m_engine, std::move(stopToken), progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        const std::optional<an::ImageWriteAccess<float>> access = destination->write();
        if (!access.has_value())
        {
            return notInitialized("Workflow::runChain::write");
        }
        const an::ImageLayout& destinationLayout = access->view().layout();
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            *execution, program, std::span<const an::PointwiseImageView>{bound},
            an::PointwiseMutableImageView{.samples = access->view().data(), .layout = &destinationLayout},
            an::KernelValidityView{});
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }

    const an::OperationEffect effect = an::pointwiseProgramEffect(program);
    an::OperationOutcome outcome;
    outcome.operation = std::move(operation);
    outcome.detail = std::move(detail);
    if (effect.semantics == an::SemanticEffect::Restates)
    {
        outcome.semantics = semantics;
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(effect, outcome);
    if (!change.hasValue())
    {
        return change.status();
    }
    const an::Status recorded = destination->transition(*change);
    if (recorded.failed())
    {
        return recorded;
    }
    return std::move(*destination);
}

an::Result<RunReport> Workflow::run(std::stop_token stopToken, WorkflowObserver* observer)
{
    /// One progress sink for the whole run, so the report can say how many reports the engine made
    /// across every operation the workflow started.
    class CountingProgress final : public an::ProgressSink
    {
    public:
        void onProgress(const an::ProgressReport&) noexcept override
        {
            ++count;
        }

        std::uint64_t count = 0u;
    };

    CountingProgress progress;
    RunReport report;

    const an::Result<an::ImageDescriptor> descriptor = frameDescriptor();
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }

    const auto finishStage = [&report, observer](Stage stage, bool executed, std::uint32_t operations,
                                                 ArtifactKey key) {
        report.stages.push_back(StageRecord{.stage = stage,
                                            .executed = executed,
                                            .reused = !executed,
                                            .operations = operations,
                                            .key = std::move(key)});
        ++report.workflowProgressReports;
        if (observer != nullptr)
        {
            observer->onStageCompleted(static_cast<std::uint32_t>(report.stages.size()), kStageCount, stage);
        }
    };

    // The identities of the frames the workflow was given. They are the probe's, not the engine's:
    // an image carries two generations and no identity, and two images that have each been written
    // once carry the same numbers, so a key built from generations alone cannot tell them apart.
    std::vector<FrameRef> lightRefs;
    lightRefs.reserve(m_lights.size());
    for (std::size_t index = 0u; index < m_lights.size(); ++index)
    {
        lightRefs.push_back(FrameRef{.identity = kLightIdentityBase + index, .image = &m_lights[index]});
    }
    std::vector<FrameRef> darkRefs;
    darkRefs.reserve(m_darks.size());
    for (std::size_t index = 0u; index < m_darks.size(); ++index)
    {
        darkRefs.push_back(FrameRef{.identity = kDarkIdentityBase + index, .image = &m_darks[index]});
    }
    const FrameRef flatRef{.identity = kFlatIdentity, .image = &m_flat};

    // ---- Stage one: the master dark ------------------------------------------------------------
    ArtifactKey masterDarkKey = makeKey(Stage::MasterDark, "average", std::span<const FrameRef>{darkRefs});
    FrameRef masterDark = cachedFrame(masterDarkKey);
    if (masterDark.image != nullptr)
    {
        finishStage(Stage::MasterDark, false, 0u, masterDarkKey);
    }
    else
    {
        const an::Result<an::PointwiseProgram> sum =
            an::buildBinaryArithmeticProgram(chainShape(), an::BinaryArithmeticOperation::Add);
        if (!sum.hasValue())
        {
            return sum.status();
        }
        an::Image<float> accumulated{};
        std::uint32_t operations = 0u;
        for (std::size_t index = 1u; index < darkRefs.size(); ++index)
        {
            const FrameRef running{.identity = kRunningIdentity, .image = &accumulated};
            const std::array<FrameRef, 2u> pair{index == 1u ? darkRefs[0] : running, darkRefs[index]};
            an::Result<an::Image<float>> partial =
                runChain(stopToken, &progress, *sum, std::span<const FrameRef>{pair}, *descriptor, std::nullopt,
                         "an::executePointwiseProgram", "accumulate a dark frame");
            if (!partial.hasValue())
            {
                return partial.status();
            }
            accumulated = std::move(*partial);
            ++operations;
        }
        const an::Result<an::PointwiseProgram> scale =
            an::buildScaleBiasProgram(chainShape(), 1.0 / static_cast<double>(darkRefs.size()), 0.0, std::nullopt);
        if (!scale.hasValue())
        {
            return scale.status();
        }
        const std::array<FrameRef, 1u> one{FrameRef{.identity = kRunningIdentity, .image = &accumulated}};
        an::Result<an::Image<float>> averaged =
            runChain(stopToken, &progress, *scale, std::span<const FrameRef>{one}, *descriptor, std::nullopt,
                     "an::executePointwiseProgram", "divide by the dark count");
        if (!averaged.hasValue())
        {
            return averaged.status();
        }
        ++operations;
        masterDark = store(masterDarkKey, std::move(*averaged));
        finishStage(Stage::MasterDark, true, operations, masterDarkKey);
    }

    // ---- Stage two: calibration ----------------------------------------------------------------
    const an::Result<an::ImageSemantics> calibrated = calibratedSemantics(true, true, false);
    if (!calibrated.hasValue())
    {
        return calibrated.status();
    }
    const an::Result<an::PointwiseProgram> subtract =
        an::buildBinaryArithmeticProgram(chainShape(), an::BinaryArithmeticOperation::Subtract);
    if (!subtract.hasValue())
    {
        return subtract.status();
    }
    const an::Result<an::PointwiseProgram> divide =
        an::buildBinaryArithmeticProgram(chainShape(), an::BinaryArithmeticOperation::Divide);
    if (!divide.hasValue())
    {
        return divide.status();
    }

    std::vector<FrameRef> calibratedLights;
    std::vector<ArtifactKey> calibratedKeys;
    calibratedLights.reserve(lightRefs.size());
    calibratedKeys.reserve(lightRefs.size());
    std::uint32_t calibrateOperations = 0u;
    bool calibrateExecuted = false;
    for (std::size_t index = 0u; index < lightRefs.size(); ++index)
    {
        const std::array<FrameRef, 3u> inputs{lightRefs[index], masterDark, flatRef};
        ArtifactKey key = makeKey(Stage::Calibrate, "subtract dark, divide flat, light " + std::to_string(index),
                                  std::span<const FrameRef>{inputs});
        const FrameRef cached = cachedFrame(key);
        if (cached.image != nullptr)
        {
            calibratedLights.push_back(cached);
            calibratedKeys.push_back(std::move(key));
            continue;
        }
        calibrateExecuted = true;
        const std::array<FrameRef, 2u> darkPair{lightRefs[index], masterDark};
        an::Result<an::Image<float>> darkRemoved =
            runChain(stopToken, &progress, *subtract, std::span<const FrameRef>{darkPair}, *descriptor, std::nullopt,
                     "an::executePointwiseProgram", "subtract the master dark");
        if (!darkRemoved.hasValue())
        {
            return darkRemoved.status();
        }
        ++calibrateOperations;
        const std::array<FrameRef, 2u> flatPair{FrameRef{.identity = kRunningIdentity, .image = &*darkRemoved},
                                                flatRef};
        an::Result<an::Image<float>> flatFielded =
            runChain(stopToken, &progress, *divide, std::span<const FrameRef>{flatPair}, *descriptor, std::nullopt,
                     "an::executePointwiseProgram", "divide by the flat field");
        if (!flatFielded.hasValue())
        {
            return flatFielded.status();
        }
        ++calibrateOperations;
        const an::Status stated = statePipelineMeaning(*flatFielded, *calibrated, "probe::Workflow::calibrate",
                                                       "master dark and flat applied");
        if (stated.failed())
        {
            return stated;
        }
        calibratedLights.push_back(store(key, std::move(*flatFielded)));
        calibratedKeys.push_back(std::move(key));
    }
    finishStage(Stage::Calibrate, calibrateExecuted, calibrateOperations,
                calibratedKeys.empty() ? ArtifactKey{} : calibratedKeys.front());

    // ---- Stage three: the defect mask ----------------------------------------------------------
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kFrameWidth, kFrameHeight, 1u, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument);
    }
    an::Result<an::ValidityMask> combined = an::ValidityMask::createAllValid(*m_engine, *extent);
    if (!combined.hasValue())
    {
        return combined.status();
    }
    an::Result<an::ValidityMask> perFrame = an::ValidityMask::create(*m_engine, *extent);
    if (!perFrame.hasValue())
    {
        return perFrame.status();
    }
    std::uint32_t maskOperations = 0u;
    for (const FrameRef& light : calibratedLights)
    {
        {
            an::Result<an::ExecutionContext> execution = startOperation(*m_engine, stopToken, &progress);
            if (!execution.hasValue())
            {
                return execution.status();
            }
            const std::optional<an::ImageLayout> layout = light.image->layout();
            const std::optional<an::ValidityMaskView<std::uint8_t>> frameView = perFrame->view();
            if (!layout.has_value() || !frameView.has_value())
            {
                return notInitialized("Workflow::run::maskViews");
            }
            const an::MaskThresholdRequest request{
                .threshold = an::SampleThreshold{
                    .predicate = an::MaskPredicate::InsideClosedRange, .lowerBound = 0.0, .upperBound = 1.0}};
            const an::Status built = an::buildImageMask(*execution, an::SampleType::Float32,
                                                        light.image->storage().data(), *layout, request, *frameView);
            if (built.failed())
            {
                return built;
            }
            ++maskOperations;
        }

        {
            an::Result<an::ExecutionContext> combining = startOperation(*m_engine, stopToken, &progress);
            if (!combining.hasValue())
            {
                return combining.status();
            }
            const std::optional<an::ValidityMaskView<std::uint8_t>> combinedView = combined->view();
            const std::optional<an::ValidityMaskView<const std::uint8_t>> frameConstView = perFrame->view();
            if (!combinedView.has_value() || !frameConstView.has_value())
            {
                return notInitialized("Workflow::run::combinedView");
            }
            const an::Status merged = an::combineImageMasks(*combining, *combinedView, combinedView->constView(),
                                                            *frameConstView, an::ValidityCombination::Intersection);
            if (merged.failed())
            {
                return merged;
            }
            ++maskOperations;
        }
    }
    {
        an::Result<an::ExecutionContext> execution = startOperation(*m_engine, stopToken, &progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        const std::optional<an::ValidityMaskView<const std::uint8_t>> view = combined->view();
        if (!view.has_value())
        {
            return notInitialized("Workflow::run::countView");
        }
        const an::Result<an::ValidityCount> counted = an::countImageMask(*execution, *view);
        if (!counted.hasValue())
        {
            return counted.status();
        }
        ++maskOperations;
        report.validPixels = counted->valid;
        report.totalPixels = counted->total;
    }
    finishStage(Stage::DefectMask, true, maskOperations,
                makeKey(Stage::DefectMask, "inside [0, 1] intersected across the lights",
                        std::span<const FrameRef>{calibratedLights}));

    // ---- Stage four: normalization -------------------------------------------------------------
    const an::Result<an::ImageSemantics> normalized = calibratedSemantics(true, true, true);
    if (!normalized.hasValue())
    {
        return normalized.status();
    }
    double referenceMean = 0.0;
    std::uint32_t normalizeOperations = 0u;
    {
        an::Result<an::ExecutionContext> execution = startOperation(*m_engine, stopToken, &progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        const std::optional<an::ImageLayout> layout = calibratedLights.front().image->layout();
        if (!layout.has_value())
        {
            return notInitialized("Workflow::run::referenceLayout");
        }
        const an::Result<an::BasicStatistics> reference = an::computeImageBasicStatistics(
            *execution, an::SampleType::Float32, calibratedLights.front().image->storage().data(), *layout, 0u);
        if (!reference.hasValue())
        {
            return reference.status();
        }
        referenceMean = reference->mean;
    }

    std::vector<FrameRef> normalizedLights;
    std::vector<ArtifactKey> normalizedKeys;
    normalizedLights.reserve(calibratedLights.size());
    normalizedKeys.reserve(calibratedLights.size());
    bool normalizeExecuted = false;
    for (std::size_t index = 0u; index < calibratedLights.size(); ++index)
    {
        const std::array<FrameRef, 1u> inputs{calibratedLights[index]};
        ArtifactKey key = makeKey(Stage::Normalize, "scale onto the reference mean, light " + std::to_string(index),
                                  std::span<const FrameRef>{inputs});
        const FrameRef cached = cachedFrame(key);
        if (cached.image != nullptr)
        {
            normalizedLights.push_back(cached);
            normalizedKeys.push_back(std::move(key));
            continue;
        }
        normalizeExecuted = true;
        double frameMean = 0.0;
        {
            an::Result<an::ExecutionContext> measuring = startOperation(*m_engine, stopToken, &progress);
            if (!measuring.hasValue())
            {
                return measuring.status();
            }
            const std::optional<an::ImageLayout> layout = calibratedLights[index].image->layout();
            if (!layout.has_value())
            {
                return notInitialized("Workflow::run::normalizeLayout");
            }
            const an::Result<an::BasicStatistics> statistics = an::computeImageBasicStatistics(
                *measuring, an::SampleType::Float32, calibratedLights[index].image->storage().data(), *layout, 0u);
            if (!statistics.hasValue())
            {
                return statistics.status();
            }
            frameMean = statistics->mean;
        }
        ++normalizeOperations;
        const double factor = frameMean == 0.0 ? 1.0 : referenceMean / frameMean;
        const an::Result<an::PointwiseProgram> scale =
            an::buildScaleBiasProgram(chainShape(), factor, 0.0, std::nullopt);
        if (!scale.hasValue())
        {
            return scale.status();
        }
        an::Result<an::Image<float>> scaled =
            runChain(stopToken, &progress, *scale, std::span<const FrameRef>{inputs}, *descriptor, std::nullopt,
                     "an::executePointwiseProgram", "scale onto the reference level");
        if (!scaled.hasValue())
        {
            return scaled.status();
        }
        ++normalizeOperations;
        const an::Status stated = statePipelineMeaning(*scaled, *normalized, "probe::Workflow::normalize",
                                                       "reference mean " + std::to_string(referenceMean));
        if (stated.failed())
        {
            return stated;
        }
        normalizedLights.push_back(store(key, std::move(*scaled)));
        normalizedKeys.push_back(std::move(key));
    }
    finishStage(Stage::Normalize, normalizeExecuted, normalizeOperations,
                normalizedKeys.empty() ? ArtifactKey{} : normalizedKeys.front());

    // ---- Stage five: integration ---------------------------------------------------------------
    ArtifactKey integrateKey = makeKey(Stage::Integrate, "average", std::span<const FrameRef>{normalizedLights});
    FrameRef integrated = cachedFrame(integrateKey);
    std::uint32_t integrateOperations = 0u;
    if (integrated.image == nullptr)
    {
        const an::Result<an::PointwiseProgram> sum =
            an::buildBinaryArithmeticProgram(chainShape(), an::BinaryArithmeticOperation::Add);
        if (!sum.hasValue())
        {
            return sum.status();
        }
        an::Image<float> accumulated{};
        for (std::size_t index = 1u; index < normalizedLights.size(); ++index)
        {
            const FrameRef running{.identity = kRunningIdentity, .image = &accumulated};
            const std::array<FrameRef, 2u> pair{index == 1u ? normalizedLights[0] : running, normalizedLights[index]};
            an::Result<an::Image<float>> partial =
                runChain(stopToken, &progress, *sum, std::span<const FrameRef>{pair}, *descriptor, std::nullopt,
                         "an::executePointwiseProgram", "accumulate a normalized light");
            if (!partial.hasValue())
            {
                return partial.status();
            }
            accumulated = std::move(*partial);
            ++integrateOperations;
        }
        const an::Result<an::PointwiseProgram> scale = an::buildScaleBiasProgram(
            chainShape(), 1.0 / static_cast<double>(normalizedLights.size()), 0.0, std::nullopt);
        if (!scale.hasValue())
        {
            return scale.status();
        }
        const std::array<FrameRef, 1u> one{FrameRef{.identity = kRunningIdentity, .image = &accumulated}};
        an::Result<an::Image<float>> averaged =
            runChain(stopToken, &progress, *scale, std::span<const FrameRef>{one}, *descriptor, std::nullopt,
                     "an::executePointwiseProgram", "divide by the light count");
        if (!averaged.hasValue())
        {
            return averaged.status();
        }
        ++integrateOperations;
        integrated = store(integrateKey, std::move(*averaged));
    }
    finishStage(Stage::Integrate, integrateOperations != 0u, integrateOperations, integrateKey);

    // ---- Stage six: the output decision --------------------------------------------------------
    {
        an::Image<float>* writable = nullptr;
        for (CacheEntry& entry : m_cache)
        {
            if (entry.key == integrateKey)
            {
                writable = &entry.image;
                break;
            }
        }
        if (writable == nullptr)
        {
            return notInitialized("Workflow::run::integratedArtifact");
        }
        if (!writable->metadata().outputIntent().has_value())
        {
            const an::Result<an::OutputIntent> intent =
                an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt16});
            if (!intent.hasValue())
            {
                return intent.status();
            }
            an::Result<an::ProvenanceEntry> entry =
                an::ProvenanceEntry::create("probe::Workflow::stateOutput", "sixteen-bit integer output");
            if (!entry.hasValue())
            {
                return entry.status();
            }
            const an::Status stated =
                writable->transition(an::MetadataTransition{.outputIntent = *intent, .record = std::move(*entry)});
            if (stated.failed())
            {
                return stated;
            }
        }
        report.statedOutputIntent = writable->metadata().outputIntent().has_value();
        report.integratedSemanticGeneration = writable->semanticGeneration();
        for (const an::ProvenanceEntry& recorded : writable->metadata().provenance().entries())
        {
            report.integratedLineage.push_back(recorded.operation() + ": " + recorded.detail());
        }

        an::Result<an::ExecutionContext> execution = startOperation(*m_engine, stopToken, &progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        const std::optional<an::ImageLayout> layout = writable->layout();
        if (!layout.has_value())
        {
            return notInitialized("Workflow::run::outputLayout");
        }
        const an::Result<an::BasicStatistics> statistics = an::computeImageBasicStatistics(
            *execution, an::SampleType::Float32, writable->storage().data(), *layout, 0u);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        report.integratedMean = statistics->mean;
    }
    finishStage(Stage::StateOutput, true, 1u,
                makeKey(Stage::StateOutput, "sixteen-bit integer output", std::span<const FrameRef>{}));

    report.engineProgressReports = progress.count;
    return report;
}

} // namespace probe
