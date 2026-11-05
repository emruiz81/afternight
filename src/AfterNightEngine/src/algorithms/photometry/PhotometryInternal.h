// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what the photometry units share: failures, exact small-sample ranks and a
// typed reader over one channel of a checked frame.

#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

/// The helpers the photometry implementation files share: failure builders, exact small-sample
/// medians and a validity-honouring reader over one channel of a checked frame.
namespace an::photometry_detail
{

/// Builds one described failure in the algorithms domain.
///
/// @param code why the operation failed.
/// @param message what a person reads.
/// @param operation the entry point speaking.
/// @return the failure.
[[nodiscard]] inline Status failure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Builds a ceiling failure carrying both counts.
///
/// @param message what a person reads.
/// @param operation the entry point speaking.
/// @param requested how much the caller supplied.
/// @param limit how much this build accepts.
/// @return the failure.
[[nodiscard]] inline Status limitFailure(const char* const message, const char* const operation,
                                         const std::uint64_t requested, const std::uint64_t limit)
{
    return Status::failure(ErrorCode::LimitExceeded,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                    StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

/// Whether both coordinates of a position are finite numbers.
///
/// @param x the horizontal position.
/// @param y the vertical position.
/// @return true when both are finite.
[[nodiscard]] inline bool finitePosition(const double x, const double y) noexcept
{
    return std::isfinite(x) && std::isfinite(y);
}

/// The exact median of a non-empty range that this call may reorder.
///
/// The upper middle is selected by partition; for an even count the largest value below it is then
/// found by a linear scan, so the answer is the value an ascending sort would place there and does
/// not depend on the partition's permutation.
///
/// @param values scratch values, reordered by this call. Must not be empty.
/// @return the median.
[[nodiscard]] inline double selectMedian(std::span<double> values) noexcept
{
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    double result = values[middle];
    if ((values.size() % 2u) == 0u)
    {
        const auto lower = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
        result = (*lower + result) * 0.5;
    }
    return result;
}

/// The median absolute deviation of a range about a stated centre.
///
/// @param values the values, which are only read.
/// @param center the centre the deviations are taken from.
/// @param scratch storage at least as long as `values`, reordered by this call.
/// @return the median of `|value - center|`.
[[nodiscard]] inline double medianAbsoluteDeviation(std::span<const double> values, const double center,
                                                    std::span<double> scratch) noexcept
{
    for (std::size_t index = 0u; index < values.size(); ++index)
    {
        scratch[index] = std::abs(values[index] - center);
    }
    return selectMedian(scratch.first(values.size()));
}

/// Reads one channel of a checked frame as doubles, honouring its validity rule and mask.
///
/// The same reader the comet and star domains use, restated here because a domain may not reach
/// another domain's private headers. A read returns false for a sample the frame says means
/// nothing, so a caller never has to know which of the five rules it was.
///
/// @tparam SampleT the frame's resident representation.
template <typename SampleT> class ChannelReader
{
public:
    /// Binds one channel of one frame.
    ///
    /// @param frame the checked frame.
    /// @param channel which channel, below the frame's channel count.
    ChannelReader(const StarFrameView& frame, const std::uint32_t channel) noexcept
        : m_frame(&frame), m_channel(channel), m_plane(channel / frame.layout().samplesPerPixel()),
          m_sampleInPixel(channel % frame.layout().samplesPerPixel())
    {
    }

    /// The typed row of this channel.
    ///
    /// @param y the row.
    /// @return the first sample of the row; consecutive pixels are `samplesPerPixel()` apart.
    [[nodiscard]] const SampleT* row(const std::uint32_t y) const noexcept
    {
        const ImageLayout& layout = m_frame->layout();
        const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(m_frame->samples());
        const std::uint64_t offset = static_cast<std::uint64_t>(m_plane) * layout.planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                     static_cast<std::uint64_t>(m_sampleInPixel) * sizeof(SampleT);
        return reinterpret_cast<const SampleT*>(bytes + offset);
    }

    /// How many samples apart consecutive pixels of a row are.
    ///
    /// @return the stride in samples.
    [[nodiscard]] std::uint32_t pixelStride() const noexcept
    {
        return m_frame->layout().samplesPerPixel();
    }

    /// Whether the frame's validity rule and mask admit one sample.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @param sample the sample's own value.
    /// @return true when the sample means something.
    [[nodiscard]] bool admits(const std::uint32_t x, const std::uint32_t y, const SampleT sample) const noexcept
    {
        const KernelValidityView& validity = m_frame->validity();
        if (!sampleIsValidByValue(validity.descriptor, sample))
        {
            return false;
        }
        if (validity.mask != nullptr)
        {
            const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
            const std::uint32_t plane = mask.isShared() ? 0u : m_channel;
            const std::uint64_t maskOffset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes() +
                                             static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
            if ((mask.data()[maskOffset] & static_cast<std::uint8_t>(1u << (x % 8u))) == 0u)
            {
                return false;
            }
        }
        return true;
    }

    /// Reads one sample as a double when the frame says it means something.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @param value receives the sample.
    /// @return true when the sample was admitted and `value` set.
    [[nodiscard]] bool read(const std::uint32_t x, const std::uint32_t y, double& value) const noexcept
    {
        const SampleT sample = row(y)[static_cast<std::size_t>(x) * pixelStride()];
        if (!admits(x, y, sample))
        {
            return false;
        }
        value = static_cast<double>(sample);
        return true;
    }

private:
    /// The frame being read.
    const StarFrameView* m_frame = nullptr;

    /// Which channel.
    std::uint32_t m_channel = 0u;

    /// Which stored plane holds it.
    std::uint32_t m_plane = 0u;

    /// Which sample of a pixel it is inside that plane.
    std::uint32_t m_sampleInPixel = 0u;
};

/// Whether a checked frame is three-channel RGB, which every colour photometry needs.
///
/// @param frame the checked frame.
/// @return true when the descriptor's schema is RGB with three channels.
[[nodiscard]] inline bool isRgbFrame(const StarFrameView& frame) noexcept
{
    return frame.valid() && frame.descriptor().channelSchema() == ChannelSchema::Rgb &&
           frame.descriptor().extent().channelCount() == 3u;
}

} // namespace an::photometry_detail
