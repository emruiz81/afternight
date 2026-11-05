// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - bias, dark and flat calibration: the checked frame view, the plan that
// prepares flat means and an optimized dark scale, and the one-pass executor.
//
// Planning reads samples only where a decision needs them: the central third of the flat for one
// Float64 mean per channel or CFA block site, and the central 512-pixel square of the light, dark
// and optional bias for the six moments the dark-scale search reads. Everything else a plan holds
// comes from descriptions, so a plan can be prepared for a light that is not resident.
//
// Execution takes one of two paths and reports which. The compiled row, chosen from the CPU
// dispatch table, serves a non-mosaiced frame set that is all-valid and shares one arrangement; the
// scalar row here serves everything else and is the authority the compiled rows are held to. Both
// widen every step to Float64 and narrow back to the storage type after it, in the order pedestal,
// bias, scaled dark, normalized flat, so they store identical bits.

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>

namespace an
{

// The plan's only writer and the executor's reader of its closed state. Keeping the accessors here
// rather than as members keeps the published plan free of setters a caller could reach.
class CalibrationPlanAccess
{
public:
    static auto& flatFactors(CalibrationPlan& plan) noexcept
    {
        return plan.m_flatFactors;
    }
    static const auto& flatFactors(const CalibrationPlan& plan) noexcept
    {
        return plan.m_flatFactors;
    }
    static auto& cfa(CalibrationPlan& plan) noexcept
    {
        return plan.m_cfa;
    }
    static const auto& cfa(const CalibrationPlan& plan) noexcept
    {
        return plan.m_cfa;
    }
    static double& pedestal(CalibrationPlan& plan) noexcept
    {
        return plan.m_pedestal;
    }
    static double pedestal(const CalibrationPlan& plan) noexcept
    {
        return plan.m_pedestal;
    }
    static double& darkScale(CalibrationPlan& plan) noexcept
    {
        return plan.m_effectiveDarkScale;
    }
    static double darkScale(const CalibrationPlan& plan) noexcept
    {
        return plan.m_effectiveDarkScale;
    }
    static std::uint64_t& darkSamples(CalibrationPlan& plan) noexcept
    {
        return plan.m_darkOptimizationSamples;
    }
    static std::uint32_t& width(CalibrationPlan& plan) noexcept
    {
        return plan.m_width;
    }
    static std::uint32_t width(const CalibrationPlan& plan) noexcept
    {
        return plan.m_width;
    }
    static std::uint32_t& height(CalibrationPlan& plan) noexcept
    {
        return plan.m_height;
    }
    static std::uint32_t height(const CalibrationPlan& plan) noexcept
    {
        return plan.m_height;
    }
    static std::uint32_t& channels(CalibrationPlan& plan) noexcept
    {
        return plan.m_channels;
    }
    static std::uint32_t channels(const CalibrationPlan& plan) noexcept
    {
        return plan.m_channels;
    }
    static std::uint32_t& flatChannels(CalibrationPlan& plan) noexcept
    {
        return plan.m_flatChannels;
    }
    static std::uint32_t flatChannels(const CalibrationPlan& plan) noexcept
    {
        return plan.m_flatChannels;
    }
    static std::uint32_t& flatGroups(CalibrationPlan& plan) noexcept
    {
        return plan.m_flatGroupCount;
    }
    static std::uint32_t flatGroups(const CalibrationPlan& plan) noexcept
    {
        return plan.m_flatGroupCount;
    }
    static std::uint32_t& darkEvaluations(CalibrationPlan& plan) noexcept
    {
        return plan.m_darkOptimizationEvaluations;
    }
    static SampleType& sampleType(CalibrationPlan& plan) noexcept
    {
        return plan.m_sampleType;
    }
    static SampleType sampleType(const CalibrationPlan& plan) noexcept
    {
        return plan.m_sampleType;
    }
    static NumericDomain& numericDomain(CalibrationPlan& plan) noexcept
    {
        return plan.m_numericDomain;
    }
    static NumericDomain numericDomain(const CalibrationPlan& plan) noexcept
    {
        return plan.m_numericDomain;
    }
    static ChannelSchema& channelSchema(CalibrationPlan& plan) noexcept
    {
        return plan.m_channelSchema;
    }
    static ChannelSchema channelSchema(const CalibrationPlan& plan) noexcept
    {
        return plan.m_channelSchema;
    }
    static ColorSpace& colorSpace(CalibrationPlan& plan) noexcept
    {
        return plan.m_colorSpace;
    }
    static ColorSpace colorSpace(const CalibrationPlan& plan) noexcept
    {
        return plan.m_colorSpace;
    }
    static CoordinateOrigin& coordinateOrigin(CalibrationPlan& plan) noexcept
    {
        return plan.m_coordinateOrigin;
    }
    static CoordinateOrigin coordinateOrigin(const CalibrationPlan& plan) noexcept
    {
        return plan.m_coordinateOrigin;
    }
    static double& pixelAspectRatio(CalibrationPlan& plan) noexcept
    {
        return plan.m_pixelAspectRatio;
    }
    static double pixelAspectRatio(const CalibrationPlan& plan) noexcept
    {
        return plan.m_pixelAspectRatio;
    }
    static FlatNormalizationMode& flatNormalization(CalibrationPlan& plan) noexcept
    {
        return plan.m_flatNormalization;
    }
    static FlatNormalizationMode flatNormalization(const CalibrationPlan& plan) noexcept
    {
        return plan.m_flatNormalization;
    }
    static DarkScalingMode& darkScaling(CalibrationPlan& plan) noexcept
    {
        return plan.m_darkScaling;
    }
    static bool& usesBias(CalibrationPlan& plan) noexcept
    {
        return plan.m_usesBias;
    }
    static bool usesBias(const CalibrationPlan& plan) noexcept
    {
        return plan.m_usesBias;
    }
    static bool& usesDark(CalibrationPlan& plan) noexcept
    {
        return plan.m_usesDark;
    }
    static bool usesDark(const CalibrationPlan& plan) noexcept
    {
        return plan.m_usesDark;
    }
    static bool& usesFlat(CalibrationPlan& plan) noexcept
    {
        return plan.m_usesFlat;
    }
    static bool usesFlat(const CalibrationPlan& plan) noexcept
    {
        return plan.m_usesFlat;
    }
    static bool& valid(CalibrationPlan& plan) noexcept
    {
        return plan.m_valid;
    }
};

namespace
{

constexpr const char* kPrepareOperation = "an::prepareImageCalibration";
constexpr const char* kExecuteOperation = "an::calibrateImage";

[[nodiscard]] Status calibrationFailure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] Status parameterFailure(const char* operation, ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool pointerIsAligned(const void* pointer, SampleType sampleType) noexcept
{
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    return sampleType == SampleType::Float64 ? address % alignof(double) == 0u : address % alignof(float) == 0u;
}

[[nodiscard]] Status checkValidityBinding(const KernelValidityView& validity, const ImageExtent& extent,
                                          const char* operation)
{
    if (validity.maskPlane != 0u)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "an image-wide calibration validity view starts at mask plane zero", "validity");
    }
    if (validity.descriptor.needsMask())
    {
        if (validity.mask == nullptr)
        {
            return parameterFailure(operation, ErrorCode::InvalidArgument,
                                    "an explicit validity rule needs the mask that decides it", "validity");
        }
        return checkMaskCovers(*validity.mask, extent, operation);
    }
    if (validity.mask != nullptr)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "a validity mask was supplied for a rule that does not read one", "validity");
    }
    return Status::success();
}

