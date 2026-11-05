// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - measured H-alpha, green and the compatibility H-alpha/OIII pair from a Bayer
// mosaic.
//
// Every product reads the mosaic block by block through the positions `CfaDescriptor::colorAt`
// reports for the first two-by-two block; a named Bayer pattern repeats every two samples, so that
// block's positions hold for the whole picture and no phase has a coordinate table of its own. The
// single-plane products are one scheduled row pass each over any resident representation. The pair
// makes a serial pass that accumulates each green and blue position's finite mean in long double -
// the level equalization needs all three before any output - and then writes its two planes, the OIII
// plane's red positions interpolated from their equalized neighbours at the point of use.
//
// The factor-two resampling is this file's own separable Lanczos filter, the definition the public
// header states. An output's taps depend only on its parity, so the filter needs three phases: the
// half-pixel phase every reduced output uses, and the quarter and three-quarter phases that
// alternate between odd and even enlarged outputs. The horizontal pass reads each block of outputs'
// source window once and writes a Float64 working plane; the vertical pass narrows once into the
// destination.

#include <AfterNightEngine/Algorithms/Color/NarrowbandExtraction.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/color/NarrowbandInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>

namespace an
{
namespace
{

using narrowband_detail::failure;

constexpr const char* kPlanOperation = "an::planNarrowbandExtraction";
constexpr const char* kDescriptorOperation = "an::measuredBayerChannelDescriptor";
constexpr const char* kHydrogenOperation = "an::extractBayerHydrogenAlpha";
constexpr const char* kGreenOperation = "an::extractBayerGreen";
constexpr const char* kPairOperation = "an::extractBayerHydrogenAlphaAndOxygenIII";
constexpr const char* kTransitionOperation = "an::narrowbandExtractionTransition";
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kAxialWeight = 1.414213562373095048801688724209698079;

/// Where each colour sits in a two-by-two Bayer block, as an index `y * 2 + x` within the block.
struct BayerPositions
{
    /// The red photosite.
    std::uint32_t red = 0u;

    /// The blue photosite.
    std::uint32_t blue = 0u;

    /// The two green photosites, in raster order.
    std::array<std::uint32_t, 2u> green{};
};

/// Whether a filter description is one of the four named two-by-two Bayer patterns.
///
/// @param cfa the description.
/// @return true for a mosaiced, untabulated pattern whose block is two samples wide.
[[nodiscard]] bool isNamedBayer(const CfaDescriptor& cfa) noexcept
{
    return cfa.isMosaiced() && !cfa.hasFilterTable() && cfaPatternBlockSize(cfa.pattern()) == 2u;
}

/// The positions of the colours in the first block, asked of the descriptor so its phase applies.
///
/// @param cfa a named Bayer description.
/// @return the four positions.
[[nodiscard]] BayerPositions positionsOf(const CfaDescriptor& cfa) noexcept
{
    BayerPositions positions{};
    std::uint32_t green = 0u;
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            const std::uint32_t index = y * 2u + x;
            switch (cfa.colorAt(x, y))
            {
            case CfaColor::Red:
                positions.red = index;
                break;
            case CfaColor::Green:
                positions.green[green++] = index;
                break;
            case CfaColor::Blue:
                positions.blue = index;
                break;
            }
        }
    }
    return positions;
}

/// One plane of samples reached through its first row and its row stride, resolved once.
///
/// `narrowband_detail::sampleAt` and `writeAt` resolve a checked row offset for every sample, which
/// suits code that touches a handful and not a pass that touches every sample, several of them more
/// than once: behind a reference to the layout, a compiler that cannot prove the layout unchanged
/// by the stores around it recomputes that offset each time. These address the same sample - the
/// first channel of the plane, one step of `samplesPerPixel` per pixel - once the row is known.
///
/// @tparam SampleT the resident representation.
/// @tparam ByteT `const std::uint8_t` to read, `std::uint8_t` to write.
template <typename SampleT, typename ByteT> class PlaneRows
{
public:
    /// The plane the first channel of `samples` lies in.
    ///
    /// @param samples the buffer, as the checked accessors take it.
    /// @param layout where its samples are.
    PlaneRows(std::conditional_t<std::is_const_v<ByteT>, const void*, void*> samples,
              const ImageLayout& layout) noexcept
        : m_first(static_cast<ByteT*>(samples)), m_rowStrideBytes(layout.rowStrideBytes()),
          m_step(layout.samplesPerPixel())
    {
    }

    /// One row.
    ///
    /// @param y which row, inside the plane.
    /// @return its first sample.
    [[nodiscard]] auto* row(std::uint32_t y) const noexcept
    {
        using Sample = std::conditional_t<std::is_const_v<ByteT>, const SampleT, SampleT>;
        return reinterpret_cast<Sample*>(m_first + static_cast<std::uint64_t>(y) * m_rowStrideBytes);
    }

    /// Samples between one pixel and the next along a row.
    ///
    /// @return the step.
    [[nodiscard]] std::uint32_t step() const noexcept
    {
        return m_step;
    }

    /// One sample, widened.
    ///
    /// @param x which column.
    /// @param y which row.
    /// @return the sample as a double.
    [[nodiscard]] double at(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return static_cast<double>(row(y)[static_cast<std::size_t>(x) * m_step]);
    }

private:
    ByteT* m_first = nullptr;
    std::uint64_t m_rowStrideBytes = 0u;
    std::uint32_t m_step = 1u;
};

