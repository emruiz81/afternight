// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Interop/ExternalBuffer.h.
//
// The vocabulary every adapter shares, and the three rules that are the same whichever library is
// on the other side: the two numeric domains have to agree, a retained borrow has to be of the
// borrower's own memory, and a write permission has to be backed by a writable view.
//
// The domain rule is the one that matters most. A conversion chosen by source pixel type normalizes
// an 8-bit signed image and does not normalize an 8-bit unsigned one, and neither the call site nor
// the resulting matrix records which happened. These cases check that a bridge cannot express that:
// it carries what the samples mean and what the library will assume, and a difference between them
// is a refusal rather than a scale factor.

#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>

namespace
{

constexpr an::NumericDomain kAllDomains[] = {an::NumericDomain::NormalizedLinear, an::NumericDomain::EncodedNonlinear,
                                             an::NumericDomain::RawSensorCode, an::NumericDomain::PhysicalScaled};

constexpr an::ExternalLibrary kAllLibraries[] = {an::ExternalLibrary::OpenCv, an::ExternalLibrary::Fftw,
                                                 an::ExternalLibrary::Gsl};

constexpr an::BridgeRefusal kAllRefusals[] = {an::BridgeRefusal::None,
                                              an::BridgeRefusal::UnknownContract,
                                              an::BridgeRefusal::NumericDomainDiffers,
                                              an::BridgeRefusal::BorrowOutlivesSamples,
                                              an::BridgeRefusal::WriteThroughReadOnlyView,
                                              an::BridgeRefusal::NoSamples,
                                              an::BridgeRefusal::SampleTypeNotRepresentable,
                                              an::BridgeRefusal::ChannelCountNotRepresentable,
                                              an::BridgeRefusal::ExtentExceedsLibraryIndex,
                                              an::BridgeRefusal::RowStrideNotRepresentable,
                                              an::BridgeRefusal::ArrangementNotRepresentable,
                                              an::BridgeRefusal::AlignmentInsufficient};

/// A contract every rule accepts, so a case can break exactly one field.
[[nodiscard]] constexpr an::ExternalBufferContract shareableContract() noexcept
{
    return an::ExternalBufferContract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                      .libraryDomain = an::NumericDomain::NormalizedLinear,
                                      .ownership = an::BufferOwnership::EngineOwned,
                                      .scope = an::BorrowScope::SingleCall,
                                      .mutability = an::BufferMutability::ReadOnly};
}

TEST(ExternalBufferTest, AContractThatSaysNothingUnusualIsAccepted)
{
    EXPECT_EQ(an::checkExternalBufferContract(shareableContract(), true), an::BridgeRefusal::None);
    EXPECT_EQ(an::checkExternalBufferContract(shareableContract(), false), an::BridgeRefusal::None);
}

TEST(ExternalBufferTest, TwoDomainsThatDisagreeAreRefusedRatherThanRescaled)
{
    for (const an::NumericDomain engineDomain : kAllDomains)
    {
        for (const an::NumericDomain libraryDomain : kAllDomains)
        {
            an::ExternalBufferContract contract = shareableContract();
            contract.engineDomain = engineDomain;
            contract.libraryDomain = libraryDomain;

            const an::BridgeRefusal refusal = an::checkExternalBufferContract(contract, true);
            if (engineDomain == libraryDomain)
            {
                EXPECT_EQ(refusal, an::BridgeRefusal::None)
                    << an::numericDomainName(engineDomain) << " to " << an::numericDomainName(libraryDomain);
            }
            else
            {
                EXPECT_EQ(refusal, an::BridgeRefusal::NumericDomainDiffers)
                    << an::numericDomainName(engineDomain) << " to " << an::numericDomainName(libraryDomain);
            }
        }
    }
}

TEST(ExternalBufferTest, EngineMemoryIsNeverLentToSomethingThatKeepsIt)
{
    an::ExternalBufferContract contract = shareableContract();
    contract.ownership = an::BufferOwnership::EngineOwned;

    contract.scope = an::BorrowScope::SingleCall;
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::None);
    contract.scope = an::BorrowScope::Operation;
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::None);
    contract.scope = an::BorrowScope::Retained;
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::BorrowOutlivesSamples);
}

