// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One plane of a colour image in a named encoding, extracted, processed and recomposed.
///
/// A process that works on one intensity plane - a gamma, a curve, a filter, a denoiser - is offered
/// to a user on a single channel or on one component of a colour space: the red channel, the CIE
/// lightness, the HSV value. This header is the one dispatch for all of them, so every client
/// converts, extracts and recomposes with the same conversion, representation and clamp, and
/// `ChannelScope` is the one closed list of what can be chosen. What a user reads beside each choice
/// is the client's to write.
///
/// **What a scope is.** `ChannelScope::Whole` is the image itself. `Red`, `Green` and `Blue` are the
/// stored channels, read as they are. Every other scope is one component of another encoding of the
/// image's red, green and blue: CIE L*a*b* and its cylindrical form L*C*h, CIE XYZ, HSL, HSV, HSI and
/// YCbCr. The request states what the source's red, green and blue are - their primaries, transfer
/// function and white - because a conversion to CIE lightness of samples encoded with the sRGB curve
/// and of linear ones are different conversions, and nothing here guesses which a source holds.
///
/// **In what units a component is handed over.** `ChannelScopeUnits::UnitInterval`, the default, puts
/// every component of an in-gamut colour inside the unit interval, which is what an intensity process
/// expects: L*a*b* and L*C*h in the packing `PerceptualEncoding::UnitPacked` names, XYZ divided by the
/// white's own tristimulus values so the white is one in all three, and a hue as a fraction of a turn.
/// `ChannelScopeUnits::Natural` hands over the CIE units, the tristimulus values with the white's
/// luminance at one, and a hue in degrees.
///
/// **The working plane.** A stored channel is handed over in the source's representation. A component
/// of another encoding is computed in double and held in a Float32 working plane - Float64 when the
/// source is Float64 - so an integer source's lightness is not rounded to that integer's codes before
/// the process sees it. The process writes a separate output plane that starts as a copy of its input,
/// unless the process declares that it writes every sample of it.
///
/// **What is copied.** A sample is copied only where nothing else writes it. An operation that writes
/// every output sample (`ChannelScopeOperation::writesEveryOutputSample`) is handed an output that
/// starts with whatever it held, and a caller whose destination already holds the source
/// (`ChannelScopeDestination::HoldsSource`) has none of the source's samples copied into it again: a
/// process that would otherwise copy the whole image twice before it writes a sample copies it once,
/// or not at all.
///
/// **What is converted once.** A caller that runs one operation after another over the same source and
/// the same component of another encoding - a real-time preview does, a frame each time a control
/// moves - prepares the scope once (`prepareChannelScope`) and hands the preparation with the source
/// (`ChannelScopeSource::prepared`): every call then runs the operation and the conversion back alone,
/// and recomposes what the unprepared call recomposes, bit for bit.
///
/// **Recomposition.** A stored channel is written back in place of the original and the other channels
/// are copied. A component of another encoding replaces that component of the source's encoding, which
/// the inverse conversion turns back into red, green and blue, narrowed once to the source's
/// representation by the engine's own conversion: nearest, ties to even, saturated at an integer's
/// range. Nothing is clamped to the unit interval in a floating result. An alpha channel is copied.
///
/// **Validity.** A stored channel is handed over with the source's own rule and mask for that channel,
/// and the process honours them. A pixel whose red, green or blue the source's rule or mask rejects has
/// no colour to convert: its component is handed over as a quiet NaN, the plane is then described by
/// `ValidityRule::FiniteOnly`, and the pixel is recomposed exactly as the source held it.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>

namespace an
{

/// The memory and policy one caller's operations share. Defined in `Runtime/EngineContext.h`.
class EngineContext;

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// A source already converted into a scope's encoding. Defined below.
class ChannelScopePreparation;

/// Which part of an image an operation runs on.
///
/// One closed list for every client that offers the choice: a process's channel selector, a viewer's
/// channel display, the curves process. The order is a contract - a client may persist the value - and
/// a new scope goes at the end.
enum class ChannelScope : std::uint8_t
{
    /// The image itself, every channel.
    Whole = 0u,