template <typename SampleT> using PlaneReader = PlaneRows<SampleT, const std::uint8_t>;

template <typename SampleT> using PlaneWriter = PlaneRows<SampleT, std::uint8_t>;

[[nodiscard]] Status validateSourceDescriptor(const ImageDescriptor& source, bool floatingOnly, const char* operation)
{
    if (source.channelSchema() != ChannelSchema::Mono || source.extent().channelCount() != 1u ||
        !isNamedBayer(source.cfa()))
    {
        return failure(operation, ErrorCode::Unsupported,
                       "narrowband extraction needs a one-channel named Bayer mosaic");
    }
    if (floatingOnly && !narrowband_detail::supportedWorkingType(source.sampleType()))
    {
        return failure(operation, ErrorCode::Unsupported,
                       "the H-alpha/OIII pair runs on a Float32 or Float64 working mosaic");
    }
    if (floatingOnly && source.numericDomain() != NumericDomain::NormalizedLinear)
    {
        return failure(operation, ErrorCode::Unsupported, "the H-alpha/OIII pair needs a normalized working domain");
    }
    if (source.extent().width() < 2u || source.extent().height() < 2u)
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "narrowband extraction needs one complete Bayer block");
    }
    return Status::success();
}

[[nodiscard]] Result<ImageDescriptor> monoDescriptor(const ImageDescriptor& source, std::uint32_t width,
                                                     std::uint32_t height, const char* operation)
{
    const std::optional<ImageExtent> extent = ImageExtent::create(width, height, 1u, defaultEngineLimits());
    if (!extent.has_value())
    {
        return failure(operation, ErrorCode::LimitExceeded, "the extraction output exceeds engine limits");
    }
    ImageDescriptorRequest request = source.toRequest();
    request.extent = *extent;
    request.channelSchema = ChannelSchema::Mono;
    request.colorSpace = ColorSpace::Unspecified;
    request.cfa = {};
    const std::optional<ImageDescriptor> output = ImageDescriptor::create(request);
    return output.has_value() ? Result<ImageDescriptor>{*output}
                              : Result<ImageDescriptor>{failure(operation, ErrorCode::IncompatibleArguments,
                                                                "the extraction output description is inconsistent")};
}

[[nodiscard]] Status validateLayoutForDescriptor(const void* samples, const ImageLayout& layout,
                                                 const ImageDescriptor& descriptor, const char* operation)
{
    if (!narrowband_detail::alignedFor(samples, descriptor.sampleType()) ||
        layout.elementSizeBytes() != sampleTypeSizeBytes(descriptor.sampleType()))
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "an extraction pointer or layout disagrees with its sample type");
    }
    if (layout.extent() != descriptor.extent())
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "an extraction layout does not match its planned description");
    }
    return Status::success();
}

[[nodiscard]] Status validateCommon(const ExecutionContext& execution, const ImageDescriptor& sourceDescriptor,
                                    const void* source, const ImageLayout& sourceLayout, void* destination,
                                    const ImageLayout& destinationLayout, const ImageDescriptor& destinationDescriptor,
                                    const char* operation)
{
    if (!execution.valid())
    {
        return failure(operation, ErrorCode::NotInitialized, "a narrowband extraction has no execution context");
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (const Status valid = validateLayoutForDescriptor(source, sourceLayout, sourceDescriptor, operation);
        valid.failed())
    {
        return valid;
    }
    if (const Status valid =
            validateLayoutForDescriptor(destination, destinationLayout, destinationDescriptor, operation);
        valid.failed())
    {
        return valid;
    }
    if (narrowband_detail::overlaps(source, sourceLayout, destination, destinationLayout))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "a narrowband extraction source and destination are disjoint");
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] Result<NarrowbandExtractionResult>
extractMeasuredTyped(const ExecutionContext& execution, const ImageDescriptor& sourceDescriptor, const void* source,
                     const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                     bool green)
{
    const BayerPositions positions = positionsOf(sourceDescriptor.cfa());
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * (green ? 3u : 2u),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * (green ? 7u : 2u)};
    const PlaneReader<SampleT> input(source, sourceLayout);
    const PlaneWriter<SampleT> output(destination, destinationLayout);
    const std::size_t inputStep = input.step();
    const std::size_t outputStep = output.step();
    const auto body = [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t outputY = static_cast<std::uint32_t>(row);
            SampleT* const written = output.row(outputY);
            if (!green)
            {
                const SampleT* const red = input.row(outputY * 2u + positions.red / 2u);
                const std::size_t redColumn = positions.red % 2u;
                for (std::uint32_t outputX = 0u; outputX < width; ++outputX)
                {
                    // Through Float64 and back, which is exact for every sample type.
                    written[outputX * outputStep] = static_cast<SampleT>(
                        static_cast<double>(red[(static_cast<std::size_t>(outputX) * 2u + redColumn) * inputStep]));
                }
                continue;
            }
            const SampleT* const firstRow = input.row(outputY * 2u + positions.green[0] / 2u);
            const SampleT* const secondRow = input.row(outputY * 2u + positions.green[1] / 2u);
            const std::size_t firstColumn = positions.green[0] % 2u;
            const std::size_t secondColumn = positions.green[1] % 2u;
            for (std::uint32_t outputX = 0u; outputX < width; ++outputX)
            {
                const SampleT first = firstRow[(static_cast<std::size_t>(outputX) * 2u + firstColumn) * inputStep];
                const SampleT second = secondRow[(static_cast<std::size_t>(outputX) * 2u + secondColumn) * inputStep];
                const double firstNormalized = convertSample<SampleT, double>(first, ConversionPolicy{}).value;
                const double secondNormalized = convertSample<SampleT, double>(second, ConversionPolicy{}).value;
                written[outputX * outputStep] =
                    convertSample<double, SampleT>((firstNormalized + secondNormalized) * 0.5, ConversionPolicy{})
                        .value;
            }
        }
        return Status::success();
    };
    const Status written = parallelFor(execution, height, cost, body);
    if (written.failed())
    {
        return written;
    }
    NarrowbandExtractionResult result{};
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total;
    result.blockColumns = width;
    result.blockRows = height;
    result.trailingColumns = sourceLayout.extent().width() % 2u;
    result.trailingRows = sourceLayout.extent().height() % 2u;
    result.plannedWorkers = plannedWorkerCount(execution, height, cost);
    return result;
}

