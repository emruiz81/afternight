// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A frame as a boundary produced it, described rather than reinterpreted.
///
/// A decoder that converts to float on load and flattens interleaved data into what the algorithms
/// expect loses the fact that any of that happened. The cost is not the conversion - it is that
/// afterwards nothing can say what the file held, so an encoder writing the frame back guesses, and a
/// caller that wanted the original representation has no way to ask for it.
///
/// A raw frame is the alternative: the bytes exactly as they arrived, and a description complete
/// enough that nothing has to be inferred from them. Its representation may be one this engine
/// stores or one it does not, its byte order is stated rather than assumed native, its rows may be
/// padded by whoever wrote them, and the memory may belong to the engine, to a mapping, to a
/// decoding library or to the caller.
///
/// **Adoption is the point of the ownership distinction.** A frame whose representation is one of
/// the eight, whose bytes are in host order, and whose memory came from the engine can become an
/// `Image` with no copy and no conversion at all - the buffer moves and nothing is touched.
/// `checkAdoptable` is where that is decided, once, with the reason named when the answer is no.
///
/// **A representation outside the eight is carried, not converted.** Eight bytes of signed integer
/// is a real file format and nothing this engine stores; such a frame can be described, measured,
/// copied and written back out unchanged, and what it cannot do is silently become a processing
/// image. That refusal is `RawFrameDescriptor::residentSampleType` returning nothing, and every path
/// into the typed model goes through it.
///
/// The description is deliberately the same vocabulary the resident model uses - the channel schema,
/// the colour basis, the numeric domain, the filter array and its phase, the coordinate origin - and
/// is refused for the same reasons, so a boundary and an image cannot describe the same picture in
/// two different languages. What it adds is the sample format and the byte order, which are the two
/// facts a resident image never has to carry because it is always in the host's order and always one
/// of the eight.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace an
{

/// What a decoder states about a frame, before any of it has been checked against the rest.
///
/// A plain aggregate for the reason `ImageDescriptorRequest` is one: a call site names each field
/// rather than passing nine values positionally and getting two of the enumerations the wrong way
/// round. Every field is required, and the pixel aspect is the field that catches a wholly unfilled
/// request, because zero is the one value of it that no frame has.
struct RawFrameDescriptorRequest
{
    /// The frame's pixel dimensions, already validated against the limits in force.
    ImageExtent extent;

    /// How wide one sample is and what kind of value it holds. May name a representation this
    /// engine does not store.
    RawSampleFormat sampleFormat;

    /// In which order the bytes of one sample sit. `ByteOrder::Native` says no swap is needed, and
    /// is the only admissible value for a one-byte sample.
    ByteOrder byteOrder;

    /// What the channels are.
    ChannelSchema channelSchema;

    /// What basis the channels are expressed in.
    ColorSpace colorSpace;

    /// What the magnitudes mean.
    NumericDomain numericDomain;

    /// The colour filter array over the sensor plane, or the default no-mosaic value.
    CfaDescriptor cfa;

    /// Which corner row zero is at. A decoder states what its format defines rather than what the
    /// engine prefers, which is the whole reason this field exists at a boundary.
    CoordinateOrigin coordinateOrigin;

    /// The width of one pixel divided by its height, as the sensor sampled the sky. Must be
    /// positive and finite.
    double pixelAspectRatio;
};

/// Everything true of a frame at a boundary except where its bytes are and what is in them.
///
/// There is no way to build one that has not been checked: the constructor is private and the two
/// static entry points below are the only way in. So a function taking a `RawFrameDescriptor` knows
/// its sample format is one this engine will describe, its byte order is meaningful for that
/// format, its channel count matches its schema, its schema and basis can describe the same values,
/// its numeric domain suits its basis and - when physically scaled - is held in floating point, and
/// any filter array it has sits over exactly one plane of a mono schema.
///
/// It carries meaning and not memory, exactly as `ImageDescriptor` does. Where the bytes sit is an
/// `ImageLayout`, which `RawFrame` pairs with this.
///
/// Trivially copyable and usable in a constant expression.
class RawFrameDescriptor
{
public:
    /// Whether a descriptor can be built from this request, and if not, why not.
    ///
    /// The order is a contract, and it is the order `ImageDescriptor::check` uses for the fields
    /// they share. Every value is checked for membership of its own enumeration first, so a rule is
    /// never applied to a value it cannot interpret; the combination rules follow in the order the
    /// fields are declared, so a request with two problems reports the same one on every build.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the geometry and meaning to build from.
    /// @return `DescriptorError::None` when a descriptor can be built; otherwise the first rule
    ///         that refused.
    [[nodiscard]] static constexpr DescriptorError check(const RawFrameDescriptorRequest& request) noexcept
    {
        if (!isRawSampleClass(request.sampleFormat.sampleClass))
        {
            return DescriptorError::UnknownRawSampleClass;
        }
        if (!isRawSampleSize(request.sampleFormat.sizeBytes))
        {
            return DescriptorError::RawSampleSizeNotSupported;
        }
        if (!isByteOrder(request.byteOrder))
        {
            return DescriptorError::UnknownByteOrder;
        }
        if (!isRawByteOrderMeaningful(request.sampleFormat) && request.byteOrder != ByteOrder::Native)
        {
            return DescriptorError::ByteOrderNotMeaningful;
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
        if (!isCfaPattern(request.cfa.pattern()))
        {
            return DescriptorError::UnknownCfaPattern;
        }
        if (!isCoordinateOrigin(request.coordinateOrigin))
        {
            return DescriptorError::UnknownCoordinateOrigin;
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
        if (request.numericDomain == NumericDomain::PhysicalScaled &&
            request.sampleFormat.sampleClass != RawSampleClass::FloatingPoint)
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

    /// A descriptor built from this request, when one can be.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the geometry and meaning to build from.
    /// @return the descriptor, or an empty optional exactly when `check` names a reason.
    [[nodiscard]] static constexpr std::optional<RawFrameDescriptor>
    create(const RawFrameDescriptorRequest& request) noexcept
    {
        if (check(request) != DescriptorError::None)
        {
            return std::nullopt;
        }
        return RawFrameDescriptor{request};
    }

    /// The boundary description of a resident image, for the frame a writer is handed.
    ///
    /// The inverse of `toImageDescriptor`, and the way back out of the processing model: an image's
    /// geometry, representation, channels, basis, domain, filter array, origin and pixel aspect, with
    /// the bytes stated in the host's own order because an image's samples always are. A caller writing
    /// what an operation computed needs exactly this, and without it spells the same nine facts out
    /// field by field in a second type.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param descriptor the resident image's description.
    /// @return the frame description, whose `toImageDescriptor` is `descriptor` again; an empty optional
    ///         only if a rule this descriptor applies were ever stricter than the image descriptor's.
    [[nodiscard]] static constexpr std::optional<RawFrameDescriptor>
    fromImageDescriptor(const ImageDescriptor& descriptor) noexcept
    {
        return create(RawFrameDescriptorRequest{.extent = descriptor.extent(),
                                                .sampleFormat = rawSampleFormatOf(descriptor.sampleType()),
                                                .byteOrder = ByteOrder::Native,
                                                .channelSchema = descriptor.channelSchema(),
                                                .colorSpace = descriptor.colorSpace(),
                                                .numericDomain = descriptor.numericDomain(),
                                                .cfa = descriptor.cfa(),
                                                .coordinateOrigin = descriptor.coordinateOrigin(),
                                                .pixelAspectRatio = descriptor.pixelAspectRatio()});
    }

    /// The frame's pixel dimensions.
    ///
    /// @return the validated extent.
    [[nodiscard]] constexpr const ImageExtent& extent() const noexcept
    {
        return m_extent;
    }

    /// How wide one sample is and what kind of value it holds.
    ///
    /// @return the boundary format, which may name a representation this engine does not store.
    [[nodiscard]] constexpr const RawSampleFormat& sampleFormat() const noexcept
    {
        return m_sampleFormat;
    }

    /// In which order the bytes of one sample sit.
    ///
    /// @return the stated order. `ByteOrder::Native` for every one-byte format.
    [[nodiscard]] constexpr ByteOrder byteOrder() const noexcept
    {
        return m_byteOrder;
    }

    /// What the channels are.
    ///
    /// @return the channel schema.
    [[nodiscard]] constexpr ChannelSchema channelSchema() const noexcept
    {
        return m_channelSchema;
    }

    /// What basis the channels are expressed in.
    ///
    /// @return the colour space.
    [[nodiscard]] constexpr ColorSpace colorSpace() const noexcept
    {
        return m_colorSpace;
    }

    /// What the magnitudes mean.
    ///
    /// @return the numeric domain.
    [[nodiscard]] constexpr NumericDomain numericDomain() const noexcept
    {
        return m_numericDomain;
    }

    /// The colour filter array over the sensor plane.
    ///
    /// @return the filter array and its phase, which is the default no-mosaic value for a frame
    ///         that has none.
    [[nodiscard]] constexpr const CfaDescriptor& cfa() const noexcept
    {
        return m_cfa;
    }

    /// Which corner row zero is at.
    ///
    /// @return the coordinate origin the boundary format defines.
    [[nodiscard]] constexpr CoordinateOrigin coordinateOrigin() const noexcept
    {
        return m_coordinateOrigin;
    }

    /// The width of one pixel divided by its height.
    ///
    /// @return a positive finite ratio; one for square pixels.
    [[nodiscard]] constexpr double pixelAspectRatio() const noexcept
    {
        return m_pixelAspectRatio;
    }

    /// The resident representation these samples already are, when they are one of the eight.
    ///
    /// The question every path into the typed model asks first. A frame that answers nothing can be
    /// carried, measured and written back unchanged, and nothing may compute with it until a
    /// conversion for its representation exists.
    ///
    /// @return the representation, or an empty optional for a format outside the eight.
    [[nodiscard]] constexpr std::optional<SampleType> residentSampleType() const noexcept
    {
        return residentSampleTypeOf(m_sampleFormat);
    }

    /// Whether these bytes can be read as their samples without swapping.
    ///
    /// @return true when the stated order is `ByteOrder::Native` or is the order this host uses.
    [[nodiscard]] constexpr bool isHostOrder() const noexcept
    {
        return m_byteOrder == ByteOrder::Native || m_byteOrder == nativeByteOrder();
    }

    /// Whether this frame has a colour filter array over it.
    ///
    /// @return true when the filter array describes a mosaic.
    [[nodiscard]] constexpr bool isMosaiced() const noexcept
    {
        return m_cfa.isMosaiced();
    }

    /// The same picture described as a resident image, when its representation is one of the eight.
    ///
    /// The bridge from a boundary description to the processing model. Every rule this descriptor
    /// applied is also an `ImageDescriptor` rule, so the build below succeeds whenever the sample
    /// format is resident - but it is returned as an optional rather than asserted, because a rule
    /// that grew on one side and not the other should refuse rather than abort.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @return the resident description, or an empty optional when the representation is outside
    ///         the eight.
    [[nodiscard]] constexpr std::optional<ImageDescriptor> toImageDescriptor() const noexcept
    {
        const std::optional<SampleType> resident = residentSampleType();
        if (!resident.has_value())
        {
            return std::nullopt;
        }
        return ImageDescriptor::create(ImageDescriptorRequest{.extent = m_extent,
                                                              .sampleType = *resident,
                                                              .channelSchema = m_channelSchema,
                                                              .colorSpace = m_colorSpace,
                                                              .numericDomain = m_numericDomain,
                                                              .cfa = m_cfa,
                                                              .coordinateOrigin = m_coordinateOrigin,
                                                              .pixelAspectRatio = m_pixelAspectRatio});
    }

    /// The request this descriptor was built from.
    ///
    /// There is no setter, for the reason `ImageDescriptor` has none: a boundary that changes what
    /// a frame means takes the description apart, states the change, and builds a new one, so a
    /// change that breaks a rule is refused where it is made.
    ///
    /// @return every field, in a form `create` accepts.
    [[nodiscard]] constexpr RawFrameDescriptorRequest toRequest() const noexcept
    {
        return RawFrameDescriptorRequest{.extent = m_extent,
                                         .sampleFormat = m_sampleFormat,
                                         .byteOrder = m_byteOrder,
                                         .channelSchema = m_channelSchema,
                                         .colorSpace = m_colorSpace,
                                         .numericDomain = m_numericDomain,
                                         .cfa = m_cfa,
                                         .coordinateOrigin = m_coordinateOrigin,
                                         .pixelAspectRatio = m_pixelAspectRatio};
    }

    /// Whether two descriptors describe the same frame.
    ///
    /// @param left one descriptor.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend constexpr bool operator==(const RawFrameDescriptor& left,
                                                   const RawFrameDescriptor& right) noexcept = default;

private:
    /// Builds from a request that has already been checked.
    ///
    /// @param request the validated values.
    constexpr explicit RawFrameDescriptor(const RawFrameDescriptorRequest& request) noexcept
        : m_extent(request.extent), m_cfa(request.cfa), m_pixelAspectRatio(request.pixelAspectRatio),
          m_sampleFormat(request.sampleFormat), m_byteOrder(request.byteOrder), m_channelSchema(request.channelSchema),
          m_colorSpace(request.colorSpace), m_numericDomain(request.numericDomain),
          m_coordinateOrigin(request.coordinateOrigin)
    {
    }

    /// Whether a ratio describes a pixel with a shape.
    ///
    /// Written without `<cmath>`, which has no constexpr guarantee for `isfinite`: a value that is
    /// not greater than zero covers zero, every negative and not-a-number, and comparing against
    /// infinity covers the rest.
    ///
    /// @param value the ratio to test.
    /// @return true when the value is positive and finite.
    [[nodiscard]] static constexpr bool isPositiveFinite(double value) noexcept
    {
        return value > 0.0 && value < std::numeric_limits<double>::infinity();
    }

    /// The frame's pixel dimensions.
    ImageExtent m_extent;

    /// The colour filter array and its phase.
    CfaDescriptor m_cfa;

    /// The width of one pixel divided by its height.
    double m_pixelAspectRatio;

    /// How wide one sample is and what kind of value it holds.
    RawSampleFormat m_sampleFormat;

    /// In which order the bytes of one sample sit.
    ByteOrder m_byteOrder;

    /// What the channels are.
    ChannelSchema m_channelSchema;

    /// What basis the channels are expressed in.
    ColorSpace m_colorSpace;

    /// What the magnitudes mean.
    NumericDomain m_numericDomain;

    /// Which corner row zero is at.
    CoordinateOrigin m_coordinateOrigin;
};

/// What is left of a raw frame once an image has taken it over.
///
/// The three parts an `Image` needs and nothing else: the description as the resident model states
/// it, the geometry the bytes are already in, and the engine memory itself. It exists so that
/// handing a frame over is one move of one value rather than three out-parameters, and so that the
/// descriptor here is the *resident* one - a frame whose representation is outside the eight cannot
/// produce this type at all, which is where that refusal becomes structural rather than remembered.
///
/// Move-only, because the storage is.
struct AdoptedFrame
{
    /// What the image is, in the resident vocabulary.
    ImageDescriptor descriptor;

    /// Where its samples sit, exactly as the boundary wrote them - padding, strides and all.
    ImageLayout layout;

    /// The engine memory the bytes are in, counted against the context it came from.
    ImageStorageBytes storage;
};

/// A frame as a boundary produced it: what it is, where its bytes sit, and the bytes.
///
/// Move-only, because the buffer underneath is. A default-constructed frame holds nothing, which is
/// also what a move leaves behind; `valid` is how to ask.
///
/// Construction is the only place the three parts are checked against each other, and it checks
/// exactly three things: that the layout describes samples of the descriptor's width, that the
/// layout describes the descriptor's extent, and that the buffer is at least as large as the layout
/// says the frame is. After that a row is a span that stops before the padding, and nothing
/// downstream re-derives whether the bytes are there.
class RawFrame
{
public:
    /// A frame that holds nothing.
    RawFrame() noexcept = default;

    /// Takes over another frame, leaving it holding nothing.
    ///
    /// @param other the frame to take over.
    RawFrame(RawFrame&& other) noexcept = default;

    /// Releases this frame and takes over another.
    ///
    /// @param other the frame to take over.
    /// @return this frame.
    RawFrame& operator=(RawFrame&& other) noexcept = default;

    /// Copying is deleted: a frame is expensive and shared responsibility for its release is a
    /// decision a call site should have to write.
    RawFrame(const RawFrame&) = delete;

    /// Copying is deleted. See the copy constructor.
    RawFrame& operator=(const RawFrame&) = delete;

    /// Pairs a description, a memory geometry and the bytes.
    ///
    /// @param descriptor what the frame is.
    /// @param layout where its samples sit, already validated as geometry. Its element size must be
    ///        the descriptor's sample width and its extent the descriptor's extent.
    /// @param buffer the bytes. Must hold at least the layout's total, and is taken over.
    /// @return the frame; `ErrorCode::NotInitialized` when the buffer holds nothing;
    ///         `ErrorCode::IncompatibleArguments` when the layout and the descriptor disagree - the
    ///         message naming `GeometryError::ElementSizeMismatch` when the sample widths differ,
    ///         and naming no geometry rule when the extents do, because that is two descriptions
    ///         disagreeing rather than a fault in the layout; or `ErrorCode::OutOfRange` carrying
    ///         the required and available byte counts when the buffer is too small. The buffer is
    ///         left untouched on refusal.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<RawFrame> create(const RawFrameDescriptor& descriptor,
                                                                      const ImageLayout& layout, SampleBuffer&& buffer);

    /// Whether this frame holds bytes.
    ///
    /// @return false for a default-constructed frame and one that has been moved from.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// What the frame is.
    ///
    /// @return the description, or an empty optional when this frame holds nothing. An optional
    ///         rather than a reference because there is no description of no frame.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<RawFrameDescriptor> descriptor() const noexcept;

    /// Where the samples sit.
    ///
    /// @return the layout, or an empty optional when this frame holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> layout() const noexcept;

    /// The bytes and who releases them.
    ///
    /// @return the buffer, which holds nothing when this frame does.
    [[nodiscard]] AFTERNIGHTENGINE_API const SampleBuffer& buffer() const noexcept;

    /// The bytes, without permission to change them.
    ///
    /// @return the whole region the buffer describes, which may be larger than the layout when the
    ///         buffer was allocated with capacity to spare.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::byte> bytes() const noexcept;

    /// The bytes, through which they may be changed.
    ///
    /// @return the region, or an empty optional when this frame holds nothing or its buffer is not
    ///         writable.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::span<std::byte>> mutableBytes() noexcept;

    /// One row of one plane, stopping before whatever padding the strides describe.
    ///
    /// @param planeIndex which plane. Out of range yields an empty optional.
    /// @param y which row. Out of range yields an empty optional.
    /// @return the row's samples as bytes, or an empty optional when this frame holds nothing or
    ///         the indices name no row.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::span<const std::byte>> row(std::uint32_t planeIndex,
                                                                                     std::uint32_t y) const noexcept;

    /// One row of one plane, through which the samples may be changed.
    ///
    /// @param planeIndex which plane.
    /// @param y which row.
    /// @return the row, or an empty optional when this frame holds nothing, its buffer is not
    ///         writable, or the indices name no row.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::span<std::byte>> mutableRow(std::uint32_t planeIndex,
                                                                                      std::uint32_t y) noexcept;

    /// Whether an image can take this frame over with no copy and no conversion, and if not, why
    /// not.
    ///
    /// The three conditions are separate failures because a caller answers each of them
    /// differently: an unsupported representation needs a conversion, a foreign byte order needs a
    /// swap, and memory the engine does not own needs a copy into memory it does.
    ///
    /// @return success when the frame can be adopted; `ErrorCode::NotInitialized` when it holds
    ///         nothing; `ErrorCode::Unsupported` when its representation is outside the eight;
    ///         `ErrorCode::IncompatibleArguments` when its bytes are not in host order; or
    ///         `ErrorCode::InvalidState` naming the ownership when its memory is not the engine's.
    [[nodiscard]] AFTERNIGHTENGINE_API Status checkAdoptable() const;

    /// Hands the description, the geometry and the engine memory over, leaving this frame holding
    /// nothing.
    ///
    /// What an image calls when it adopts a decoded frame. It refuses for exactly the reasons
    /// `checkAdoptable` names, so a caller may ask first or simply act and read the failure.
    ///
    /// @return the resident description, the layout and the storage, or whatever `checkAdoptable`
    ///         reported. Nothing is mutated on refusal.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<AdoptedFrame> releaseAdoptable();

    /// Gives the bytes back now rather than at the end of the scope.
    ///
    /// Idempotent, and leaves the frame exactly as a default-constructed one.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    /// Builds from parts that have already been checked against each other.
    ///
    /// @param descriptor what the frame is.
    /// @param layout where its samples sit.
    /// @param buffer the bytes.
    RawFrame(const RawFrameDescriptor& descriptor, const ImageLayout& layout, SampleBuffer&& buffer) noexcept;

    /// What the frame is, or nothing when it holds nothing.
    std::optional<RawFrameDescriptor> m_descriptor;

    /// Where its samples sit, or nothing when it holds nothing.
    std::optional<ImageLayout> m_layout;

    /// The bytes and who releases them.
    SampleBuffer m_buffer;
};

} // namespace an
