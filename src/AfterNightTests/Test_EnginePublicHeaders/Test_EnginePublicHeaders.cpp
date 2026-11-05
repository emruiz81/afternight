// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Public-header smoke client for AfterNightEngine.
//
// This is built the way a consumer outside the repository would build against the engine: the
// public include root and the engine's import library, and nothing else. No private include
// directory, no other AfterNight header, and deliberately no test framework - a consumer
// linking the engine would not link one, and the point of this executable is to be exactly as
// well equipped as that consumer.
//
// What it proves is worth more than its size suggests: the published surface links from outside
// its own project, and the public include root is sufficient to reach it. That breaks quietly - a
// symbol that is declared but not exported still compiles everywhere and fails only at the link
// step of the first program outside the engine.
//
// Self-containment is proved next door rather than here. `SelfCompile/` holds one translation
// unit per public header, each including that header alone, because a client that includes the
// whole surface at once cannot tell which header supplied what.

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePlanner.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePixmap.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackSource.h>
#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathEvaluator.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathLowering.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramError.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>
#include <AfterNightEngine/Algorithms/Statistics/HistogramTransform.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ResampleKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/TransferKernel.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/LibraryInfo.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/DescriptorText.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>
#include <AfterNightEngine/Interop/FftwAdapter.h>
#include <AfterNightEngine/Interop/GslAdapter.h>
#include <AfterNightEngine/Interop/OpenCvAdapter.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ParallelExecutor.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/ScratchArena.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

namespace
{

/// A progress sink implemented the way a consumer outside this repository would implement one:
/// against the published interface alone, with no test framework behind it.
class CountingTelemetrySink final : public an::ProgressSink, public an::DiagnosticSink, public an::TraceSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        ++progressCount;
        lastOperationId = report.operationId;
        lastSequence = report.sequence;
    }

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        ++diagnosticCount;
        lastOperationId = event.operationId;
        lastSequence = event.sequence;
    }

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        ++traceCount;
        lastOperationId = event.operationId;
        lastSequence = event.sequence;
    }

    std::uint64_t progressCount = 0u;
    std::uint64_t diagnosticCount = 0u;
    std::uint64_t traceCount = 0u;
    std::uint64_t lastOperationId = 0u;
    std::uint64_t lastSequence = 0u;
};

template <typename Type>
concept CompleteType = requires { sizeof(Type); };

static_assert(std::is_standard_layout_v<an::EngineVersion>);
static_assert(std::is_standard_layout_v<an::ProgressReport>);
static_assert(std::is_standard_layout_v<an::DiagnosticEvent>);
static_assert(std::is_standard_layout_v<an::TraceEvent>);
static_assert(std::is_standard_layout_v<an::OperationCounters>);
static_assert(std::is_standard_layout_v<an::KernelContract>);
static_assert(std::is_standard_layout_v<an::KernelRegistration>);
static_assert(std::is_trivially_copyable_v<an::KernelPlanKey>);
static_assert(std::is_trivially_copyable_v<an::KernelRunResult>);
static_assert(!CompleteType<an::EngineContextState>);
static_assert(!CompleteType<an::OperationArena>);
static_assert(!CompleteType<an::OperationTelemetryState>);
static_assert(!std::is_copy_constructible_v<an::EngineContext>);
static_assert(!std::is_copy_constructible_v<an::ExecutionContext>);

/// Adds two partial values of a reduction, as a consumer outside this repository would write one:
/// a plain function, because a merge has no state to capture.
///
/// @param left one partial.
/// @param right the other.
/// @return their sum.
[[nodiscard]] double addPartials(double left, double right) noexcept
{
    return left + right;
}

} // namespace