[[nodiscard]] bool sameFrameShape(const CalibrationPlan& plan, const CalibrationFrameView& frame) noexcept
{
    const ImageDescriptor& descriptor = frame.descriptor();
    return frame.valid() && descriptor.sampleType() == CalibrationPlanAccess::sampleType(plan) &&
           descriptor.extent().width() == CalibrationPlanAccess::width(plan) &&
           descriptor.extent().height() == CalibrationPlanAccess::height(plan) &&
           descriptor.extent().channelCount() == CalibrationPlanAccess::channels(plan) &&
           descriptor.channelSchema() == CalibrationPlanAccess::channelSchema(plan) &&
           descriptor.colorSpace() == CalibrationPlanAccess::colorSpace(plan) &&
           descriptor.numericDomain() == CalibrationPlanAccess::numericDomain(plan) &&
           descriptor.cfa() == CalibrationPlanAccess::cfa(plan) &&
           descriptor.coordinateOrigin() == CalibrationPlanAccess::coordinateOrigin(plan) &&
           descriptor.pixelAspectRatio() == CalibrationPlanAccess::pixelAspectRatio(plan);
}

[[nodiscard]] bool frameValidityIsAllValid(const CalibrationFrameView& frame) noexcept
{
    return frame.validity().descriptor.isAllValid();
}

[[nodiscard]] bool allValidityIsAllValid(const CalibrationFrameView& input, const CalibrationMasters& masters) noexcept
{
    return frameValidityIsAllValid(input) && (!masters.bias.has_value() || frameValidityIsAllValid(*masters.bias)) &&
           (!masters.dark.has_value() || frameValidityIsAllValid(*masters.dark)) &&
           (!masters.flat.has_value() || frameValidityIsAllValid(*masters.flat));
}

template <typename SampleT>
[[nodiscard]] const SampleT* sampleAddress(const CalibrationFrameView& frame, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t channel) noexcept
{
    const ImageLayout& layout = frame.layout();
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    const auto* base = static_cast<const std::byte*>(frame.samples());
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<const SampleT*>(base + offset);
}

template <typename SampleT>
[[nodiscard]] SampleT* sampleAddress(void* samples, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t channel) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    auto* base = static_cast<std::byte*>(samples);
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<SampleT*>(base + offset);
}

template <typename SampleT>
[[nodiscard]] bool sampleParticipates(const CalibrationFrameView& frame, SampleT value, std::uint32_t x,
                                      std::uint32_t y, std::uint32_t channel) noexcept
{
    if (!sampleIsValidByValue(frame.validity().descriptor, value))
    {
        return false;
    }
    if (!frame.validity().descriptor.needsMask())
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *frame.validity().mask;
    const std::uint32_t plane = mask.isShared() ? 0u : channel;
    return mask.test(plane, x, y).value_or(false);
}

[[nodiscard]] std::uint32_t cfaSide(const CfaDescriptor& cfa) noexcept
{
    return cfa.isMosaiced() ? cfaPatternBlockSize(cfa.pattern()) : 0u;
}

// Which prepared flat mean a flat sample divides by: its site in the repeating CFA block for a
// mosaiced light, its own channel for a flat with the light's channels, and the single mean for a
// Mono flat shared by an RGB light.
[[nodiscard]] std::uint32_t flatGroupAt(const CalibrationPlan& plan, std::uint32_t x, std::uint32_t y,
                                        std::uint32_t flatChannel) noexcept
{
    const std::uint32_t side = cfaSide(CalibrationPlanAccess::cfa(plan));
    if (side != 0u)
    {
        return (y % side) * side + (x % side);
    }
    return CalibrationPlanAccess::flatChannels(plan) == CalibrationPlanAccess::channels(plan) ? flatChannel : 0u;
}

template <typename SampleT>
[[nodiscard]] Status prepareFlatMeans(const ExecutionContext& execution, const CalibrationFrameView& flat,
                                      CalibrationPlan& plan)
{
    const std::uint32_t regionWidth = CalibrationPlanAccess::width(plan) / 3u;
    const std::uint32_t regionHeight = CalibrationPlanAccess::height(plan) / 3u;
    if (regionWidth == 0u || regionHeight == 0u)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::OutOfRange,
                                  "a central-third flat normalization needs at least three pixels on each side");
    }
    const std::uint32_t firstX = CalibrationPlanAccess::width(plan) / 3u;
    const std::uint32_t firstY = CalibrationPlanAccess::height(plan) / 3u;
    std::array<double, kMaxFlatNormalizationGroups> sums{};
    std::array<std::uint64_t, kMaxFlatNormalizationGroups> counts{};
    for (std::uint32_t y = firstY; y < firstY + regionHeight; ++y)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        for (std::uint32_t x = firstX; x < firstX + regionWidth; ++x)
        {
            for (std::uint32_t channel = 0u; channel < CalibrationPlanAccess::flatChannels(plan); ++channel)
            {
                const SampleT value = *sampleAddress<SampleT>(flat, x, y, channel);
                if (!sampleParticipates(flat, value, x, y, channel))
                {
                    continue;
                }
                if (!std::isfinite(static_cast<double>(value)))
                {
                    return calibrationFailure(kPrepareOperation, ErrorCode::InvalidData,
                                              "a flat normalization region contains a non-finite sample that its "
                                              "validity says participates");
                }
                const std::uint32_t group = flatGroupAt(plan, x, y, channel);
                sums[group] += static_cast<double>(value);
                ++counts[group];
            }
        }
    }
    for (std::uint32_t group = 0u; group < CalibrationPlanAccess::flatGroups(plan); ++group)
    {
        if (counts[group] == 0u)
        {
            return calibrationFailure(kPrepareOperation, ErrorCode::EmptySelection,
                                      "one flat channel or CFA site has no valid central samples");
        }
        const double mean = sums[group] / static_cast<double>(counts[group]);
        if (!std::isfinite(mean) || mean == 0.0)
        {
            return calibrationFailure(kPrepareOperation, ErrorCode::InvalidData,
                                      "one flat channel or CFA site has a zero or non-finite central mean");
        }
        CalibrationPlanAccess::flatFactors(plan)[group] = mean;
    }
    return Status::success();
}

