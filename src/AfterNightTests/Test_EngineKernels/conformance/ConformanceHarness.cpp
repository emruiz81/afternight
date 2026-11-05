// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ConformanceHarness.h"

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace conformance
{
namespace
{

/// The widths a case may have.
///
/// Every one of them is either a whole number of the widest vector this engine compiles for, one
/// short of one, or one past one. That is where a kernel tail handling is either right or wrong,
/// and a sweep over round numbers alone would never reach it.
constexpr std::array<std::uint32_t, 14u> kWidths{1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 33u, 64u, 65u, 127u};

/// The heights a case may have.
constexpr std::array<std::uint32_t, 6u> kHeights{1u, 2u, 3u, 5u, 8u, 17u};

/// The row padding a case may ask for, in samples.
constexpr std::array<std::uint32_t, 4u> kPaddings{0u, 1u, 3u, 8u};

/// The thread counts a case may ask for.
constexpr std::array<std::uint32_t, 4u> kThreads{1u, 2u, 3u, 4u};

/// The representations a case may read.
constexpr std::array<an::SampleType, 8u> kSampleTypes{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

/// The representations a byte swap may be asked for, which excludes the one-byte pair.
constexpr std::array<an::SampleType, 6u> kSwappableTypes{an::SampleType::UInt16,  an::SampleType::Int16,
                                                         an::SampleType::UInt32,  an::SampleType::Int32,
                                                         an::SampleType::Float32, an::SampleType::Float64};

/// The validity rules a case may apply.
constexpr std::array<ValidityChoice, 5u> kValidityChoices{ValidityChoice::All, ValidityChoice::FiniteOnly,
                                                          ValidityChoice::ZeroIsNoData, ValidityChoice::Mask,
                                                          ValidityChoice::Range};

/// The engine limits every generated geometry is checked against.
[[nodiscard]] const an::EngineLimits& limits() noexcept
{
    static const an::EngineLimits value = an::defaultEngineLimits();
    return value;
}

/// One execution context forced onto one target.
///
/// @param engine the engine context.
/// @param target the implementation to force.
/// @param threads how many threads the operation may use.
/// @return the execution context, or the refusal.
[[nodiscard]] an::Result<an::ExecutionContext> executionFor(const an::EngineContext& engine, const Target& target,
                                                            std::uint32_t threads)
{
    an::ExecutionOptions options;
    options.maxConcurrency = threads;
    options.backend = target.backend;
    options.instructionSet = target.instructionSet;
    // The whole harness depends on this: a comparison against the reference is worth nothing if the
    // engine quietly ran the same code twice.
    options.backendFallback = an::BackendFallback::Forbidden;
    return an::ExecutionContext::create(engine, options);
}

/// A layout over a buffer whose rows carry a stated number of padding samples.
///
/// @param type the representation.
/// @param width pixels per row.
/// @param height rows.
/// @param planes how many planes.
/// @param samplesPerPixel how many of a pixel samples share a plane.
/// @param paddingSamples padding after each row.
/// @return the layout, or nothing when the geometry is not describable.
[[nodiscard]] std::optional<an::ImageLayout> paddedLayout(an::SampleType type, std::uint32_t width,
                                                          std::uint32_t height, std::uint32_t planes,
                                                          std::uint32_t samplesPerPixel, std::uint32_t paddingSamples)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, planes * samplesPerPixel, limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::uint32_t elementSize = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementSize,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planes,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        (static_cast<std::uint64_t>(width) * samplesPerPixel + paddingSamples) * elementSize;
    const std::uint64_t planeStride = rowStride * height;
    return an::ImageLayout::createWithStrides(request, rowStride, planeStride, limits());
}

/// Fills a buffer with bytes nothing else would produce.
///
/// @param random the sequence.
/// @param bytes where they go.
void fillRandomBytes(Random& random, std::span<std::byte> bytes)
{
    for (std::byte& byte : bytes)
    {
        byte = static_cast<std::byte>(random.below(256u));
    }
}

/// Fills a run of Float32 samples with ordinary values and, occasionally, an exceptional one.
///
/// @param random the sequence.
/// @param samples where they go.
/// @param specials whether an exceptional value may appear.
void fillFloatSamples(Random& random, std::span<float> samples, bool specials)
{
    for (float& sample : samples)
    {
        if (specials && random.oneIn(23u))
        {
            switch (random.below(6u))
            {
            case 0u:
                sample = (std::numeric_limits<float>::quiet_NaN)();
                break;
            case 1u:
                sample = (std::numeric_limits<float>::infinity)();
                break;
            case 2u:
                sample = -(std::numeric_limits<float>::infinity)();
                break;
            case 3u:
                sample = 0.0f;
                break;
            case 4u:
                sample = -0.0f;
                break;
            default:
                sample = (std::numeric_limits<float>::denorm_min)();
                break;
            }
            continue;
        }
        sample = static_cast<float>(random.unit() * 2.0 - 0.5);
    }
}

/// The bytes of a padded plane, as a span of Float32 samples over the whole buffer.
///
/// @param bytes the buffer.
/// @return the samples, including the padding, which the generator also fills so that a kernel that
///         wrote into padding is visible in the comparison.
[[nodiscard]] std::span<float> floatsOf(std::span<std::byte> bytes)
{
    return std::span<float>{reinterpret_cast<float*>(bytes.data()), bytes.size() / sizeof(float)};
}

/// The read-only spelling of `floatsOf`.
[[nodiscard]] std::span<const float> floatsOf(std::span<const std::byte> bytes)
{
    return std::span<const float>{reinterpret_cast<const float*>(bytes.data()), bytes.size() / sizeof(float)};
}

/// The validity inputs one case asks for, and the memory they need.
struct Validity
{
    /// The rule, already built.
    an::ValidityDescriptor descriptor{};

    /// The packed mask bits, when the case asked for a mask.
    std::vector<std::uint8_t> bits;

    /// The mask view over `bits`, when there is one.
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;

    /// The view handed to a kernel.
    [[nodiscard]] an::KernelValidityView view() const noexcept
    {
        return an::KernelValidityView{.descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr};
    }
};

/// Builds the validity inputs one case asks for.
///
/// @param generated the case.
/// @param planes how many bit-planes a mask would need.
/// @return the rule and its memory, which must outlive the view it hands out.
[[nodiscard]] std::unique_ptr<Validity> validityFor(const Case& generated, std::uint32_t planes)
{
    auto validity = std::make_unique<Validity>();
    an::ValidityDescriptorRequest request;
    switch (generated.validity)
    {
    case ValidityChoice::All:
        request.rule = an::ValidityRule::AllValid;
        break;
    case ValidityChoice::FiniteOnly:
        request.rule = an::ValidityRule::FiniteOnly;
        break;
    case ValidityChoice::ZeroIsNoData:
        request.rule = an::ValidityRule::ZeroIsNoData;
        break;
    case ValidityChoice::Mask:
        request.rule = an::ValidityRule::ExplicitMask;
        break;
    case ValidityChoice::Range:
        request.rule = an::ValidityRule::DomainRange;
        request.lowestValid = 0.0;
        request.highestValid = 1.0;
        break;
    }
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
    if (!descriptor.has_value())
    {
        return nullptr;
    }
    validity->descriptor = *descriptor;
    if (generated.validity != ValidityChoice::Mask)
    {
        return validity;
    }

    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(generated.width, generated.height, planes, limits());
    if (!extent.has_value())
    {
        return nullptr;
    }
    const std::uint64_t rowBytes = an::packedMaskRowBytes(generated.width);
    validity->bits.assign(static_cast<std::size_t>(rowBytes) * generated.height * planes, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{validity->bits}, *extent);
    if (!writable.has_value())
    {
        return nullptr;
    }
    writable->fill(true);
    // A mask that rejects nothing is the same case as no mask at all, so a deterministic scatter of
    // rejections is written into it: every seventh pixel of every plane, plus the last one, which is
    // the one a tail is most likely to get wrong.
    Random masking(generated.seed ^ (std::uint64_t{generated.index} << 32u) ^ 0x9E3779B97F4A7C15ull);
    for (std::uint32_t plane = 0u; plane < planes; ++plane)
    {
        for (std::uint32_t y = 0u; y < generated.height; ++y)
        {
            for (std::uint32_t x = 0u; x < generated.width; ++x)
            {
                if (masking.oneIn(7u))
                {
                    static_cast<void>(writable->setValid(plane, x, y, false));
                }
            }
        }
        static_cast<void>(writable->setValid(plane, generated.width - 1u, generated.height - 1u, false));
    }
    validity->mask = writable->constView();
    return validity;
}

/// Records what a run dispatched to and what it reported.
///
/// @param execution the operation.
/// @param status what it reported.
/// @param outcome where the answer goes.
void recordStatus(const an::ExecutionContext& execution, const an::Status& status, Outcome& outcome)
{
    outcome.dispatched = an::instructionSetOfDispatchedCode(execution);
    outcome.ran = status.ok();
    outcome.code = status.code();
}

/// The outcome of a case the harness could not set up, which is its own failure rather than a
/// divergence.
///
/// @param reason what could not be built.
/// @return the outcome.
[[nodiscard]] Outcome setupFailure(const char* reason)
{
    Outcome outcome;
    outcome.setupFailure = reason;
    return outcome;
}

// --------------------------------------------------------------------------------------- subjects

/// Copies one image into another whose rows are padded differently.
[[nodiscard]] Outcome runCopy(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> source = paddedLayout(generated.sampleType, generated.width, generated.height,
                                                               generated.channels, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination = paddedLayout(
        generated.sampleType, generated.width, generated.height, generated.channels, 1u, generated.destinationPadding);
    if (!source.has_value() || !destination.has_value())
    {
        return setupFailure("the copy geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index);
    std::vector<std::byte> input(static_cast<std::size_t>(source->totalBytes()));
    fillRandomBytes(random, input);
    // The destination starts as a pattern nothing writes, so padding that a kernel touched shows up
    // in the comparison as a difference rather than as memory nobody looked at.
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});

    Outcome outcome;
    const an::Status status =
        an::copyImageSamples(*execution, generated.sampleType, input.data(), *source, output.data(), *destination);
    recordStatus(*execution, status, outcome);
    outcome.bytes = std::move(output);
    return outcome;
}

/// Writes one sample pattern over every sample of a padded image.
[[nodiscard]] Outcome runFill(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout = paddedLayout(generated.sampleType, generated.width, generated.height,
                                                               generated.channels, 1u, generated.destinationPadding);
    if (!layout.has_value())
    {
        return setupFailure("the fill geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    std::array<std::byte, 8u> value{};
    Random random(generated.seed ^ generated.index);
    fillRandomBytes(random, std::span<std::byte>{value.data(), an::sampleTypeSizeBytes(generated.sampleType)});
    std::vector<std::byte> output(static_cast<std::size_t>(layout->totalBytes()), std::byte{0xA5});

    Outcome outcome;
    const an::Status status =
        an::fillImageSamples(*execution, generated.sampleType, value.data(), output.data(), *layout);
    recordStatus(*execution, status, outcome);
    outcome.bytes = std::move(output);
    return outcome;
}

/// Converts one representation into another, counting what it met.
[[nodiscard]] Outcome runConvert(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> source = paddedLayout(generated.sampleType, generated.width, generated.height,
                                                               generated.channels, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination =
        paddedLayout(generated.destinationSampleType, generated.width, generated.height, generated.channels, 1u,
                     generated.destinationPadding);
    if (!source.has_value() || !destination.has_value())
    {
        return setupFailure("the conversion geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index);
    std::vector<std::byte> input(static_cast<std::size_t>(source->totalBytes()));
    fillRandomBytes(random, input);
    if (generated.sampleType == an::SampleType::Float32)
    {
        fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), true);
    }
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});

    Outcome outcome;
    an::ConversionCounters counters;
    const an::Status status = an::convertImageSamples(*execution, generated.sampleType, input.data(), *source,
                                                      generated.destinationSampleType, output.data(), *destination,
                                                      an::ConversionPolicy{}, counters);
    recordStatus(*execution, status, outcome);
    outcome.bytes = std::move(output);
    outcome.values = {static_cast<double>(counters.samplesConverted), static_cast<double>(counters.nonFiniteInputs),
                      static_cast<double>(counters.saturatedSamples)};
    outcome.valueTolerances = {0.0, 0.0, 0.0};
    return outcome;
}

/// Moves an image from a plane per channel to channels interleaved.
[[nodiscard]] Outcome runRepack(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::uint32_t channels = (std::max)(generated.channels, 2u);
    const std::optional<an::ImageLayout> source =
        paddedLayout(generated.sampleType, generated.width, generated.height, channels, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination = paddedLayout(
        generated.sampleType, generated.width, generated.height, 1u, channels, generated.destinationPadding);
    if (!source.has_value() || !destination.has_value())
    {
        return setupFailure("the repack geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index);
    std::vector<std::byte> input(static_cast<std::size_t>(source->totalBytes()));
    fillRandomBytes(random, input);
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});

    Outcome outcome;
    const an::Status status =
        an::repackImageSamples(*execution, generated.sampleType, input.data(), *source, output.data(), *destination);
    recordStatus(*execution, status, outcome);
    outcome.bytes = std::move(output);
    return outcome;
}

/// Reverses the bytes of every sample, in place.
[[nodiscard]] Outcome runByteSwap(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout = paddedLayout(generated.sampleType, generated.width, generated.height,
                                                               generated.channels, 1u, generated.sourcePadding);
    if (!layout.has_value())
    {
        return setupFailure("the byte-swap geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index);
    std::vector<std::byte> samples(static_cast<std::size_t>(layout->totalBytes()));
    fillRandomBytes(random, samples);

    Outcome outcome;
    const an::Status status = an::swapImageByteOrder(*execution, generated.sampleType, samples.data(), *layout);
    recordStatus(*execution, status, outcome);
    outcome.bytes = std::move(samples);
    return outcome;
}

/// Executes a generated fused pointwise chain over one to three Float32 channels.
[[nodiscard]] Outcome runPointwise(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout = paddedLayout(
        an::SampleType::Float32, generated.width, generated.height, generated.channels, 1u, generated.sourcePadding);
    if (!layout.has_value())
    {
        return setupFailure("the pointwise geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index ^ 0x5851F42D4C957F2Dull);
    an::PointwiseProgramBuilder builder;
    builder.channels(generated.channels).sources(1u).readingSamples(an::SampleType::Float32);
    if ((generated.variant & 1u) != 0u)
    {
        // The contraction contract: a scale followed by a bias may round once instead of twice, and
        // the scalar reference writes its fused multiply-add at the same point the vector body does.
        builder.allowingContraction();
    }
    const std::uint32_t steps = 1u + random.below(4u);
    for (std::uint32_t step = 0u; step < steps; ++step)
    {
        switch (random.below(3u))
        {
        case 0u:
            builder.append(an::pointwiseScale(random.unit() * 3.0 - 1.0));
            break;
        case 1u:
            builder.append(an::pointwiseBias(random.unit() - 0.5));
            break;
        default:
            builder.append(an::pointwiseClamp(-0.25, 1.25));
            break;
        }
    }
    an::Result<an::PointwiseProgram> program = builder.build();
    if (!program.hasValue())
    {
        return setupFailure("the generated pointwise program was refused");
    }

    std::vector<std::byte> input(static_cast<std::size_t>(layout->totalBytes()));
    fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), true);
    std::vector<std::byte> output(static_cast<std::size_t>(layout->totalBytes()), std::byte{0xA5});
    const std::unique_ptr<Validity> validity = validityFor(generated, generated.channels);
    if (validity == nullptr)
    {
        return setupFailure("the validity rule is not describable");
    }

    void* const destinationBytes = generated.inPlace ? input.data() : output.data();
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = input.data(), .layout = &*layout}};
    Outcome outcome;
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        *execution, *program, sources, an::PointwiseMutableImageView{.samples = destinationBytes, .layout = &*layout},
        validity->view());
    recordStatus(*execution, ran.status(), outcome);
    outcome.bytes = generated.inPlace ? std::move(input) : std::move(output);
    if (ran.hasValue())
    {
        outcome.values = {static_cast<double>(ran->run.selection.valid), static_cast<double>(ran->run.outputsWritten),
                          static_cast<double>(ran->conversion.samplesConverted)};
        outcome.valueTolerances = {0.0, 0.0, 0.0};
    }
    return outcome;
}

/// Reduces one stored component to the stable statistics and their forward error bounds.
[[nodiscard]] Outcome runReduction(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout =
        paddedLayout(generated.sampleType, generated.width, generated.height, 1u, 1u, generated.sourcePadding);
    if (!layout.has_value())
    {
        return setupFailure("the reduction geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index ^ 0xD1B54A32D192ED03ull);
    std::vector<std::byte> input(static_cast<std::size_t>(layout->totalBytes()));
    fillRandomBytes(random, input);
    if (generated.sampleType == an::SampleType::Float32)
    {
        // A selected non-finite sample is refused by this family rather than reduced, so it is
        // generated deliberately: both implementations must refuse the same input with the same
        // code, which is a agreement the harness checks and a tolerance could not.
        fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), (generated.variant & 2u) != 0u);
    }
    const std::unique_ptr<Validity> validity = validityFor(generated, 1u);
    if (validity == nullptr)
    {
        return setupFailure("the validity rule is not describable");
    }
    const an::KernelPlaneSource source{.sampleType = generated.sampleType, .samples = input.data(), .layout = &*layout};

    Outcome outcome;
    const an::Result<an::ReductionStatistics> ran = an::reducePlaneStatistics(*execution, source, validity->view());
    recordStatus(*execution, ran.status(), outcome);
    if (ran.hasValue())
    {
        outcome.values = {static_cast<double>(ran->run.selection.valid),
                          static_cast<double>(ran->run.selection.total),
                          ran->minimum,
                          ran->maximum,
                          ran->infinityNorm,
                          ran->sum,
                          ran->mean,
                          ran->sumSquaredDeviations,
                          ran->l1Norm,
                          ran->l2Norm};
        const double count = static_cast<double>((std::max)(ran->run.selection.valid, std::uint64_t{1}));
        outcome.valueTolerances = {0.0,
                                   0.0,
                                   0.0,
                                   0.0,
                                   0.0,
                                   ran->sumError.absoluteError,
                                   ran->sumError.absoluteError / count,
                                   ran->centeredMomentError.absoluteError,
                                   ran->l1Error.absoluteError,
                                   ran->squaredL2Error.absoluteError};
    }
    return outcome;
}

/// Convolves a Float32 plane with a generated separable kernel.
[[nodiscard]] Outcome runStencil(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 1u, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 1u, 1u, generated.destinationPadding);
    if (!layout.has_value() || !destination.has_value())
    {
        return setupFailure("the stencil geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index ^ 0x2545F4914F6CDD1Dull);
    std::vector<std::byte> input(static_cast<std::size_t>(layout->totalBytes()));
    fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), false);
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});

    const std::uint32_t horizontalRadius = static_cast<std::uint32_t>(generated.variant % 4u);
    const std::uint32_t verticalRadius = static_cast<std::uint32_t>((generated.variant / 4u) % 3u);
    std::vector<double> horizontal(2u * horizontalRadius + 1u, 0.0);
    std::vector<double> vertical(2u * verticalRadius + 1u, 0.0);
    for (double& tap : horizontal)
    {
        tap = random.unit();
    }
    for (double& tap : vertical)
    {
        tap = random.unit();
    }
    constexpr std::array<an::KernelBorderMode, 5u> kBorders{
        an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
        an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap};
    const an::StencilBorderPolicy borders{.border = kBorders[(generated.variant / 16u) % kBorders.size()],
                                          .constantBorder = 0.125f,
                                          .invalidNeighbor = -0.5f};
    const std::unique_ptr<Validity> validity = validityFor(generated, 1u);
    if (validity == nullptr)
    {
        return setupFailure("the validity rule is not describable");
    }

    const std::span<const float> sourceSamples = floatsOf(std::span<const std::byte>{input});
    const std::span<float> destinationSamples = floatsOf(std::span<std::byte>{output});
    const std::optional<an::PlaneView<const float>> sourcePlane = an::PlaneView<const float>::create(
        sourceSamples, generated.width, generated.height, 1u, layout->rowStrideBytes());
    const std::optional<an::PlaneView<float>> destinationPlane = an::PlaneView<float>::create(
        destinationSamples, generated.width, generated.height, 1u, destination->rowStrideBytes());
    if (!sourcePlane.has_value() || !destinationPlane.has_value())
    {
        return setupFailure("the stencil planes are not describable");
    }

    Outcome outcome;
    const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
        *execution, *sourcePlane, *destinationPlane,
        an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical, .borders = borders},
        validity->view());
    recordStatus(*execution, ran.status(), outcome);
    outcome.bytes = std::move(output);
    if (ran.hasValue())
    {
        outcome.values = {static_cast<double>(ran->run.selection.valid), static_cast<double>(ran->run.outputsWritten)};
        outcome.valueTolerances = {0.0, 0.0};
    }
    return outcome;
}

