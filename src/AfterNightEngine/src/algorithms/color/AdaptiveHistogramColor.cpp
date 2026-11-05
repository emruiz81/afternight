// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - colour-preserving CLAHE: RGB to Lab, equalize the lightness, Lab back to RGB.
//
// One context-accounted block holds everything the composition owns: the three-plane Lab working
// image, a separate lightness plane for the equalized result, and - when the caller's mask has one
// plane per channel - a packed validity plane that is the AND of the three. The equalized plane is
// separate because `equalizeImageAdaptiveHistogram` needs a destination disjoint from its source; it
// starts as a copy of `L`, so the centres CLAHE leaves unwritten keep their source lightness, and it
// is copied back into the Lab image before the inverse conversion.
//
// A plan is a value a caller can also build through its public constructor, so every execution
// re-plans from the plan's request and runs the conversions it rebuilt rather than the ones the plan
// carries.

#include <AfterNightEngine/Algorithms/Color/AdaptiveHistogramColor.h>

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planPerceptualAdaptiveHistogramEqualization";
constexpr const char* kExecuteOperation = "an::equalizeImagePerceptualAdaptiveHistogram";

[[nodiscard]] Status colorHistogramFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] ColorEncoding naturalLab(ChromaticWhitePoint whitePoint) noexcept
{
    return ColorEncoding{.schema = ChannelSchema::Lab,
                         .primaries = RgbPrimaries::Srgb,
                         .transfer = ColorTransferFunction::Linear,
                         .whitePoint = whitePoint,
                         .perceptual = PerceptualEncoding::NaturalUnits};
}

[[nodiscard]] Result<ImageLayout> planarLayout(const ImageExtent& extent, std::uint32_t elementBytes,
                                               const char* operation)
{
    const std::optional<ImageLayout> layout = ImageLayout::create({.extent = extent,
                                                                   .elementSizeBytes = elementBytes,
                                                                   .samplesPerPixel = 1u,
                                                                   .planeCount = extent.channelCount(),
                                                                   .rowAlignmentBytes = kDefaultBufferAlignmentBytes},
                                                                  defaultEngineLimits());
    if (!layout.has_value())
    {
        return colorHistogramFailure(ErrorCode::LimitExceeded, operation,
                                     "the context-owned planar working layout is not representable");
    }
    return *layout;
}

[[nodiscard]] Status checkInvocation(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                     const ImageLayout& sourceLayout, void* destination,
                                     const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return colorHistogramFailure(ErrorCode::NotInitialized, kExecuteOperation,
                                     "perceptual CLAHE needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if ((sampleType != SampleType::Float32 && sampleType != SampleType::Float64) || source == nullptr ||
        destination == nullptr)
    {
        return colorHistogramFailure(ErrorCode::InvalidArgument, kExecuteOperation,
                                     "perceptual CLAHE needs non-null Float32 or Float64 images");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (sourceLayout.elementSizeBytes() != elementBytes || destinationLayout.elementSizeBytes() != elementBytes ||
        sourceLayout.extent() != destinationLayout.extent() || sourceLayout.extent().channelCount() != 3u)
    {
        return colorHistogramFailure(ErrorCode::IncompatibleArguments, kExecuteOperation,
                                     "perceptual CLAHE needs two layouts for the same three-channel image");
    }
    return Status::success();
}

} // namespace

PerceptualAdaptiveHistogramPlan::PerceptualAdaptiveHistogramPlan(const PerceptualAdaptiveHistogramRequest& request,
                                                                 const ColorConversionPlan& toLab,
                                                                 const ColorConversionPlan& fromLab) noexcept
    : m_request(request), m_toLab(toLab), m_fromLab(fromLab)
{
}

const PerceptualAdaptiveHistogramRequest& PerceptualAdaptiveHistogramPlan::request() const noexcept
{
    return m_request;
}

const ColorConversionPlan& PerceptualAdaptiveHistogramPlan::toLab() const noexcept
{
    return m_toLab;
}

const ColorConversionPlan& PerceptualAdaptiveHistogramPlan::fromLab() const noexcept
{
    return m_fromLab;
}