// Sums over the accepted central-square samples of the pedestal- and bias-corrected light ("raw")
// and the unscaled dark, in Float64. A sample any of the three frames excludes is skipped, and a
// non-finite one that participates is refused rather than summed.
struct DarkMoments
{
    std::uint64_t count = 0u;
    double rawSum = 0.0;
    double darkSum = 0.0;
    double rawSquares = 0.0;
    double darkSquares = 0.0;
    double products = 0.0;
};

template <typename SampleT>
[[nodiscard]] Status prepareDarkMoments(const ExecutionContext& execution, const CalibrationFrameView& input,
                                        const CalibrationMasters& masters, const CalibrationPlan& plan,
                                        DarkMoments& moments)
{
    const std::uint32_t firstX = (CalibrationPlanAccess::width(plan) - kCalibrationDarkOptimizationSide) / 2u;
    const std::uint32_t firstY = (CalibrationPlanAccess::height(plan) - kCalibrationDarkOptimizationSide) / 2u;
    for (std::uint32_t y = firstY; y < firstY + kCalibrationDarkOptimizationSide; ++y)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        for (std::uint32_t x = firstX; x < firstX + kCalibrationDarkOptimizationSide; ++x)
        {
            SampleT raw = *sampleAddress<SampleT>(input, x, y, 0u);
            const SampleT dark = *sampleAddress<SampleT>(*masters.dark, x, y, 0u);
            if (!sampleParticipates(input, raw, x, y, 0u) || !sampleParticipates(*masters.dark, dark, x, y, 0u))
            {
                continue;
            }
            if (CalibrationPlanAccess::pedestal(plan) != 0.0)
            {
                raw = static_cast<SampleT>(static_cast<double>(raw) + CalibrationPlanAccess::pedestal(plan));
            }
            if (masters.bias.has_value())
            {
                const SampleT bias = *sampleAddress<SampleT>(*masters.bias, x, y, 0u);
                if (!sampleParticipates(*masters.bias, bias, x, y, 0u))
                {
                    continue;
                }
                raw = static_cast<SampleT>(static_cast<double>(raw) - static_cast<double>(bias));
            }
            const double widenedRaw = static_cast<double>(raw);
            const double widenedDark = static_cast<double>(dark);
            if (!std::isfinite(widenedRaw) || !std::isfinite(widenedDark))
            {
                return calibrationFailure(kPrepareOperation, ErrorCode::InvalidData,
                                          "dark optimization cannot build moments from non-finite samples");
            }
            ++moments.count;
            moments.rawSum += widenedRaw;
            moments.darkSum += widenedDark;
            moments.rawSquares += widenedRaw * widenedRaw;
            moments.darkSquares += widenedDark * widenedDark;
            moments.products += widenedRaw * widenedDark;
        }
    }
    return Status::success();
}

// The objective is the standard deviation of (raw - k * dark) over the square, which the moments
// give without another scan: var(raw) + k^2 var(dark) - 2k cov(raw, dark). A golden-section search
// over [0, 2] narrows to 0.001 or stops at 64 evaluations and answers the final interval's midpoint.
[[nodiscard]] Result<double> optimizeDarkScale(const DarkMoments& moments, std::uint32_t& evaluations)
{
    if (moments.count == 0u)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::EmptySelection,
                                  "dark optimization found no valid samples in its central square");
    }
    const double inverseCount = 1.0 / static_cast<double>(moments.count);
    const double rawVariance =
        moments.rawSquares * inverseCount - (moments.rawSum * inverseCount) * (moments.rawSum * inverseCount);
    const double darkVariance =
        moments.darkSquares * inverseCount - (moments.darkSum * inverseCount) * (moments.darkSum * inverseCount);
    const double covariance =
        moments.products * inverseCount - (moments.rawSum * inverseCount) * (moments.darkSum * inverseCount);
    if (!std::isfinite(rawVariance) || !std::isfinite(darkVariance) || !std::isfinite(covariance) ||
        darkVariance <= (std::numeric_limits<double>::epsilon)())
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidData,
                                  "dark optimization needs finite non-degenerate dark variation");
    }
    const auto noise = [&](double factor) {
        ++evaluations;
        const double variance = rawVariance + factor * factor * darkVariance - 2.0 * factor * covariance;
        return std::sqrt((std::max)(variance, 0.0));
    };
    constexpr double kGolden = 0.6180339887498948482;
    double lower = 0.0;
    double upper = 2.0;
    double left = upper - kGolden * (upper - lower);
    double right = lower + kGolden * (upper - lower);
    double leftNoise = noise(left);
    double rightNoise = noise(right);
    while (upper - lower > 0.001 && evaluations < 64u)
    {
        if (leftNoise < rightNoise)
        {
            upper = right;
            right = left;
            rightNoise = leftNoise;
            left = upper - kGolden * (upper - lower);
            leftNoise = noise(left);
        }
        else
        {
            lower = left;
            left = right;
            leftNoise = rightNoise;
            right = lower + kGolden * (upper - lower);
            rightNoise = noise(right);
        }
    }
    return (lower + upper) * 0.5;
}

[[nodiscard]] Status checkMaster(const ImageDescriptor& light, const CalibrationFrameView& master, const char* role,
                                 bool flatMayBeMono)
{
    if (!master.valid())
    {
        return parameterFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                "a stated calibration master holds no frame", role);
    }
    const ImageDescriptor& candidate = master.descriptor();
    if (candidate.sampleType() != light.sampleType() || candidate.extent().width() != light.extent().width() ||
        candidate.extent().height() != light.extent().height() || candidate.numericDomain() != light.numericDomain() ||
        candidate.coordinateOrigin() != light.coordinateOrigin() ||
        candidate.pixelAspectRatio() != light.pixelAspectRatio())
    {
        return parameterFailure(kPrepareOperation, ErrorCode::IncompatibleArguments,
                                "a calibration master disagrees with the light's representation, geometry or numeric "
                                "domain",
                                role);
    }
    const bool sameChannels = candidate.extent().channelCount() == light.extent().channelCount() &&
                              candidate.channelSchema() == light.channelSchema() &&
                              candidate.colorSpace() == light.colorSpace() && candidate.cfa() == light.cfa();
    const bool monoFlat = flatMayBeMono && light.channelSchema() == ChannelSchema::Rgb &&
                          candidate.channelSchema() == ChannelSchema::Mono && !candidate.cfa().isMosaiced();
    if (!sameChannels && !monoFlat)
    {
        return parameterFailure(kPrepareOperation, ErrorCode::IncompatibleArguments,
                                "bias and dark masters must match the light's channels and CFA; only an RGB flat may "
                                "instead be Mono",
                                role);
    }
    return Status::success();
}