/// Runs the bilateral filter over a Float32 plane, whose interior shares each pair's range weight.
[[nodiscard]] Outcome runBilateral(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 1u, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 1u, 1u, generated.destinationPadding);
    if (!layout.has_value() || !destination.has_value())
    {
        return setupFailure("the bilateral geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index ^ 0x6A09E667F3BCC909ull);
    std::vector<std::byte> input(static_cast<std::size_t>(layout->totalBytes()));
    fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), false);
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});

    constexpr std::array<double, 4u> kSpatialSigmas{0.8, 1.5, 2.0, 3.0};
    constexpr std::array<double, 3u> kRangeSigmas{0.05, 0.2, 1.0};
    constexpr std::array<an::KernelBorderMode, 5u> kBorders{
        an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
        an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap};
    an::BilateralRequest request{};
    request.spatialSigma = kSpatialSigmas[generated.variant % kSpatialSigmas.size()];
    request.rangeSigma = kRangeSigmas[(generated.variant / 4u) % kRangeSigmas.size()];
    request.borders = an::StencilBorderPolicy{.border = kBorders[(generated.variant / 12u) % kBorders.size()],
                                              .constantBorder = 0.125f,
                                              .invalidNeighbor = -0.5f};
    const an::Result<an::BilateralPlan> plan = an::planBilateralFilter(request);
    if (!plan.hasValue())
    {
        return setupFailure("the bilateral plan was refused");
    }
    const std::unique_ptr<Validity> validity = validityFor(generated, 1u);
    if (validity == nullptr)
    {
        return setupFailure("the validity rule is not describable");
    }
    const an::SpatialSelection selection{.validity = validity->descriptor,
                                         .mask = validity->mask.has_value() ? &*validity->mask : nullptr};

    Outcome outcome;
    const an::Result<an::NoiseReductionResult> ran = an::filterImageBilateral(
        *execution, an::SampleType::Float32, input.data(), *layout, output.data(), *destination, *plan, selection);
    recordStatus(*execution, ran.status(), outcome);
    outcome.bytes = std::move(output);
    if (ran.hasValue())
    {
        outcome.values = {static_cast<double>(ran->filter.run.selection.valid),
                          static_cast<double>(ran->filter.run.outputsWritten)};
        outcome.valueTolerances = {0.0, 0.0};
    }
    return outcome;
}

