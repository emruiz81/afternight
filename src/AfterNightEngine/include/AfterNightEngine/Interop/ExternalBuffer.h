// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an external library is being told about the samples it is handed.
///
/// An adapter to OpenCV, FFTW or GSL is not a conversion. It is a claim that some other library may
/// read - or write - bytes this engine owns, or the reverse, and the claim has four independent
/// parts: what the samples are, what they *mean*, who owns them, and how long the borrow lasts. A
/// contract here states all four. A bridge that picked a conversion from the pixel type alone would
/// hand an 8-bit image to OpenCV as codes and a signed 8-bit image as a fraction of full scale with
/// nothing in either call saying so, which is why a contract carries *two* numeric domains rather
/// than one.
///
/// Two domains, because a bridge is the one place where the meaning an image carries and the meaning
/// a library will assume are separately knowable. When they agree the samples can be shared as they
/// stand. When they differ, no adapter in this engine rescales them quietly: the bridge is refused
/// by name, and the caller converts through `Image/ImageConversion.h`, where the five decisions a
/// conversion makes are stated and the cost appears in a profile.
///
/// Nothing here includes an external library, and no external type appears in any of it. A library's
/// buffer is *described* - the fields its own structure has, in the types its own structure uses -
/// so the description can be checked before anything constructs the foreign object, and so a caller
/// that does not use OpenCV pays nothing for OpenCV's existence. The adapter directory for each
/// library is where its header goes when an algorithm needs the library itself; the include rules
/// already allow it there and nowhere else.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <cstdint>

namespace an
{

/// An external library this engine has an adapter for.
///
/// Named rather than inferred so a diagnostic can say which bridge refused without the caller
/// carrying that fact separately. The set is the libraries a sample buffer is bridged to, and it
/// grows when such an adapter is written, not before; the metadata and document adapters share no
/// samples and are not in it.
enum class ExternalLibrary : std::uint8_t
{
    /// OpenCV, whose two-dimensional matrix interleaves its channels within a row.
    OpenCv,

    /// FFTW, whose arrays are strided in elements and whose plans may require an aligned base.
    Fftw,

    /// GSL, whose matrices and vectors are row-major floating point with an element stride.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Gsl
};

/// Which side of a bridge owns the samples.
///
/// The question that decides who may free them, and it is asked explicitly because getting it wrong
/// is silent in both directions: a library structure that believes it owns engine memory will
/// release memory the engine still holds, and an engine that believes it owns library memory will
/// release memory the library still holds.
enum class BufferOwnership : std::uint8_t
{
    /// The engine allocated the samples and will free them. The library may look at them and must
    /// never release them.
    EngineOwned,

    /// The library allocated the samples and will free them. The engine may view them and must
    /// never adopt them into owned storage without copying.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    LibraryOwned
};

/// How long the borrower keeps the samples it was handed.
///
/// Stated by the caller because no adapter can observe it: a description is a pointer and some
/// numbers, and nothing about those says whether the structure built from them is destroyed before
/// the next statement or stored in a member. It is here because the failure it prevents - a foreign
/// header outliving the memory it points at - reproduces as a use-after-free far from the bridge.
enum class BorrowScope : std::uint8_t
{
    /// The borrow ends when the adapted call returns. The foreign structure is a local, and the
    /// samples outlive it by construction.
    SingleCall,

    /// The borrow lasts for one operation: the foreign structure is built at the start, used
    /// throughout, and destroyed before the execution context that scoped the work ends.
    Operation,

    /// The borrower keeps the samples after the operation that lent them has finished. Only its own
    /// memory may be borrowed that way, which is what makes this value refusable rather than
    /// advisory.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Retained
};

/// Whether the borrower may write through the description.
///
/// Separate from the view's own constness because the two answer different questions: the view says
/// what this translation unit may do, and this says what the library is being permitted to do. A
/// bridge that promised writes over a read-only view is refused rather than resolved.
enum class BufferMutability : std::uint8_t
{
    /// The borrower reads and never writes.
    ReadOnly,

