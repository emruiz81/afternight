// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - one-pass execution of the validated pointwise IR.
//
// Every invocation is validated completely before the first write, then walked once over row
// ranges on the operation's threads. Within a range there are three ways a sample is computed, in
// the order they are tried:
//
// - a contiguous all-valid range whose pixel count is a whole number of the widest vector goes to
//   the compiled run as one flattened span - any contiguous range of integer samples, whose run
//   narrows its own last partial vector;
// - otherwise each row goes to the compiled run, which writes the whole vectors at its start and
//   says how many pixels that was;
// - whatever is left - a row's tail, every pixel under a validity rule other than all-valid, and
//   every program the dispatch table has no compiled run for - is evaluated one pixel at a time by
//   the scalar evaluator and narrowed through the conversion policy.
//
// A compiled run over integer samples - a mask's or a modulation's blend over a camera frame - is
// the evaluator's arithmetic at the program's width with its narrowing, so which pixels it takes and
// which the evaluator does changes no sample and no count.
//
// A lower-neighbour preview curve is refused unless the private entry permits it, and then takes a
// narrow path of its own. An eight- or sixteen-bit image's program that is one exact lookup - an
// integer curve - takes another: the scalar evaluator's answer for each code is made once and every
// sample looked up in it. A Float32 program that is one sampled curve - a floating image's curve -
// takes a third: its table narrowed once, read by a compiled run. Counters are accumulated per range
// and published with relaxed atomics, because only their totals are read, after the walk.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/pointwise/PointwiseExecutionInternal.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/ConversionKernels.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::executePointwiseProgram";

[[nodiscard]] Status pointwiseFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool isPlanarProgramLayout(const ImageLayout& layout, SampleType sampleType,
                                         std::uint32_t channels) noexcept
{
    return layout.elementSizeBytes() == sampleTypeSizeBytes(sampleType) && layout.samplesPerPixel() == 1u &&
           layout.planeCount() == channels && layout.extent().channelCount() == channels;
}

[[nodiscard]] bool containsRealtimePreviewApproximation(const PointwiseProgram& program) noexcept
{
    return std::ranges::any_of(program.steps(), [](const PointwiseOp& step) {
        return step.kind == PointwiseOpKind::SampledCurve && step.sampling == PointwiseCurveSampling::Lower;
    });
}

[[nodiscard]] bool pointerIsSampleAligned(const void* pointer, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(pointer) % elementBytes == 0u;
}

[[nodiscard]] bool exactlyAliases(PointwiseImageView source, PointwiseMutableImageView destination) noexcept
{
    return source.samples == destination.samples && *source.layout == *destination.layout;
}

// Everything the walk relies on, checked before any sample is written: the execution, the backend,
// the source count, every pointer's presence and alignment, every layout's planar shape and
// representation, a common extent, overlap other than exact aliasing, and the packed mask's
// presence, coverage and separation from the destination.
[[nodiscard]] Status validateInvocation(const ExecutionContext& execution, const PointwiseProgram& program,
                                        std::span<const PointwiseImageView> sources,
                                        PointwiseMutableImageView destination, const KernelValidityView& validity)
{
    if (!execution.valid())
    {
        return pointwiseFailure(ErrorCode::NotInitialized,
                                "the pointwise executor was given an empty execution context");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (sources.size() != program.sourceCount())
    {
        return pointwiseFailure(ErrorCode::InvalidArgument,
                                "the number of bound images does not match the pointwise program's source count");
    }
    if (destination.samples == nullptr || destination.layout == nullptr)
    {
        return pointwiseFailure(ErrorCode::InvalidArgument,
                                "the pointwise destination has a null samples or layout pointer");
    }
    const SampleType sampleType = program.sourceSampleType();
    if (!isSampleType(sampleType) || !isPlanarProgramLayout(*destination.layout, sampleType, program.channelCount()))
    {
        return pointwiseFailure(
            ErrorCode::IncompatibleArguments,
            "the pointwise destination is not a planar image of the program's representation and channel count");
    }
    if (!pointerIsSampleAligned(destination.samples, destination.layout->elementSizeBytes()))
    {
        return pointwiseFailure(ErrorCode::InvalidArgument,
                                "the pointwise destination is not aligned for its resident sample type");
    }

    for (const PointwiseImageView& source : sources)
    {
        if (source.samples == nullptr || source.layout == nullptr)
        {
            return pointwiseFailure(ErrorCode::InvalidArgument,
                                    "a pointwise source has a null samples or layout pointer");
        }
        if (!isPlanarProgramLayout(*source.layout, sampleType, program.channelCount()) ||
            !(source.layout->extent() == destination.layout->extent()))
        {
            return pointwiseFailure(
                ErrorCode::IncompatibleArguments,
                "a pointwise source does not have the destination's planar shape and representation");
        }
        if (!pointerIsSampleAligned(source.samples, source.layout->elementSizeBytes()))
        {
            return pointwiseFailure(ErrorCode::InvalidArgument,
                                    "a pointwise source is not aligned for its resident sample type");
        }
        if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(),
                           static_cast<std::uint8_t*>(destination.samples), destination.layout->totalBytes()) &&
            !exactlyAliases(source, destination))
        {
            return pointwiseFailure(
                ErrorCode::InvalidArgument,
                "a pointwise source and destination overlap without being the same image and layout");
        }
    }

    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return pointwiseFailure(ErrorCode::InvalidArgument,
                                    "a packed mask was supplied for a validity rule that does not use one");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return pointwiseFailure(ErrorCode::InvalidArgument,
                                "the explicit-mask pointwise invocation was given no packed mask");
    }
    if (validity.mask->width() != destination.layout->extent().width() ||
        validity.mask->height() != destination.layout->extent().height() ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return pointwiseFailure(ErrorCode::IncompatibleArguments,
                                "the pointwise validity mask does not cover the destination extent and selected plane");
    }
    if (regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<std::uint8_t*>(destination.samples), destination.layout->totalBytes()))
    {
        return pointwiseFailure(ErrorCode::InvalidArgument,
                                "the pointwise validity mask overlaps the writable destination image");
    }
    return Status::success();
}

