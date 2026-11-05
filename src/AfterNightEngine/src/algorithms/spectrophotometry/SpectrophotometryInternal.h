// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what the spectrophotometry units share: failures and a typed reader over one
// channel of a checked frame.

#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace an::spectrophotometry_detail
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

/// Whether a checked frame is three-channel RGB.
///
/// @param frame the checked frame.
/// @return true when the descriptor's schema is RGB with three channels.
[[nodiscard]] inline bool isRgbFrame(const StarFrameView& frame) noexcept
{
    return frame.valid() && frame.descriptor().channelSchema() == ChannelSchema::Rgb &&
           frame.descriptor().extent().channelCount() == 3u;
}

/// Reads one channel of a checked frame as doubles, honouring its validity rule and mask.
///
/// The same reader the photometry, comet and star domains carry; a domain may not reach another
/// domain's private headers, so each states it.
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

    /// Reads one sample as a double when the frame says it means something.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @param value receives the sample.
    /// @return true when the sample was admitted and `value` set.
    [[nodiscard]] bool read(const std::uint32_t x, const std::uint32_t y, double& value) const noexcept
    {
        const ImageLayout& layout = m_frame->layout();
        const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(m_frame->samples());
        const std::uint64_t offset = static_cast<std::uint64_t>(m_plane) * layout.planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                     static_cast<std::uint64_t>(m_sampleInPixel) * sizeof(SampleT);
        const SampleT* const row = reinterpret_cast<const SampleT*>(bytes + offset);
        const SampleT sample = row[static_cast<std::size_t>(x) * layout.samplesPerPixel()];
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

} // namespace an::spectrophotometry_detail