/// Holds green to one of the two neutral ceilings over a three-plane Float32 image.
[[nodiscard]] Outcome runChromatic(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 3u, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 3u, 1u, generated.destinationPadding);
    if (!layout.has_value() || !destination.has_value())
    {
        return setupFailure("the chromatic geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index ^ 0xBB67AE8584CAA73Bull);
    std::vector<std::byte> input(static_cast<std::size_t>(layout->totalBytes()));
    fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), (generated.variant & 8u) != 0u);
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});

    constexpr std::array<an::ChromaticNoiseMethod, 2u> kMethods{an::ChromaticNoiseMethod::AverageNeutral,
                                                                an::ChromaticNoiseMethod::MaximumNeutral};
    constexpr std::array<double, 2u> kWhiteLevels{1.0, 65535.0};
    const an::ChromaticNoiseRequest request{.method = kMethods[generated.variant % kMethods.size()],
                                            .amount = 1.0,
                                            .whiteLevel = kWhiteLevels[(generated.variant / 2u) % kWhiteLevels.size()]};
    const an::Result<an::ChromaticNoisePlan> plan = an::planChromaticNoiseReduction(request);
    if (!plan.hasValue())
    {
        return setupFailure("the chromatic plan was refused");
    }
    const std::unique_ptr<Validity> validity = validityFor(generated, 1u);
    if (validity == nullptr)
    {
        return setupFailure("the validity rule is not describable");
    }
    const an::SpatialSelection selection{.validity = validity->descriptor,
                                         .mask = validity->mask.has_value() ? &*validity->mask : nullptr};

    Outcome outcome;
    const an::Result<an::NoiseReductionResult> ran = an::reduceImageChromaticNoise(
        *execution, an::SampleType::Float32, input.data(), *layout, output.data(), *destination, *plan, selection);
    recordStatus(*execution, ran.status(), outcome);
    outcome.bytes = std::move(output);
    if (ran.hasValue())
    {
        outcome.values = {static_cast<double>(ran->filter.run.selection.valid),
                          static_cast<double>(ran->filter.run.outputsWritten),
                          static_cast<double>(ran->samplesChanged)};
        outcome.valueTolerances = {0.0, 0.0, 0.0};
    }
    return outcome;
}