    /// The stored red channel.
    Red = 1u,

    /// The stored green channel.
    Green = 2u,

    /// The stored blue channel.
    Blue = 3u,

    /// CIE L*, the lightness of L*a*b*.
    CieLightness = 4u,

    /// CIE a*, the green-red opponent of L*a*b*.
    CieA = 5u,

    /// CIE b*, the blue-yellow opponent of L*a*b*.
    CieB = 6u,

    /// CIE C*, the chroma of L*C*h.
    CieChroma = 7u,

    /// CIE h, the hue angle of L*C*h.
    CieHue = 8u,

    /// CIE X.
    CieX = 9u,

    /// CIE Y, the luminance.
    CieY = 10u,

    /// CIE Z.
    CieZ = 11u,

    /// The hue of HSL.
    HslHue = 12u,

    /// The saturation of HSL.
    HslSaturation = 13u,

    /// The lightness of HSL.
    HslLightness = 14u,

    /// The hue of HSV.
    HsvHue = 15u,

    /// The saturation of HSV.
    HsvSaturation = 16u,

    /// The value of HSV.
    HsvValue = 17u,

    /// The hue of HSI.
    HsiHue = 18u,

    /// The saturation of HSI.
    HsiSaturation = 19u,

    /// The intensity of HSI.
    HsiIntensity = 20u,

    /// The luma of YCbCr.
    YCbCrLuma = 21u,

    /// The blue-difference chroma of YCbCr.
    YCbCrBlueChroma = 22u,