TEST(ExternalBufferTest, ALibraryMayRetainItsOwnMemory)
{
    an::ExternalBufferContract contract = shareableContract();
    contract.ownership = an::BufferOwnership::LibraryOwned;
    contract.scope = an::BorrowScope::Retained;

    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::None);
}

TEST(ExternalBufferTest, AWritePermissionNeedsSamplesTheCallerCanWrite)
{
    an::ExternalBufferContract contract = shareableContract();
    contract.mutability = an::BufferMutability::ReadWrite;

    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::None);
    EXPECT_EQ(an::checkExternalBufferContract(contract, false), an::BridgeRefusal::WriteThroughReadOnlyView);

    contract.mutability = an::BufferMutability::ReadOnly;
    EXPECT_EQ(an::checkExternalBufferContract(contract, false), an::BridgeRefusal::None);
}

TEST(ExternalBufferTest, AValueFromOutsideAnEnumerationIsNamedRatherThanInterpreted)
{
    an::ExternalBufferContract contract = shareableContract();
    contract.scope = static_cast<an::BorrowScope>(7);
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::UnknownContract);

    contract = shareableContract();
    contract.engineDomain = static_cast<an::NumericDomain>(9);
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::UnknownContract);

    contract = shareableContract();
    contract.ownership = static_cast<an::BufferOwnership>(5);
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::UnknownContract);

    contract = shareableContract();
    contract.mutability = static_cast<an::BufferMutability>(4);
    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::UnknownContract);
}

TEST(ExternalBufferTest, AnUnknownValueIsReportedBeforeAnyRuleIsAppliedToIt)
{
    // Both broken at once: the domains also disagree, and the membership answer wins, because a
    // rule applied to a value nobody can interpret reports the wrong reason.
    an::ExternalBufferContract contract = shareableContract();
    contract.engineDomain = static_cast<an::NumericDomain>(11);
    contract.libraryDomain = an::NumericDomain::RawSensorCode;

    EXPECT_EQ(an::checkExternalBufferContract(contract, true), an::BridgeRefusal::UnknownContract);
}

TEST(ExternalBufferTest, TheEngineDomainComesFromTheImageRatherThanFromTheCaller)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(16u, 8u, 1u, limits);
    ASSERT_TRUE(extent.has_value());

    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = an::SampleType::UInt16,
                                             .channelSchema = an::ChannelSchema::Mono,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = an::NumericDomain::RawSensorCode,
                                             .cfa = an::CfaDescriptor{},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    ASSERT_TRUE(descriptor.has_value());

    const an::ExternalBufferContract contract =
        an::externalBufferContract(*descriptor, an::NumericDomain::RawSensorCode, an::BufferOwnership::EngineOwned,
                                   an::BorrowScope::Operation, an::BufferMutability::ReadOnly);
    EXPECT_EQ(contract.engineDomain, an::NumericDomain::RawSensorCode);
    EXPECT_EQ(an::checkExternalBufferContract(contract, false), an::BridgeRefusal::None);

    // The same sensor image described to a library that will read it as a fraction of full scale.
    // A silent rescale is the defect this shape invites, and here it is a refusal.
    const an::ExternalBufferContract normalizing =
        an::externalBufferContract(*descriptor, an::NumericDomain::NormalizedLinear, an::BufferOwnership::EngineOwned,
                                   an::BorrowScope::Operation, an::BufferMutability::ReadOnly);
    EXPECT_EQ(an::checkExternalBufferContract(normalizing, false), an::BridgeRefusal::NumericDomainDiffers);
}

TEST(ExternalBufferTest, MembershipTestsCoverExactlyTheEnumerators)
{
    for (const an::ExternalLibrary library : kAllLibraries)
    {
        EXPECT_TRUE(an::isExternalLibrary(library)) << an::externalLibraryName(library);
    }
    EXPECT_FALSE(an::isExternalLibrary(static_cast<an::ExternalLibrary>(3)));

    EXPECT_TRUE(an::isBufferOwnership(an::BufferOwnership::EngineOwned));
    EXPECT_TRUE(an::isBufferOwnership(an::BufferOwnership::LibraryOwned));
    EXPECT_FALSE(an::isBufferOwnership(static_cast<an::BufferOwnership>(2)));

    EXPECT_TRUE(an::isBorrowScope(an::BorrowScope::Retained));
    EXPECT_FALSE(an::isBorrowScope(static_cast<an::BorrowScope>(3)));

    EXPECT_TRUE(an::isBufferMutability(an::BufferMutability::ReadWrite));
    EXPECT_FALSE(an::isBufferMutability(static_cast<an::BufferMutability>(2)));
}