[[nodiscard]] bool rangesOverlap(const void* left, const ImageLayout& leftLayout, const void* right,
                                 const ImageLayout& rightLayout) noexcept
{
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    const std::uintptr_t leftEnd = leftBegin + static_cast<std::uintptr_t>(leftLayout.totalBytes());
    const std::uintptr_t rightEnd = rightBegin + static_cast<std::uintptr_t>(rightLayout.totalBytes());
    return leftBegin < rightEnd && rightBegin < leftEnd;
}

[[nodiscard]] bool sameArrangement(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return left.samplesPerPixel() == right.samplesPerPixel() && left.planeCount() == right.planeCount();
}

[[nodiscard]] bool canUseCompiledRows(const CalibrationPlan& plan, const CalibrationFrameView& input,
                                      const CalibrationMasters& masters, const ImageLayout& destinationLayout) noexcept
{
    if (CalibrationPlanAccess::cfa(plan).isMosaiced() || !allValidityIsAllValid(input, masters) ||
        !sameArrangement(input.layout(), destinationLayout))
    {
        return false;
    }
    if ((masters.bias.has_value() && !sameArrangement(input.layout(), masters.bias->layout())) ||
        (masters.dark.has_value() && !sameArrangement(input.layout(), masters.dark->layout())))
    {
        return false;
    }
    if (masters.flat.has_value() &&
        CalibrationPlanAccess::flatChannels(plan) == CalibrationPlanAccess::channels(plan) &&
        !sameArrangement(input.layout(), masters.flat->layout()))
    {
        return false;
    }
    return true;
}

template <typename SampleT>
[[nodiscard]] Result<CalibrationResult>
runCompiledRows(const ExecutionContext& execution, const CalibrationPlan& plan, const CalibrationFrameView& input,
                const CalibrationMasters& masters, void* destination, const ImageLayout& destinationLayout,
                CalibrationExecutionPath path, const BackendSelection& backend,
                CalibrationRunOutcome (*run)(const SampleT*, const SampleT*, const SampleT*, const SampleT*, SampleT*,
                                             std::uint64_t, const CalibrationRunParameters&) noexcept)
{
    const bool planar = input.layout().samplesPerPixel() == 1u;
    const std::uint64_t tightRowBytes =
        static_cast<std::uint64_t>(CalibrationPlanAccess::width(plan)) * sizeof(SampleT);
    const auto hasTightRows = [&](const ImageLayout& layout) { return layout.rowStrideBytes() == tightRowBytes; };
    const bool flattenTightMonoRows = planar && CalibrationPlanAccess::channels(plan) == 1u &&
                                      hasTightRows(input.layout()) && hasTightRows(destinationLayout) &&
                                      (!masters.bias.has_value() || hasTightRows(masters.bias->layout())) &&
                                      (!masters.dark.has_value() || hasTightRows(masters.dark->layout())) &&
                                      (!masters.flat.has_value() || hasTightRows(masters.flat->layout()));
    const std::uint64_t taskCount = static_cast<std::uint64_t>(CalibrationPlanAccess::height(plan)) *
                                    (planar ? CalibrationPlanAccess::channels(plan) : 1u);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(CalibrationPlanAccess::width(plan)) *
                                        CalibrationPlanAccess::channels(plan) * sizeof(SampleT) * 5u,
                        .operationsPerItem = static_cast<std::uint64_t>(CalibrationPlanAccess::width(plan)) *
                                             CalibrationPlanAccess::channels(plan) * 8u};
    // A tight mono chunk is one contiguous vector run. Padded rows and channel-dependent
    // normalization keep the row path below, where their address or parameter changes are explicit.
    const auto runTightRows = [&](TaskRange range) {
        const auto firstRow = static_cast<std::uint32_t>(range.begin);
        const SampleT* light = sampleAddress<SampleT>(input, 0u, firstRow, 0u);
        const SampleT* bias =
            masters.bias.has_value() ? sampleAddress<SampleT>(*masters.bias, 0u, firstRow, 0u) : nullptr;
        const SampleT* dark =
            masters.dark.has_value() ? sampleAddress<SampleT>(*masters.dark, 0u, firstRow, 0u) : nullptr;
        const SampleT* flat =
            masters.flat.has_value() ? sampleAddress<SampleT>(*masters.flat, 0u, firstRow, 0u) : nullptr;
        SampleT* output = sampleAddress<SampleT>(destination, destinationLayout, 0u, firstRow, 0u);
        const double* normalization = masters.flat.has_value() && CalibrationPlanAccess::flatNormalization(plan) ==
                                                                      FlatNormalizationMode::CentralMean
                                          ? CalibrationPlanAccess::flatFactors(plan).data()
                                          : nullptr;
        const CalibrationRunParameters parameters{.pedestal = CalibrationPlanAccess::pedestal(plan),
                                                  .darkScale = CalibrationPlanAccess::darkScale(plan),
                                                  .flatNormalization = normalization,
                                                  .channelCount = 1u,
                                                  .flatChannelCount = 1u};
        const std::uint64_t sampleCount =
            (range.end - range.begin) * static_cast<std::uint64_t>(CalibrationPlanAccess::width(plan));
        return run(light, bias, dark, flat, output, sampleCount, parameters);
    };
    if (flattenTightMonoRows && execution.admittedConcurrency() <= 1u)
    {
        // One admitted worker has nothing to hand to the task arena. Walk the same scheduler-sized
        // grains here, retaining its cancellation frequency without paying an arena boundary.
        CalibrationRunOutcome outcome{};
        const std::uint64_t grain = chooseGrainSize(taskCount, cost, 1u);
        for (std::uint64_t begin = 0u; begin < taskCount; begin += grain)
        {
            const Status running = execution.checkCancellation();
            if (running.failed())
            {
                return running;
            }
            const CalibrationRunOutcome rows =
                runTightRows(TaskRange{.begin = begin, .end = (std::min)(begin + grain, taskCount)});
            outcome.samplesWritten += rows.samplesWritten;
            outcome.nonFiniteOutputs += rows.nonFiniteOutputs;
        }
        return CalibrationResult{.backend = backend,
                                 .path = path,
                                 .samplesWritten = outcome.samplesWritten,
                                 .nonFiniteOutputs = outcome.nonFiniteOutputs,
                                 .invalidInputs = 0u,
                                 .plannedWorkers = 1u,
                                 .imagePasses = 1u};
    }
    std::atomic<std::uint64_t> written{0u};
    std::atomic<std::uint64_t> nonFinite{0u};
    const Status completed = parallelFor(execution, taskCount, cost, [&](TaskRange range) {
        std::uint64_t localWritten = 0u;
        std::uint64_t localNonFinite = 0u;
        if (flattenTightMonoRows)
        {
            const CalibrationRunOutcome rows = runTightRows(range);
            written.fetch_add(rows.samplesWritten, std::memory_order_relaxed);
            nonFinite.fetch_add(rows.nonFiniteOutputs, std::memory_order_relaxed);
            return Status::success();
        }
        for (std::uint64_t task = range.begin; task < range.end; ++task)
        {
            const std::uint32_t channel =
                planar ? static_cast<std::uint32_t>(task / CalibrationPlanAccess::height(plan)) : 0u;
            const std::uint32_t y = planar ? static_cast<std::uint32_t>(task % CalibrationPlanAccess::height(plan))
                                           : static_cast<std::uint32_t>(task);
            const SampleT* lightRow = sampleAddress<SampleT>(input, 0u, y, channel);
            const SampleT* biasRow =
                masters.bias.has_value() ? sampleAddress<SampleT>(*masters.bias, 0u, y, channel) : nullptr;
            const SampleT* darkRow =
                masters.dark.has_value() ? sampleAddress<SampleT>(*masters.dark, 0u, y, channel) : nullptr;
            const std::uint32_t flatChannel = CalibrationPlanAccess::flatChannels(plan) == 1u ? 0u : channel;
            const SampleT* flatRow = masters.flat.has_value()
                                         ? sampleAddress<SampleT>(*masters.flat, 0u, y, planar ? flatChannel : 0u)
                                         : nullptr;
            SampleT* outputRow = sampleAddress<SampleT>(destination, destinationLayout, 0u, y, channel);
            const double* normalization = nullptr;
            if (masters.flat.has_value() &&
                CalibrationPlanAccess::flatNormalization(plan) == FlatNormalizationMode::CentralMean)
            {
                normalization = planar ? &CalibrationPlanAccess::flatFactors(plan)[flatChannel]
                                       : CalibrationPlanAccess::flatFactors(plan).data();
            }
            const CalibrationRunParameters parameters{
                .pedestal = CalibrationPlanAccess::pedestal(plan),
                .darkScale = CalibrationPlanAccess::darkScale(plan),
                .flatNormalization = normalization,
                .channelCount = planar ? 1u : CalibrationPlanAccess::channels(plan),
                .flatChannelCount = planar ? 1u : CalibrationPlanAccess::flatChannels(plan)};
            const CalibrationRunOutcome row =
                run(lightRow, biasRow, darkRow, flatRow, outputRow, CalibrationPlanAccess::width(plan), parameters);
            localWritten += row.samplesWritten;
            localNonFinite += row.nonFiniteOutputs;
        }
        written.fetch_add(localWritten, std::memory_order_relaxed);
        nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        return Status::success();
    });
    if (completed.failed())
    {
        return completed;
    }
    return CalibrationResult{.backend = backend,
                             .path = path,
                             .samplesWritten = written.load(std::memory_order_relaxed),
                             .nonFiniteOutputs = nonFinite.load(std::memory_order_relaxed),
                             .invalidInputs = 0u,
                             .plannedWorkers = plannedWorkerCount(execution, taskCount, cost),
                             .imagePasses = 1u};
}