/// Applies one transcendental to a Float32 plane.
[[nodiscard]] Outcome runMath(const an::EngineContext& engine, const Target& target, const Case& generated)
{
    const std::optional<an::ImageLayout> layout =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 1u, 1u, generated.sourcePadding);
    const std::optional<an::ImageLayout> destination =
        paddedLayout(an::SampleType::Float32, generated.width, generated.height, 1u, 1u, generated.destinationPadding);
    if (!layout.has_value() || !destination.has_value())
    {
        return setupFailure("the transcendental geometry is not describable");
    }
    an::Result<an::ExecutionContext> execution = executionFor(engine, target, generated.threads);
    if (!execution.hasValue())
    {
        return setupFailure("the execution context was refused");
    }

    Random random(generated.seed ^ generated.index ^ 0x14057B7EF767814Full);
    std::vector<std::byte> input(static_cast<std::size_t>(layout->totalBytes()));
    fillFloatSamples(random, floatsOf(std::span<std::byte>{input}), true);
    std::vector<std::byte> output(static_cast<std::size_t>(destination->totalBytes()), std::byte{0xA5});
    const std::unique_ptr<Validity> validity = validityFor(generated, 1u);
    if (validity == nullptr)
    {
        return setupFailure("the validity rule is not describable");
    }

    const std::span<const float> sourceSamples = floatsOf(std::span<const std::byte>{input});
    const std::optional<an::PlaneView<const float>> sourcePlane = an::PlaneView<const float>::create(
        sourceSamples, generated.width, generated.height, 1u, layout->rowStrideBytes());
    const bool inPlace = generated.inPlace;
    const std::span<float> destinationSamples =
        inPlace ? floatsOf(std::span<std::byte>{input}) : floatsOf(std::span<std::byte>{output});
    const std::optional<an::PlaneView<float>> destinationPlane =
        an::PlaneView<float>::create(destinationSamples, generated.width, generated.height, 1u,
                                     inPlace ? layout->rowStrideBytes() : destination->rowStrideBytes());
    if (!sourcePlane.has_value() || !destinationPlane.has_value())
    {
        return setupFailure("the transcendental planes are not describable");
    }

    constexpr std::array<an::MathFunction, 3u> kFunctions{an::MathFunction::Exp, an::MathFunction::Log,
                                                          an::MathFunction::Pow};
    const an::MathKernelParameters parameters{.function = kFunctions[generated.variant % kFunctions.size()],
                                              .exponent = 1.0 / 2.2};

    Outcome outcome;
    outcome.function = parameters.function;
    outcome.exponent = parameters.exponent;
    // The arguments are kept because the bound is derived from them: a transcendental accuracy
    // contract is a function of what went in, and the result alone cannot answer it. They are kept
    // in the destination positions rather than their own, because the two buffers are padded
    // differently and a comparison that read the source at a destination index would ask for the
    // bound of a sample that produced nothing.
    const std::uint64_t destinationStrideSamples =
        (inPlace ? layout->rowStrideBytes() : destination->rowStrideBytes()) / sizeof(float);
    outcome.sourceSamples.assign(destinationSamples.size(), 1.0f);
    for (std::uint32_t y = 0u; y < generated.height; ++y)
    {
        for (std::uint32_t x = 0u; x < generated.width; ++x)
        {
            const std::size_t sourceIndex =
                static_cast<std::size_t>(y) * (layout->rowStrideBytes() / sizeof(float)) + x;
            const std::size_t destinationIndex = static_cast<std::size_t>(y) * destinationStrideSamples + x;
            if (sourceIndex < sourceSamples.size() && destinationIndex < outcome.sourceSamples.size())
            {
                outcome.sourceSamples[destinationIndex] = sourceSamples[sourceIndex];
            }
        }
    }
    const an::Result<an::MathExecutionResult> ran =
        an::applyMathFunctionPlane(*execution, *sourcePlane, *destinationPlane, parameters, validity->view());
    recordStatus(*execution, ran.status(), outcome);
    outcome.bytes = inPlace ? std::move(input) : std::move(output);
    if (ran.hasValue())
    {
        outcome.values = {static_cast<double>(ran->run.selection.valid), static_cast<double>(ran->run.outputsWritten),
                          static_cast<double>(ran->nonFiniteOutputs)};
        outcome.valueTolerances = {0.0, 0.0, 0.0};
    }
    return outcome;
}

