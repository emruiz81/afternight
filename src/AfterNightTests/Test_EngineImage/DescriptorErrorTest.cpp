// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/DescriptorError.h.
//
// The vocabulary is only worth having if each reason is distinguishable from the others, so these
// cases check exactly that: the count matches the enumeration, and no two reasons share a name or a
// description. A duplicated name is the failure mode that matters here - it makes two different
// refusals read identically in a log while the code still tells them apart, so nobody notices the
// engine is answering the wrong question until they read the source.

#include <AfterNightEngine/Image/DescriptorError.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

namespace
{

constexpr an::DescriptorError kAllErrors[] = {an::DescriptorError::None,
                                              an::DescriptorError::UnknownSampleType,
                                              an::DescriptorError::UnknownChannelSchema,
                                              an::DescriptorError::UnknownColorSpace,
                                              an::DescriptorError::UnknownNumericDomain,
                                              an::DescriptorError::UnknownCfaPattern,
                                              an::DescriptorError::UnknownCoordinateOrigin,
                                              an::DescriptorError::ChannelCountMismatch,
                                              an::DescriptorError::ColorSpaceMismatch,
                                              an::DescriptorError::NumericDomainMismatch,
                                              an::DescriptorError::NumericDomainNotRepresentable,
                                              an::DescriptorError::CfaOriginOutOfPattern,
                                              an::DescriptorError::CfaOriginNotZero,
                                              an::DescriptorError::CfaRequiresSingleChannel,
                                              an::DescriptorError::CfaRequiresMonoSchema,
                                              an::DescriptorError::CfaTableRequired,
                                              an::DescriptorError::CfaTableNotAllowed,
                                              an::DescriptorError::UnknownCfaColor,
                                              an::DescriptorError::CfaTableColorCountsWrong,
                                              an::DescriptorError::CfaTableLineCountsWrong,
                                              an::DescriptorError::CfaTableGreenNeighbourhoodWrong,
                                              an::DescriptorError::PixelAspectNotPositive,
                                              an::DescriptorError::ExtentRefused,
                                              an::DescriptorError::MalformedText,
                                              an::DescriptorError::UnknownField,
                                              an::DescriptorError::MissingField,
                                              an::DescriptorError::DuplicateField,
                                              an::DescriptorError::UnknownRawSampleClass,
                                              an::DescriptorError::RawSampleSizeNotSupported,
                                              an::DescriptorError::UnknownByteOrder,
                                              an::DescriptorError::ByteOrderNotMeaningful,
                                              an::DescriptorError::UnknownValidityRule,
                                              an::DescriptorError::ValidityRangeMissing,
                                              an::DescriptorError::ValidityRangeNotAllowed,
                                              an::DescriptorError::ValidityRangeNotFinite,
                                              an::DescriptorError::ValidityRangeNotOrdered};

TEST(DescriptorErrorTest, TheCountCoversEveryEnumerator)
{
    EXPECT_EQ(an::kDescriptorErrorCount, sizeof(kAllErrors) / sizeof(kAllErrors[0]));
}

TEST(DescriptorErrorTest, NoneIsTheOnlyAcceptance)
{
    for (const an::DescriptorError error : kAllErrors)
    {
        EXPECT_EQ(error == an::DescriptorError::None, std::strcmp(an::descriptorErrorName(error), "None") == 0);
    }
}

TEST(DescriptorErrorTest, EveryReasonHasItsOwnNameAndItsOwnSentence)
{
    for (const an::DescriptorError left : kAllErrors)
    {
        EXPECT_GT(std::strlen(an::descriptorErrorName(left)), 0u);
        EXPECT_GT(std::strlen(an::descriptorErrorDescription(left)), 0u);
        for (const an::DescriptorError right : kAllErrors)
        {
            const bool sameName = std::strcmp(an::descriptorErrorName(left), an::descriptorErrorName(right)) == 0;
            const bool sameText =
                std::strcmp(an::descriptorErrorDescription(left), an::descriptorErrorDescription(right)) == 0;
            EXPECT_EQ(sameName, left == right);
            EXPECT_EQ(sameText, left == right);
        }
    }
}

TEST(DescriptorErrorTest, AValueOutsideTheEnumerationSaysSoRatherThanNamingSomethingElse)
{
    const an::DescriptorError stranger = static_cast<an::DescriptorError>(200);
    EXPECT_STREQ(an::descriptorErrorName(stranger), "UnknownDescriptorError");
    EXPECT_GT(std::strlen(an::descriptorErrorDescription(stranger)), 0u);
}

TEST(DescriptorErrorTest, TheNameIsTheEnumeratorSpelling)
{
    EXPECT_STREQ(an::descriptorErrorName(an::DescriptorError::CfaRequiresMonoSchema), "CfaRequiresMonoSchema");
    EXPECT_STREQ(an::descriptorErrorName(an::DescriptorError::PixelAspectNotPositive), "PixelAspectNotPositive");
}

} // namespace