template <typename SampleT>
[[nodiscard]] Result<CalibrationResult> runScalar(const ExecutionContext& execution, const CalibrationPlan& plan,
                                                  const CalibrationFrameView& input, const CalibrationMasters& masters,
                                                  void* destination, const ImageLayout& destinationLayout)
{
    const std::uint64_t rows = CalibrationPlanAccess::height(plan);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(CalibrationPlanAccess::width(plan)) *
                                        CalibrationPlanAccess::channels(plan) * sizeof(SampleT) * 5u,
                        .operationsPerItem = static_cast<std::uint64_t>(CalibrationPlanAccess::width(plan)) *
                                             CalibrationPlanAccess::channels(plan) * 12u};
    std::atomic<std::uint64_t> written{0u};
    std::atomic<std::uint64_t> nonFinite{0u};
    std::atomic<std::uint64_t> invalid{0u};
    const Status completed = parallelFor(execution, rows, cost, [&](TaskRange range) {
        std::uint64_t localWritten = 0u;
        std::uint64_t localNonFinite = 0u;
        std::uint64_t localInvalid = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            for (std::uint32_t x = 0u; x < CalibrationPlanAccess::width(plan); ++x)
            {
                for (std::uint32_t channel = 0u; channel < CalibrationPlanAccess::channels(plan); ++channel)
                {
                    SampleT value = *sampleAddress<SampleT>(input, x, y, channel);
                    bool participates = sampleParticipates(input, value, x, y, channel);
                    if (CalibrationPlanAccess::pedestal(plan) != 0.0 && participates)
                    {
                        value =
                            static_cast<SampleT>(static_cast<double>(value) + CalibrationPlanAccess::pedestal(plan));
                    }
                    if (masters.bias.has_value())
                    {
                        const SampleT bias = *sampleAddress<SampleT>(*masters.bias, x, y, channel);
                        participates = participates && sampleParticipates(*masters.bias, bias, x, y, channel);
                        if (participates)
                        {
                            value = static_cast<SampleT>(static_cast<double>(value) - static_cast<double>(bias));
                        }
                    }
                    if (masters.dark.has_value())
                    {
                        const SampleT dark = *sampleAddress<SampleT>(*masters.dark, x, y, channel);
                        participates = participates && sampleParticipates(*masters.dark, dark, x, y, channel);
                        if (participates)
                        {
                            const SampleT scaledDark =
                                CalibrationPlanAccess::darkScale(plan) == 1.0
                                    ? dark
                                    : static_cast<SampleT>(static_cast<double>(dark) *
                                                           CalibrationPlanAccess::darkScale(plan));
                            value = static_cast<SampleT>(static_cast<double>(value) - static_cast<double>(scaledDark));
                        }
                    }
                    if (masters.flat.has_value())
                    {
                        const std::uint32_t flatChannel =
                            CalibrationPlanAccess::flatChannels(plan) == 1u ? 0u : channel;
                        SampleT divisor = *sampleAddress<SampleT>(*masters.flat, x, y, flatChannel);
                        participates = participates && sampleParticipates(*masters.flat, divisor, x, y, flatChannel);
                        if (participates &&
                            CalibrationPlanAccess::flatNormalization(plan) == FlatNormalizationMode::CentralMean)
                        {
                            const std::uint32_t group = flatGroupAt(plan, x, y, flatChannel);
                            divisor = static_cast<SampleT>(static_cast<double>(divisor) /
                                                           CalibrationPlanAccess::flatFactors(plan)[group]);
                        }
                        if (participates)
                        {
                            value =
                                divisor == static_cast<SampleT>(0)
                                    ? std::numeric_limits<SampleT>::quiet_NaN()
                                    : static_cast<SampleT>(static_cast<double>(value) / static_cast<double>(divisor));
                        }
                    }
                    if (!participates)
                    {
                        value = std::numeric_limits<SampleT>::quiet_NaN();
                        ++localInvalid;
                    }
                    *sampleAddress<SampleT>(destination, destinationLayout, x, y, channel) = value;
                    ++localWritten;
                    localNonFinite += std::isfinite(static_cast<double>(value)) ? 0u : 1u;
                }
            }
        }
        written.fetch_add(localWritten, std::memory_order_relaxed);
        nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        invalid.fetch_add(localInvalid, std::memory_order_relaxed);
        return Status::success();
    });
    if (completed.failed())
    {
        return completed;
    }
    return CalibrationResult{
        .backend = BackendSelection{.backend = ComputeBackend::Reference, .instructionSet = InstructionSet::Scalar},
        .path = CalibrationExecutionPath::Reference,
        .samplesWritten = written.load(std::memory_order_relaxed),
        .nonFiniteOutputs = nonFinite.load(std::memory_order_relaxed),
        .invalidInputs = invalid.load(std::memory_order_relaxed),
        .plannedWorkers = plannedWorkerCount(execution, rows, cost),
        .imagePasses = 1u};
}

