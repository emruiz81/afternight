// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Everything true of an image except where its bytes are and what is in them.
///
/// The separate vocabularies each answer one question honestly, and this is where they meet - which
/// makes it the only place that can refuse a combination none of them could refuse alone. Lab
/// channels in a display RGB basis, a filter array over three colour channels, a physical quantity
/// in an integer sample: each of those is a sentence made of words that are individually fine.
///
/// It carries geometry and meaning and not memory. How many pixels there are is here; where they sit
/// is `Core/ImageLayout.h`, which a storage or a view pairs with a descriptor. That split is what
/// lets a descriptor survive a reshape and a layout survive a change of colour space, and it is why
/// the sample type is here as a fact about meaning while the element size lives with the addresses
/// derived from it.
///
/// There is no setter. An algorithm that changes what an image means takes the descriptor apart with
/// `toRequest`, states the change, and builds a new descriptor - so a change that breaks a rule is
/// refused where it is made, rather than travelling with the image until something reads it.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace an
{

/// Which corner of the image row zero is at.
///
/// The two conventions this engine reads disagree, and the disagreement is invisible in the pixels:
/// FITS counts rows from the bottom and every other format this engine handles counts from the top.
/// Carrying which one an image uses is what stops a vertical flip being applied twice, or not at
/// all, on the path between a decoder and a display.
enum class CoordinateOrigin : std::uint8_t
{
    /// Row zero is the top row. What TIFF, PNG, JPEG and a display buffer mean.
    TopLeft,

    /// Row zero is the bottom row. What FITS means.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    BottomLeft
};

/// Whether a value is one of the origins this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param origin the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isCoordinateOrigin(CoordinateOrigin origin) noexcept
{
    return static_cast<std::uint8_t>(origin) <= static_cast<std::uint8_t>(CoordinateOrigin::BottomLeft);
}

/// The origin's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// Thread-safe and allocation-free.
///
/// @param origin the origin to name. A value outside the enumeration is named
///        `"UnknownCoordinateOrigin"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* coordinateOriginName(CoordinateOrigin origin) noexcept;

/// What a caller states about an image, before any of it has been checked against the rest.
///
/// A plain aggregate so that a call site can name each field rather than pass eight values
/// positionally and get two of the enumerations the wrong way round. Every field is required: a
/// field left out takes its type's zero, and this engine would rather refuse a descriptor than
/// decide on a caller's behalf that an image is monochrome or that its pixels are square.
///
/// The pixel aspect is the field that catches a wholly unfilled request, because zero is the one
/// value of it that no image has.
struct ImageDescriptorRequest
{
    /// The image's pixel dimensions, already validated against the limits in force.
    ImageExtent extent;

    /// How one sample is represented.
    SampleType sampleType;

    /// What the channels are.
    ChannelSchema channelSchema;

    /// What basis the channels are expressed in.
    ColorSpace colorSpace;

    /// What the magnitudes mean.
    NumericDomain numericDomain;

    /// The colour filter array over the sensor plane, or the default no-mosaic value.
    CfaDescriptor cfa;

    /// Which corner row zero is at.
    CoordinateOrigin coordinateOrigin;

    /// The width of one pixel divided by its height, as the sensor sampled the sky. One for the
    /// square pixels almost every sensor has; anything else describes an anamorphic sampling that a
    /// resize must correct rather than preserve. Must be positive and finite.
    double pixelAspectRatio;
};

/// Everything true of an image except where its samples are and what is in them.
///
/// There is no way to build one that has not been checked: the constructor is private and the two
/// static entry points below are the only way in. So a function taking an `ImageDescriptor` knows
/// its channel count matches its schema, its schema and basis can describe the same values, its
/// domain is one its samples can carry, and any filter array it has sits over exactly one plane.
///
/// Trivially copyable and usable in a constant expression.
class ImageDescriptor
{
public:
    /// Whether a descriptor can be built from this request, and if not, why not.
    ///
    /// This is the entry point for a caller that has to report the reason - to a user, or as the
    /// context of a failure it returns. A caller that only needs the value uses `create`.
    ///
    /// The order is a contract. Every value is checked for membership of its own enumeration first,
    /// so a rule is never applied to a value it cannot interpret and an unknown enumerator is never
    /// reported as the combination rule that happened to touch it. The filter array's own rules
    /// follow, through `CfaDescriptor::check`, and then the combination rules, in the order the
    /// fields are declared, so a request with two problems reports the same one on every build. The
    /// extent is not re-checked: it was validated when it was built.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the geometry and meaning to build from.
    /// @return `DescriptorError::None` when a descriptor can be built; otherwise the first rule that
    ///         refused.
    [[nodiscard]] static constexpr DescriptorError check(const ImageDescriptorRequest& request) noexcept
    {
        if (!isSampleType(request.sampleType))
        {
            return DescriptorError::UnknownSampleType;
        }
        if (!isChannelSchema(request.channelSchema))
        {
            return DescriptorError::UnknownChannelSchema;
        }
        if (!isColorSpace(request.colorSpace))
        {
            return DescriptorError::UnknownColorSpace;
        }
        if (!isNumericDomain(request.numericDomain))
        {
            return DescriptorError::UnknownNumericDomain;
        }
        if (!isCoordinateOrigin(request.coordinateOrigin))
        {
            return DescriptorError::UnknownCoordinateOrigin;
        }

        const DescriptorError cfaError = CfaDescriptor::check(request.cfa);
        if (cfaError != DescriptorError::None)
        {
            return cfaError;
        }

        const std::optional<std::uint32_t> fixedChannels = channelCountOf(request.channelSchema);
        if (fixedChannels.has_value() && *fixedChannels != request.extent.channelCount())
        {
            return DescriptorError::ChannelCountMismatch;
        }
        if (!isColorSpaceCompatible(request.channelSchema, request.colorSpace))
        {
            return DescriptorError::ColorSpaceMismatch;
        }
        if (!isNumericDomainCompatible(request.colorSpace, request.numericDomain))
        {
            return DescriptorError::NumericDomainMismatch;
        }
        if (!isNumericDomainRepresentable(request.numericDomain, request.sampleType))
        {
            return DescriptorError::NumericDomainNotRepresentable;
        }
        if (request.cfa.isMosaiced())
        {
            if (request.extent.channelCount() != 1u)
            {
                return DescriptorError::CfaRequiresSingleChannel;
            }
            if (request.channelSchema != ChannelSchema::Mono)
            {
                return DescriptorError::CfaRequiresMonoSchema;
            }
        }
        if (!isPositiveFinite(request.pixelAspectRatio))
        {
            return DescriptorError::PixelAspectNotPositive;
        }
        return DescriptorError::None;
    }

    /// A descriptor for this request, where it describes one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the geometry and meaning to build from.
    /// @return the descriptor, or an empty optional exactly when `check` would name a reason.
    ///         Nothing is allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<ImageDescriptor> create(const ImageDescriptorRequest& request) noexcept
    {
        if (check(request) != DescriptorError::None)
        {
            return std::nullopt;
        }
        return ImageDescriptor{request};
    }

    /// The image's pixel dimensions.
    ///
    /// @return the validated extent, whose channel count agrees with the channel schema.
    [[nodiscard]] constexpr const ImageExtent& extent() const noexcept
    {
        return m_extent;
    }

    /// How one sample is represented.
    ///
    /// @return the validated sample type.
    [[nodiscard]] constexpr SampleType sampleType() const noexcept
    {
        return m_sampleType;
    }

    /// What the channels are.
    ///
    /// @return the validated channel schema.
    [[nodiscard]] constexpr ChannelSchema channelSchema() const noexcept
    {
        return m_channelSchema;
    }

    /// What basis the channels are expressed in.
    ///
    /// @return the validated colour space.
    [[nodiscard]] constexpr ColorSpace colorSpace() const noexcept
    {
        return m_colorSpace;
    }

    /// What the magnitudes mean.
    ///
    /// @return the validated numeric domain.
    [[nodiscard]] constexpr NumericDomain numericDomain() const noexcept
    {
        return m_numericDomain;
    }

    /// The colour filter array over the sensor plane.
    ///
    /// @return the validated filter array, which is the no-mosaic default for every image whose
    ///         mosaic has been resolved or never existed.
    [[nodiscard]] constexpr const CfaDescriptor& cfa() const noexcept
    {
        return m_cfa;
    }

    /// Which corner row zero is at.
    ///
    /// @return the validated coordinate origin.
    [[nodiscard]] constexpr CoordinateOrigin coordinateOrigin() const noexcept
    {
        return m_coordinateOrigin;
    }

    /// The width of one pixel divided by its height.
    ///
    /// @return the validated ratio, always positive and finite.
    [[nodiscard]] constexpr double pixelAspectRatio() const noexcept
    {
        return m_pixelAspectRatio;
    }

    /// Whether this image still has its filter array unresolved.
    ///
    /// @return true when the filter array names a pattern, which construction proved means one
    ///         channel of plain intensity.
    [[nodiscard]] constexpr bool isMosaiced() const noexcept
    {
        return m_cfa.isMosaiced();
    }

    /// The request this descriptor was built from.
    ///
    /// The way a change of meaning is expressed: read the request, change what the operation
    /// changes, and build a new descriptor from it. The change is then judged by the same rules the
    /// original passed, at the point the operation makes it.
    ///
    /// @return a request that `create` accepts and that produces an equal descriptor.
    [[nodiscard]] constexpr ImageDescriptorRequest toRequest() const noexcept
    {
        return ImageDescriptorRequest{.extent = m_extent,
                                      .sampleType = m_sampleType,
                                      .channelSchema = m_channelSchema,
                                      .colorSpace = m_colorSpace,
                                      .numericDomain = m_numericDomain,
                                      .cfa = m_cfa,
                                      .coordinateOrigin = m_coordinateOrigin,
                                      .pixelAspectRatio = m_pixelAspectRatio};
    }

    /// Two descriptors are equal when every field is equal.
    ///
    /// The pixel aspect participates as the floating value it is, so two descriptors that differ
    /// only by a ratio computed two different ways compare unequal. That is the intended answer:
    /// this comparison decides whether two images may be treated as describing the same thing, and
    /// a difference too small to see is still a difference somebody introduced.
    friend constexpr bool operator==(const ImageDescriptor& left, const ImageDescriptor& right) noexcept = default;

private:
    /// Builds the value once the request has been checked. Private, so that every descriptor in
    /// existence has been through `check`.
    ///
    /// @param request the checked request.
    constexpr explicit ImageDescriptor(const ImageDescriptorRequest& request) noexcept
        : m_extent(request.extent), m_cfa(request.cfa), m_pixelAspectRatio(request.pixelAspectRatio),
          m_sampleType(request.sampleType), m_channelSchema(request.channelSchema), m_colorSpace(request.colorSpace),
          m_numericDomain(request.numericDomain), m_coordinateOrigin(request.coordinateOrigin)
    {
    }

    /// Whether a ratio is a number a pixel could have, without leaving a constant expression.
    ///
    /// `std::isfinite` is not usable here, so the two comparisons do the work: not-a-number fails
    /// the first because every comparison against it is false, and both infinities fail one of
    /// them.
    ///
    /// @param value the ratio to test.
    /// @return true when the value is positive, finite and not zero.
    [[nodiscard]] static constexpr bool isPositiveFinite(double value) noexcept
    {
        return value > 0.0 && value <= (std::numeric_limits<double>::max)();
    }

    /// The image's validated pixel dimensions.
    ImageExtent m_extent;

    /// The validated colour filter array.
    CfaDescriptor m_cfa;

    /// The validated pixel aspect ratio.
    double m_pixelAspectRatio;

    /// The validated sample representation.
    SampleType m_sampleType;

    /// The validated channel schema.
    ChannelSchema m_channelSchema;

    /// The validated colour basis.
    ColorSpace m_colorSpace;

    /// The validated numeric domain.
    NumericDomain m_numericDomain;

    /// The validated coordinate origin.
    CoordinateOrigin m_coordinateOrigin;
};

} // namespace an