[[nodiscard]] bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

template <ResidentSample SampleT>
[[nodiscard]] bool isDirectLowerCurve(const PointwiseProgram& program, const KernelValidityView& validity) noexcept
{
    const std::span<const PointwiseOp> steps = program.steps();
    return validity.descriptor.isAllValid() && validity.mask == nullptr && program.sourceCount() == 1u &&
           steps.size() == 1u && steps.front().kind == PointwiseOpKind::SampledCurve &&
           steps.front().sampling == PointwiseCurveSampling::Lower &&
           steps.front().domainPolicy == PointwiseCurveDomainPolicy::Clamp &&
           ((std::is_same_v<SampleT, float> && program.precision() == ComputePrecision::Float32) ||
            (std::is_same_v<SampleT, double> && program.precision() == ComputePrecision::Float64));
}

// A lower-neighbour preview curve is deliberately as simple as a plain table loop. Sending it
// through the general pointwise interpreter made the real-time path substantially slower than such
// a loop: every sample built erased source arrays, returned a Result and ran the generic conversion
// matrix. The program has already proved all of those decisions, so this narrowly matched executor
// can do the one multiply, clamp and table read directly.
template <ResidentSample SampleT>
[[nodiscard]] Result<PointwiseExecutionResult>
executeDirectLowerCurve(const ExecutionContext& execution, const PointwiseProgram& program,
                        std::span<const PointwiseImageView> sources, PointwiseMutableImageView destination)
{
    const PointwiseOp& step = program.steps().front();
    const std::span<const double> entries = program.table(step.table);
    const std::uint32_t width = destination.layout->extent().width();
    const std::uint32_t height = destination.layout->extent().height();
    const std::uint32_t channels = program.channelCount();
    const SampleT low = static_cast<SampleT>(step.low);
    const SampleT span = static_cast<SampleT>(step.high) - low;
    const SampleT last = static_cast<SampleT>(entries.size() - 1u);

    // The preview-gamma shape is both much more common and much narrower than the general
    // lower-neighbour contract below. Keep its resident table in Float32 and walk a contiguous
    // planar image as one run. Besides avoiding a Float64 gather, this removes a division and
    // two layout-offset lookups from every sample. Values outside the normalized domain still
    // take the documented clamp path, and NaNs still survive.
    if constexpr (std::is_same_v<SampleT, float>)
    {
        constexpr std::size_t kPreviewTableEntries = 1024u;
        const NormalizedLowerCurveFloat32RunFn compiledRun = normalizedLowerCurveFloat32RunFor(execution);
        if (entries.size() == kPreviewTableEntries && step.low == 0.0 && step.high == 1.0 &&
            sources.front().layout->isContiguous() && destination.layout->isContiguous() && compiledRun != nullptr)
        {
            std::array<float, kPreviewTableEntries> residentEntries{};
            std::transform(entries.begin(), entries.end(), residentEntries.begin(),
                           [](double entry) { return static_cast<float>(entry); });

            const std::uint64_t sampleCount = static_cast<std::uint64_t>(width) * height * channels;
            const float* const source = static_cast<const float*>(sources.front().samples);
            float* const output = static_cast<float*>(destination.samples);
            // A destination this large does not stay cached for its next reader anyway, and a streaming
            // store writes a line without first reading it; below it, ordinary stores are the faster.
            constexpr std::uint64_t kStreamingBytes = std::uint64_t{32u} << 20u;
            const bool stream = sampleCount * sizeof(float) >= kStreamingBytes &&
                                static_cast<const void*>(source) != static_cast<const void*>(output);
            const TaskCost previewCost{.bytesPerItem = sizeof(float) * 2u, .operationsPerItem = 2u};
            std::atomic<std::uint64_t> previewNonFinite{0u};
            const Status previewWalked =
                parallelFor(execution, sampleCount, previewCost, [&](TaskRange range) -> Status {
                    const std::uint64_t localNonFinite =
                        compiledRun(source + range.begin, output + range.begin, range.end - range.begin,
                                    residentEntries.data(), static_cast<std::uint32_t>(residentEntries.size()), stream);
                    previewNonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
                    return Status::success();
                });
            if (previewWalked.failed())
            {
                return previewWalked;
            }

            PointwiseExecutionResult result;
            result.run.selection.total = static_cast<std::uint64_t>(width) * height;
            result.run.selection.valid = result.run.selection.total;
            result.run.outputsWritten = sampleCount;
            result.conversion.samplesConverted = sampleCount;
            result.conversion.nonFiniteInputs = previewNonFinite.load(std::memory_order_relaxed);
            return result;
        }
    }

    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 3u};
    std::atomic<std::uint64_t> nonFiniteInputs{0u};

    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localNonFinite = 0u;
        const std::uint8_t* const sourceBase = static_cast<const std::uint8_t*>(sources.front().samples);
        std::uint8_t* const destinationBase = static_cast<std::uint8_t*>(destination.samples);
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const SampleT* const source =
                    reinterpret_cast<const SampleT*>(sourceBase + *sources.front().layout->rowByteOffset(channel, y));
                SampleT* const output =
                    reinterpret_cast<SampleT*>(destinationBase + *destination.layout->rowByteOffset(channel, y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const SampleT input = source[x];
                    localNonFinite += std::isfinite(input) ? 0u : 1u;
                    const SampleT position = (input - low) / span * last;
                    if (std::isnan(position))
                    {
                        output[x] = std::numeric_limits<SampleT>::quiet_NaN();
                        continue;
                    }
                    const SampleT bounded = (std::min)((std::max)(position, SampleT{0}), last);
                    output[x] = static_cast<SampleT>(entries[static_cast<std::size_t>(bounded)]);
                }
            }
        }
        nonFiniteInputs.fetch_add(localNonFinite, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    PointwiseExecutionResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total * channels;
    result.conversion.samplesConverted = result.run.outputsWritten;
    result.conversion.nonFiniteInputs = nonFiniteInputs.load(std::memory_order_relaxed);
    return result;
}

