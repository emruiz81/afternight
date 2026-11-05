// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The reasons an image descriptor refuses what it was told about an image.
///
/// This is a vocabulary rather than a type with behaviour, and it is its own header for the same
/// reason `Core/GeometryError.h` is: it belongs to none of the descriptors that produce it and to
/// more layers than produce it. The colour filter array refuses for these reasons, the composite
/// descriptor refuses for these reasons, and the text form that tests are written in refuses for
/// these reasons, so collapsing them into one "invalid descriptor" would leave every caller
/// re-deriving which of them it was.
///
/// The distinctions are the ones a caller acts on differently. A value outside an enumeration is a
/// header field this build does not understand, and the answer is usually to say which file it came
/// from; a pair of fields that cannot both be true is a descriptor to correct; a malformed text
/// field is a test fixture to fix. None of those responses is the same as any other.
///
/// Geometry has its own vocabulary and keeps it. An extent or a layout refuses for reasons about
/// arithmetic and addressability, which this header would have to restate to absorb; the one case
/// where the two meet - text that names dimensions no extent can be built from - is reported here as
/// a single reason that says to go and ask the geometry why.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstddef>

namespace an
{

/// Why a descriptor refused the values it was given.
///
/// Returned by the `check` entry point of each descriptor type, which is where a caller goes for the
/// reason; the matching `create` entry point returns the value and no reason. `None` is the only
/// enumerator that means the values were accepted.
///
/// Every entry point checks facts before combinations: a value outside an enumeration is refused
/// before any rule that would have to interpret it, because a rule cannot judge a value it does not
/// understand and reporting the rule instead of the unknown value names the wrong problem.
enum class DescriptorError
{
    /// The values describe an image this engine can represent.
    None,

    /// The sample type is not one of the eight this engine stores. A file header that says
    /// something else, or a caller compiled against a build with a different set.
    UnknownSampleType,

    /// The channel schema is not one of the arrangements of channel meaning this engine knows.
    UnknownChannelSchema,

    /// The colour space is not one of the bases this engine knows.
    UnknownColorSpace,

    /// The numeric domain is not one of the value conventions this engine knows.
    UnknownNumericDomain,

    /// The colour filter array pattern is not one this engine can describe. A mosaic whose
    /// repeating block is not one of the described patterns has no phase this descriptor can index,
    /// so it is refused here rather than carried as an approximation.
    UnknownCfaPattern,

    /// The coordinate origin is neither of the two row orders this engine reads.
    UnknownCoordinateOrigin,

    /// The channel schema fixes how many channels an image of it has, and the extent says a
    /// different number. Three channels are not RGBA and four are not RGB, whatever the caller
    /// believes about the buffer.
    ChannelCountMismatch,

    /// The channel schema and the colour space cannot describe the same values - Lab channels in a
    /// display RGB basis, or hue and saturation over a perceptual one.
    ColorSpaceMismatch,

    /// The colour space and the numeric domain contradict each other - a colorimetric basis over
    /// raw sensor codes, or a tristimulus space holding nonlinearly encoded values.
    NumericDomainMismatch,

    /// The sample type cannot carry values in this domain. A physically scaled value in an integer
    /// sample is the case that exists: the descriptor holds no scale factor, so nothing in the
    /// resident image would say what physical quantity the integer stands for.
    NumericDomainNotRepresentable,

    /// The colour filter array phase is outside the block it indexes. A phase is a position within
    /// the repeating pattern, so one at or beyond the block size names no pixel of it.
    CfaOriginOutOfPattern,

    /// A colour filter array phase was given for an image that has no mosaic. There is nothing for
    /// it to be out of step with, so a nonzero value is an unset field somewhere rather than a
    /// fact.
    CfaOriginNotZero,

    /// A colour filter array was given for an image with more than one channel. The array sits over
    /// one sensor plane; an image with separate colour channels has already resolved it.
    CfaRequiresSingleChannel,

    /// A colour filter array was given for an image whose one channel is not plain monochrome
    /// intensity. What the filter array means is which filter covered each pixel of a single
    /// recorded plane, and a schema that claims the channel means something else contradicts that.
    CfaRequiresMonoSchema,

    /// A tabulated mosaic was named without its table. Its filters are not fixed by the pattern's
    /// name, so a descriptor for one cannot be built from the name alone.
    CfaTableRequired,

    /// A filter table was carried by a mosaic whose filters its pattern already fixes, or by an
    /// image with no mosaic at all. Two statements of which filter covers a pixel are one too
    /// many, and nothing decides which of them wins.
    CfaTableNotAllowed,

    /// An entry of a filter table is not one of the three filter colours this build knows. A table
    /// read from a decoder that reports a fourth value, or one assembled from an uninitialised
    /// buffer.
    UnknownCfaColor,

    /// A filter table does not hold eight red, twenty green and eight blue filters. Those totals
    /// are a property of the mosaic rather than a convention, so a block with different ones is not
    /// a phase or an orientation of the mosaic but a different thing.
    CfaTableColorCountsWrong,