[[nodiscard]] Result<NarrowbandExtractionResult>
dispatchMeasured(const ExecutionContext& execution, SampleType sampleType, const ImageDescriptor& sourceDescriptor,
                 const void* source, const ImageLayout& sourceLayout, void* destination,
                 const ImageLayout& destinationLayout, bool green)
{
    switch (sampleType)
    {
    case SampleType::UInt8:
        return extractMeasuredTyped<std::uint8_t>(execution, sourceDescriptor, source, sourceLayout, destination,
                                                  destinationLayout, green);
    case SampleType::Int8:
        return extractMeasuredTyped<std::int8_t>(execution, sourceDescriptor, source, sourceLayout, destination,
                                                 destinationLayout, green);
    case SampleType::UInt16:
        return extractMeasuredTyped<std::uint16_t>(execution, sourceDescriptor, source, sourceLayout, destination,
                                                   destinationLayout, green);
    case SampleType::Int16:
        return extractMeasuredTyped<std::int16_t>(execution, sourceDescriptor, source, sourceLayout, destination,
                                                  destinationLayout, green);
    case SampleType::UInt32:
        return extractMeasuredTyped<std::uint32_t>(execution, sourceDescriptor, source, sourceLayout, destination,
                                                   destinationLayout, green);
    case SampleType::Int32:
        return extractMeasuredTyped<std::int32_t>(execution, sourceDescriptor, source, sourceLayout, destination,
                                                  destinationLayout, green);
    case SampleType::Float32:
        return extractMeasuredTyped<float>(execution, sourceDescriptor, source, sourceLayout, destination,
                                           destinationLayout, green);
    case SampleType::Float64:
        return extractMeasuredTyped<double>(execution, sourceDescriptor, source, sourceLayout, destination,
                                            destinationLayout, green);
    }
    return failure(green ? kGreenOperation : kHydrogenOperation, ErrorCode::InvalidArgument,
                   "the source sample type is unknown");
}

// The normalized sinc, `sin(pi t) / (pi t)`, taken as one near zero.
[[nodiscard]] double sinc(double value) noexcept
{
    if (std::abs(value) < 1.0e-12)
    {
        return 1.0;
    }
    const double angle = kPi * value;
    return std::sin(angle) / angle;
}

[[nodiscard]] double lanczosWeight(double distance) noexcept
{
    return std::abs(distance) < 4.0 ? sinc(distance) * sinc(distance / 4.0) : 0.0;
}

/// The eight tap weights of one output phase and the reciprocal of their sum.
struct LanczosPhase
{
    /// Weights of the source samples at `center - 3` through `center + 4`.
    std::array<double, 8u> weights{};

    /// One over the weights' sum, which normalizes the result; zero when the sum vanishes.
    double reciprocalSum = 1.0;
};

/// The tap weights for an output centred `fraction` of a sample beyond its centre tap.
///
/// @param fraction the output centre's distance past `center`, in source samples.
/// @return the phase.
[[nodiscard]] LanczosPhase makeLanczosPhase(double fraction) noexcept
{
    LanczosPhase phase{};
    double sum = 0.0;
    for (std::int32_t index = 0; index < 8; ++index)
    {
        phase.weights[static_cast<std::size_t>(index)] = lanczosWeight(fraction - (index - 3));
        sum += phase.weights[static_cast<std::size_t>(index)];
    }
    phase.reciprocalSum = std::abs(sum) > std::numeric_limits<double>::epsilon() ? 1.0 / sum : 0.0;
    return phase;
}