/// Whether an eight- or sixteen-bit image's program is one exact lookup with an entry for every code, over every
/// sample: an integer curve's whole program.
template <ResidentSample SampleT>
[[nodiscard]] bool isDirectLookup(const PointwiseProgram& program, const KernelValidityView& validity) noexcept
{
    const std::span<const PointwiseOp> steps = program.steps();
    return validity.descriptor.isAllValid() && validity.mask == nullptr && program.sourceCount() == 1u &&
           steps.size() == 1u && steps.front().kind == PointwiseOpKind::Lut &&
           program.table(steps.front().table).size() == std::size_t{1u} << (8u * sizeof(SampleT));
}

/// Bytes borrowed from the execution's memory resource for one call, returned however it ends.
class ExecutionTable
{
public:
    ExecutionTable() = default;
    ~ExecutionTable()
    {
        if (m_memory != nullptr)
        {
            m_memory->deallocate(m_data, m_bytes, kDefaultBufferAlignmentBytes);
        }
    }
    ExecutionTable(const ExecutionTable&) = delete;
    ExecutionTable& operator=(const ExecutionTable&) = delete;
    ExecutionTable(ExecutionTable&&) = delete;
    ExecutionTable& operator=(ExecutionTable&&) = delete;

    [[nodiscard]] Status allocate(MemoryResource& memory, std::uint64_t bytes)
    {
        Result<void*> block = memory.allocate(bytes, kDefaultBufferAlignmentBytes);
        if (!block.hasValue())
        {
            return block.status();
        }
        m_memory = &memory;
        m_data = *block;
        m_bytes = bytes;
        return Status::success();
    }