    /// The red-difference chroma of YCbCr.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    YCbCrRedChroma = 23u
};

/// How many scopes this build knows.
inline constexpr std::uint32_t kChannelScopeCount = static_cast<std::uint32_t>(ChannelScope::YCbCrRedChroma) + 1u;

/// Whether a value is one of the scopes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scope the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isChannelScope(ChannelScope scope) noexcept
{
    return static_cast<std::uint8_t>(scope) <= static_cast<std::uint8_t>(ChannelScope::YCbCrRedChroma);
}

/// The scope's own name, for a log line, a diagnostic field or a persisted key. Not a label for a user.
///
/// Thread-safe and allocation-free.
///
/// @param scope the scope to name. A value outside the enumeration is named `"UnknownChannelScope"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller and
///         is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* channelScopeName(ChannelScope scope) noexcept;

/// Which encoding a scope takes its plane from.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scope the scope to ask about.
/// @return `ChannelSchema::Rgb` for the three stored channels; `Lab`, `Lch`, `Xyz`, `Hsl`, `Hsv`, `Hsi`
///         or `YCbCr` for a component of another encoding; and `ChannelSchema::Named` for
///         `ChannelScope::Whole` and for a value outside the enumeration, neither of which is one plane
///         of one encoding.
[[nodiscard]] constexpr ChannelSchema channelScopeEncoding(ChannelScope scope) noexcept
{
    switch (scope)
    {
    case ChannelScope::Red:
    case ChannelScope::Green:
    case ChannelScope::Blue:
        return ChannelSchema::Rgb;
    case ChannelScope::CieLightness:
    case ChannelScope::CieA:
    case ChannelScope::CieB:
        return ChannelSchema::Lab;
    case ChannelScope::CieChroma:
    case ChannelScope::CieHue:
        return ChannelSchema::Lch;
    case ChannelScope::CieX:
    case ChannelScope::CieY:
    case ChannelScope::CieZ:
        return ChannelSchema::Xyz;
    case ChannelScope::HslHue:
    case ChannelScope::HslSaturation:
    case ChannelScope::HslLightness:
        return ChannelSchema::Hsl;
    case ChannelScope::HsvHue:
    case ChannelScope::HsvSaturation:
    case ChannelScope::HsvValue:
        return ChannelSchema::Hsv;
    case ChannelScope::HsiHue:
    case ChannelScope::HsiSaturation:
    case ChannelScope::HsiIntensity:
        return ChannelSchema::Hsi;
    case ChannelScope::YCbCrLuma:
    case ChannelScope::YCbCrBlueChroma:
    case ChannelScope::YCbCrRedChroma:
        return ChannelSchema::YCbCr;
    case ChannelScope::Whole:
        break;
    }
    return ChannelSchema::Named;
}

/// Which channel of its encoding a scope's plane is.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scope the scope to ask about.
/// @return the channel index within `channelScopeEncoding(scope)`: zero, one or two. L*C*h's lightness is
///         channel zero, so `CieChroma` is one and `CieHue` two. Zero for `ChannelScope::Whole` and for a
///         value outside the enumeration.
[[nodiscard]] constexpr std::uint32_t channelScopeComponent(ChannelScope scope) noexcept
{
    switch (scope)
    {
    case ChannelScope::Green:
    case ChannelScope::CieA:
    case ChannelScope::CieChroma:
    case ChannelScope::CieY:
    case ChannelScope::HslSaturation:
    case ChannelScope::HsvSaturation:
    case ChannelScope::HsiSaturation:
    case ChannelScope::YCbCrBlueChroma:
        return 1u;
    case ChannelScope::Blue:
    case ChannelScope::CieB:
    case ChannelScope::CieHue:
    case ChannelScope::CieZ:
    case ChannelScope::HslLightness:
    case ChannelScope::HsvValue:
    case ChannelScope::HsiIntensity:
    case ChannelScope::YCbCrRedChroma:
        return 2u;
    case ChannelScope::Whole:
    case ChannelScope::Red:
    case ChannelScope::CieLightness:
    case ChannelScope::CieX:
    case ChannelScope::HslHue:
    case ChannelScope::HsvHue:
    case ChannelScope::HsiHue:
    case ChannelScope::YCbCrLuma:
        break;
    }
    return 0u;
}

/// In what units a component of another encoding is handed over.
enum class ChannelScopeUnits : std::uint8_t
{
    /// Every component of an in-gamut colour inside the unit interval: L*a*b* and L*C*h unit-packed as
    /// `PerceptualEncoding::UnitPacked` states, XYZ divided by the white's tristimulus values, a hue as
    /// a fraction of a turn. Luma and chroma are always full range with the chroma centred on one half.
    UnitInterval = 0u,

    /// The CIE units - lightness to a hundred, signed opponents, chroma from zero, a hue in degrees -
    /// the tristimulus values with the white's luminance at one, and HSL, HSV and HSI hues in degrees.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Natural = 1u
};

/// Whether a value is one of the unit conventions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param units the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isChannelScopeUnits(ChannelScopeUnits units) noexcept
{
    return units == ChannelScopeUnits::UnitInterval || units == ChannelScopeUnits::Natural;
}

/// Everything a channel scope decides, stated once.
///
/// A plain aggregate. The planner checks it, and the plan keeps a copy.
struct ChannelScopeRequest
{
    /// Which part of the image.
    ChannelScope scope = ChannelScope::Whole;

    /// What the source's red, green and blue are. Its schema is `ChannelSchema::Rgb`: an RGBA source's
    /// alpha is the scope's to carry, not the conversion's. Read by the scopes of another encoding
    /// alone, and checked whatever the scope.
    ColorEncoding sourceEncoding{};

    /// In what units a component of another encoding is handed over.
    ChannelScopeUnits units = ChannelScopeUnits::UnitInterval;

    /// Which luma weights the YCbCr scopes use.
    YCbCrVariant yCbCr = YCbCrVariant::Rec601FullRange;
};

/// A checked channel scope.
///
/// Built only by `planChannelScope`, so holding one means the request was accepted. Copyable and
/// allocation-free: for a component of another encoding it holds the two colour plans, to that encoding
/// and back, which the planner built once.
class ChannelScopePlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const ChannelScopeRequest& request() const noexcept
    {
        return m_request;
    }

