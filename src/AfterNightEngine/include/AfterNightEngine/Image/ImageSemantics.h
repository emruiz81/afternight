// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What has been done to an image, and under which transfer function.
///
/// This is the half of an image's meaning that the descriptor deliberately does not carry, and the
/// split between the two is the rule worth stating first: **the descriptor is the only answer to
/// what the samples are**. Which channels, in what basis, in what numeric domain, under what filter
/// array - all of that is `Image/ImageDescriptor.h`, and duplicating any of it here would produce
/// two answers with no rule about which wins, which is exactly the failure the descriptor was
/// written to end.
///
/// What is left is three things the descriptor cannot state.
///
/// **Which transfer function a nonlinear encoding is.** The descriptor says `EncodedNonlinear` and
/// says no more, on purpose: the encoding belongs with the transforms that apply and remove it. But
/// something has to remember which one was applied, or removing it is a guess, so it is here.
///
/// **What has already been done.** Whether the bias is subtracted, whether the mosaic is resolved,
/// whether the frame is registered. Answered by looking at what a process did earlier in the same
/// session, applying a calibration twice is possible and invisible; a recorded state makes the second
/// application a question somebody can ask.
///
/// **Which of the samples mean anything.** `Image/ValidityDescriptor.h` is here rather than in the
/// descriptor because it is the same kind of fact as the two above: a claim about what has happened
/// to these samples rather than about what they are. A calibration divide that produced NaN where
/// the flat held a zero says so here, and every reader of the image gets the same answer instead of
/// each algorithm deciding for itself whether an exact zero is data.
///
/// All three are semantic: a result computed under one and read under another is wrong, so a change
/// to any of them invalidates derived results. That is why this object and the astrometric solution
/// are the two that move an image's semantic generation, and why the acquisition record and the
/// preserved cards beside them do not.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cstdint>
#include <optional>

namespace an
{

/// Which nonlinear encoding an image's magnitudes have been through.
///
/// Named rather than described by an exponent alone, because the two encodings this engine meets
/// most are piecewise and are not a pure power law: writing them as a gamma is a small error near
/// black that a round trip turns into a visible one.
enum class TransferFunction : std::uint8_t
{
    /// Nothing is claimed about the encoding. What an image whose provenance did not record one
    /// has, and what a linear image should not use - `Linear` says that positively.
    Unspecified,

    /// The magnitudes stand in fixed proportion to the light recorded. The state a calibrated
    /// astronomical frame is in, and the one every physical computation assumes.
    Linear,

    /// The sRGB transfer function, including its linear segment near black.
    Srgb,

    /// The Rec. 709 transfer function, including its linear segment near black.
    Rec709,

    /// A pure power law with the exponent this object carries beside it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PureGamma
};

/// Whether a value is one of the transfer functions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param transfer the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isTransferFunction(TransferFunction transfer) noexcept
{
    return static_cast<std::uint8_t>(transfer) <= static_cast<std::uint8_t>(TransferFunction::PureGamma);
}

/// The transfer function's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param transfer the function to name. A value outside the enumeration is named
///        `"UnknownTransferFunction"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* transferFunctionName(TransferFunction transfer) noexcept;

/// Which steps of the pipeline an image has already been through.
///
/// A plain aggregate of named flags rather than a bit set, because the flags are read far more
/// often than they are combined and `state.flatFielded` is a sentence where a masked bit test is a
/// puzzle. Every one of them defaults to false, which is the correct description of a frame that
/// has just been decoded.
///
/// Each flag answers exactly one question: has this operation been applied to these samples. None
/// of them says whether it *should* be - that is a plan's business - and none of them carries how
/// it was done, which belongs in provenance.
struct ProcessingState
{
    /// A bias or offset frame has been subtracted.
    bool biasSubtracted = false;

    /// A dark frame has been subtracted.
    bool darkSubtracted = false;

    /// A flat field has been divided out.
    bool flatFielded = false;

    /// Known bad pixels and columns have been repaired.
    bool cosmeticCorrected = false;

    /// The colour filter array has been resolved into colour channels. An image whose descriptor
    /// still carries a mosaic has not been through this, which is a consistency a caller may check
    /// and this object does not enforce - the descriptor and the state are separate facts and an
    /// intermediate that is one and not yet the other is legitimate.
    bool demosaiced = false;

    /// A background model has been removed.
    bool backgroundExtracted = false;

    /// The frame has been aligned to a reference.
    bool registered = false;

    /// The frame has been scaled to a common level for combination.
    bool normalized = false;

    /// The channel scaling has been set from a colour reference rather than from the camera.
    bool colorCalibrated = false;