Result<PerceptualAdaptiveHistogramPlan>
planPerceptualAdaptiveHistogramEqualization(const PerceptualAdaptiveHistogramRequest& request)
{
    if (checkColorEncoding(request.rgbEncoding) != ColorEncodingError::None ||
        request.rgbEncoding.schema != ChannelSchema::Rgb)
    {
        return colorHistogramFailure(ErrorCode::IncompatibleArguments, kPlanOperation,
                                     "perceptual CLAHE needs one valid three-channel RGB encoding");
    }
    const ColorEncoding lab = naturalLab(request.rgbEncoding.whitePoint);
    Result<ColorConversionPlan> toLab = planColorConversion(request.rgbEncoding, lab);
    if (!toLab.hasValue())
    {
        return toLab.status();
    }
    Result<ColorConversionPlan> fromLab = planColorConversion(lab, request.rgbEncoding);
    if (!fromLab.hasValue())
    {
        return fromLab.status();
    }
    Result<AdaptiveHistogramPlan> lightness = planAdaptiveHistogramEqualization({.tileColumns = request.tileColumns,
                                                                                 .tileRows = request.tileRows,
                                                                                 .binCount = request.binCount,
                                                                                 .clipLimit = request.clipLimit,
                                                                                 .lowest = 0.0,
                                                                                 .highest = 100.0,
                                                                                 .output = request.output});
    if (!lightness.hasValue())
    {
        return lightness.status();
    }
    return PerceptualAdaptiveHistogramPlan{request, *toLab, *fromLab};
}