    /// The borrower may write, so the samples must be reachable through a mutable view.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ReadWrite
};

/// Why a bridge cannot share the samples as they stand.
///
/// One vocabulary across all three adapters, so a caller that wants "share it if you can, otherwise
/// copy" branches on one enumeration rather than three. Every value names a rule with an argument
/// behind it, and a later adapter with a legitimate case argues with the rule rather than with a
/// condition buried in a function.
///
/// `None` is first and means the bridge holds; every other value is a refusal.
enum class BridgeRefusal : std::uint8_t
{
    /// No refusal: the samples can be shared exactly as they are.
    None,

    /// A value in the contract is outside its own enumeration, so no rule could be applied to it
    /// without first deciding what it meant.
    UnknownContract,

    /// The engine's samples and the library's reading of them mean different things - codes against
    /// a fraction of full scale, a linear quantity against an encoded one. Nothing is rescaled
    /// here; convert through `Image/ImageConversion.h` and bridge the result.
    NumericDomainDiffers,

    /// The borrower would keep engine-owned samples after the lending operation ends. Retaining is
    /// only ever a property of the borrower's own memory.
    BorrowOutlivesSamples,

    /// The contract permits writes and the samples are reachable only through a read-only view.
    WriteThroughReadOnlyView,

    /// There are no samples to describe: a null base pointer, or a description with none.
    NoSamples,

    /// This library has no representation for the engine's sample type, or the engine has none for
    /// the library's. OpenCV, for instance, defines no unsigned 32-bit matrix depth at all.
    SampleTypeNotRepresentable,

    /// The channel count is outside what the library's own structure can express.
    ChannelCountNotRepresentable,

    /// A dimension does not fit the integer type the library indexes with.
    ExtentExceedsLibraryIndex,

    /// The row stride cannot be expressed in the library's terms: not a whole number of elements
    /// where the library counts elements, or outside the type it holds a stride in.
    RowStrideNotRepresentable,

    /// The samples of one pixel are not adjacent, or the planes are not where this library's memory
    /// model requires them - an interleaving library handed planar storage, most often.
    ArrangementNotRepresentable,

    /// The base address does not meet the alignment the caller stated its plan requires.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AlignmentInsufficient
};

/// What a bridge is being told about the samples it is asked to share.
///
/// A plain aggregate with no defaults. Every field is a decision somebody has to make, and a field
/// left out would take its type's zero - which would silently mean "read-only, engine-owned, ends
/// with the call, normalized linear on both sides". Three of those four are the safe answer and the
/// fourth is the finding this whole header exists for, so none of them is given away.
struct ExternalBufferContract
{
    /// What the engine's samples mean, which is `ImageDescriptor::numericDomain()` when the samples
    /// came from an image.
    NumericDomain engineDomain;

    /// What the library will treat the samples as. Equal to `engineDomain` for an operation that
    /// does not care - a warp, a transform, a copy - and different for one that does, which is
    /// exactly the case a bridge refuses.
    NumericDomain libraryDomain;

    /// Which side allocated the samples and will free them.
    BufferOwnership ownership;

    /// How long the borrower keeps them.
    BorrowScope scope;

    /// Whether the borrower may write through the description.
    BufferMutability mutability;

    /// Whether two contracts say the same thing.
    ///
    /// @param left one contract.
    /// @param right the other.
    /// @return true when all five fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const ExternalBufferContract& left,
                                                   const ExternalBufferContract& right) noexcept = default;
};

/// Whether a value is one of the libraries this build adapts.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param library the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isExternalLibrary(ExternalLibrary library) noexcept
{
    return static_cast<std::uint8_t>(library) <= static_cast<std::uint8_t>(ExternalLibrary::Gsl);
}

/// Whether a value is one of the ownership answers.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param ownership the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isBufferOwnership(BufferOwnership ownership) noexcept
{
    return static_cast<std::uint8_t>(ownership) <= static_cast<std::uint8_t>(BufferOwnership::LibraryOwned);
}

/// Whether a value is one of the borrow scopes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scope the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isBorrowScope(BorrowScope scope) noexcept
{
    return static_cast<std::uint8_t>(scope) <= static_cast<std::uint8_t>(BorrowScope::Retained);
}

/// Whether a value is one of the mutability answers.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mutability the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isBufferMutability(BufferMutability mutability) noexcept
{
    return static_cast<std::uint8_t>(mutability) <= static_cast<std::uint8_t>(BufferMutability::ReadWrite);
}