    [[nodiscard]] void* data() const noexcept
    {
        return m_data;
    }

private:
    MemoryResource* m_memory = nullptr;
    void* m_data = nullptr;
    std::uint64_t m_bytes = 0u;
};

// An integer curve's program is one exact lookup with an entry for every code, so every sample is an index the
// table holds and the evaluator's answer for a code is the same wherever it occurs: the entry at the program's
// width, narrowed once to the representation. Those answers are made once, a table of codes, by the compiled
// conversion, which rounds as the evaluator does and counts what it saturated or found not finite, and each sample
// is looked up in it. Only a table where either happened needs the counters the evaluator would have kept - a
// saturated narrowing or a non-finite entry counted at every sample that reads it - so only then is each code's
// answer made again with its flags.
template <ResidentSample SampleT>
[[nodiscard]] Result<PointwiseExecutionResult>
executeDirectLookup(const ExecutionContext& execution, const PointwiseProgram& program,
                    std::span<const PointwiseImageView> sources, PointwiseMutableImageView destination)
{
    const PointwiseOp& step = program.steps().front();
    const std::span<const double> entries = program.table(step.table);
    const std::size_t codes = entries.size();
    MemoryResource* const memory = execution.memoryResource();
    if (memory == nullptr)
    {
        return pointwiseFailure(ErrorCode::NotInitialized, "the execution lends no memory for the lookup's answers");
    }
    ExecutionTable answerStorage;
    if (const Status taken = answerStorage.allocate(*memory, codes * sizeof(SampleT)); taken.failed())
    {
        return taken;
    }
    SampleT* const answers = static_cast<SampleT*>(answerStorage.data());
    const ConversionPolicy outputPolicy{.domain = DomainMapping::CodeValue};
    const bool wide = program.precision() == ComputePrecision::Float64;
    const ConvertRunFn convert =
        convertRunFor(execution, wide ? SampleType::Float64 : SampleType::Float32, kSampleTypeOf<SampleT>);
    if (convert == nullptr)
    {
        return pointwiseFailure(ErrorCode::Internal, "no conversion narrows the lookup's entries to its codes");
    }
    // A Float32 program reads each entry narrowed to Float32 first; a block of them is narrowed on the stack.
    constexpr std::size_t kNarrowedBlock = 1024u;
    std::atomic<std::uint64_t> flaggedEntries{0u};
    const TaskCost answerCost{.bytesPerItem = sizeof(double) + sizeof(SampleT), .operationsPerItem = 2u};
    const Status answered = parallelFor(execution, codes, answerCost, [&](TaskRange range) -> Status {
        ConversionCounters met;
        std::uint64_t refused = 0u;
        if (wide)
        {
            const ConversionRunOutcome outcome = convert(entries.data() + range.begin, answers + range.begin,
                                                         range.end - range.begin, outputPolicy, met);
            refused += outcome.rejected ? 1u : 0u;
        }
        else
        {
            std::array<float, kNarrowedBlock> narrowed{};
            for (std::uint64_t first = range.begin; first < range.end; first += kNarrowedBlock)
            {
                const std::uint64_t count = (std::min)(range.end - first, std::uint64_t{kNarrowedBlock});
                for (std::uint64_t index = 0u; index < count; ++index)
                {
                    narrowed[index] = static_cast<float>(entries[first + index]);
                }
                const ConversionRunOutcome outcome =
                    convert(narrowed.data(), answers + first, count, outputPolicy, met);
                refused += outcome.rejected ? 1u : 0u;
            }
        }
        flaggedEntries.fetch_add(met.saturatedSamples + met.nonFiniteInputs + refused, std::memory_order_relaxed);
        return Status::success();
    });
    if (answered.failed())
    {
        return answered;
    }

    constexpr std::uint8_t kSaturated = 1u;
    constexpr std::uint8_t kNonFinite = 2u;
    const bool flagged = flaggedEntries.load(std::memory_order_relaxed) != 0u;
    ExecutionTable flagStorage;
    std::uint8_t* flags = nullptr;
    if (flagged)
    {
        if (const Status taken = flagStorage.allocate(*memory, codes); taken.failed())
        {
            return taken;
        }
        flags = static_cast<std::uint8_t*>(flagStorage.data());
        const TaskCost flagCost{.bytesPerItem = sizeof(double) + sizeof(SampleT) + 1u, .operationsPerItem = 8u};
        const Status marked = parallelFor(execution, codes, flagCost, [&](TaskRange range) -> Status {
            for (std::uint64_t code = range.begin; code < range.end; ++code)
            {
                const double value = wide ? entries[code] : static_cast<double>(static_cast<float>(entries[code]));
                const ConvertedSample<SampleT> narrowed = convertSample<double, SampleT>(value, outputPolicy);
                answers[code] = narrowed.value;
                flags[code] = static_cast<std::uint8_t>((narrowed.saturated ? kSaturated : 0u) |
                                                        (narrowed.sourceWasNonFinite ? kNonFinite : 0u));
            }
            return Status::success();
        });
        if (marked.failed())
        {
            return marked;
        }
    }

    const std::uint32_t width = destination.layout->extent().width();
    const std::uint32_t height = destination.layout->extent().height();
    const std::uint32_t channels = program.channelCount();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels};
    std::atomic<std::uint64_t> saturatedSamples{0u};
    std::atomic<std::uint64_t> nonFiniteInputs{0u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localSaturated = 0u;
        std::uint64_t localNonFinite = 0u;
        const std::uint8_t* const sourceBase = static_cast<const std::uint8_t*>(sources.front().samples);
        std::uint8_t* const destinationBase = static_cast<std::uint8_t*>(destination.samples);
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const SampleT* const source =
                    reinterpret_cast<const SampleT*>(sourceBase + *sources.front().layout->rowByteOffset(channel, y));
                SampleT* const output =
                    reinterpret_cast<SampleT*>(destinationBase + *destination.layout->rowByteOffset(channel, y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    output[x] = answers[source[x]];
                }
                if (flagged)
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const std::uint8_t flag = flags[source[x]];
                        localSaturated += (flag & kSaturated) != 0u ? 1u : 0u;
                        localNonFinite += (flag & kNonFinite) != 0u ? 1u : 0u;
                    }
                }
            }
        }
        saturatedSamples.fetch_add(localSaturated, std::memory_order_relaxed);
        nonFiniteInputs.fetch_add(localNonFinite, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    PointwiseExecutionResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total * channels;
    result.conversion.samplesConverted = result.run.outputsWritten;
    result.conversion.nonFiniteInputs = nonFiniteInputs.load(std::memory_order_relaxed);
    result.conversion.saturatedSamples = saturatedSamples.load(std::memory_order_relaxed);
    return result;
}