// ---------------------------------------------------------------------------------- specializers

void specializeAnyType(Random& random, Case& generated)
{
    generated.sampleType = random.pick(std::span<const an::SampleType>{kSampleTypes});
    generated.channels = 1u + random.below(3u);
}

void specializeConversion(Random& random, Case& generated)
{
    generated.sampleType = random.pick(std::span<const an::SampleType>{kSampleTypes});
    generated.destinationSampleType = random.pick(std::span<const an::SampleType>{kSampleTypes});
    generated.channels = 1u + random.below(3u);
}

void specializeRepack(Random& random, Case& generated)
{
    generated.sampleType = random.pick(std::span<const an::SampleType>{kSampleTypes});
    generated.channels = 2u + random.below(3u);
}

void specializeByteSwap(Random& random, Case& generated)
{
    generated.sampleType = random.pick(std::span<const an::SampleType>{kSwappableTypes});
    generated.channels = 1u + random.below(2u);
}

void specializeFloatPlane(Random& random, Case& generated)
{
    generated.sampleType = an::SampleType::Float32;
    generated.channels = 1u;
    static_cast<void>(random);
}

void specializePointwise(Random& random, Case& generated)
{
    generated.sampleType = an::SampleType::Float32;
    generated.channels = 1u + random.below(3u);
    generated.destinationPadding = generated.sourcePadding;
}