TEST(ExternalBufferTest, EveryNameIsTheSpellingOfItsOwnEnumerator)
{
    EXPECT_STREQ(an::externalLibraryName(an::ExternalLibrary::OpenCv), "OpenCv");
    EXPECT_STREQ(an::externalLibraryName(an::ExternalLibrary::Fftw), "Fftw");
    EXPECT_STREQ(an::externalLibraryName(an::ExternalLibrary::Gsl), "Gsl");
    EXPECT_STREQ(an::externalLibraryName(static_cast<an::ExternalLibrary>(200)), "UnknownExternalLibrary");

    EXPECT_STREQ(an::bufferOwnershipName(an::BufferOwnership::EngineOwned), "EngineOwned");
    EXPECT_STREQ(an::bufferOwnershipName(an::BufferOwnership::LibraryOwned), "LibraryOwned");
    EXPECT_STREQ(an::bufferOwnershipName(static_cast<an::BufferOwnership>(200)), "UnknownBufferOwnership");

    EXPECT_STREQ(an::borrowScopeName(an::BorrowScope::SingleCall), "SingleCall");
    EXPECT_STREQ(an::borrowScopeName(an::BorrowScope::Operation), "Operation");
    EXPECT_STREQ(an::borrowScopeName(an::BorrowScope::Retained), "Retained");
    EXPECT_STREQ(an::borrowScopeName(static_cast<an::BorrowScope>(200)), "UnknownBorrowScope");

    EXPECT_STREQ(an::bufferMutabilityName(an::BufferMutability::ReadOnly), "ReadOnly");
    EXPECT_STREQ(an::bufferMutabilityName(an::BufferMutability::ReadWrite), "ReadWrite");
    EXPECT_STREQ(an::bufferMutabilityName(static_cast<an::BufferMutability>(200)), "UnknownBufferMutability");

    EXPECT_STREQ(an::bridgeRefusalName(static_cast<an::BridgeRefusal>(200)), "UnknownBridgeRefusal");
}

TEST(ExternalBufferTest, NoTwoRefusalsShareAName)
{
    for (const an::BridgeRefusal left : kAllRefusals)
    {
        const char* const leftName = an::bridgeRefusalName(left);
        ASSERT_NE(leftName, nullptr);
        EXPECT_STRNE(leftName, "UnknownBridgeRefusal") << static_cast<int>(left);

        for (const an::BridgeRefusal right : kAllRefusals)
        {
            if (left == right)
            {
                continue;
            }
            EXPECT_STRNE(leftName, an::bridgeRefusalName(right)) << leftName;
        }
    }
}

TEST(ExternalBufferTest, AContractComparesFieldByField)
{
    const an::ExternalBufferContract contract = shareableContract();
    an::ExternalBufferContract other = contract;
    EXPECT_EQ(contract, other);

    other.scope = an::BorrowScope::Operation;
    EXPECT_NE(contract, other);
}

// The rules are constant expressions, so a build that got one wrong fails here rather than at run
// time. These are the three that decide whether memory is shared, which is the answer worth
// proving cannot depend on anything but the contract.
static_assert(an::checkExternalBufferContract(shareableContract(), false) == an::BridgeRefusal::None);
static_assert(
    an::checkExternalBufferContract(an::ExternalBufferContract{.engineDomain = an::NumericDomain::RawSensorCode,
                                                               .libraryDomain = an::NumericDomain::NormalizedLinear,
                                                               .ownership = an::BufferOwnership::EngineOwned,
                                                               .scope = an::BorrowScope::SingleCall,
                                                               .mutability = an::BufferMutability::ReadOnly},
                                    true) == an::BridgeRefusal::NumericDomainDiffers);
static_assert(
    an::checkExternalBufferContract(an::ExternalBufferContract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                                               .libraryDomain = an::NumericDomain::NormalizedLinear,
                                                               .ownership = an::BufferOwnership::EngineOwned,
                                                               .scope = an::BorrowScope::Retained,
                                                               .mutability = an::BufferMutability::ReadOnly},
                                    true) == an::BridgeRefusal::BorrowOutlivesSamples);

} // namespace