// With half-pixel centres, a reduced output `x` sits at source `2x + 0.5`, half a sample past tap
// `2x`; an enlarged output sits at `x / 2 - 0.25`, three quarters past tap `x / 2 - 1` when `x` is
// even and a quarter past tap `x / 2` when it is odd. `phaseFor` and `lanczosCenter` state those two
// facts, and `phases` holds the quarter, half and three-quarter phases in that order.
[[nodiscard]] const LanczosPhase& phaseFor(std::uint32_t coordinate, bool enlarge,
                                           const std::array<LanczosPhase, 3u>& phases) noexcept
{
    if (!enlarge)
    {
        return phases[1u];
    }
    return (coordinate & 1u) == 0u ? phases[2u] : phases[0u];
}

[[nodiscard]] std::int64_t lanczosCenter(std::uint32_t coordinate, bool enlarge) noexcept
{
    if (!enlarge)
    {
        return static_cast<std::int64_t>(coordinate) * 2;
    }
    return static_cast<std::int64_t>(coordinate / 2u) - ((coordinate & 1u) == 0u ? 1 : 0);
}

template <typename SampleT, typename ReadT>
[[nodiscard]] Status resizeLanczosSeparable(const ExecutionContext& execution, std::uint32_t sourceWidth,
                                            std::uint32_t sourceHeight, std::uint32_t destinationWidth,
                                            std::uint32_t destinationHeight, const ReadT& read, void* destination,
                                            const ImageLayout& destinationLayout)
{
    const bool enlarge = static_cast<std::uint64_t>(sourceWidth) * 2u == destinationWidth &&
                         static_cast<std::uint64_t>(sourceHeight) * 2u == destinationHeight;
    const bool reduce = static_cast<std::uint64_t>(destinationWidth) * 2u == sourceWidth &&
                        static_cast<std::uint64_t>(destinationHeight) * 2u == sourceHeight;
    if (!enlarge && !reduce)
    {
        return failure(kPairOperation, ErrorCode::Internal,
                       "the checked narrowband scaling is not an exact factor of two");
    }
    if (sourceHeight > (std::numeric_limits<std::uint64_t>::max)() / destinationWidth / sizeof(double))
    {
        return failure(kPairOperation, ErrorCode::LimitExceeded, "the narrowband Lanczos working plane size overflows");
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(destinationWidth) * sourceHeight * sizeof(double);
    narrowband_detail::ContextBlock block;
    if (const Status allocated = block.take(execution, bytes, kPairOperation); allocated.failed())
    {
        return allocated;
    }
    double* intermediate = block.doubles();
    const std::array<LanczosPhase, 3u> phases{makeLanczosPhase(0.25), makeLanczosPhase(0.5), makeLanczosPhase(0.75)};

    const TaskCost horizontalCost{.bytesPerItem = static_cast<std::uint64_t>(destinationWidth) * 9u * sizeof(double),
                                  .operationsPerItem = static_cast<std::uint64_t>(destinationWidth) * 20u};
    // The source a block of adjacent outputs reads, read once. Eight taps per output over a source
    // that advances two samples per output (or half of one) would otherwise read each source sample
    // four times over - and the sample a reduction reads is interpolated from its neighbours at every
    // red photosite. Each output still sums the same eight values in the same order.
    constexpr std::uint32_t kBlockOutputs = 64u;
    constexpr std::size_t kWindowSamples = 2u * kBlockOutputs + 8u;
    const std::int64_t lastSourceX = static_cast<std::int64_t>(sourceWidth) - 1;
    const auto horizontal = [&](TaskRange rows) -> Status {
        std::array<double, kWindowSamples> window{};
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            double* const filtered = intermediate + static_cast<std::uint64_t>(y) * destinationWidth;
            for (std::uint32_t blockStart = 0u; blockStart < destinationWidth; blockStart += kBlockOutputs)
            {
                const std::uint32_t blockEnd = (std::min)(destinationWidth, blockStart + kBlockOutputs);
                const std::int64_t windowStart = lanczosCenter(blockStart, enlarge) - 3;
                const std::int64_t windowEnd = lanczosCenter(blockEnd - 1u, enlarge) + 5;
                for (std::int64_t sourceX = windowStart; sourceX < windowEnd; ++sourceX)
                {
                    window[static_cast<std::size_t>(sourceX - windowStart)] =
                        read(static_cast<std::uint32_t>(std::clamp(sourceX, std::int64_t{0}, lastSourceX)), y);
                }
                for (std::uint32_t x = blockStart; x < blockEnd; ++x)
                {
                    const LanczosPhase& phase = phaseFor(x, enlarge, phases);
                    const double* const taps =
                        window.data() + static_cast<std::size_t>(lanczosCenter(x, enlarge) - 3 - windowStart);
                    double sum = 0.0;
                    for (std::size_t index = 0u; index < 8u; ++index)
                    {
                        sum += taps[index] * phase.weights[index];
                    }
                    filtered[x] = sum * phase.reciprocalSum;
                }
            }
        }
        return Status::success();
    };
    if (const Status filtered = parallelFor(execution, sourceHeight, horizontalCost, horizontal); filtered.failed())
    {
        return filtered;
    }

    const TaskCost verticalCost{.bytesPerItem = static_cast<std::uint64_t>(destinationWidth) * 9u * sizeof(double),
                                .operationsPerItem = static_cast<std::uint64_t>(destinationWidth) * 20u};
    const PlaneWriter<SampleT> output(destination, destinationLayout);
    const std::size_t outputStep = output.step();
    const auto vertical = [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            const LanczosPhase& phase = phaseFor(y, enlarge, phases);
            const std::int64_t center = lanczosCenter(y, enlarge);
            std::array<const double*, 8u> tapRows{};
            for (std::size_t index = 0u; index < 8u; ++index)
            {
                const std::int64_t sourceY = std::clamp(center + static_cast<std::int64_t>(index) - 3, std::int64_t{0},
                                                        static_cast<std::int64_t>(sourceHeight) - 1);
                tapRows[index] = intermediate + static_cast<std::uint64_t>(sourceY) * destinationWidth;
            }
            SampleT* const written = output.row(y);
            for (std::uint32_t x = 0u; x < destinationWidth; ++x)
            {
                double sum = 0.0;
                for (std::size_t index = 0u; index < 8u; ++index)
                {
                    sum += tapRows[index][x] * phase.weights[index];
                }
                written[x * outputStep] = static_cast<SampleT>(sum * phase.reciprocalSum);
            }
        }
        return Status::success();
    };
    return parallelFor(execution, destinationHeight, verticalCost, vertical);
}