void specializeReduction(Random& random, Case& generated)
{
    generated.sampleType = random.pick(std::span<const an::SampleType>{kSampleTypes});
    generated.channels = 1u;
}

/// Every subject, with the numeric contract each one is compared under.
constexpr std::array<Subject, 11u> kSubjects{
    Subject{.name = "transfer.copy",
            .run = &runCopy,
            .specialize = &specializeAnyType,
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = false},
    Subject{.name = "transfer.fill",
            .run = &runFill,
            .specialize = &specializeAnyType,
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = false},
    Subject{.name = "transfer.repack",
            .run = &runRepack,
            .specialize = &specializeRepack,
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = false},
    Subject{.name = "transfer.byteswap",
            .run = &runByteSwap,
            .specialize = &specializeByteSwap,
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = false},
    Subject{.name = "conversion.convert",
            .run = &runConvert,
            .specialize = &specializeConversion,
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = false},
    Subject{.name = "pointwise.program",
            .run = &runPointwise,
            .specialize = &specializePointwise,
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = true},
    Subject{.name = "reduction.statistics",
            .run = &runReduction,
            .specialize = &specializeReduction,
            .bytes = ByteComparison::None,
            .relativeTolerance = 0.0,
            .allowsInPlace = false},
    Subject{.name = "stencil.separable",
            .run = &runStencil,
            .specialize = &specializeFloatPlane,
            .bytes = ByteComparison::Float32Relative,
            // The two passes accumulate in Float64 in tap order on both backends, so what separates
            // them is the last rounding rather than a reassociation. The family own suite compares
            // at this tolerance and this harness uses the same number rather than a second opinion.
            .relativeTolerance = 1e-12,
            .allowsInPlace = false},
    Subject{.name = "math.transcendental",
            .run = &runMath,
            .specialize = &specializeFloatPlane,
            .bytes = ByteComparison::Float32Transcendental,
            .relativeTolerance = 0.0,
            .allowsInPlace = true},
    Subject{.name = "noise.bilateral",
            .run = &runBilateral,
            .specialize = &specializeFloatPlane,
            .bytes = ByteComparison::Float32Relative,
            // Every range weight is one exponential inside the vector tier - five units in the last
            // place of a Float32 weight - and an output is a weighted mean of samples inside the
            // unit interval, so the two backends part by a small multiple of that tier's relative
            // distance; the bound here leaves that an order of magnitude of room without admitting
            // a wrong tap.
            .relativeTolerance = 1e-5,
            .allowsInPlace = false},
    Subject{.name = "noise.chromatic",
            .run = &runChromatic,
            .specialize = &specializeFloatPlane,
            // The compiled row spells every selection as the scalar row does, so the bits agree
            // exactly, not-a-number samples included.
            .bytes = ByteComparison::Exact,
            .relativeTolerance = 0.0,
            .allowsInPlace = false}};