/// Refuses a light description calibration cannot plan for, on the same rules a checked view applies.
[[nodiscard]] Status checkLightDescription(const ImageDescriptor& light)
{
    if (light.sampleType() != SampleType::Float32 && light.sampleType() != SampleType::Float64)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::Unsupported,
                                  "calibration writes a floating working image; describe the light at the "
                                  "floating representation it is calibrated in");
    }
    if (light.channelSchema() != ChannelSchema::Mono && light.channelSchema() != ChannelSchema::Rgb)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::Unsupported,
                                  "calibration accepts Mono sensor data or RGB channels");
    }
    if (light.numericDomain() == NumericDomain::EncodedNonlinear)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::Unsupported,
                                  "calibration arithmetic has no meaning in a nonlinear encoded domain");
    }
    return Status::success();
}

/// Prepares a plan for a described light, reading the light's samples only for optimized dark scaling.
///
/// @param execution the operation.
/// @param light what the light is.
/// @param input the light itself, or null when only its description is known.
/// @param masters the optional bias, dark and flat.
/// @param request the pedestal, dark rule and flat rule.
/// @return the plan or the refusal.
[[nodiscard]] Result<CalibrationPlan> preparePlan(const ExecutionContext& execution, const ImageDescriptor& light,
                                                  const CalibrationFrameView* input, const CalibrationMasters& masters,
                                                  const CalibrationRequest& request)
{
    if (!execution.valid())
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::NotInitialized,
                                  "calibration planning needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!isFlatNormalizationMode(request.flatNormalization) || !isDarkScalingMode(request.darkScaling))
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                  "the calibration request contains an unknown normalization or scaling rule");
    }
    if (!std::isfinite(request.pedestal) || request.pedestal < 0.0 || !std::isfinite(request.darkScale) ||
        request.darkScale < 0.0)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                  "pedestal and fixed dark scale must be finite and non-negative");
    }
    if (!masters.bias.has_value() && !masters.dark.has_value() && !masters.flat.has_value() && request.pedestal == 0.0)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                  "a calibration plan needs a bias, dark, flat or non-zero pedestal");
    }
    if (request.darkScaling == DarkScalingMode::OptimizeCentralRegion && !masters.dark.has_value())
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                  "optimized dark scaling needs a dark master");
    }
    if (request.darkScaling == DarkScalingMode::OptimizeCentralRegion && input == nullptr)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                  "optimized dark scaling reads the light's samples; prepare it from the light "
                                  "itself rather than from its description");
    }
    if (masters.bias.has_value())
    {
        const Status checked = checkMaster(light, *masters.bias, "bias", false);
        if (checked.failed())
        {
            return checked;
        }
    }
    if (masters.dark.has_value())
    {
        const Status checked = checkMaster(light, *masters.dark, "dark", false);
        if (checked.failed())
        {
            return checked;
        }
    }
    if (masters.flat.has_value())
    {
        const Status checked = checkMaster(light, *masters.flat, "flat", true);
        if (checked.failed())
        {
            return checked;
        }
    }

    CalibrationPlan plan{};
    CalibrationPlanAccess::width(plan) = light.extent().width();
    CalibrationPlanAccess::height(plan) = light.extent().height();
    CalibrationPlanAccess::channels(plan) = light.extent().channelCount();
    CalibrationPlanAccess::sampleType(plan) = light.sampleType();
    CalibrationPlanAccess::numericDomain(plan) = light.numericDomain();
    CalibrationPlanAccess::channelSchema(plan) = light.channelSchema();
    CalibrationPlanAccess::colorSpace(plan) = light.colorSpace();
    CalibrationPlanAccess::cfa(plan) = light.cfa();
    CalibrationPlanAccess::coordinateOrigin(plan) = light.coordinateOrigin();
    CalibrationPlanAccess::pixelAspectRatio(plan) = light.pixelAspectRatio();
    CalibrationPlanAccess::pedestal(plan) = request.pedestal;
    CalibrationPlanAccess::darkScale(plan) = request.darkScale;
    CalibrationPlanAccess::flatNormalization(plan) = request.flatNormalization;
    CalibrationPlanAccess::darkScaling(plan) = request.darkScaling;
    CalibrationPlanAccess::usesBias(plan) = masters.bias.has_value();
    CalibrationPlanAccess::usesDark(plan) = masters.dark.has_value();
    CalibrationPlanAccess::usesFlat(plan) = masters.flat.has_value();
    CalibrationPlanAccess::flatFactors(plan).fill(1.0);
    if (masters.flat.has_value())
    {
        CalibrationPlanAccess::flatChannels(plan) = masters.flat->descriptor().extent().channelCount();
        if (request.flatNormalization == FlatNormalizationMode::CentralMean)
        {
            const std::uint32_t side = cfaSide(CalibrationPlanAccess::cfa(plan));
            CalibrationPlanAccess::flatGroups(plan) =
                side != 0u ? side * side : CalibrationPlanAccess::flatChannels(plan);
            const Status normalized = CalibrationPlanAccess::sampleType(plan) == SampleType::Float32
                                          ? prepareFlatMeans<float>(execution, *masters.flat, plan)
                                          : prepareFlatMeans<double>(execution, *masters.flat, plan);
            if (normalized.failed())
            {
                return normalized;
            }
        }
    }
    if (request.darkScaling == DarkScalingMode::OptimizeCentralRegion)
    {
        if (CalibrationPlanAccess::channels(plan) != 1u ||
            CalibrationPlanAccess::width(plan) < kCalibrationDarkOptimizationSide ||
            CalibrationPlanAccess::height(plan) < kCalibrationDarkOptimizationSide)
        {
            return calibrationFailure(kPrepareOperation, ErrorCode::OutOfRange,
                                      "optimized dark scaling needs a Mono central 512 by 512 square");
        }
        DarkMoments moments{};
        const Status prepared = CalibrationPlanAccess::sampleType(plan) == SampleType::Float32
                                    ? prepareDarkMoments<float>(execution, *input, masters, plan, moments)
                                    : prepareDarkMoments<double>(execution, *input, masters, plan, moments);
        if (prepared.failed())
        {
            return prepared;
        }
        Result<double> optimized = optimizeDarkScale(moments, CalibrationPlanAccess::darkEvaluations(plan));
        if (!optimized.hasValue())
        {
            return optimized.status();
        }
        CalibrationPlanAccess::darkScale(plan) = *optimized;
        CalibrationPlanAccess::darkSamples(plan) = moments.count;
    }
    CalibrationPlanAccess::valid(plan) = true;
    return plan;
}

} // namespace