    /// A row or a column of a filter table holds unequal numbers of red and blue filters, or a
    /// number of them outside one to two. Every line of the mosaic samples both colours, and a line
    /// that does not is a table transcribed wrongly rather than an unusual sensor.
    CfaTableLineCountsWrong,

    /// A green filter of a filter table has a four-neighbour cross that no green filter of the
    /// mosaic has: unequal red and blue neighbours, neither one nor two of each, or two of each
    /// whose left and right neighbours differ. This is the rule that separates a table with the
    /// right totals from a table that describes the mosaic.
    CfaTableGreenNeighbourhoodWrong,

    /// The pixel aspect ratio is not a positive finite number. Zero, negative, infinite and
    /// not-a-number all describe a pixel with no shape.
    PixelAspectNotPositive,

    /// The dimensions could be read but no extent can be built from them under the limits in force.
    /// The reason is geometry's rather than the descriptor's, and `ImageExtent::check` names it.
    ExtentRefused,

    /// The text is not the form this engine writes: not space-separated `key=value` pairs, or a
    /// value that is not the shape its field takes.
    MalformedText,

    /// The text carries a field this descriptor does not have. Ignoring it would silently accept a
    /// descriptor from a build that knows something this one does not.
    UnknownField,

    /// The text leaves out a field. Every field is required, because a default would be this
    /// engine guessing what an image means.
    MissingField,

    /// The text carries one field twice. Taking either occurrence would be a choice nobody made.
    DuplicateField,

    /// The class of value a raw sample holds is not one this engine describes. A boundary is where
    /// this arrives: a header field that says something else, or a caller compiled against a build
    /// with a different set.
    ///
    /// This and the three reasons below it are appended rather than sorted into the groups they
    /// belong with, because every enumerator above them already crosses the library boundary as a
    /// value and renumbering one would change what an already-compiled caller reads.
    UnknownRawSampleClass,

    /// A raw sample is a width this engine will not describe. Zero, wider than the boundary
    /// maximum, or not a power of two - the last because the stride arithmetic underneath admits
    /// no other, so a three-byte sample is refused rather than carried into a geometry that cannot
    /// address it.
    RawSampleSizeNotSupported,

    /// The byte order is not one of the orders this engine reads.
    UnknownByteOrder,

    /// A byte order was stated for a sample that occupies one byte. One byte has one order, so a
    /// stated one produces two descriptions of identical bytes that then compare unequal.
    ByteOrderNotMeaningful,

    /// The rule an image uses to say which of its samples mean something is not one this engine
    /// knows.
    ///
    /// This and the four reasons below it are appended rather than sorted into the groups they
    /// belong with, for the reason the raw-sample reasons above them were: every enumerator already
    /// crosses the library boundary as a value, and renumbering one would change what an
    /// already-compiled caller reads.
    UnknownValidityRule,

    /// A validity rule that decides by a range was given without one. There is nothing to compare a
    /// sample against, so the rule names no set of samples at all.
    ValidityRangeMissing,

    /// A validity range was given for a rule that decides some other way. Nothing would read it, so
    /// a stated one is an unset field somewhere rather than a fact.
    ValidityRangeNotAllowed,

    /// A bound of a validity range is not a finite number. Every comparison against not-a-number is
    /// false and an infinite bound is not a bound, so neither describes a range.
    ValidityRangeNotFinite,

    /// A validity range's lowest bound is above its highest. No sample satisfies it, which is a
    /// range stated the wrong way round rather than a deliberate empty selection.
    ///
    /// Deliberately the last enumerator; `kDescriptorErrorCount` is derived from it.
    ValidityRangeNotOrdered
};

/// How many reasons `DescriptorError` holds, counting `None`.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `ValidityRangeNotOrdered` stays last. It exists so a table indexed by reason -
/// a counter per reason in a fixture harness - can be sized without its author restating the
/// enumeration.
inline constexpr std::size_t kDescriptorErrorCount =
    static_cast<std::size_t>(DescriptorError::ValidityRangeNotOrdered) + 1u;

/// The reason's own name, for a log line or a test failure message.
///
/// The spelling of the enumerator: `DescriptorError::CfaRequiresMonoSchema` is
/// `"CfaRequiresMonoSchema"`. Stable, and safe to write into a machine-read diagnostic field -
/// unlike a description, which exists to be rewritten. A caller still branches on the reason itself
/// rather than on this text.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownDescriptorError"`, which is what a reason from a newer build looks like here.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* descriptorErrorName(DescriptorError error) noexcept;

/// A sentence describing the reason, for a person reading a log or a failing test.
///
/// It says what the reason means in general, so it is necessarily vaguer than a message that names
/// the two fields of the particular descriptor that disagreed.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to describe. A value outside the enumeration gets a sentence saying so.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* descriptorErrorDescription(DescriptorError error) noexcept;

} // namespace an