    /// The conversion from the source's red, green and blue to the scope's encoding.
    ///
    /// @return the plan, or no value for `ChannelScope::Whole` and the three stored channels.
    [[nodiscard]] const std::optional<ColorConversionPlan>& forward() const noexcept
    {
        return m_forward;
    }

    /// The conversion from the scope's encoding back to the source's red, green and blue.
    ///
    /// @return the plan, or no value where `forward` has none.
    [[nodiscard]] const std::optional<ColorConversionPlan>& inverse() const noexcept
    {
        return m_inverse;
    }

private:
    /// Records a checked request and its conversions.
    ///
    /// @param request the request `planChannelScope` accepted.
    /// @param forward the conversion to the scope's encoding, or no value.
    /// @param inverse the conversion back, or no value.
    ChannelScopePlan(const ChannelScopeRequest& request, std::optional<ColorConversionPlan> forward,
                     std::optional<ColorConversionPlan> inverse) noexcept
        : m_request(request), m_forward(forward), m_inverse(inverse)
    {
    }

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<ChannelScopePlan> planChannelScope(const ChannelScopeRequest& request);

    /// The checked request.
    ChannelScopeRequest m_request;

    /// The conversion to the scope's encoding.
    std::optional<ColorConversionPlan> m_forward;

    /// The conversion back.
    std::optional<ColorConversionPlan> m_inverse;
};

/// Checks a request and makes the plan.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param request the scope, the source's encoding and the units.
/// @return the plan; or `ErrorCode::InvalidArgument` for a scope, units or luma variant outside its
///         enumeration, or a source encoding `checkColorEncoding` refuses or whose schema is not
///         `ChannelSchema::Rgb`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ChannelScopePlan> planChannelScope(const ChannelScopeRequest& request);

/// The image a channel scope reads.
///
/// Every member is borrowed for the call. `ChannelScope::Whole` takes any image; the other scopes take
/// a source whose schema is `ChannelSchema::Rgb` or `ChannelSchema::Rgba`. A component of another
/// encoding needs samples that are a fraction of full scale: an integer source in any domain but a
/// sensor code, or a floating one in a normalized or encoded domain. A source that states a colour
/// basis must state the one the request's encoding names; one that states none is read under it.
struct ChannelScopeSource
{
    /// What the samples are. Must not be null.
    const ImageDescriptor* descriptor = nullptr;

    /// The first byte of the samples, aligned for their representation. Must not be null.
    const void* samples = nullptr;

    /// Where the samples sit; it must describe the descriptor. Must not be null.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something. A mask with one plane per channel gives each channel its own
    /// plane; any other mask gives every channel the view's `maskPlane`.
    KernelValidityView validity{};

    /// The source already converted into the scope's encoding, made by `prepareChannelScope` for these
    /// samples and this scope, or null to convert it in the call. With one, a call runs the operation and
    /// the conversion back alone. Whether it was made from these very samples is the caller's statement
    /// and is not checked; a preparation made for another request or extent is refused.
    const ChannelScopePreparation* prepared = nullptr;
};

/// A source converted into a scope's encoding once, for every later call over the same samples.
///
/// What a component of another encoding needs before its operation runs - the source's red, green and
/// blue converted into the scope's encoding, the component put into its units and the pixels with no
/// colour marked - made once by `prepareChannelScope` into memory of its own. A call handed it through
/// `ChannelScopeSource::prepared` reads the component where it lies, converts back from it and from the
/// two planes the operation does not touch, and recomposes what the unprepared call does.
///
/// It states the request and the extent it was made for, and a call refuses one made for another; it
/// cannot know whether the samples and the validity it is handed with are still the ones it was made
/// from - it holds the pixels its source's rule or mask rejected as not-a-number, and decides from
/// them whether the operation's plane is read where it lies - so a caller keeps it under a revision
/// that moves with the image's samples and with its validity alike. Immutable once made, so any
/// number of calls may read it at once. Move-only.
class ChannelScopePreparation
{
public:
    /// A preparation of nothing, which every call refuses.
    AFTERNIGHTENGINE_API ChannelScopePreparation() noexcept;