const char* flatNormalizationModeName(FlatNormalizationMode mode) noexcept
{
    switch (mode)
    {
    case FlatNormalizationMode::None:
        return "None";
    case FlatNormalizationMode::CentralMean:
        return "CentralMean";
    }
    return "UnknownFlatNormalizationMode";
}

const char* darkScalingModeName(DarkScalingMode mode) noexcept
{
    switch (mode)
    {
    case DarkScalingMode::Fixed:
        return "Fixed";
    case DarkScalingMode::OptimizeCentralRegion:
        return "OptimizeCentralRegion";
    }
    return "UnknownDarkScalingMode";
}

const char* calibrationExecutionPathName(CalibrationExecutionPath path) noexcept
{
    switch (path)
    {
    case CalibrationExecutionPath::Reference:
        return "Reference";
    case CalibrationExecutionPath::CompiledFused:
        return "CompiledFused";
    }
    return "UnknownCalibrationExecutionPath";
}

Result<CalibrationFrameView> CalibrationFrameView::create(const void* samples, std::uint64_t bufferBytes,
                                                          const ImageLayout& layout, const ImageDescriptor& descriptor,
                                                          const KernelValidityView& validity)
{
    if (samples == nullptr)
    {
        return parameterFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                "a calibration frame needs sample storage", "samples");
    }
    if (descriptor.sampleType() != SampleType::Float32 && descriptor.sampleType() != SampleType::Float64)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::Unsupported,
                                  "calibration writes a floating working image; convert an integer boundary frame "
                                  "explicitly before planning");
    }
    if (descriptor.channelSchema() != ChannelSchema::Mono && descriptor.channelSchema() != ChannelSchema::Rgb)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::Unsupported,
                                  "calibration accepts Mono sensor data or RGB channels");
    }
    if (descriptor.numericDomain() == NumericDomain::EncodedNonlinear)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::Unsupported,
                                  "calibration arithmetic has no meaning in a nonlinear encoded domain");
    }
    if (!layoutDescribes(descriptor, layout) || storageArrangementOf(layout) == StorageArrangement::SemiPlanar)
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                  "the layout does not describe this calibration frame in a planar or interleaved "
                                  "arrangement");
    }
    if (bufferBytes < layout.totalBytes())
    {
        return parameterFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                "the calibration buffer does not cover every byte named by its layout", "bufferBytes");
    }
    if (!pointerIsAligned(samples, descriptor.sampleType()))
    {
        return parameterFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                "the calibration storage is not aligned for its sample type", "samples");
    }
    const Status validityReady = checkValidityBinding(validity, descriptor.extent(), kPrepareOperation);
    if (validityReady.failed())
    {
        return validityReady;
    }
    return CalibrationFrameView{samples, bufferBytes, layout, descriptor, validity};
}

double CalibrationPlan::flatNormalizationFactor(std::uint32_t group) const noexcept
{
    return group < m_flatGroupCount ? m_flatFactors[group] : std::numeric_limits<double>::quiet_NaN();
}

std::span<const double> CalibrationPlan::flatNormalizationFactors() const noexcept
{
    return std::span<const double>(m_flatFactors.data(), m_flatGroupCount);
}