/// Whether a Float32 program is one sampled curve the compiled run reads, over every sample: a floating image's curve.
template <ResidentSample SampleT>
[[nodiscard]] bool isDirectSampledCurve(const PointwiseProgram& program, const KernelValidityView& validity) noexcept
{
    const std::span<const PointwiseOp> steps = program.steps();
    return std::is_same_v<SampleT, float> && program.precision() == ComputePrecision::Float32 &&
           validity.descriptor.isAllValid() && validity.mask == nullptr && program.sourceCount() == 1u &&
           steps.size() == 1u && steps.front().kind == PointwiseOpKind::SampledCurve &&
           (steps.front().sampling == PointwiseCurveSampling::Linear ||
            steps.front().sampling == PointwiseCurveSampling::Nearest) &&
           steps.front().domainPolicy != PointwiseCurveDomainPolicy::Reject;
}

// A floating image's curve is one sampled-curve step. The evaluator narrows each entry it reads to Float32, so the
// table is narrowed once into the execution's memory and the compiled run reads it, a contiguous image as one span
// and any other a row at a time; every sample's bits and the count of outputs that are not finite are the
// evaluator's.
[[nodiscard]] Result<PointwiseExecutionResult> executeDirectSampledCurve(const ExecutionContext& execution,
                                                                         const PointwiseProgram& program,
                                                                         std::span<const PointwiseImageView> sources,
                                                                         PointwiseMutableImageView destination,
                                                                         SampledCurveFloat32RunFn run)
{
    const PointwiseOp& step = program.steps().front();
    const std::span<const double> entries = program.table(step.table);
    MemoryResource* const memory = execution.memoryResource();
    if (memory == nullptr)
    {
        return pointwiseFailure(ErrorCode::NotInitialized, "the execution lends no memory for the curve's table");
    }
    ExecutionTable tableStorage;
    const Status taken = tableStorage.allocate(*memory, entries.size() * sizeof(float));
    if (taken.failed())
    {
        return taken;
    }
    float* const table = static_cast<float*>(tableStorage.data());
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        table[index] = static_cast<float>(entries[index]);
    }
    const std::uint32_t tableEntries = static_cast<std::uint32_t>(entries.size());

    const std::uint32_t width = destination.layout->extent().width();
    const std::uint32_t height = destination.layout->extent().height();
    const std::uint32_t channels = program.channelCount();
    const float* const sourceBase = static_cast<const float*>(sources.front().samples);
    float* const destinationBase = static_cast<float*>(destination.samples);
    std::atomic<std::uint64_t> nonFinite{0u};
    Status walked = Status::success();
    if (sources.front().layout->isContiguous() && destination.layout->isContiguous())
    {
        const std::uint64_t sampleCount = static_cast<std::uint64_t>(width) * height * channels;
        const TaskCost cost{.bytesPerItem = sizeof(float) * 2u, .operationsPerItem = 12u};
        walked = parallelFor(execution, sampleCount, cost, [&](TaskRange range) -> Status {
            nonFinite.fetch_add(run(sourceBase + range.begin, destinationBase + range.begin, range.end - range.begin,
                                    step, table, tableEntries),
                                std::memory_order_relaxed);
            return Status::success();
        });
    }
    else
    {
        const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(float) * 2u,
                            .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 12u};
        walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
            std::uint64_t local = 0u;
            for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(y64);
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const float* const source =
                        reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(sourceBase) +
                                                       *sources.front().layout->rowByteOffset(channel, y));
                    float* const output = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(destinationBase) +
                                                                   *destination.layout->rowByteOffset(channel, y));
                    local += run(source, output, width, step, table, tableEntries);
                }
            }
            nonFinite.fetch_add(local, std::memory_order_relaxed);
            return Status::success();
        });
    }
    if (walked.failed())
    {
        return walked;
    }

    PointwiseExecutionResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total * channels;
    result.conversion.samplesConverted = result.run.outputsWritten;
    result.conversion.nonFiniteInputs = nonFinite.load(std::memory_order_relaxed);
    return result;
}