    /// Returns the planes to the memory resource they came from.
    AFTERNIGHTENGINE_API ~ChannelScopePreparation();

    /// Takes over another preparation, leaving it one of nothing.
    ///
    /// @param other the preparation to take over.
    AFTERNIGHTENGINE_API ChannelScopePreparation(ChannelScopePreparation&& other) noexcept;

    /// Lets this preparation's planes go and takes over another's.
    ///
    /// @param other the preparation to take over.
    /// @return this preparation.
    AFTERNIGHTENGINE_API ChannelScopePreparation& operator=(ChannelScopePreparation&& other) noexcept;

    /// Copying is deleted: the planes are a whole image's worth of memory.
    ChannelScopePreparation(const ChannelScopePreparation&) = delete;

    /// Copying is deleted. See the copy constructor.
    ChannelScopePreparation& operator=(const ChannelScopePreparation&) = delete;

    /// Whether this holds planes.
    ///
    /// @return false for a preparation of nothing and for one moved from.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// What it was made for.
    ///
    /// @return the request of the plan it was prepared under.
    [[nodiscard]] const ChannelScopeRequest& request() const noexcept
    {
        return m_request;
    }

    /// The width of the source it was made from.
    ///
    /// @return the width in pixels; zero for a preparation of nothing.
    [[nodiscard]] std::uint32_t width() const noexcept
    {
        return m_width;
    }

    /// The height of the source it was made from.
    ///
    /// @return the height in pixels; zero for a preparation of nothing.
    [[nodiscard]] std::uint32_t height() const noexcept
    {
        return m_height;
    }

    /// The representation the planes are held in.
    ///
    /// @return Float32, or Float64 for a Float64 source.
    [[nodiscard]] SampleType sampleType() const noexcept
    {
        return m_sampleType;
    }

    /// Pixels of the source that had no colour to convert, which every call recomposes as the source holds
    /// them.
    ///
    /// @return how many.
    [[nodiscard]] std::uint64_t invalidPixels() const noexcept
    {
        return m_invalidPixels;
    }

    /// The first byte of the three planes, in the scope's encoding: the component's own in its units, with
    /// a quiet NaN where a pixel has no colour, and the other two as the conversion wrote them.
    ///
    /// @return the pointer, or null for a preparation of nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const void* samples() const noexcept;

    /// Where the three planes sit: one plane per channel, rows aligned to the engine's buffer alignment.
    ///
    /// @return the layout, or no value for a preparation of nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> layout() const noexcept;

    /// How many bytes the planes hold.
    ///
    /// @return the bytes, padding included; zero for a preparation of nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t byteCount() const noexcept;

private:
    /// The preparer is the only one that fills a preparation.
    friend AFTERNIGHTENGINE_API Result<ChannelScopePreparation> prepareChannelScope(const EngineContext& engine,
                                                                                    const ExecutionContext& execution,
                                                                                    const ChannelScopePlan& plan,
                                                                                    const ChannelScopeSource& source);

    /// The request of the plan it was prepared under.
    ChannelScopeRequest m_request{};

    /// The source's width.
    std::uint32_t m_width = 0u;

    /// The source's height.
    std::uint32_t m_height = 0u;

    /// The representation of the planes.
    SampleType m_sampleType = SampleType::Float32;

    /// The source's pixels with no colour to convert.
    std::uint64_t m_invalidPixels = 0u;

    /// The three planes.
    ImageStorageBytes m_planes;
};

