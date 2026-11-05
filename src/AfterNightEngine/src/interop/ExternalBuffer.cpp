// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Interop/ExternalBuffer.h>

// The name tables the bridge vocabulary needs, kept out of the header for the reason every other
// name table in this engine is: a switch compiled into every translation unit that describes a
// foreign buffer would be the interop layer paying for strings almost nothing reads.
//
// This file sits directly under the interop layer rather than in an adapter's directory, because
// the vocabulary belongs to no one library. A file in `interop/<library>` may include that library
// and nothing else; a file here may include none, which is exactly right for the five enumerations
// every adapter shares.
//
// None of the switches carries a `default` label, so a value added to an enumeration without a name
// here is a compile failure rather than a blank in somebody's log. The trailing return is for a
// value cast in from outside the enumeration, which is a different thing and says so.

namespace an
{

const char* externalLibraryName(ExternalLibrary library) noexcept
{
    switch (library)
    {
    case ExternalLibrary::OpenCv:
        return "OpenCv";
    case ExternalLibrary::Fftw:
        return "Fftw";
    case ExternalLibrary::Gsl:
        return "Gsl";
    }
    return "UnknownExternalLibrary";
}

const char* bufferOwnershipName(BufferOwnership ownership) noexcept
{
    switch (ownership)
    {
    case BufferOwnership::EngineOwned:
        return "EngineOwned";
    case BufferOwnership::LibraryOwned:
        return "LibraryOwned";
    }
    return "UnknownBufferOwnership";
}

const char* borrowScopeName(BorrowScope scope) noexcept
{
    switch (scope)
    {
    case BorrowScope::SingleCall:
        return "SingleCall";
    case BorrowScope::Operation:
        return "Operation";
    case BorrowScope::Retained:
        return "Retained";
    }
    return "UnknownBorrowScope";
}

const char* bufferMutabilityName(BufferMutability mutability) noexcept
{
    switch (mutability)
    {
    case BufferMutability::ReadOnly:
        return "ReadOnly";
    case BufferMutability::ReadWrite:
        return "ReadWrite";
    }
    return "UnknownBufferMutability";
}

const char* bridgeRefusalName(BridgeRefusal refusal) noexcept
{
    switch (refusal)
    {
    case BridgeRefusal::None:
        return "None";
    case BridgeRefusal::UnknownContract:
        return "UnknownContract";
    case BridgeRefusal::NumericDomainDiffers:
        return "NumericDomainDiffers";
    case BridgeRefusal::BorrowOutlivesSamples:
        return "BorrowOutlivesSamples";
    case BridgeRefusal::WriteThroughReadOnlyView:
        return "WriteThroughReadOnlyView";
    case BridgeRefusal::NoSamples:
        return "NoSamples";
    case BridgeRefusal::SampleTypeNotRepresentable:
        return "SampleTypeNotRepresentable";
    case BridgeRefusal::ChannelCountNotRepresentable:
        return "ChannelCountNotRepresentable";
    case BridgeRefusal::ExtentExceedsLibraryIndex:
        return "ExtentExceedsLibraryIndex";
    case BridgeRefusal::RowStrideNotRepresentable:
        return "RowStrideNotRepresentable";
    case BridgeRefusal::ArrangementNotRepresentable:
        return "ArrangementNotRepresentable";
    case BridgeRefusal::AlignmentInsufficient:
        return "AlignmentInsufficient";
    }
    return "UnknownBridgeRefusal";
}

} // namespace an