template <ResidentSample SampleT>
[[nodiscard]] Result<PointwiseExecutionResult>
executeAs(const ExecutionContext& execution, const PointwiseProgram& program,
          std::span<const PointwiseImageView> sources, PointwiseMutableImageView destination,
          const KernelValidityView& validity)
{
    if (execution.backend() != ComputeBackend::Reference && isDirectLowerCurve<SampleT>(program, validity))
    {
        return executeDirectLowerCurve<SampleT>(execution, program, sources, destination);
    }
    if constexpr (std::is_same_v<SampleT, std::uint8_t> || std::is_same_v<SampleT, std::uint16_t>)
    {
        if (execution.backend() != ComputeBackend::Reference && isDirectLookup<SampleT>(program, validity))
        {
            return executeDirectLookup<SampleT>(execution, program, sources, destination);
        }
    }
    if constexpr (std::is_same_v<SampleT, float>)
    {
        if (execution.backend() != ComputeBackend::Reference && isDirectSampledCurve<SampleT>(program, validity))
        {
            if (const SampledCurveFloat32RunFn run = sampledCurveFloat32RunFor(execution); run != nullptr)
            {
                return executeDirectSampledCurve(execution, program, sources, destination, run);
            }
        }
    }

    const std::uint32_t width = destination.layout->extent().width();
    const std::uint32_t height = destination.layout->extent().height();
    const std::uint32_t channels = program.channelCount();
    const std::uint32_t sourceCount = program.sourceCount();
    const PointwiseRunFn pointwiseRun = pointwiseRunFor(execution, program);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) *
                                        static_cast<std::uint64_t>(sourceCount + 1u),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * program.steps().size()};

    std::atomic<std::uint64_t> validPixels{0u};
    std::atomic<std::uint64_t> convertedSamples{0u};
    std::atomic<std::uint64_t> nonFiniteInputs{0u};
    std::atomic<std::uint64_t> saturatedSamples{0u};
    const ConversionPolicy outputPolicy{.domain = DomainMapping::CodeValue};

    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<const SampleT*, kMaxPointwiseSources * kMaxPointwiseChannels> sourceRows{};
        std::array<SampleT*, kMaxPointwiseChannels> destinationRows{};
        std::array<const void*, kMaxPointwiseSources * kMaxPointwiseChannels> erasedSourceRows{};
        std::array<void*, kMaxPointwiseChannels> erasedDestinationRows{};
        std::array<double, kMaxPointwiseSources * kMaxPointwiseChannels> pixelSources{};
        std::array<double, kMaxPointwiseChannels> pixelOutput{};
        ConversionCounters localConversion;
        std::uint64_t localValid = 0u;

        const bool contiguous =
            destination.layout->isContiguous() && std::ranges::all_of(sources, [](const PointwiseImageView& source) {
                return source.layout->isContiguous();
            });
        // Every compiled Float32 table has at most sixteen lanes and every Float64 table at most
        // eight. A whole multiple is required before calling the flattened path: a partial run may
        // have written its vector prefix in place, so falling back over the whole range would apply
        // the program twice to that prefix. A run over integer samples writes its own last partial
        // vector, so it takes any range whole.
        constexpr std::uint64_t kMaximumCpuLanes = std::is_same_v<SampleT, float> ? 16u : 8u;
        constexpr bool kRunWritesWholeRange = !std::is_floating_point_v<SampleT>;
        const std::uint64_t flattenedPixels = (range.end - range.begin) * width;
        if (validity.descriptor.isAllValid() && pointwiseRun != nullptr && contiguous &&
            (kRunWritesWholeRange || flattenedPixels % kMaximumCpuLanes == 0u))
        {
            const std::uint64_t firstPixel = range.begin * width;
            for (std::uint32_t sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex)
            {
                const std::uint8_t* const base = static_cast<const std::uint8_t*>(sources[sourceIndex].samples);
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    sourceRows[sourceIndex * channels + channel] = reinterpret_cast<const SampleT*>(
                        base + *sources[sourceIndex].layout->rowByteOffset(channel, 0u));
                    erasedSourceRows[sourceIndex * channels + channel] =
                        sourceRows[sourceIndex * channels + channel] + firstPixel;
                }
            }
            std::uint8_t* const destinationBase = static_cast<std::uint8_t*>(destination.samples);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                destinationRows[channel] =
                    reinterpret_cast<SampleT*>(destinationBase + *destination.layout->rowByteOffset(channel, 0u));
                erasedDestinationRows[channel] = destinationRows[channel] + firstPixel;
            }
            const PointwiseRunOutcome vectorized =
                pointwiseRun(program, erasedSourceRows.data(), erasedDestinationRows.data(), flattenedPixels);
            if (vectorized.pixelsProcessed == flattenedPixels)
            {
                validPixels.fetch_add(flattenedPixels, std::memory_order_relaxed);
                convertedSamples.fetch_add(flattenedPixels * channels, std::memory_order_relaxed);
                nonFiniteInputs.fetch_add(vectorized.nonFiniteOutputs, std::memory_order_relaxed);
                saturatedSamples.fetch_add(vectorized.saturatedOutputs, std::memory_order_relaxed);
                return Status::success();
            }
        }

        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex)
            {
                const std::uint8_t* const base = static_cast<const std::uint8_t*>(sources[sourceIndex].samples);
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    sourceRows[sourceIndex * channels + channel] = reinterpret_cast<const SampleT*>(
                        base + *sources[sourceIndex].layout->rowByteOffset(channel, y));
                    erasedSourceRows[sourceIndex * channels + channel] = sourceRows[sourceIndex * channels + channel];
                }
            }
            std::uint8_t* const destinationBase = static_cast<std::uint8_t*>(destination.samples);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                destinationRows[channel] =
                    reinterpret_cast<SampleT*>(destinationBase + *destination.layout->rowByteOffset(channel, y));
                erasedDestinationRows[channel] = destinationRows[channel];
            }

            std::uint64_t firstScalarPixel = 0u;
            if (validity.descriptor.isAllValid() && pointwiseRun != nullptr)
            {
                const PointwiseRunOutcome vectorized =
                    pointwiseRun(program, erasedSourceRows.data(), erasedDestinationRows.data(), width);
                firstScalarPixel = vectorized.pixelsProcessed;
                localValid += firstScalarPixel;
                localConversion.samplesConverted += firstScalarPixel * channels;
                localConversion.nonFiniteInputs += vectorized.nonFiniteOutputs;
                localConversion.saturatedSamples += vectorized.saturatedOutputs;
                if (firstScalarPixel == width)
                {
                    continue;
                }
            }

            for (std::uint32_t x = static_cast<std::uint32_t>(firstScalarPixel); x < width; ++x)
            {
                bool accepted = maskAccepts(validity, x, y);
                for (std::uint32_t sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex)
                {
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        const SampleT value = sourceRows[sourceIndex * channels + channel][x];
                        pixelSources[sourceIndex * channels + channel] = static_cast<double>(value);
                        if (sourceIndex == 0u && !sampleIsValidByValue(validity.descriptor, value))
                        {
                            accepted = false;
                        }
                    }
                }
                if (!accepted)
                {
                    continue;
                }

                const Status evaluated = evaluatePointwisePixel(
                    program, std::span<const double>(pixelSources.data(), sourceCount * channels),
                    std::span<double>(pixelOutput.data(), channels));
                if (evaluated.failed())
                {
                    return evaluated;
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const ConvertedSample<SampleT> narrowed =
                        convertSample<double, SampleT>(pixelOutput[channel], outputPolicy);
                    destinationRows[channel][x] = narrowed.value;
                    ++localConversion.samplesConverted;
                    localConversion.nonFiniteInputs += narrowed.sourceWasNonFinite ? 1u : 0u;
                    localConversion.saturatedSamples += narrowed.saturated ? 1u : 0u;
                }
                ++localValid;
            }
        }

        validPixels.fetch_add(localValid, std::memory_order_relaxed);
        convertedSamples.fetch_add(localConversion.samplesConverted, std::memory_order_relaxed);
        nonFiniteInputs.fetch_add(localConversion.nonFiniteInputs, std::memory_order_relaxed);
        saturatedSamples.fetch_add(localConversion.saturatedSamples, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    PointwiseExecutionResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = convertedSamples.load(std::memory_order_relaxed);
    result.conversion.samplesConverted = result.run.outputsWritten;
    result.conversion.nonFiniteInputs = nonFiniteInputs.load(std::memory_order_relaxed);
    result.conversion.saturatedSamples = saturatedSamples.load(std::memory_order_relaxed);
    return result;
}

} // namespace