/// Converts a source into a scope's encoding once, for every later call over the same samples.
///
/// The work `applyChannelScope` does for a component of another encoding before it runs the operation -
/// the promotion, the colour transform, the component's units and the marking of pixels with no colour -
/// done into memory the preparation owns, so a call handed it does none of that again.
///
/// Thread-safe with respect to the source, which is only read. Runs on the operation's admitted threads,
/// and the answer does not depend on how many.
///
/// @param engine the context the planes' memory and budget come from; it must outlive the preparation.
/// @param execution the operation policy and engine-context lease.
/// @param plan the scope: a component of another encoding.
/// @param source what it converts; a preparation it carries is not read.
/// @return the preparation; `ErrorCode::IncompatibleArguments` for `ChannelScope::Whole` or a stored
///         channel, which convert nothing, and for a source this scope does not read; the other failures
///         `extractChannelScope` describes for the source and the execution; or a memory-budget refusal.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ChannelScopePreparation>
prepareChannelScope(const EngineContext& engine, const ExecutionContext& execution, const ChannelScopePlan& plan,
                    const ChannelScopeSource& source);

/// What a scope hands its operation to read: the whole source, or one plane.
///
/// Borrowed for the operation's call and no longer.
struct ChannelScopeInput
{
    /// What the samples are: the source's description for `ChannelScope::Whole`, and otherwise the one
    /// `channelScopePlaneDescriptor` states.
    const ImageDescriptor* descriptor = nullptr;

    /// The first sample.
    const void* samples = nullptr;

    /// Where the samples sit.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something, as this header's opening states.
    KernelValidityView validity{};
};

/// Where a scope's operation writes: the destination for `ChannelScope::Whole`, and otherwise a plane the
/// scope owns. It has the input's description and extent, and starts as a copy of the input - unless the
/// operation writes every output sample, when it starts holding whatever it held: for
/// `ChannelScope::Whole`, what the caller's destination held.
///
/// Borrowed for the operation's call and no longer.
struct ChannelScopeOutput
{
    /// What the samples are: the input's description.
    const ImageDescriptor* descriptor = nullptr;

    /// The first sample, writable.
    void* samples = nullptr;

    /// Where the samples sit.
    const ImageLayout* layout = nullptr;
};

/// A caller's operation, run once over the scoped samples.
///
/// It composes the engine's operations - a pointwise program, a filter, a stretch - over the input and
/// writes the output, and may leave any output sample alone unless it declares that it writes every one.
/// It runs on the thread that called `applyChannelScope`, inside that call, under the execution it is
/// given, so it may run parallel work through that execution and honour its cancellation.
class ChannelScopeOperation
{
public:
    /// Releases the caller's operation state.
    virtual ~ChannelScopeOperation() = default;

    /// Runs the operation. An exception that escapes it is returned as a failure.
    ///
    /// @param execution the scope's execution, borrowed for the call.
    /// @param input what to read, borrowed for the call.
    /// @param output what to write, borrowed for the call.
    /// @return success, or a typed failure the scope returns as it is.
    [[nodiscard]] virtual Status run(const ExecutionContext& execution, const ChannelScopeInput& input,
                                     const ChannelScopeOutput& output) = 0;

    /// Whether `run` writes every sample of the output it is handed, whatever the input's values and validity, and
    /// reads none of the output before it writes it.
    ///
    /// The scope then does not start the output as a copy of the input: the output of `ChannelScope::Whole` - the
    /// caller's destination - holds what it held, and a stored channel's plane holds what the scope's working memory
    /// held. An operation that may leave a sample alone, as one that honours a validity rule or a mask does, answers
    /// false, the default, and finds the input there. An operation that answers true and then leaves a sample alone
    /// leaves whatever the output held.
    ///
    /// Thread-safe and allocation-free. Asked once per call of `applyChannelScope`, before `run`.
    ///
    /// @return true when `run` writes every output sample.
    [[nodiscard]] virtual bool writesEveryOutputSample() const noexcept
    {
        return false;
    }
};

/// What a caller's destination holds when a scope is applied to it.
enum class ChannelScopeDestination : std::uint8_t
{
    /// Nothing the scope may rely on. Every sample is written: the operation's, the recomposition's, or a copy of
    /// the source's.
    Unwritten = 0u,