template <typename SampleT, typename ReadT>
[[nodiscard]] Status writeNativePlane(const ExecutionContext& execution, std::uint32_t width, std::uint32_t height,
                                      const ReadT& read, void* destination, const ImageLayout& destinationLayout,
                                      std::uint64_t operationsPerPixel)
{
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * operationsPerPixel};
    const PlaneWriter<SampleT> output(destination, destinationLayout);
    const std::size_t outputStep = output.step();
    const auto body = [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            SampleT* const written = output.row(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                written[x * outputStep] = static_cast<SampleT>(read(x, y));
            }
        }
        return Status::success();
    };
    return parallelFor(execution, height, cost, body);
}

template <typename SampleT>
[[nodiscard]] Result<NarrowbandExtractionResult>
extractPairTyped(const ExecutionContext& execution, const NarrowbandExtractionPlan& plan, const void* source,
                 const ImageLayout& sourceLayout, void* hydrogenAlphaDestination,
                 const ImageLayout& hydrogenAlphaLayout, void* oxygenIIIDestination, const ImageLayout& oxygenIIILayout)
{
    const BayerPositions positions = positionsOf(plan.sourceDescriptor().cfa());
    const std::uint32_t fullWidth = plan.sourceDescriptor().extent().width() & ~1u;
    const std::uint32_t fullHeight = plan.sourceDescriptor().extent().height() & ~1u;
    const std::uint32_t halfWidth = fullWidth / 2u;
    const std::uint32_t halfHeight = fullHeight / 2u;
    const PlaneReader<SampleT> input(source, sourceLayout);
    const std::size_t inputStep = input.step();
    std::array<long double, 4u> sums{};
    std::array<std::uint64_t, 4u> counts{};
    std::uint64_t nonFinite = 0u;
    const auto accumulatePosition = [&](const SampleT* samples, std::uint32_t firstX, std::uint32_t position) noexcept {
        if (position == positions.red)
        {
            return;
        }
        for (std::uint32_t x = firstX; x < fullWidth; x += 2u)
        {
            const double value = static_cast<double>(samples[static_cast<std::size_t>(x) * inputStep]);
            if (!std::isfinite(value))
            {
                ++nonFinite;
                continue;
            }
            sums[position] += value;
            ++counts[position];
        }
    };
    for (std::uint32_t y = 0u; y < fullHeight; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        const SampleT* const samples = input.row(y);
        const std::uint32_t rowPosition = (y & 1u) * 2u;
        accumulatePosition(samples, 0u, rowPosition);
        accumulatePosition(samples, 1u, rowPosition + 1u);
    }
    for (const std::uint32_t position :
         std::array<std::uint32_t, 3u>{positions.green[0], positions.green[1], positions.blue})
    {
        if (counts[position] == 0u)
        {
            return failure(kPairOperation, ErrorCode::EmptySelection,
                           "one OIII photosite position has no finite source sample");
        }
    }
    std::array<double, 4u> means{};
    for (const std::uint32_t position :
         std::array<std::uint32_t, 3u>{positions.green[0], positions.green[1], positions.blue})
    {
        means[position] = static_cast<double>(sums[position] / counts[position]);
    }
    const double common = (means[positions.green[0]] + means[positions.green[1]] + means[positions.blue]) / 3.0;
    std::array<double, 4u> factors{1.0, 1.0, 1.0, 1.0};
    for (const std::uint32_t position :
         std::array<std::uint32_t, 3u>{positions.green[0], positions.green[1], positions.blue})
    {
        factors[position] =
            std::abs(means[position]) > std::numeric_limits<double>::epsilon() ? common / means[position] : 1.0;
    }
    const auto hydrogenAt = [&](std::uint32_t x, std::uint32_t y) noexcept {
        const std::uint32_t sourceX = x * 2u + positions.red % 2u;
        const std::uint32_t sourceY = y * 2u + positions.red / 2u;
        const double value = input.at(sourceX, sourceY);
        return std::isfinite(value) ? value : 0.0;
    };
    const auto oxygenMeasuredAt = [&](std::uint32_t x, std::uint32_t y) noexcept -> std::optional<double> {
        const std::uint32_t position = (y & 1u) * 2u + (x & 1u);
        const double value = input.at(x, y);
        return std::isfinite(value) ? std::optional<double>{value * factors[position]} : std::nullopt;
    };
    // A named Bayer pattern repeats every two samples, so a photosite's position in its block is
    // its colour: the one `positionsOf` found red is red wherever the block repeats.
    const auto oxygenAt = [&](std::uint32_t x, std::uint32_t y) noexcept {
        const std::uint32_t position = (y & 1u) * 2u + (x & 1u);
        if (position != positions.red)
        {
            return oxygenMeasuredAt(x, y).value_or(0.0);
        }
        double sum = 0.0;
        double weight = 0.0;
        if (x > 0u && x + 1u < fullWidth && y > 0u && y + 1u < fullHeight)
        {
            const SampleT* const above = input.row(y - 1u);
            const SampleT* const middle = input.row(y);
            const SampleT* const below = input.row(y + 1u);
            const std::size_t before = static_cast<std::size_t>(x - 1u) * inputStep;
            const std::size_t center = static_cast<std::size_t>(x) * inputStep;
            const std::size_t after = static_cast<std::size_t>(x + 1u) * inputStep;
            const std::uint32_t horizontalPosition = positions.red ^ 1u;
            const std::uint32_t verticalPosition = positions.red ^ 2u;
            const auto include = [&](SampleT sample, std::uint32_t neighbourPosition, double thisWeight) noexcept {
                const double value = static_cast<double>(sample);
                if (std::isfinite(value))
                {
                    sum += (value * factors[neighbourPosition]) * thisWeight;
                    weight += thisWeight;
                }
            };
            // This is the generic loop below written out for the overwhelmingly common interior
            // case. Keep its top-to-bottom, left-to-right accumulation order: that is part of the
            // operation's one numeric result, independently of how many workers execute it.
            include(above[before], positions.blue, 1.0);
            include(above[center], verticalPosition, kAxialWeight);
            include(above[after], positions.blue, 1.0);
            include(middle[before], horizontalPosition, kAxialWeight);
            include(middle[after], horizontalPosition, kAxialWeight);
            include(below[before], positions.blue, 1.0);
            include(below[center], verticalPosition, kAxialWeight);
            include(below[after], positions.blue, 1.0);
            return weight > 0.0 ? sum / weight : 0.0;
        }
        for (std::int32_t dy = -1; dy <= 1; ++dy)
        {
            for (std::int32_t dx = -1; dx <= 1; ++dx)
            {
                if ((dx == 0 && dy == 0) || static_cast<std::int64_t>(x) + dx < 0 ||
                    static_cast<std::int64_t>(y) + dy < 0 || static_cast<std::int64_t>(x) + dx >= fullWidth ||
                    static_cast<std::int64_t>(y) + dy >= fullHeight)
                {
                    continue;
                }
                const std::uint32_t neighbourX = static_cast<std::uint32_t>(static_cast<std::int64_t>(x) + dx);
                const std::uint32_t neighbourY = static_cast<std::uint32_t>(static_cast<std::int64_t>(y) + dy);
                const std::optional<double> neighbour = oxygenMeasuredAt(neighbourX, neighbourY);
                if (!neighbour.has_value())
                {
                    continue;
                }
                const double thisWeight = dx == 0 || dy == 0 ? kAxialWeight : 1.0;
                sum += *neighbour * thisWeight;
                weight += thisWeight;
            }
        }
        return weight > 0.0 ? sum / weight : 0.0;
    };
    Status written = Status::success();
    switch (plan.scaling())
    {
    case NarrowbandExtractionScaling::Native:
        written = writeNativePlane<SampleT>(execution, halfWidth, halfHeight, hydrogenAt, hydrogenAlphaDestination,
                                            hydrogenAlphaLayout, 2u);
        if (written.ok())
        {
            written = writeNativePlane<SampleT>(execution, fullWidth, fullHeight, oxygenAt, oxygenIIIDestination,
                                                oxygenIIILayout, 16u);
        }
        break;
    case NarrowbandExtractionScaling::UpsampleHydrogenAlpha:
        written = resizeLanczosSeparable<SampleT>(execution, halfWidth, halfHeight, fullWidth, fullHeight, hydrogenAt,
                                                  hydrogenAlphaDestination, hydrogenAlphaLayout);
        if (written.ok())
        {
            written = writeNativePlane<SampleT>(execution, fullWidth, fullHeight, oxygenAt, oxygenIIIDestination,
                                                oxygenIIILayout, 16u);
        }
        break;
    case NarrowbandExtractionScaling::DownsampleOxygenIII:
        written = resizeLanczosSeparable<SampleT>(execution, fullWidth, fullHeight, halfWidth, halfHeight, oxygenAt,
                                                  oxygenIIIDestination, oxygenIIILayout);
        if (written.ok())
        {
            written = writeNativePlane<SampleT>(execution, halfWidth, halfHeight, hydrogenAt, hydrogenAlphaDestination,
                                                hydrogenAlphaLayout, 2u);
        }
        break;
    }
    if (written.failed())
    {
        return written;
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(fullWidth) * sizeof(SampleT) * 3u,
                        .operationsPerItem = static_cast<std::uint64_t>(fullWidth) * 20u};
    NarrowbandExtractionResult result{};
    result.run.selection.total = static_cast<std::uint64_t>(fullWidth) * fullHeight;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten =
        static_cast<std::uint64_t>(hydrogenAlphaLayout.extent().width()) * hydrogenAlphaLayout.extent().height() +
        static_cast<std::uint64_t>(oxygenIIILayout.extent().width()) * oxygenIIILayout.extent().height();
    result.blockColumns = halfWidth;
    result.blockRows = halfHeight;
    result.trailingColumns = sourceLayout.extent().width() - fullWidth;
    result.trailingRows = sourceLayout.extent().height() - fullHeight;
    result.nonFiniteSamples = nonFinite;
    result.plannedWorkers = plannedWorkerCount(execution, fullHeight, cost);
    result.workingBytes = plan.scaling() == NarrowbandExtractionScaling::Native
                              ? 0u
                              : static_cast<std::uint64_t>(fullWidth) * halfHeight * sizeof(double);
    return result;
}

} // namespace