int main()
{
    // The check a consumer is expected to make before anything else. Here it also proves the
    // inline half of the version header resolves against the exported half, which is the part a
    // client outside the engine exercises and the engine's own tests cannot.
    if (!an::engineMatchesHeaders())
    {
        std::fprintf(stderr, "the loaded engine was built from different public headers\n");
        return 1;
    }

    an::EngineVersion staleVersion = an::engineHeaderVersion();
    ++staleVersion.patchVersion;
    if (an::engineCompatibility(an::engineHeaderVersion(), an::kEngineAbiGeneration - 1) !=
            an::EngineCompatibility::AbiMismatch ||
        an::engineCompatibility(staleVersion, an::kEngineAbiGeneration) != an::EngineCompatibility::VersionMismatch)
    {
        std::fprintf(stderr, "the public surface did not distinguish a stale layout from a stale version\n");
        return 1;
    }

    const an::LibraryInfo info = an::libraryInfo();

    {
        const auto drizzleExtent = an::ImageExtent::create(7u, 5u, 1u, an::defaultEngineLimits());
        const auto drizzleInput = an::ImageDescriptor::create({.extent = *drizzleExtent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
        auto drizzleEngine = an::EngineContext::create({.maxConcurrency = 1u});
        if (!drizzleInput.has_value() || !drizzleEngine.hasValue())
            return 1;
        auto drizzleExecution = an::ExecutionContext::create(*drizzleEngine, {});
        const auto drizzleModel = an::DrizzleModel::create({.input = *drizzleInput, .frameCount = 3u});
        if (!drizzleExecution.hasValue() || !drizzleModel.hasValue())
            return 1;
        const auto drizzleMap = an::DrizzleMapDescriptor::create(*drizzleModel, {});
        const auto drizzlePlan = an::planDrizzle(*drizzleExecution, *drizzleModel, {});
        if (!drizzleMap.hasValue() || !drizzlePlan.hasValue() || drizzlePlan->destinationRows != 10u ||
            drizzleMap->outputExtent().width() != 14u ||
            an::drizzleOperationEffect().validity.produced.rule() != an::ValidityRule::ExplicitMask)
        {
            std::fprintf(stderr, "the public surface did not validate and plan a drizzle sequence\n");
            return 1;
        }
        auto geometryCache = an::DrizzleGeometryCache::create(*drizzleExecution);
        if (!geometryCache.hasValue())
            return 1;
        auto geometry = geometryCache->get(*drizzleExecution, *drizzleMap);
        if (!geometry.hasValue())
            return 1;
        auto coordinates = an::generateDrizzlePixmap(*drizzleExecution, *geometry, {.haloRows = 1u});
        if (!coordinates.hasValue() || !coordinates->view().hasValue())
            return 1;
        auto corners = an::interpolateDrizzleCorners(*coordinates->view(), 0u, 0u, 0.5);
        if (!corners.hasValue() ||
            !an::mapDrizzlePointInverse(*drizzleMap, {(*corners)[0].x, (*corners)[0].y}).hasValue())
            return 1;
        std::array<an::DrizzleFootprint, 1u> footprints;
        auto footprintReport =
            an::interpolateDrizzleCornerRow(*drizzleExecution, *coordinates->view(), 0u, 0u, 0.5, footprints);
        if (!footprintReport.hasValue() || footprintReport->mapped != 1u || footprints[0] != *corners)
            return 1;
        std::array<float, 35u> samples{};
        std::array<double, 140u> sums{}, weights{};
        const auto layout = an::ImageLayout::create({*drizzleExtent, 4u, 1u, 1u, 1u}, drizzleExecution->limits());
        const an::DrizzleInputBand input{{*layout, std::as_bytes(std::span{samples}), an::SampleType::Float32}};
        const auto accumulated = an::accumulateDrizzleBand(*drizzleExecution, *drizzleModel, input,
                                                           *coordinates->view(), {0u, 10u, sums, weights});
        const auto table = an::DrizzleKernelTable::create(an::DrizzleKernel::Lanczos3);
        if (!accumulated.hasValue() || accumulated->selected != 35u || accumulated->contributions == 0u ||
            !table.hasValue() || table->lookup(0.0f).value() != 1.0f ||
            !an::drizzleSquareOverlap(*corners, 0u, 0u).hasValue())
            return 1;
        const std::array<an::DrizzleFrameData, 3u> frames{{{input.pixels}, {input.pixels}, {input.pixels}}};
        const std::array<an::DrizzleFrameParameters, 3u> mappings{{{*drizzleMap}, {*drizzleMap}, {*drizzleMap}}};
        auto source = an::ResidentDrizzleFrameSource::create(frames, an::DrizzleSourceAccess::Region);
        if (!source.hasValue())
            return 1;
        auto result = an::executeDrizzle(*drizzleEngine, *drizzleExecution, *drizzleModel, *source, mappings);
        if (!result.hasValue() || result->report.selected != 105u || !result->mask.valid())
            return 1;
        auto stackSource = an::DrizzledStackFrameSource::create(*drizzleEngine, *drizzleModel, *source, mappings);
        if (!stackSource.hasValue())
            return 1;
        // Moved out of its result into the caller's own object, as a consumer keeps one: the move writes the source's
        // virtual-table pointer here, so the table has to be one the library publishes.
        an::DrizzledStackFrameSource kept = std::move(*stackSource);
        if (kept.access() != an::StackSourceAccess::WholeFrame)
            return 1;
    }

    if (info.name == nullptr || std::strcmp(info.name, "AfterNightEngine") != 0)
    {
        std::fprintf(stderr, "the public surface reported an unexpected library name\n");
        return 1;
    }
    if (info.configuration == nullptr || info.compiler == nullptr)
    {
        std::fprintf(stderr, "the public surface reported an incomplete library identity\n");
        return 1;
    }

    // The checked geometry primitives, exercised the way a consumer outside the engine reaches
    // them: through the public include root alone. They are header-only, so what this proves is
    // not that they link but that the published surface is enough to use them - a header that
    // needed a private one beside it would fail here.
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8192u, 6144u, 3u, limits);
    if (!extent.has_value())
    {
        std::fprintf(stderr, "the public surface refused an ordinary frame geometry\n");
        return 1;
    }

    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = 3u,
                                         .rowAlignmentBytes = an::kDefaultBufferAlignmentBytes};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    if (!layout.has_value() || layout->totalBytes() != extent->sampleCount() * request.elementSizeBytes)
    {
        std::fprintf(stderr, "the public surface computed an unexpected frame size\n");
        return 1;
    }

    // And the refusal, which is the half a consumer has to be able to see: a descriptor that does
    // not describe its own image comes back as a reason rather than as a number.
    if (an::ImageLayout::checkWithStrides(request, 1u, 1u, limits) != an::GeometryError::StrideTooSmall)
    {
        std::fprintf(stderr, "the public surface accepted a stride smaller than its own rows\n");
        return 1;
    }

    // The image model on top of that geometry. Its rules are header-only like the geometry, but its
    // vocabulary names and its text form are compiled into the library, so this is where an
    // unexported name table would fail at the link step of the first program outside the engine.
    const an::ImageDescriptorRequest descriptorRequest{.extent = *extent,
                                                       .sampleType = an::SampleType::Float32,
                                                       .channelSchema = an::ChannelSchema::Rgb,
                                                       .colorSpace = an::ColorSpace::LinearRgb,
                                                       .numericDomain = an::NumericDomain::NormalizedLinear,
                                                       .cfa = an::CfaDescriptor{},
                                                       .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                                       .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(descriptorRequest);
    if (!descriptor.has_value() || an::storageArrangementOf(*layout) != an::StorageArrangement::Planar)
    {
        std::fprintf(stderr, "the public surface refused an ordinary frame description\n");
        return 1;
    }

    const std::string descriptorText = an::formatImageDescriptor(*descriptor);
    if (an::parseImageDescriptor(descriptorText, limits) != descriptor)
    {
        std::fprintf(stderr, "the public surface did not read back the description it wrote\n");
        return 1;
    }

    an::ImageDescriptorRequest impossible = descriptor->toRequest();
    impossible.numericDomain = an::NumericDomain::RawSensorCode;
    if (an::ImageDescriptor::check(impossible) != an::DescriptorError::NumericDomainMismatch)
    {
        std::fprintf(stderr, "the public surface accepted sensor codes in a colorimetric basis\n");
        return 1;
    }

    // The result and error model, reached the same way. Unlike the geometry above, part of this one
    // is compiled into the library rather than into the caller - the code tables and the exception
    // translators - so this is where a symbol that is declared and not exported would fail, and it
    // would fail at the link step of the first program outside the engine rather than anywhere in
    // the engine's own tests.
    const an::Result<an::ImageLayout> produced = *layout;
    if (!produced.hasValue() || produced.status().failed() || produced->totalBytes() != layout->totalBytes())
    {
        std::fprintf(stderr, "the public surface lost a value it reported as produced\n");
        return 1;
    }

    const an::Status refused = an::Status::failure(
        an::ErrorCode::MemoryBudgetExceeded, an::StatusDetail{.message = "the frame does not fit the configured budget",
                                                              .operation = "Test_EnginePublicHeaders::main",
                                                              .domain = an::ErrorDomain::Memory,
                                                              .context = {an::StatusContextField::ofCount(
                                                                  an::ContextKey::Requested, layout->totalBytes())}});
    const an::StatusContextField* requested = refused.findContext(an::ContextKey::Requested);
    if (refused.ok() || refused.category() != an::ErrorCategory::Resource || requested == nullptr ||
        requested->count != layout->totalBytes())
    {
        std::fprintf(stderr, "the public surface did not report a budget failure it was given\n");
        return 1;
    }

    // The translation half, which is the only place an exception is allowed to exist at all.
    constexpr an::ExternalBoundary kBoundary{
        .library = "none", .operation = "Test_EnginePublicHeaders::main", .domain = an::ErrorDomain::Interop};
    const an::Status translated =
        an::callAcrossBoundary(kBoundary, [] { throw std::runtime_error("a library said no"); });
    if (translated.code() != an::ErrorCode::ExternalFailure ||
        std::strcmp(translated.message(), "a library said no") != 0)
    {
        std::fprintf(stderr, "the public surface let an exception cross a boundary untranslated\n");
        return 1;
    }

    // The runtime, which is the first part of this engine that owns something rather than
    // describing something. A consumer outside the repository reaches it the same way: ask the
    // build what it offers, configure a context from that, and hold the context.
    const an::EngineCapabilities capabilities = an::engineCapabilities();
    if (capabilities.version != an::engineHeaderVersion() || capabilities.hostConcurrency == 0u)
    {
        std::fprintf(stderr, "the public surface reported capabilities a caller cannot configure from\n");
        return 1;
    }

    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.limits = limits;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    if (!created.hasValue() || created->maxConcurrency() != 2u || created->id() == 0u)
    {
        std::fprintf(stderr, "the public surface did not build a context it was given valid options for\n");
        return 1;
    }

    an::EngineContext context = std::move(*created);

    // The lifetime rule, exercised rather than described: a context with work outstanding refuses
    // to shut down, and takes the same instruction again once the work is finished.
    an::Result<an::EngineContextLease> lease = context.acquireLease();
    if (!lease.hasValue() || lease->contextId() != context.id() || context.quiescent())
    {
        std::fprintf(stderr, "the public surface did not count a lease it handed out\n");
        return 1;
    }
    if (context.shutdown().code() != an::ErrorCode::Busy)
    {
        std::fprintf(stderr, "the public surface shut a context down while work was outstanding\n");
        return 1;
    }

    lease->release();
    const an::Result<an::EngineContext> replacement = an::EngineContext::createReplacement(context, options);
    if (!replacement.hasValue() || context.valid() || replacement->id() == 0u)
    {
        std::fprintf(stderr, "the public surface did not replace a quiescent context\n");
        return 1;
    }

    // One operation in that context, driven the way an algorithm drives it: a stop token it does
    // not own, a sink it does own, a narrower share of the machine than the engine permits, and a
    // checkpoint that answers in the vocabulary the rest of the surface uses.
    CountingTelemetrySink telemetry;

    std::stop_source stopSource;
    an::ExecutionOptions executionOptions;
    executionOptions.stopToken = stopSource.get_token();
    executionOptions.progressSink = &telemetry;
    executionOptions.diagnosticSink = &telemetry;
    executionOptions.traceSink = &telemetry;
    executionOptions.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*replacement, executionOptions);
    if (!execution.hasValue() || execution->maxConcurrency() != 1u || execution->operationId() == 0u ||
        execution->engineContextId() != replacement->id())
    {
        std::fprintf(stderr, "the public surface did not start an operation it was given valid options for\n");
        return 1;
    }

    execution->reportProgress(1u, 2u, "smoke");
    execution->reportDiagnostic(an::DiagnosticSeverity::Info, an::ErrorDomain::Runtime, "smoke", "client event");
    execution->reportTrace(an::TraceEventKind::Counter, "smoke", "callbacks", 3.0, "events");
    const an::OperationCounters operationCounters = execution->operationCounters();
    if (telemetry.progressCount != 1u || telemetry.diagnosticCount != 1u || telemetry.traceCount != 1u ||
        telemetry.lastOperationId != execution->operationId() || telemetry.lastSequence != 3u ||
        operationCounters.operationId != execution->operationId() || operationCounters.progressReports != 1u ||
        operationCounters.diagnosticEvents != 1u || operationCounters.traceEvents != 1u ||
        operationCounters.lastSequence != 3u)
    {
        std::fprintf(stderr, "the public surface did not correlate callbacks and counters for one operation\n");
        return 1;
    }

    if (execution->checkCancellation().failed())
    {
        std::fprintf(stderr, "the public surface reported a cancellation nobody asked for\n");
        return 1;
    }

    // The scheduler, the memory and the dispatch, driven the way a kernel drives them. This is the
    // part worth linking from outside the engine rather than only calling from inside it: a
    // parallel loop reaches a task arena, an allocator and a dispatch table that all live behind
    // the published surface, and a consumer sees none of those three in a header it includes.
    an::MemoryResource* const memory = replacement->memoryResource();
    if (memory == nullptr || execution->memoryResource() != memory)
    {
        std::fprintf(stderr, "the public surface did not hand out the context's memory resource\n");
        return 1;
    }

    an::Result<an::ScratchArena> arena = an::ScratchArena::create(*replacement);
    if (!arena.hasValue() || !arena->valid())
    {
        std::fprintf(stderr, "the public surface refused to build a scratch arena over its own resource\n");
        return 1;
    }

    const an::Result<void*> scratch = arena->allocate(4096u);
    if (!scratch.hasValue() || arena->counters().liveBytes != 4096u || memory->counters().liveBytes == 0u)
    {
        std::fprintf(stderr, "the public surface did not account for the scratch it handed out\n");
        return 1;
    }

    // A second operation, because the first is about to be cancelled and a loop needs one that is
    // not. It is narrowed to a single thread so that what this client reports is a number it can
    // predict on any machine.
    an::ExecutionOptions loopOptions;
    loopOptions.scratch = &arena.value();
    loopOptions.maxConcurrency = 1u;
    const an::Result<an::ExecutionContext> loop = an::ExecutionContext::create(*replacement, loopOptions);
    if (!loop.hasValue() || loop->admittedConcurrency() != 1u || loop->scratch() != &arena.value())
    {
        std::fprintf(stderr, "the public surface did not start an operation over the arena it was given\n");
        return 1;
    }

    std::uint64_t visited = 0u;
    const an::Status walked = an::parallelFor(*loop, 1000u, an::TaskCost{}, [&](an::TaskRange range) {
        visited += range.size();
        return an::Status::success();
    });
    if (walked.failed() || visited != 1000u)
    {
        std::fprintf(stderr, "the public surface did not visit every item of a parallel loop\n");
        return 1;
    }

    const an::Result<double> total = an::parallelReduce(
        *loop, 100u, an::TaskCost{}, 0.0,
        [](an::TaskRange range, double& partial) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                partial += static_cast<double>(index);
            }
            return an::Status::success();
        },
        &addPartials);
    if (!total.hasValue() || *total != 4950.0)
    {
        std::fprintf(stderr, "the public surface reduced a hundred integers to the wrong number\n");
        return 1;
    }

    const an::Result<an::BackendSelection> selected = an::selectBackend(*loop);
    if (!selected.hasValue() || selected->instructionSet == an::InstructionSet::Automatic ||
        !an::supportsInstructionSet(an::cpuCapabilities(), selected->instructionSet) ||
        selected->instructionSet != an::instructionSetOfDispatchedCode(*loop))
    {
        std::fprintf(stderr, "the public surface did not report an implementation this host can run\n");
        return 1;
    }

    // An image, taken from the same context. This is the first thing in the surface that owns a
    // frame's worth of memory, and it is worth a consumer exercising because three separate parts
    // of the library have to agree for it to work: the arrangement derives the geometry from the
    // description, the storage takes the bytes from the context's own resource and counts them, and
    // the view pairs the typed pointer with the layout without checking anything a second time.
    const an::ImageDescriptorRequest smallRequest{.extent = an::ImageExtent::create(64u, 32u, 3u, limits).value(),
                                                  .sampleType = an::SampleType::UInt16,
                                                  .channelSchema = an::ChannelSchema::Rgb,
                                                  .colorSpace = an::ColorSpace::LinearRgb,
                                                  .numericDomain = an::NumericDomain::NormalizedLinear,
                                                  .cfa = an::CfaDescriptor{},
                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                  .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> smallDescriptor = an::ImageDescriptor::create(smallRequest);
    const std::optional<an::ImageLayoutRequest> planarRequest =
        smallDescriptor.has_value() ? an::layoutRequestFor(*smallDescriptor, an::StorageArrangement::Planar)
                                    : std::nullopt;
    if (!planarRequest.has_value() || planarRequest->planeCount != 3u || planarRequest->samplesPerPixel != 1u)
    {
        std::fprintf(stderr, "the public surface did not derive a planar geometry from a description\n");
        return 1;
    }

    const std::uint64_t liveBeforeImage = memory->counters().liveBytes;
    an::Result<an::ImageStorage<std::uint16_t>> image =
        an::ImageStorage<std::uint16_t>::createFilled(*replacement, *planarRequest, 1234u);
    if (!image.hasValue() || !image->valid() || image->contextId() != replacement->id() ||
        memory->counters().liveBytes <= liveBeforeImage)
    {
        std::fprintf(stderr, "the public surface did not account for an image it allocated\n");
        return 1;
    }

    const std::optional<an::ImageView<std::uint16_t>> imageView = image->view();
    const std::optional<an::PlaneView<std::uint16_t>> greenPlane =
        imageView.has_value() ? imageView->plane(1u) : std::nullopt;
    const std::optional<std::span<std::uint16_t>> greenRow =
        greenPlane.has_value() ? greenPlane->row(0u) : std::nullopt;
    if (!greenRow.has_value() || greenRow->size() != 64u || (*greenRow)[63] != 1234u)
    {
        std::fprintf(stderr, "the public surface did not fill every sample of every plane it allocated\n");
        return 1;
    }
    if (!an::viewsOverlap(*imageView, *imageView) || image->layout()->totalBytes() != image->logicalBytes())
    {
        std::fprintf(stderr, "the public surface disagreed with itself about the image it is holding\n");
        return 1;
    }

    const std::uint64_t liveWithImage = memory->counters().liveBytes;
    image->release();
    if (memory->counters().liveBytes >= liveWithImage || image->valid())
    {
        std::fprintf(stderr, "the public surface kept an image's memory after releasing it\n");
        return 1;
    }

    // The validity model and the conversion boundary. Both are reached here rather than only in the
    // engine's own tests because both are mostly exported rather than header-only: the rule names,
    // the policy application, the empty-selection failure, the mask allocation and the dispatched
    // conversion all live in the library, and a symbol declared and not exported would fail at this
    // link step and at no earlier one.
    const std::optional<an::ValidityDescriptor> zeroIsAbsent =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    const an::ValidityDescriptor claims[] = {an::ValidityDescriptor::allValid(),
                                             zeroIsAbsent.value_or(an::ValidityDescriptor{})};
    const an::Result<an::ValidityDescriptor> combined = an::applyValidityPolicy(
        an::ValidityTransition{.policy = an::ValidityPolicy::Combine}, std::span<const an::ValidityDescriptor>{claims});
    if (!zeroIsAbsent.has_value() || !combined.hasValue() || !combined->needsMask() ||
        std::strcmp(an::validityRuleName(combined->rule()), "ExplicitMask") != 0)
    {
        std::fprintf(stderr, "the public surface did not combine two different validity claims into a mask\n");
        return 1;
    }

    if (an::requireSelection(an::ValidityCount{.valid = 0u, .total = 16u}, "smoke").code() !=
        an::ErrorCode::EmptySelection)
    {
        std::fprintf(stderr, "the public surface did not report a reduction over nothing as one\n");
        return 1;
    }

    const std::optional<an::ImageExtent> maskExtent = an::ImageExtent::create(10u, 4u, 1u, limits);
    an::Result<an::ValidityMask> mask =
        maskExtent.has_value() ? an::ValidityMask::createAllValid(*replacement, *maskExtent)
                               : an::Result<an::ValidityMask>{an::Status::failure(an::ErrorCode::InvalidArgument)};
    const std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask.hasValue() ? mask->view() : std::nullopt;
    if (!maskView.has_value() || an::countValidity(maskView->constView()).valid != 40u ||
        !an::countValidity(maskView->constView()).complete())
    {
        std::fprintf(stderr, "the public surface did not build a mask that claims every pixel it covers\n");
        return 1;
    }
    an::invertValidity(*maskView);
    if (!an::countValidity(maskView->constView()).empty())
    {
        std::fprintf(stderr, "the public surface did not invert a mask it built\n");
        return 1;
    }
    mask->release();

    // The conversion itself, on the operation that has not been stopped, over a frame small enough
    // that a client can state its expected answers.
    const std::optional<an::ImageExtent> smallExtent = an::ImageExtent::create(4u, 2u, 1u, limits);
    const std::optional<an::ImageLayout> sourceLayout =
        smallExtent.has_value() ? an::ImageLayout::create(an::ImageLayoutRequest{.extent = *smallExtent,
                                                                                 .elementSizeBytes = 4u,
                                                                                 .samplesPerPixel = 1u,
                                                                                 .planeCount = 1u,
                                                                                 .rowAlignmentBytes = 1u},
                                                          limits)
                                : std::nullopt;
    const std::optional<an::ImageLayout> destinationLayout =
        smallExtent.has_value() ? an::ImageLayout::create(an::ImageLayoutRequest{.extent = *smallExtent,
                                                                                 .elementSizeBytes = 2u,
                                                                                 .samplesPerPixel = 1u,
                                                                                 .planeCount = 1u,
                                                                                 .rowAlignmentBytes = 1u},
                                                          limits)
                                : std::nullopt;
    float sourceSamples[8] = {0.0f, 0.5f, 1.0f, 2.0f, -1.0f, 0.25f, 0.75f, 0.125f};
    std::uint16_t destinationSamples[8] = {};
    an::ConversionCounters conversionCounters;
    const an::Status converted =
        sourceLayout.has_value() && destinationLayout.has_value()
            ? an::convertImageSamples(*loop, an::SampleType::Float32, sourceSamples, *sourceLayout,
                                      an::SampleType::UInt16, destinationSamples, *destinationLayout,
                                      an::ConversionPolicy{}, conversionCounters)
            : an::Status::failure(an::ErrorCode::InvalidArgument);
    if (converted.failed() || conversionCounters.samplesConverted != 8u || conversionCounters.saturatedSamples != 2u ||
        destinationSamples[0] != 0u || destinationSamples[2] != 65535u || destinationSamples[3] != 65535u ||
        destinationSamples[4] != 0u)
    {
        std::fprintf(stderr, "the public surface did not convert a frame by the rules it publishes\n");
        return 1;
    }
    if (destinationSamples[1] != an::convertSample<float, std::uint16_t>(0.5f, an::ConversionPolicy{}).value)
    {
        std::fprintf(stderr, "the dispatched conversion disagreed with the scalar reference beside it\n");
        return 1;
    }

    // The external-library adapters, exercised the way the one call site that has OpenCV would:
    // state what the samples mean and what the library will treat them as, get a checked
    // description back, and build the foreign object from it. Nothing here includes an OpenCV,
    // FFTW or GSL header, which is the property worth proving from outside the engine - a consumer
    // that never uses those libraries links this surface without them.
    const std::optional<an::PlaneView<float>> sourcePlane =
        an::PlaneView<float>::create(std::span<float>{sourceSamples}, 4u, 2u, 1u, 16u);

    // The immutable kernel registry and one scalar oracle, reached through only the public surface.
    // This proves that a dispatch key a consumer records names the implementation that actually
    // runs, and that the family entry point is exported from the library rather than merely usable
    // by its own translation units.
    const std::span<const an::KernelRegistration> registry = an::kernelRegistry();
    const an::KernelRegistration* transferRegistration = nullptr;
    for (const an::KernelRegistration& registration : registry)
    {
        if (registration.contract.id.operation == an::KernelOperation::TransferCopy)
        {
            transferRegistration = &registration;
            break;
        }
    }
    const an::KernelDispatchKey transferKey{.plan = transferRegistration != nullptr
                                                        ? an::kernelPlanFor(transferRegistration->contract)
                                                        : an::KernelPlanKey{},
                                            .backend = an::ComputeBackend::Reference,
                                            .instructionSet = an::InstructionSet::Scalar};
    float copiedSamples[8] = {};
    const std::optional<an::PlaneView<float>> copiedPlane =
        an::PlaneView<float>::create(std::span<float>{copiedSamples}, 4u, 2u, 1u, 16u);
    const an::Result<an::KernelRunResult> copied =
        sourcePlane.has_value() && copiedPlane.has_value()
            ? an::referenceTransferKernel(*loop, sourcePlane->constView(), *copiedPlane, {})
            : an::Result<an::KernelRunResult>{an::Status::failure(an::ErrorCode::InvalidArgument)};
    if (registry.size() != 9u || transferRegistration == nullptr ||
        an::findKernelRegistration(transferKey) != transferRegistration || !copied.hasValue() ||
        copied->outputsWritten != 8u || copiedSamples[7] != sourceSamples[7])
    {
        std::fprintf(stderr, "the public surface did not dispatch and run its scalar transfer oracle\n");
        return 1;
    }

    const an::ExternalBufferContract shareable{.engineDomain = an::NumericDomain::NormalizedLinear,
                                               .libraryDomain = an::NumericDomain::NormalizedLinear,
                                               .ownership = an::BufferOwnership::EngineOwned,
                                               .scope = an::BorrowScope::SingleCall,
                                               .mutability = an::BufferMutability::ReadWrite};
    const std::optional<an::OpenCvMatDescription> asMatrix =
        sourcePlane.has_value() ? an::describeAsOpenCvMat(*sourcePlane, shareable, 1u, an::kOpenCvTypeLayout4)
                                : std::nullopt;
    const std::optional<an::GslMatrixDescription> asGslMatrix =
        sourcePlane.has_value() ? an::describeAsGslMatrix(*sourcePlane, shareable) : std::nullopt;
    const std::optional<an::FftwArrayDescription> asFftwArray =
        sourcePlane.has_value() ? an::describeAsFftwReal(*sourcePlane, shareable, an::FftwPlacement::OutOfPlace, 1u)
                                : std::nullopt;
    if (!asMatrix.has_value() || asMatrix->rows != 2 || asMatrix->cols != 4 ||
        asMatrix->type != an::openCvTypeCode(an::OpenCvDepth::Float32, 1, an::kOpenCvTypeLayout4).value() ||
        asMatrix->data != sourceSamples || !asGslMatrix.has_value() || asGslMatrix->size1 != 2u ||
        asGslMatrix->size2 != 4u || asGslMatrix->tda != 4u || !asFftwArray.has_value() || asFftwArray->n0 != 2 ||
        asFftwArray->n1 != 4 || asFftwArray->rowStrideReals != 4)
    {
        std::fprintf(stderr, "the public surface refused to share samples all three libraries can read\n");
        return 1;
    }

    // And the refusal a consumer has to be able to see. The same plane, described to a library that
    // will read it as sensor codes, is not shared and says which rule refused - the finding this
    // vocabulary exists for, since the alternative is a rescale by the source type that says nothing.
    an::ExternalBufferContract rescaling = shareable;
    rescaling.libraryDomain = an::NumericDomain::RawSensorCode;
    if (!sourcePlane.has_value() ||
        an::checkOpenCvMat(*sourcePlane, rescaling, 1u, an::kOpenCvTypeLayout4) !=
            an::BridgeRefusal::NumericDomainDiffers ||
        std::strcmp(an::bridgeRefusalName(an::BridgeRefusal::NumericDomainDiffers), "NumericDomainDiffers") != 0)
    {
        std::fprintf(stderr, "the public surface shared samples with a library that means something else by them\n");
        return 1;
    }

    // The core operation surface, driven the way a consumer drives it: one small frame, one
    // operation of each family, and the declaration each of them publishes about what it did.
    //
    // A self-compile unit next door proves each of these headers stands alone. This proves the other
    // half, which only a program outside the engine can: that the entry points and the declarations
    // are exported from the library rather than merely usable inside it. Everything runs on `loop`,
    // because an execution context holds a share of the engine's thread budget for as long as it
    // exists and this client already has two outstanding.
    const an::Result<an::BasicStatistics> statistics =
        an::computeImageBasicStatistics(*loop, an::SampleType::Float32, sourceSamples, *sourceLayout, 0u);
    if (!statistics.hasValue() || statistics->selection.valid != 8u || statistics->minimum != -1.0 ||
        statistics->maximum != 2.0 || an::kBasicStatisticsEffect.samples != an::SampleEffect::Reads)
    {
        std::fprintf(stderr, "the public surface did not reduce eight samples it can see\n");
        return 1;
    }

    const an::Result<an::RobustStatistics> robust = an::computeImageRobustStatistics(
        *loop, an::SampleType::Float32, sourceSamples, *sourceLayout, 0u, an::StatisticsSelection{},
        an::RobustStatisticsRequest{.medianAbsoluteDeviation = true});
    if (!robust.hasValue() || !(robust->medianAbsoluteDeviation >= 0.0) ||
        an::kRobustStatisticsEffect.samples != an::SampleEffect::Reads)
    {
        std::fprintf(stderr, "the public surface did not order the samples it reduced\n");
        return 1;
    }

    std::uint64_t bins[4] = {};
    const an::Result<an::ImageHistogram> histogram = an::computeImageHistogram(
        *loop, an::SampleType::Float32, sourceSamples, *sourceLayout, 0u, an::StatisticsSelection{},
        an::ImageHistogramRequest{}, std::span<std::uint64_t>{bins});
    if (!histogram.hasValue() || histogram->belowRange != 1u || histogram->aboveRange != 1u ||
        an::kImageHistogramEffect.samples != an::SampleEffect::Reads)
    {
        std::fprintf(stderr, "the public surface did not count the samples inside its own interval\n");
        return 1;
    }

    an::PointwisePlanShape shape;
    shape.channelCount = 1u;
    const an::Result<an::PointwiseProgram> scaled =
        an::buildScaleBiasProgram(shape, 2.0, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
    const an::Result<an::PowerProgramPlan> gamma =
        an::buildGammaProgram(an::TonePlanShape{.channelCount = 1u}, 2.2, an::GammaDirection::Encode);
    float scaledSamples[8] = {};
    const an::PointwiseImageView chainSource{.samples = sourceSamples, .layout = &sourceLayout.value()};
    const an::Result<an::PointwiseExecutionResult> chained =
        scaled.hasValue()
            ? an::executePointwiseProgram(
                  *loop, *scaled, std::span<const an::PointwiseImageView>{&chainSource, 1u},
                  an::PointwiseMutableImageView{.samples = scaledSamples, .layout = &sourceLayout.value()},
                  an::KernelValidityView{})
            : an::Result<an::PointwiseExecutionResult>{an::Status::failure(an::ErrorCode::InvalidArgument)};
    if (!chained.hasValue() || scaledSamples[1] != 1.0f || scaledSamples[5] != 0.5f || !gamma.hasValue() ||
        gamma->program().steps().empty())
    {
        std::fprintf(stderr, "the public surface did not run the chain it built\n");
        return 1;
    }

    // What that chain says it did, which is the declaration this facade exists to publish. A scale
    // and clamp changes no description and no meaning; a gamma encode changes both.
    const an::OperationEffect scaleEffect = an::pointwiseProgramEffect(*scaled);
    const an::OperationEffect gammaEffect = an::pointwiseProgramEffect(gamma->program());
    if (an::checkOperationEffect(scaleEffect) != an::OperationEffectError::None ||
        scaleEffect != an::OperationEffect{} || gammaEffect.semantics != an::SemanticEffect::Restates ||
        gammaEffect.description != an::DescriptionEffect::Restates)
    {
        std::fprintf(stderr, "the public surface did not declare what its own chains do\n");
        return 1;
    }

    const an::Result<an::MetadataTransition> transition = an::operationTransition(
        scaleEffect, an::OperationOutcome{.operation = "an::executePointwiseProgram", .detail = "scale by two"});
    if (!transition.hasValue() || !transition->record.has_value() || an::changesMeaning(*transition))
    {
        std::fprintf(stderr, "the public surface did not build the transition its declaration requires\n");
        return 1;
    }

    // Colour, masks and the expression front end: one exported entry point each, so that a symbol
    // declared and not exported fails here rather than in the first consumer.
    const an::ColorEncoding encodedRgb{.transfer = an::ColorTransferFunction::Srgb};
    const an::ColorEncoding linearRgb{.transfer = an::ColorTransferFunction::Linear};
    const an::Result<an::ColorConversionPlan> colorPlan = an::planColorConversion(encodedRgb, linearRgb);
    if (!colorPlan.hasValue() || colorPlan->isIdentity() ||
        an::colorConversionEffect(*colorPlan).semantics != an::SemanticEffect::Restates)
    {
        std::fprintf(stderr, "the public surface did not plan a colour transform it publishes\n");
        return 1;
    }

    an::Result<an::ValidityMask> selection = an::ValidityMask::create(*replacement, *smallExtent);
    const std::optional<an::ValidityMaskView<std::uint8_t>> selectionView =
        selection.hasValue() ? selection->view() : std::nullopt;
    const an::Status maskBuilt =
        selectionView.has_value()
            ? an::buildImageMask(
                  *loop, an::SampleType::Float32, sourceSamples, *sourceLayout,
                  an::MaskThresholdRequest{
                      .threshold = an::SampleThreshold{.predicate = an::MaskPredicate::AtOrAbove, .lowerBound = 0.5}},
                  *selectionView)
            : an::Status::failure(an::ErrorCode::InvalidArgument);
    const an::Result<an::ValidityCount> selectedCount =
        selectionView.has_value() ? an::countImageMask(*loop, selectionView->constView())
                                  : an::Result<an::ValidityCount>{an::Status::failure(an::ErrorCode::InvalidArgument)};
    if (maskBuilt.failed() || !selectedCount.hasValue() || selectedCount->valid != 4u ||
        an::kMaskInspectionEffect.samples != an::SampleEffect::Reads)
    {
        std::fprintf(stderr, "the public surface did not select the samples its own threshold names\n");
        return 1;
    }
    selection->release();

    const an::PixelMathParse parsed = an::parsePixelMath("$T * 0.5 + 0.25");
    if (!parsed.succeeded() || parsed.status().failed())
    {
        std::fprintf(stderr, "the public surface did not parse an expression it publishes a grammar for\n");
        return 1;
    }

    stopSource.request_stop();
    if (execution->checkCancellation().code() != an::ErrorCode::Cancelled)
    {
        std::fprintf(stderr, "the public surface did not observe a stop request\n");
        return 1;
    }

    // And a loop under the same token gives up rather than finishing, which is the half of
    // cancellation that a checkpoint call on its own does not prove.
    if (an::parallelFor(*execution, 1000u, an::TaskCost{}, [](an::TaskRange) {
            return an::Status::success();
        }).code() != an::ErrorCode::Cancelled)
    {
        std::fprintf(stderr, "the public surface ran a loop whose operation had already been stopped\n");
        return 1;
    }

    std::printf("%s %s %s, built by %s; %llu bytes for an %ux%ux%u frame; %s reports \"%s\"; context %llu holds %u "
                "of %u threads; operation %llu ran %s at %s and reported %llu %s step\n",
                info.name, an::engineVersionString(), info.configuration, info.compiler,
                static_cast<unsigned long long>(layout->totalBytes()), extent->width(), extent->height(),
                extent->channelCount(), an::errorCodeName(refused.code()), refused.message(),
                static_cast<unsigned long long>(replacement->id()), replacement->maxConcurrency(),
                capabilities.hostConcurrency, static_cast<unsigned long long>(execution->operationId()),
                an::computeBackendName(execution->backend()),
                an::reductionDeterminismName(execution->reductionDeterminism()),
                static_cast<unsigned long long>(telemetry.progressCount),
                an::diagnosticSeverityName(an::DiagnosticSeverity::Info));

    std::printf("operation %llu walked %llu items on %u thread, reduced to %.1f, dispatched to %s/%s, and holds "
                "%llu scratch bytes of %llu reserved\n",
                static_cast<unsigned long long>(loop->operationId()), static_cast<unsigned long long>(visited),
                an::plannedWorkerCount(*loop, 1000u, an::TaskCost{}), *total, an::computeBackendName(selected->backend),
                an::instructionSetName(selected->instructionSet),
                static_cast<unsigned long long>(arena->counters().liveBytes),
                static_cast<unsigned long long>(arena->reservedBytes()));
    return 0;
}