    /// A copy of the source, in the destination's own layout. The scope writes what the operation and the
    /// recomposition write and copies none of the source's samples into it: for `ChannelScope::Whole` the operation
    /// finds the source in its output, and for a stored channel the other channels are left as they are.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    HoldsSource = 1u
};

/// Whether a value is one of the destination states this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param holds the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isChannelScopeDestination(ChannelScopeDestination holds) noexcept
{
    return holds == ChannelScopeDestination::Unwritten || holds == ChannelScopeDestination::HoldsSource;
}

/// What one scope did.
struct ChannelScopeResult
{
    /// The scope that ran.
    ChannelScope scope = ChannelScope::Whole;

    /// Pixels of a component of another encoding that had no colour to convert, handed over as a quiet
    /// NaN and recomposed as the source held them. Zero for the whole image and the stored channels.
    std::uint64_t invalidPixels = 0u;
};

/// What one plane of a source is, as a description.
///
/// One channel, `ChannelSchema::Mono` with no colour basis. A stored channel keeps the source's
/// representation and numeric domain. A component of another encoding is Float32 - Float64 for a
/// Float64 source - in the domain `numericDomainOfEncoding` gives that encoding: linear for XYZ and for
/// the cylindrical and luma-chroma encodings of a linear source, encoded for CIE L*a*b* and L*C*h and
/// for those of a curve-encoded one.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the scope.
/// @param source the source's description.
/// @return the description; or `ErrorCode::IncompatibleArguments` for `ChannelScope::Whole`, which is
///         not one plane, or for a source this scope does not read, as `ChannelScopeSource` states.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor> channelScopePlaneDescriptor(const ChannelScopePlan& plan,
                                                                                       const ImageDescriptor& source);

/// What one plane of a source is, as a layout request: one plane, rows aligned to the engine's buffer
/// alignment.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the scope.
/// @param source the source's description.
/// @return the request; or what `channelScopePlaneDescriptor` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest> channelScopePlaneLayout(const ChannelScopePlan& plan,
                                                                                      const ImageDescriptor& source);

/// Writes one plane of a source, for a caller that shows it rather than processes it.
///
/// The plane `applyChannelScope` would hand its operation, written into the caller's memory: a stored
/// channel copied, or a component of another encoding converted - or copied from the source's
/// preparation, when it carries one. The destination may not overlap the source or its mask. Takes the
/// working image a conversion needs from the execution's memory resource and returns it before it
/// returns.
///
/// Thread-safe with respect to the source, which is only read. Runs on the operation's admitted
/// threads, and the answer does not depend on how many.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the scope.
/// @param source what it reads.
/// @param destination the first byte written, aligned for the plane's representation.
/// @param destinationLayout where the plane goes: one channel of the source's extent in the plane's
///        representation, padded or not.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of the source, a layout or pointer that does not
///         describe its descriptor, a null or misaligned destination, or a mask missing, unwanted or
///         overlapping the destination; `ErrorCode::IncompatibleArguments` for `ChannelScope::Whole`, a
///         source this scope does not read, a preparation made for another request, extent or
///         representation or holding nothing, a destination of another extent or representation, a
///         destination overlapping the source, or a mask that does not cover the source; a memory-budget
///         refusal; or the execution's cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ChannelScopeResult>
extractChannelScope(const ExecutionContext& execution, const ChannelScopePlan& plan, const ChannelScopeSource& source,
                    void* destination, const ImageLayout& destinationLayout);