const char* narrowbandExtractionScalingName(NarrowbandExtractionScaling scaling) noexcept
{
    switch (scaling)
    {
    case NarrowbandExtractionScaling::Native:
        return "Native";
    case NarrowbandExtractionScaling::UpsampleHydrogenAlpha:
        return "UpsampleHydrogenAlpha";
    case NarrowbandExtractionScaling::DownsampleOxygenIII:
        return "DownsampleOxygenIII";
    }
    return "UnknownNarrowbandExtractionScaling";
}

Result<ImageDescriptor> measuredBayerChannelDescriptor(const ImageDescriptor& source)
{
    if (const Status valid = validateSourceDescriptor(source, false, kDescriptorOperation); valid.failed())
    {
        return valid;
    }
    return monoDescriptor(source, source.extent().width() / 2u, source.extent().height() / 2u, kDescriptorOperation);
}

Result<NarrowbandExtractionPlan> planNarrowbandExtraction(const ImageDescriptor& source,
                                                          NarrowbandExtractionScaling scaling)
{
    if (!isNarrowbandExtractionScaling(scaling))
    {
        return failure(kPlanOperation, ErrorCode::InvalidArgument,
                       "the extraction scaling is not one this build implements");
    }
    if (const Status valid = validateSourceDescriptor(source, true, kPlanOperation); valid.failed())
    {
        return valid;
    }
    const std::uint32_t fullWidth = source.extent().width() & ~1u;
    const std::uint32_t fullHeight = source.extent().height() & ~1u;
    const std::uint32_t halfWidth = fullWidth / 2u;
    const std::uint32_t halfHeight = fullHeight / 2u;
    const bool hydrogenFull = scaling == NarrowbandExtractionScaling::UpsampleHydrogenAlpha;
    const bool oxygenHalf = scaling == NarrowbandExtractionScaling::DownsampleOxygenIII;
    const Result<ImageDescriptor> hydrogen = monoDescriptor(source, hydrogenFull ? fullWidth : halfWidth,
                                                            hydrogenFull ? fullHeight : halfHeight, kPlanOperation);
    if (!hydrogen.hasValue())
    {
        return hydrogen.status();
    }
    const Result<ImageDescriptor> oxygen = monoDescriptor(source, oxygenHalf ? halfWidth : fullWidth,
                                                          oxygenHalf ? halfHeight : fullHeight, kPlanOperation);
    if (!oxygen.hasValue())
    {
        return oxygen.status();
    }
    return NarrowbandExtractionPlan{source, *hydrogen, *oxygen, scaling};
}