std::optional<ImageDescriptor> CalibrationPlan::description() const noexcept
{
    const std::optional<ImageExtent> extent = ImageExtent::create(m_width, m_height, m_channels, defaultEngineLimits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<ImageDescriptor> descriptor =
        ImageDescriptor::create(ImageDescriptorRequest{.extent = *extent,
                                                       .sampleType = m_sampleType,
                                                       .channelSchema = m_channelSchema,
                                                       .colorSpace = m_colorSpace,
                                                       .numericDomain = m_numericDomain,
                                                       .cfa = m_cfa,
                                                       .coordinateOrigin = m_coordinateOrigin,
                                                       .pixelAspectRatio = m_pixelAspectRatio});
    return descriptor;
}

Result<CalibrationPlan> prepareImageCalibration(const ExecutionContext& execution, const CalibrationFrameView& input,
                                                const CalibrationMasters& masters, const CalibrationRequest& request)
{
    if (!execution.valid())
    {
        return calibrationFailure(kPrepareOperation, ErrorCode::NotInitialized,
                                  "calibration planning needs an execution holding an engine-context lease");
    }
    if (!input.valid())
    {
        return parameterFailure(kPrepareOperation, ErrorCode::InvalidArgument,
                                "calibration planning needs a checked light", "input");
    }
    return preparePlan(execution, input.descriptor(), &input, masters, request);
}

Result<CalibrationPlan> prepareImageCalibration(const ExecutionContext& execution, const ImageDescriptor& light,
                                                const CalibrationMasters& masters, const CalibrationRequest& request)
{
    const Status described = checkLightDescription(light);
    if (described.failed())
    {
        return described;
    }
    return preparePlan(execution, light, nullptr, masters, request);
}

Result<CalibrationResult> calibrateImage(const ExecutionContext& execution, const CalibrationPlan& plan,
                                         const CalibrationFrameView& input, const CalibrationMasters& masters,
                                         void* destination, std::uint64_t destinationBytes,
                                         const ImageLayout& destinationLayout)
{
    if (!execution.valid() || !plan.valid())
    {
        return calibrationFailure(kExecuteOperation, ErrorCode::NotInitialized,
                                  "calibration execution needs an execution lease and prepared plan");
    }
    if (!input.valid() || !sameFrameShape(plan, input) || masters.bias.has_value() != plan.m_usesBias ||
        masters.dark.has_value() != plan.m_usesDark || masters.flat.has_value() != plan.m_usesFlat)
    {
        return calibrationFailure(kExecuteOperation, ErrorCode::IncompatibleArguments,
                                  "the light or master roles do not match the prepared plan");
    }
    if (destination == nullptr || !pointerIsAligned(destination, plan.m_sampleType))
    {
        return parameterFailure(kExecuteOperation, ErrorCode::InvalidArgument,
                                "calibration needs aligned destination storage", "destination");
    }
    if (destinationLayout.extent() != input.layout().extent() ||
        destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(plan.m_sampleType) ||
        storageArrangementOf(destinationLayout) == StorageArrangement::SemiPlanar)
    {
        return calibrationFailure(kExecuteOperation, ErrorCode::IncompatibleArguments,
                                  "the destination layout does not describe the prepared light");
    }
    if (destinationBytes < destinationLayout.totalBytes())
    {
        return parameterFailure(kExecuteOperation, ErrorCode::InvalidArgument,
                                "the destination buffer does not cover every byte named by its layout",
                                "destinationBytes");
    }
    if ((masters.bias.has_value() && checkMaster(input.descriptor(), *masters.bias, "bias", false).failed()) ||
        (masters.dark.has_value() && checkMaster(input.descriptor(), *masters.dark, "dark", false).failed()) ||
        (masters.flat.has_value() && (checkMaster(input.descriptor(), *masters.flat, "flat", true).failed() ||
                                      masters.flat->descriptor().extent().channelCount() != plan.m_flatChannels)))
    {
        return calibrationFailure(kExecuteOperation, ErrorCode::IncompatibleArguments,
                                  "one execution master no longer matches the prepared light");
    }
    if (rangesOverlap(destination, destinationLayout, input.samples(), input.layout()) ||
        (masters.bias.has_value() &&
         rangesOverlap(destination, destinationLayout, masters.bias->samples(), masters.bias->layout())) ||
        (masters.dark.has_value() &&
         rangesOverlap(destination, destinationLayout, masters.dark->samples(), masters.dark->layout())) ||
        (masters.flat.has_value() &&
         rangesOverlap(destination, destinationLayout, masters.flat->samples(), masters.flat->layout())))
    {
        return calibrationFailure(kExecuteOperation, ErrorCode::Aliasing,
                                  "calibration requires a destination disjoint from the light and every master");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    const bool eligible = canUseCompiledRows(plan, input, masters, destinationLayout);
    if (plan.m_sampleType == SampleType::Float32)
    {
        const CalibrationFloat32RunFn compiled = eligible ? calibrationFloat32RunFor(execution) : nullptr;
        if (compiled != nullptr)
        {
            Result<BackendSelection> selected = selectBackend(execution);
            if (!selected.hasValue())
            {
                return selected.status();
            }
            return runCompiledRows<float>(execution, plan, input, masters, destination, destinationLayout,
                                          CalibrationExecutionPath::CompiledFused, *selected, compiled);
        }
        if (execution.requestedBackend() == ComputeBackend::Cpu &&
            execution.backendFallback() == BackendFallback::Forbidden)
        {
            return calibrationFailure(kExecuteOperation, ErrorCode::UnsupportedBackend,
                                      "the forced CPU calibration cannot serve this CFA, validity or layout");
        }
        return runScalar<float>(execution, plan, input, masters, destination, destinationLayout);
    }
    const CalibrationFloat64RunFn compiled = eligible ? calibrationFloat64RunFor(execution) : nullptr;
    if (compiled != nullptr)
    {
        Result<BackendSelection> selected = selectBackend(execution);
        if (!selected.hasValue())
        {
            return selected.status();
        }
        return runCompiledRows<double>(execution, plan, input, masters, destination, destinationLayout,
                                       CalibrationExecutionPath::CompiledFused, *selected, compiled);
    }
    if (execution.requestedBackend() == ComputeBackend::Cpu &&
        execution.backendFallback() == BackendFallback::Forbidden)
    {
        return calibrationFailure(kExecuteOperation, ErrorCode::UnsupportedBackend,
                                  "the forced CPU calibration cannot serve this CFA, validity or layout");
    }
    return runScalar<double>(execution, plan, input, masters, destination, destinationLayout);
}

Result<MetadataTransition> imageCalibrationTransition(const CalibrationPlan& plan, const ImageSemantics& source)
{
    if (!plan.valid())
    {
        return calibrationFailure("an::imageCalibrationTransition", ErrorCode::NotInitialized,
                                  "a calibration transition needs a prepared plan");
    }
    ImageSemanticsRequest request = source.toRequest();
    request.transferFunction = TransferFunction::Linear;
    request.gammaExponent.reset();
    request.processingState.biasSubtracted = request.processingState.biasSubtracted || plan.m_usesBias;
    request.processingState.darkSubtracted = request.processingState.darkSubtracted || plan.m_usesDark;
    request.processingState.flatFielded = request.processingState.flatFielded || plan.m_usesFlat;
    request.validity = *ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly});
    Result<ImageSemantics> semantics = ImageSemantics::create(request);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::ostringstream detail;
    detail << "bias=" << (plan.m_usesBias ? "yes" : "no") << ",dark=" << (plan.m_usesDark ? "yes" : "no")
           << ",flat=" << (plan.m_usesFlat ? "yes" : "no") << ",darkScale=" << plan.m_effectiveDarkScale
           << ",flatNormalization=" << flatNormalizationModeName(plan.m_flatNormalization);
    return operationTransition(
        kImageCalibrationEffect,
        OperationOutcome{.operation = kExecuteOperation, .detail = detail.str(), .semantics = std::move(*semantics)});
}

} // namespace an