/// Extracts a plane, runs an operation over it and recomposes the image, into a destination that holds
/// nothing the scope may rely on: `applyChannelScope` with `ChannelScopeDestination::Unwritten`.
///
/// The destination is the source's description in any layout of the source's extent and arrangement
/// family - planar, interleaved or mixed - and receives the whole recomposed image. It may not overlap
/// the source or its mask. For `ChannelScope::Whole` the destination is the operation's output, and
/// starts as a copy of the source unless the operation writes every output sample. For a plane, the
/// scope takes the input plane - a view of the source when the source is planar and the scope a stored
/// channel, a working plane otherwise - and an output plane from the execution's memory resource, and
/// returns them before it returns. A source that carries its preparation hands the operation the
/// preparation's component as its input, converts back from the operation's output and the
/// preparation's other two planes, and takes only the output and the conversion's planes.
///
/// Thread-safe with respect to the source, which is only read. The operation runs once, on the calling
/// thread. The scope's own walks run on the operation's admitted threads, and their answer does not
/// depend on how many. On a failure after the checks, a stop or the operation's own included, the
/// destination may be partly written.
///
/// @param execution the operation policy and engine-context lease, which the operation is given.
/// @param plan the scope.
/// @param source what it reads.
/// @param operation what to run over the scoped samples. Used exclusively for the call.
/// @param destination the first byte written, aligned for the source's representation.
/// @param destinationLayout where the recomposed image goes.
/// @return what was done; the failures `extractChannelScope` describes, except that `ChannelScope::Whole`
///         is accepted and the destination has the source's description; `ErrorCode::IncompatibleArguments`
///         for a destination of another extent or representation, or a preparation carried with
///         `ChannelScope::Whole` or a stored channel; or the operation's failure as it returned it, or as
///         what it threw translates to.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ChannelScopeResult>
applyChannelScope(const ExecutionContext& execution, const ChannelScopePlan& plan, const ChannelScopeSource& source,
                  ChannelScopeOperation& operation, void* destination, const ImageLayout& destinationLayout);

/// Extracts a plane, runs an operation over it and recomposes the image, into a destination whose state the caller
/// states.
///
/// What the six-argument form does when `holds` is `ChannelScopeDestination::Unwritten`. With
/// `ChannelScopeDestination::HoldsSource` nothing of the source is copied into the destination: for
/// `ChannelScope::Whole` the operation runs over the destination as it is, and for a stored channel only that
/// channel is written back. A component of another encoding is recomposed into every colour sample either way.
/// The destination's state is the caller's statement and is not checked; a destination stated as holding the source
/// that does not keeps, where the operation writes nothing, whatever it held.
///
/// Thread-safety, allocation and failures as the six-argument form states.
///
/// @param execution the operation policy and engine-context lease, which the operation is given.
/// @param plan the scope.
/// @param source what it reads.
/// @param operation what to run over the scoped samples. Used exclusively for the call.
/// @param destination the first byte written, aligned for the source's representation.
/// @param destinationLayout where the recomposed image goes.
/// @param holds what the destination holds when the call begins.
/// @return what the six-argument form returns; or `ErrorCode::InvalidArgument` for a `holds` outside its
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ChannelScopeResult>
applyChannelScope(const ExecutionContext& execution, const ChannelScopePlan& plan, const ChannelScopeSource& source,
                  ChannelScopeOperation& operation, void* destination, const ImageLayout& destinationLayout,
                  ChannelScopeDestination holds);

/// What `applyChannelScope` does to the image it writes, given what its operation does to what it is
/// handed.
///
/// The same whatever the scope. The destination is described as the source was, so an operation that
/// restates the description of what it writes - a resample, a conversion of representation - cannot be
/// scoped, and one that only reads has nothing to recompose. Everything else is the operation's own
/// declaration: what the result means, what becomes of a solution, of the source cards and of an output
/// decision, and what it does with validity. The pixels the scope recomposes as the source held them
/// change none of that.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param operationEffect what the operation declares it does to the image it writes.
/// @return the scope's effect, which writes samples and preserves the description; or
///         `ErrorCode::IncompatibleArguments` for an operation that restates the description or only
///         reads, and `ErrorCode::InvalidArgument` for a declaration `checkOperationEffect` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OperationEffect> channelScopeEffect(const OperationEffect& operationEffect);

} // namespace an