Result<NarrowbandExtractionResult> extractBayerHydrogenAlpha(const ExecutionContext& execution,
                                                             const ImageDescriptor& sourceDescriptor,
                                                             const void* source, const ImageLayout& sourceLayout,
                                                             void* destination, const ImageLayout& destinationLayout)
{
    if (const Status valid = validateSourceDescriptor(sourceDescriptor, false, kHydrogenOperation); valid.failed())
    {
        return valid;
    }
    const Result<ImageDescriptor> output = measuredBayerChannelDescriptor(sourceDescriptor);
    if (!output.hasValue())
    {
        return output.status();
    }
    if (const Status valid = validateCommon(execution, sourceDescriptor, source, sourceLayout, destination,
                                            destinationLayout, *output, kHydrogenOperation);
        valid.failed())
    {
        return valid;
    }
    return dispatchMeasured(execution, sourceDescriptor.sampleType(), sourceDescriptor, source, sourceLayout,
                            destination, destinationLayout, false);
}

Result<NarrowbandExtractionResult> extractBayerGreen(const ExecutionContext& execution,
                                                     const ImageDescriptor& sourceDescriptor, const void* source,
                                                     const ImageLayout& sourceLayout, void* destination,
                                                     const ImageLayout& destinationLayout)
{
    if (const Status valid = validateSourceDescriptor(sourceDescriptor, false, kGreenOperation); valid.failed())
    {
        return valid;
    }
    const Result<ImageDescriptor> output = measuredBayerChannelDescriptor(sourceDescriptor);
    if (!output.hasValue())
    {
        return output.status();
    }
    if (const Status valid = validateCommon(execution, sourceDescriptor, source, sourceLayout, destination,
                                            destinationLayout, *output, kGreenOperation);
        valid.failed())
    {
        return valid;
    }
    return dispatchMeasured(execution, sourceDescriptor.sampleType(), sourceDescriptor, source, sourceLayout,
                            destination, destinationLayout, true);
}