Result<PointwiseExecutionResult>
detail::executePointwiseProgramImpl(const ExecutionContext& execution, const PointwiseProgram& program,
                                    std::span<const PointwiseImageView> sources, PointwiseMutableImageView destination,
                                    const KernelValidityView& validity, bool permitRealtimePreviewApproximation)
{
    const Status ready = validateInvocation(execution, program, sources, destination, validity);
    if (ready.failed())
    {
        return ready;
    }
    if (containsRealtimePreviewApproximation(program) && !permitRealtimePreviewApproximation)
    {
        return pointwiseFailure(
            ErrorCode::IncompatibleArguments,
            "a lower-neighbour curve is a real-time-preview approximation and cannot run through the production "
            "pointwise executor");
    }

    switch (program.sourceSampleType())
    {
    case SampleType::UInt8:
        return executeAs<std::uint8_t>(execution, program, sources, destination, validity);
    case SampleType::Int8:
        return executeAs<std::int8_t>(execution, program, sources, destination, validity);
    case SampleType::UInt16:
        return executeAs<std::uint16_t>(execution, program, sources, destination, validity);
    case SampleType::Int16:
        return executeAs<std::int16_t>(execution, program, sources, destination, validity);
    case SampleType::UInt32:
        return executeAs<std::uint32_t>(execution, program, sources, destination, validity);
    case SampleType::Int32:
        return executeAs<std::int32_t>(execution, program, sources, destination, validity);
    case SampleType::Float32:
        return executeAs<float>(execution, program, sources, destination, validity);
    case SampleType::Float64:
        return executeAs<double>(execution, program, sources, destination, validity);
    }
    return pointwiseFailure(ErrorCode::InvalidArgument, "the pointwise program names no resident sample type");
}