/// The contract for sharing an image's samples with a library that will read them as they mean.
///
/// The engine domain is taken from the descriptor rather than from the caller, which is the point:
/// a caller that had to restate it could restate it wrongly, and the whole finding this vocabulary
/// answers is a scale that came from somewhere other than what the image says about itself.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param descriptor what the image is; only its numeric domain is read.
/// @param libraryDomain what the library will treat the samples as.
/// @param ownership which side allocated them.
/// @param scope how long the borrower keeps them.
/// @param mutability whether the borrower may write.
/// @return the contract those five facts describe.
[[nodiscard]] constexpr ExternalBufferContract externalBufferContract(const ImageDescriptor& descriptor,
                                                                      NumericDomain libraryDomain,
                                                                      BufferOwnership ownership, BorrowScope scope,
                                                                      BufferMutability mutability) noexcept
{
    return ExternalBufferContract{.engineDomain = descriptor.numericDomain(),
                                  .libraryDomain = libraryDomain,
                                  .ownership = ownership,
                                  .scope = scope,
                                  .mutability = mutability};
}

/// Whether the contract itself permits a shared buffer, before any layout is looked at.
///
/// The half of a bridge that is the same for every library: the two domains have to agree, a
/// retained borrow has to be of the borrower's own memory, and a write permission has to be backed
/// by a writable view. Each adapter applies this first and then adds what its own memory model
/// requires, so a caller reading a refusal knows whether the problem is the claim or the layout.
///
/// The order is a contract. Every value is checked for membership of its own enumeration first, so
/// a rule is never applied to a value it cannot interpret; the rules then follow in the order the
/// fields are declared, so a contract with two problems reports the same one on every build.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param contract what the bridge is being told.
/// @param samplesAreMutable whether the samples are reachable through a mutable view, which the
///        caller knows from the view's own sample type.
/// @return `BridgeRefusal::None` when the claim holds; otherwise the first rule that refused.
[[nodiscard]] constexpr BridgeRefusal checkExternalBufferContract(const ExternalBufferContract& contract,
                                                                  bool samplesAreMutable) noexcept
{
    if (!isNumericDomain(contract.engineDomain) || !isNumericDomain(contract.libraryDomain) ||
        !isBufferOwnership(contract.ownership) || !isBorrowScope(contract.scope) ||
        !isBufferMutability(contract.mutability))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (contract.engineDomain != contract.libraryDomain)
    {
        return BridgeRefusal::NumericDomainDiffers;
    }
    if (contract.ownership == BufferOwnership::EngineOwned && contract.scope == BorrowScope::Retained)
    {
        return BridgeRefusal::BorrowOutlivesSamples;
    }
    if (contract.mutability == BufferMutability::ReadWrite && !samplesAreMutable)
    {
        return BridgeRefusal::WriteThroughReadOnlyView;
    }
    return BridgeRefusal::None;
}

/// The library's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param library the library to name. A value outside the enumeration is named
///        `"UnknownExternalLibrary"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* externalLibraryName(ExternalLibrary library) noexcept;

/// The ownership answer's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param ownership the value to name. A value outside the enumeration is named
///        `"UnknownBufferOwnership"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* bufferOwnershipName(BufferOwnership ownership) noexcept;

/// The borrow scope's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param scope the value to name. A value outside the enumeration is named `"UnknownBorrowScope"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* borrowScopeName(BorrowScope scope) noexcept;

/// The mutability answer's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param mutability the value to name. A value outside the enumeration is named
///        `"UnknownBufferMutability"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* bufferMutabilityName(BufferMutability mutability) noexcept;

/// The refusal's own name, which is what a person reads to find out which rule refused.
///
/// The engine reports a bridge refusal to a caller as a named reason rather than as a message,
/// because the reason is what a later adapter with a legitimate case has to argue with. This is the
/// spelling of that reason.
///
/// Thread-safe and allocation-free.
///
/// @param refusal the reason to name. A value outside the enumeration is named
///        `"UnknownBridgeRefusal"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* bridgeRefusalName(BridgeRefusal refusal) noexcept;

} // namespace an