Result<NarrowbandExtractionResult>
extractBayerHydrogenAlphaAndOxygenIII(const ExecutionContext& execution, const NarrowbandExtractionPlan& plan,
                                      const void* source, const ImageLayout& sourceLayout,
                                      void* hydrogenAlphaDestination, const ImageLayout& hydrogenAlphaLayout,
                                      void* oxygenIIIDestination, const ImageLayout& oxygenIIILayout)
{
    if (!plan.valid())
    {
        return failure(kPairOperation, ErrorCode::InvalidArgument, "the H-alpha/OIII extraction plan is empty");
    }
    if (const Status valid = validateSourceDescriptor(plan.sourceDescriptor(), true, kPairOperation); valid.failed())
    {
        return valid;
    }
    if (const Status valid =
            validateCommon(execution, plan.sourceDescriptor(), source, sourceLayout, hydrogenAlphaDestination,
                           hydrogenAlphaLayout, plan.hydrogenAlphaDescriptor(), kPairOperation);
        valid.failed())
    {
        return valid;
    }
    if (const Status valid = validateLayoutForDescriptor(oxygenIIIDestination, oxygenIIILayout,
                                                         plan.oxygenIIIDescriptor(), kPairOperation);
        valid.failed())
    {
        return valid;
    }
    if (narrowband_detail::overlaps(source, sourceLayout, oxygenIIIDestination, oxygenIIILayout) ||
        narrowband_detail::overlaps(hydrogenAlphaDestination, hydrogenAlphaLayout, oxygenIIIDestination,
                                    oxygenIIILayout))
    {
        return failure(kPairOperation, ErrorCode::IncompatibleArguments,
                       "the H-alpha/OIII source and both destinations are pairwise disjoint");
    }
    return plan.sourceDescriptor().sampleType() == SampleType::Float32
               ? extractPairTyped<float>(execution, plan, source, sourceLayout, hydrogenAlphaDestination,
                                         hydrogenAlphaLayout, oxygenIIIDestination, oxygenIIILayout)
               : extractPairTyped<double>(execution, plan, source, sourceLayout, hydrogenAlphaDestination,
                                          hydrogenAlphaLayout, oxygenIIIDestination, oxygenIIILayout);
}

Result<MetadataTransition> narrowbandExtractionTransition(const ImageSemantics& source,
                                                          NarrowbandExtractionProduct product,
                                                          NarrowbandExtractionScaling scaling)
{
    if (!isNarrowbandExtractionProduct(product) || !isNarrowbandExtractionScaling(scaling))
    {
        return failure(kTransitionOperation, ErrorCode::InvalidArgument,
                       "the extraction transition names a known product and scaling");
    }
    if (!source.validity().isAllValid())
    {
        return failure(kTransitionOperation, ErrorCode::IncompatibleArguments,
                       "CFA narrowband extraction rejects incomplete source data");
    }
    ImageSemanticsRequest request = source.toRequest();
    request.processingState.demosaiced = true;
    const Result<ImageSemantics> semantics = ImageSemantics::create(request);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const char* productName = product == NarrowbandExtractionProduct::HydrogenAlpha ? "HydrogenAlpha"
                              : product == NarrowbandExtractionProduct::Green       ? "Green"
                                                                                    : "HydrogenAlphaAndOxygenIII";
    return operationTransition(
        kNarrowbandExtractionEffect,
        OperationOutcome{.operation = product == NarrowbandExtractionProduct::HydrogenAlpha ? kHydrogenOperation
                                      : product == NarrowbandExtractionProduct::Green       ? kGreenOperation
                                                                                            : kPairOperation,
                         .detail = std::string{"product="} + productName +
                                   ";scaling=" + narrowbandExtractionScalingName(scaling),
                         .semantics = *semantics});
}

} // namespace an