/// The ordered position of a Float32 bit pattern, so that a subtraction counts representable values.
[[nodiscard]] std::int64_t orderedBits(float value)
{
    std::int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (bits < 0)
    {
        return static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()) - static_cast<std::int64_t>(bits);
    }
    return static_cast<std::int64_t>(bits);
}

/// Which class a result belongs to when it is not a finite normal number.
[[nodiscard]] int resultClass(float value)
{
    if (std::isnan(value))
    {
        return 0;
    }
    if (std::isinf(value))
    {
        return value > 0.0f ? 1 : 2;
    }
    if (std::abs(value) < (std::numeric_limits<float>::min)())
    {
        return 3;
    }
    return 4;
}

/// The difference between two Float32 results, in representable values.
[[nodiscard]] double ulpDistance(float left, float right)
{
    if (left == right)
    {
        return 0.0;
    }
    if (!std::isfinite(left) || !std::isfinite(right))
    {
        return (std::numeric_limits<double>::infinity)();
    }
    const std::int64_t distance = orderedBits(left) - orderedBits(right);
    return static_cast<double>(distance < 0 ? -distance : distance);
}

} // namespace

std::uint64_t Random::next() noexcept
{
    m_state += 0x9E3779B97F4A7C15ull;
    std::uint64_t value = m_state;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

std::uint32_t Random::below(std::uint32_t bound) noexcept
{
    if (bound <= 1u)
    {
        return 0u;
    }
    // Rejection rather than a modulus, so the distribution is even and does not depend on how the
    // bound divides into the generator range.
    const std::uint32_t limit =
        (std::numeric_limits<std::uint32_t>::max)() - ((std::numeric_limits<std::uint32_t>::max)() % bound) - 1u;
    for (;;)
    {
        const std::uint32_t drawn = static_cast<std::uint32_t>(next() >> 32u);
        if (drawn <= limit)
        {
            return drawn % bound;
        }
    }
}

bool Random::oneIn(std::uint32_t chance) noexcept
{
    return chance <= 1u || below(chance) == 0u;
}

double Random::unit() noexcept
{
    // Fifty-three bits, which is every value a Float64 can hold in the unit interval.
    return static_cast<double>(next() >> 11u) / 9007199254740992.0;
}

Target referenceTarget()
{
    return Target{
        .backend = an::ComputeBackend::Reference, .instructionSet = an::InstructionSet::Scalar, .name = "Reference"};
}

std::vector<Target> candidateTargets()
{
    std::vector<Target> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::uint8_t rung = static_cast<std::uint8_t>(an::InstructionSet::Scalar);
         rung <= static_cast<std::uint8_t>(an::InstructionSet::Avx512Skylake); ++rung)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(rung);
        if (candidate == an::InstructionSet::Scalar || !an::instructionSetIsCompiledIn(candidate) ||
            !an::supportsInstructionSet(capabilities, candidate))
        {
            continue;
        }
        targets.push_back(Target{.backend = an::ComputeBackend::Cpu,
                                 .instructionSet = candidate,
                                 .name = an::instructionSetName(candidate)});
    }
    return targets;
}

std::span<const Subject> subjects()
{
    return std::span<const Subject>{kSubjects};
}