Result<PointwiseExecutionResult> executePointwiseProgram(const ExecutionContext& execution,
                                                         const PointwiseProgram& program,
                                                         std::span<const PointwiseImageView> sources,
                                                         PointwiseMutableImageView destination,
                                                         const KernelValidityView& validity)
{
    return detail::executePointwiseProgramImpl(execution, program, sources, destination, validity, false);
}

// What running one chain does to the image it writes.
//
// Read off the immutable program rather than tracked while it runs, because every fact this needs
// was decided when the chain was built: a step vocabulary that cannot express a neighbourhood read
// is exactly what makes the answer a switch over the steps instead of an analysis.
OperationEffect pointwiseProgramEffect(const PointwiseProgram& program) noexcept
{
    bool changesBasis = false;
    bool changesTransfer = false;
    for (const PointwiseOp& step : program.steps())
    {
        if (step.kind == PointwiseOpKind::ChannelMatrix)
        {
            changesBasis = true;
        }
        else if (step.kind == PointwiseOpKind::Gamma || step.kind == PointwiseOpKind::Lut ||
                 step.kind == PointwiseOpKind::SampledCurve)
        {
            changesTransfer = true;
        }
    }

    const bool restatesDescription = changesBasis || program.inputDomain() != program.outputDomain();

    OperationEffect effect{};
    effect.samples = SampleEffect::Writes;
    effect.description = restatesDescription ? DescriptionEffect::Restates : DescriptionEffect::Preserves;
    effect.semantics = changesTransfer ? SemanticEffect::Restates : SemanticEffect::Preserves;
    effect.solution = SolutionEffect::Preserves;
    effect.codecMetadata = restatesDescription ? CodecMetadataEffect::Drops : CodecMetadataEffect::Preserves;
    effect.outputIntent = restatesDescription ? OutputIntentEffect::Withdraws : OutputIntentEffect::Preserves;
    effect.validity.policy = program.sourceCount() > 1u ? ValidityPolicy::Combine : ValidityPolicy::Preserve;
    return effect;
}
} // namespace an