    /// Whether two states describe the same history.
    ///
    /// @param left one state.
    /// @param right the other.
    /// @return true when every flag is equal.
    [[nodiscard]] friend constexpr bool operator==(const ProcessingState& left,
                                                   const ProcessingState& right) noexcept = default;
};

/// What a caller states about an image's processing meaning, before it has been checked.
///
/// A plain aggregate with defaults, so a caller states what it knows. The defaults describe a
/// freshly decoded frame: nothing has been applied and nothing is claimed about its encoding.
struct ImageSemanticsRequest
{
    /// Which nonlinear encoding the magnitudes have been through.
    TransferFunction transferFunction = TransferFunction::Unspecified;

    /// The exponent of the power law, required when the transfer function is
    /// `TransferFunction::PureGamma` and refused otherwise. Must be positive and finite.
    std::optional<double> gammaExponent{};

    /// Which steps of the pipeline the image has been through.
    ProcessingState processingState{};

    /// Which of the image's samples mean something. The default claims every one of them does,
    /// which is the correct description of an image nothing has invalidated.
    ValidityDescriptor validity{};
};

/// What has been done to an image, and under which transfer function.
///
/// There is no way to build one that contradicts itself: the constructor is private and `create` is
/// the only way in, so an object holding `PureGamma` always has an exponent and one holding
/// anything else never does. A default-constructed one describes a freshly decoded frame.
///
/// Immutable, and one of the two objects whose change moves an image's semantic generation.
class ImageSemantics
{
public:
    /// The meaning a freshly decoded frame has: nothing applied, nothing claimed.
    ImageSemantics() = default;

    /// The meaning stated by this request, when it is consistent.
    ///
    /// @param request what has been done and under which encoding.
    /// @return the meaning; `ErrorCode::InvalidArgument` for a transfer function outside the
    ///         enumeration, or a power law with no exponent or with one that is not positive and
    ///         finite; or `ErrorCode::IncompatibleArguments` for an exponent stated beside an
    ///         encoding that is not a power law.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ImageSemantics> create(const ImageSemanticsRequest& request);

    /// Which nonlinear encoding the magnitudes have been through.
    ///
    /// @return the transfer function.
    [[nodiscard]] AFTERNIGHTENGINE_API TransferFunction transferFunction() const noexcept;

    /// The exponent of the power law.
    ///
    /// @return the exponent, or an empty optional whenever the transfer function is not
    ///         `TransferFunction::PureGamma`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> gammaExponent() const noexcept;

    /// Which steps of the pipeline the image has been through.
    ///
    /// @return the recorded state.
    [[nodiscard]] AFTERNIGHTENGINE_API const ProcessingState& processingState() const noexcept;

    /// Which of the image's samples mean something.
    ///
    /// The rule alone. Where it says a packed mask decides, the mask travels beside the image
    /// rather than in here: this object is copied wherever metadata is copied, and a mask is
    /// memory.
    ///
    /// @return the validity description, which claims every sample means something unless
    ///         something stated otherwise.
    [[nodiscard]] AFTERNIGHTENGINE_API const ValidityDescriptor& validity() const noexcept;

    /// Whether the magnitudes stand in fixed proportion to the light recorded.
    ///
    /// The question every physical computation asks. `Unspecified` answers false, deliberately: an
    /// image that never said it was linear is not evidence that it is.
    ///
    /// @return true only for `TransferFunction::Linear`.
    [[nodiscard]] AFTERNIGHTENGINE_API bool isLinear() const noexcept;

    /// The request this meaning was built from.
    ///
    /// There is no setter, for the reason the descriptors have none: a change is stated in full and
    /// rebuilt, so one that breaks a rule is refused where it is made.
    ///
    /// @return every field, in a form `create` accepts.
    [[nodiscard]] AFTERNIGHTENGINE_API ImageSemanticsRequest toRequest() const noexcept;

    /// Whether two objects describe the same meaning.
    ///
    /// @param left one meaning.
    /// @param right the other.
    /// @return true when the transfer function, the exponent, every flag and the validity
    ///         description are equal.
    [[nodiscard]] friend bool operator==(const ImageSemantics& left, const ImageSemantics& right) = default;

private:
    /// Builds from a request whose values have already been checked.
    ///
    /// @param request the validated meaning.
    constexpr explicit ImageSemantics(const ImageSemanticsRequest& request) noexcept
        : m_gammaExponent(request.gammaExponent), m_processingState(request.processingState),
          m_validity(request.validity), m_transferFunction(request.transferFunction)
    {
    }

    /// The exponent of the power law, present exactly for `TransferFunction::PureGamma`.
    std::optional<double> m_gammaExponent{};

    /// Which steps of the pipeline the image has been through.
    ProcessingState m_processingState{};

    /// Which of the image's samples mean something.
    ValidityDescriptor m_validity{};

    /// Which nonlinear encoding the magnitudes have been through.
    TransferFunction m_transferFunction = TransferFunction::Unspecified;
};

} // namespace an