Case generateCase(const Subject& subject, std::uint64_t seed, std::uint32_t index)
{
    Random random(seed ^ (std::uint64_t{index} << 20u));
    Case generated;
    generated.index = index;
    generated.seed = seed;
    generated.width = random.pick(std::span<const std::uint32_t>{kWidths});
    generated.height = random.pick(std::span<const std::uint32_t>{kHeights});
    generated.sourcePadding = random.pick(std::span<const std::uint32_t>{kPaddings});
    generated.destinationPadding = random.pick(std::span<const std::uint32_t>{kPaddings});
    generated.threads = random.pick(std::span<const std::uint32_t>{kThreads});
    generated.validity = random.pick(std::span<const ValidityChoice>{kValidityChoices});
    generated.inPlace = subject.allowsInPlace && random.oneIn(3u);
    generated.variant = random.next();
    if (generated.inPlace)
    {
        generated.destinationPadding = generated.sourcePadding;
    }
    if (subject.specialize != nullptr)
    {
        subject.specialize(random, generated);
    }
    return generated;
}

std::string describeCase(const Subject& subject, const Case& generated)
{
    std::ostringstream text;
    text << subject.name << " case " << generated.index << " of seed " << generated.seed << ": " << generated.width
         << "x" << generated.height << " channels " << generated.channels << ", padding " << generated.sourcePadding
         << "/" << generated.destinationPadding << ", threads " << generated.threads << ", validity "
         << static_cast<int>(generated.validity) << ", in place " << (generated.inPlace ? "yes" : "no") << ", source "
         << an::sampleTypeName(generated.sampleType) << ", destination "
         << an::sampleTypeName(generated.destinationSampleType) << ", variant " << generated.variant;
    return text.str();
}

std::string compareOutcomes(const Subject& subject, const Case& generated, const Outcome& reference,
                            const Outcome& candidate)
{
    std::ostringstream text;
    if (reference.ran != candidate.ran)
    {
        text << "one run succeeded and the other did not: reference "
             << (reference.ran ? "succeeded" : an::errorCodeName(reference.code)) << ", candidate "
             << (candidate.ran ? "succeeded" : an::errorCodeName(candidate.code));
        return text.str();
    }
    if (!reference.ran)
    {
        if (reference.code != candidate.code)
        {
            text << "the two runs refused with different codes: " << an::errorCodeName(reference.code) << " and "
                 << an::errorCodeName(candidate.code);
            return text.str();
        }
        return {};
    }

    if (reference.values.size() != candidate.values.size())
    {
        return "the two runs reported a different number of scalar results";
    }
    for (std::size_t index = 0u; index < reference.values.size(); ++index)
    {
        const double tolerance = index < candidate.valueTolerances.size()
                                     ? reference.valueTolerances[index] + candidate.valueTolerances[index]
                                     : 0.0;
        const double difference = std::abs(reference.values[index] - candidate.values[index]);
        const bool bothNotANumber = std::isnan(reference.values[index]) && std::isnan(candidate.values[index]);
        if (!bothNotANumber && !(difference <= tolerance))
        {
            text << "scalar result " << index << " differs: " << reference.values[index] << " against "
                 << candidate.values[index] << ", tolerance " << tolerance;
            return text.str();
        }
    }

    if (subject.bytes == ByteComparison::None)
    {
        return {};
    }
    if (reference.bytes.size() != candidate.bytes.size())
    {
        return "the two runs wrote buffers of different sizes";
    }
    if (subject.bytes == ByteComparison::Exact)
    {
        for (std::size_t index = 0u; index < reference.bytes.size(); ++index)
        {
            if (reference.bytes[index] != candidate.bytes[index])
            {
                text << "byte " << index << " differs: " << static_cast<int>(reference.bytes[index]) << " against "
                     << static_cast<int>(candidate.bytes[index]);
                return text.str();
            }
        }
        return {};
    }

    const std::span<const float> expected = floatsOf(std::span<const std::byte>{reference.bytes});
    const std::span<const float> actual = floatsOf(std::span<const std::byte>{candidate.bytes});
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        const float left = expected[index];
        const float right = actual[index];
        if (subject.bytes == ByteComparison::Float32Relative)
        {
            if (resultClass(left) != resultClass(right))
            {
                text << "sample " << index << " changed class: " << left << " against " << right;
                return text.str();
            }
            const double difference = std::abs(static_cast<double>(left) - static_cast<double>(right));
            const double allowed = subject.relativeTolerance * (std::max)(1.0, std::abs(static_cast<double>(left)));
            if (std::isfinite(left) && std::isfinite(right) && !(difference <= allowed))
            {
                text << "sample " << index << " differs: " << left << " against " << right << ", tolerance " << allowed;
                return text.str();
            }
            continue;
        }

        // A transcendental carries the bound its own header derives for the sample that produced it,
        // and an exceptional or subnormal result is compared by class instead.
        if (resultClass(left) != resultClass(right))
        {
            text << "sample " << index << " changed class: " << left << " against " << right;
            return text.str();
        }
        if (resultClass(left) != 4)
        {
            continue;
        }
        const double distance = ulpDistance(left, right);
        const float argument = index < reference.sourceSamples.size() ? reference.sourceSamples[index] : left;
        const double allowed =
            (std::max)(an::kVectorMathUlpTier, an::mathUlpBound(reference.function, argument, reference.exponent));
        if (!(distance <= allowed))
        {
            text << "sample " << index << " differs by " << distance << " units in the last place, above " << allowed
                 << ": " << left << " against " << right;
            return text.str();
        }
    }
    static_cast<void>(generated);
    return {};
}

} // namespace conformance