Result<PerceptualAdaptiveHistogramResult>
equalizeImagePerceptualAdaptiveHistogram(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                         const ImageLayout& sourceLayout, void* destination,
                                         const ImageLayout& destinationLayout,
                                         const PerceptualAdaptiveHistogramPlan& plan, const SpatialSelection& selection)
{
    const Status invocation =
        checkInvocation(execution, sampleType, source, sourceLayout, destination, destinationLayout);
    if (invocation.failed())
    {
        return invocation;
    }
    Result<PerceptualAdaptiveHistogramPlan> checked = planPerceptualAdaptiveHistogramEqualization(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    Result<AdaptiveHistogramPlan> lightness =
        planAdaptiveHistogramEqualization({.tileColumns = checked->request().tileColumns,
                                           .tileRows = checked->request().tileRows,
                                           .binCount = checked->request().binCount,
                                           .clipLimit = checked->request().clipLimit,
                                           .lowest = 0.0,
                                           .highest = 100.0,
                                           .output = checked->request().output});
    if (!lightness.hasValue())
    {
        return lightness.status();
    }

    const ImageExtent rgbExtent = sourceLayout.extent();
    const std::optional<ImageExtent> lightnessExtent =
        ImageExtent::create(rgbExtent.width(), rgbExtent.height(), 1u, defaultEngineLimits());
    if (!lightnessExtent.has_value())
    {
        return colorHistogramFailure(ErrorCode::LimitExceeded, kExecuteOperation,
                                     "the lightness working extent is not representable");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    Result<ImageLayout> labLayout = planarLayout(rgbExtent, elementBytes, kExecuteOperation);
    Result<ImageLayout> lightnessLayout = planarLayout(*lightnessExtent, elementBytes, kExecuteOperation);
    if (!labLayout.hasValue())
    {
        return labLayout.status();
    }
    if (!lightnessLayout.hasValue())
    {
        return lightnessLayout.status();
    }

    if (selection.validity.rule() == ValidityRule::ExplicitMask && selection.mask == nullptr)
    {
        return colorHistogramFailure(ErrorCode::InvalidArgument, kExecuteOperation,
                                     "an explicit validity rule needs packed source bits");
    }
    if (selection.validity.rule() != ValidityRule::ExplicitMask && selection.mask != nullptr)
    {
        return colorHistogramFailure(ErrorCode::InvalidArgument, kExecuteOperation,
                                     "packed source bits are only read by the explicit-mask validity rule");
    }
    const bool combineMask = selection.mask != nullptr && selection.mask->planeCount() == rgbExtent.channelCount();
    if (selection.mask != nullptr &&
        (selection.mask->width() != rgbExtent.width() || selection.mask->height() != rgbExtent.height() ||
         (selection.mask->planeCount() != 1u && selection.mask->planeCount() != rgbExtent.channelCount())))
    {
        return colorHistogramFailure(ErrorCode::IncompatibleArguments, kExecuteOperation,
                                     "the source validity mask does not cover this three-channel image");
    }

    const std::uint64_t combinedMaskBytes =
        combineMask ? packedMaskRowBytes(rgbExtent.width()) * rgbExtent.height() : 0u;
    const std::uint64_t labBytes = labLayout->totalBytes();
    const std::uint64_t lightnessBytes = lightnessLayout->totalBytes();
    const std::optional<std::uint64_t> workingBytes =
        (CheckedSize{labBytes} + CheckedSize{lightnessBytes} + CheckedSize{combinedMaskBytes}).value();
    if (!workingBytes.has_value())
    {
        return colorHistogramFailure(ErrorCode::LimitExceeded, kExecuteOperation,
                                     "the perceptual CLAHE working-byte count overflows");
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, *workingBytes, kDefaultBufferAlignmentBytes, kExecuteOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const lab = static_cast<std::uint8_t*>(memory->data());
    auto* const equalizedLightness = lab + labBytes;
    auto* const combinedMaskBits = equalizedLightness + lightnessBytes;

    Status status = convertImageColor(execution, checked->toLab(), sampleType, source, sourceLayout, lab, *labLayout);
    if (status.failed())
    {
        return status;
    }
    for (std::uint32_t y = 0u; y < rgbExtent.height(); ++y)
    {
        std::memcpy(equalizedLightness + static_cast<std::uint64_t>(y) * lightnessLayout->rowStrideBytes(),
                    lab + static_cast<std::uint64_t>(y) * labLayout->rowStrideBytes(),
                    static_cast<std::size_t>(rgbExtent.width()) * elementBytes);
    }

    SpatialSelection lightnessSelection{.validity = selection.validity};
    std::optional<ValidityMaskView<const std::uint8_t>> borrowedLightnessMask;
    std::optional<ValidityMaskView<std::uint8_t>> writableCombinedMask;
    if (selection.mask != nullptr && !combineMask)
    {
        borrowedLightnessMask = ValidityMaskView<const std::uint8_t>::create(
            std::span<const std::uint8_t>{selection.mask->data(),
                                          static_cast<std::size_t>(selection.mask->spannedByteCount())},
            *lightnessExtent, selection.mask->rowStrideBytes(), selection.mask->planeStrideBytes());
        if (!borrowedLightnessMask.has_value())
        {
            return colorHistogramFailure(ErrorCode::IncompatibleArguments, kExecuteOperation,
                                         "the shared source validity plane cannot describe lightness");
        }
        lightnessSelection.mask = &*borrowedLightnessMask;
    }
    else if (combineMask)
    {
        writableCombinedMask = ValidityMaskView<std::uint8_t>::createTight(
            std::span<std::uint8_t>{combinedMaskBits, static_cast<std::size_t>(combinedMaskBytes)}, *lightnessExtent);
        if (!writableCombinedMask.has_value())
        {
            return colorHistogramFailure(ErrorCode::LimitExceeded, kExecuteOperation,
                                         "the combined lightness validity plane is not representable");
        }
        writableCombinedMask->fill(false);
        status = parallelFor(execution, rgbExtent.height(), TaskCost{.bytesPerItem = rgbExtent.width()},
                             [&](TaskRange range) {
                                 for (std::uint64_t row = range.begin; row < range.end; ++row)
                                 {
                                     const std::uint32_t y = static_cast<std::uint32_t>(row);
                                     for (std::uint32_t x = 0u; x < rgbExtent.width(); ++x)
                                     {
                                         bool valid = true;
                                         for (std::uint32_t plane = 0u; plane < rgbExtent.channelCount(); ++plane)
                                         {
                                             valid = valid && selection.mask->test(plane, x, y).value_or(false);
                                         }
                                         static_cast<void>(writableCombinedMask->setValid(0u, x, y, valid));
                                     }
                                 }
                                 return Status::success();
                             });
        if (status.failed())
        {
            return status;
        }
        borrowedLightnessMask = writableCombinedMask->constView();
        lightnessSelection.mask = &*borrowedLightnessMask;
    }

    Result<AdaptiveHistogramResult> equalized =
        equalizeImageAdaptiveHistogram(execution, sampleType, lab, *lightnessLayout, equalizedLightness,
                                       *lightnessLayout, *lightness, lightnessSelection);
    if (!equalized.hasValue())
    {
        return equalized.status();
    }
    for (std::uint32_t y = 0u; y < rgbExtent.height(); ++y)
    {
        std::memcpy(lab + static_cast<std::uint64_t>(y) * labLayout->rowStrideBytes(),
                    equalizedLightness + static_cast<std::uint64_t>(y) * lightnessLayout->rowStrideBytes(),
                    static_cast<std::size_t>(rgbExtent.width()) * elementBytes);
    }
    status =
        convertImageColor(execution, checked->fromLab(), sampleType, lab, *labLayout, destination, destinationLayout);
    if (status.failed())
    {
        return status;
    }
    const std::optional<std::uint64_t> peakWorkingBytes =
        (CheckedSize{equalized->filter.workingBytes} + CheckedSize{memory->bytes()}).value();
    if (!peakWorkingBytes.has_value())
    {
        return colorHistogramFailure(ErrorCode::LimitExceeded, kExecuteOperation,
                                     "the reported perceptual CLAHE working-byte count overflows");
    }
    PerceptualAdaptiveHistogramResult answer{
        .filter = equalized->filter, .tileCount = equalized->tileCount, .binCount = equalized->binCount};
    answer.filter.workingBytes = *peakWorkingBytes;
    return answer;
}

} // namespace an
