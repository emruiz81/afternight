// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ComputePrecision.h.
//
// The rule this header enforces is short: Float32 by default, Float64 by name. So the cases are
// organised around the two directions that can be wrong.
//
// **Reaching for Float64 without a reason is refused**, and a reason that does not apply to this
// storage or this promise is refused too. The two structural justifications are the ones the engine
// can check - the samples are already Float64, or the result must distinguish adjacent thirty-two-bit
// code values - and checking them is what stops a justification becoming a word that unlocks
// anything.
//
// **Settling for Float32 where the promise needs more is refused.** That is the thirty-two-bit
// integer case, and it is the reason a *contract* appears in a precision rule at all: the same
// storage admits Float32 under a declared tolerance and refuses it under an exact promise.

#include <AfterNightEngine/Image/ComputePrecision.h>

#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <optional>
#include <type_traits>

namespace
{

constexpr an::SampleType kAllSampleTypes[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                              an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                              an::SampleType::Float32, an::SampleType::Float64};

constexpr an::Float64Justification kAllJustifications[] = {
    an::Float64Justification::StoredInFloat64,      an::Float64Justification::ExactIntegerCodeValues,
    an::Float64Justification::CalibrationDivide,    an::Float64Justification::StackingAccumulation,
    an::Float64Justification::ReductionAccumulator, an::Float64Justification::AstrometricFitting};

constexpr an::PrecisionError kAllErrors[] = {an::PrecisionError::None,
                                             an::PrecisionError::UnknownSampleType,
                                             an::PrecisionError::UnknownComputePrecision,
                                             an::PrecisionError::UnknownOutputContract,
                                             an::PrecisionError::UnknownJustification,
                                             an::PrecisionError::Float64NeedsJustification,
                                             an::PrecisionError::JustificationWithoutFloat64,
                                             an::PrecisionError::JustificationNotApplicable,
                                             an::PrecisionError::PrecisionBelowContract};

static_assert(std::is_same_v<an::ComputeSampleOf<an::ComputePrecision::Float32>, float>);
static_assert(std::is_same_v<an::ComputeSampleOf<an::ComputePrecision::Float64>, double>);

TEST(ComputePrecisionTest, TheDefaultIsFloat32ForEverythingTheMeasurementsCover)
{
    for (const an::SampleType storage : {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16,
                                         an::SampleType::Int16, an::SampleType::Float32})
    {
        for (const an::OutputPrecisionContract contract :
             {an::OutputPrecisionContract::ExactStoredValues, an::OutputPrecisionContract::NormalizedOrLossy})
        {
            EXPECT_EQ(an::defaultComputePrecision(storage, contract), an::ComputePrecision::Float32)
                << an::sampleTypeName(storage);
            EXPECT_FALSE(an::requiresFloat64(storage, contract)) << an::sampleTypeName(storage);
        }
    }
}

TEST(ComputePrecisionTest, Float64SamplesComputeInFloat64ByDefaultWhateverThePromise)
{
    EXPECT_EQ(an::defaultComputePrecision(an::SampleType::Float64, an::OutputPrecisionContract::ExactStoredValues),
              an::ComputePrecision::Float64);
    EXPECT_EQ(an::defaultComputePrecision(an::SampleType::Float64, an::OutputPrecisionContract::NormalizedOrLossy),
              an::ComputePrecision::Float64);
}

TEST(ComputePrecisionTest, ADeclaredTolerancePermitsTheDowncastThatAnExactPromiseRefuses)
{
    EXPECT_TRUE(an::requiresFloat64(an::SampleType::Float64, an::OutputPrecisionContract::ExactStoredValues));
    EXPECT_FALSE(an::requiresFloat64(an::SampleType::Float64, an::OutputPrecisionContract::NormalizedOrLossy));

    EXPECT_EQ(an::checkComputePrecision(an::SampleType::Float64, an::OutputPrecisionContract::NormalizedOrLossy,
                                        an::ComputePrecision::Float32, std::nullopt),
              an::PrecisionError::None);
    EXPECT_EQ(an::checkComputePrecision(an::SampleType::Float64, an::OutputPrecisionContract::ExactStoredValues,
                                        an::ComputePrecision::Float32, std::nullopt),
              an::PrecisionError::PrecisionBelowContract);
}

TEST(ComputePrecisionTest, TheThirtyTwoBitIntegersAreWhereTheContractDecides)
{
    for (const an::SampleType storage : {an::SampleType::UInt32, an::SampleType::Int32})
    {
        EXPECT_EQ(an::defaultComputePrecision(storage, an::OutputPrecisionContract::ExactStoredValues),
                  an::ComputePrecision::Float64)
            << an::sampleTypeName(storage);
        EXPECT_EQ(an::defaultComputePrecision(storage, an::OutputPrecisionContract::NormalizedOrLossy),
                  an::ComputePrecision::Float32)
            << an::sampleTypeName(storage);

        EXPECT_EQ(an::checkComputePrecision(storage, an::OutputPrecisionContract::ExactStoredValues,
                                            an::ComputePrecision::Float32, std::nullopt),
                  an::PrecisionError::PrecisionBelowContract)
            << an::sampleTypeName(storage);
        EXPECT_EQ(an::checkComputePrecision(storage, an::OutputPrecisionContract::NormalizedOrLossy,
                                            an::ComputePrecision::Float32, std::nullopt),
                  an::PrecisionError::None)
            << an::sampleTypeName(storage);
    }
}

TEST(ComputePrecisionTest, Float64WithoutAReasonIsRefusedForEveryStorage)
{
    for (const an::SampleType storage : kAllSampleTypes)
    {
        for (const an::OutputPrecisionContract contract :
             {an::OutputPrecisionContract::ExactStoredValues, an::OutputPrecisionContract::NormalizedOrLossy})
        {
            EXPECT_EQ(an::checkComputePrecision(storage, contract, an::ComputePrecision::Float64, std::nullopt),
                      an::PrecisionError::Float64NeedsJustification)
                << an::sampleTypeName(storage);
        }
    }
}

TEST(ComputePrecisionTest, TheFourAlgorithmReasonsApplyWhateverTheStorageIs)
{
    for (const an::Float64Justification justification :
         {an::Float64Justification::CalibrationDivide, an::Float64Justification::StackingAccumulation,
          an::Float64Justification::ReductionAccumulator, an::Float64Justification::AstrometricFitting})
    {
        for (const an::SampleType storage : kAllSampleTypes)
        {
            EXPECT_EQ(an::checkComputePrecision(storage, an::OutputPrecisionContract::NormalizedOrLossy,
                                                an::ComputePrecision::Float64, justification),
                      an::PrecisionError::None)
                << an::float64JustificationName(justification) << " over " << an::sampleTypeName(storage);
        }
    }
}

TEST(ComputePrecisionTest, TheStructuralReasonsAreCheckedAgainstTheStorageAndThePromise)
{
    // "The samples are already Float64" is only true where they are.
    EXPECT_EQ(an::checkComputePrecision(an::SampleType::Float64, an::OutputPrecisionContract::ExactStoredValues,
                                        an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64),
              an::PrecisionError::None);
    for (const an::SampleType storage : kAllSampleTypes)
    {
        if (storage == an::SampleType::Float64)
        {
            continue;
        }
        EXPECT_EQ(an::checkComputePrecision(storage, an::OutputPrecisionContract::ExactStoredValues,
                                            an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64),
                  an::PrecisionError::JustificationNotApplicable)
            << an::sampleTypeName(storage);
    }

    // "The result must distinguish adjacent code values" needs both a thirty-two-bit integer and a
    // promise that says exactness matters.
    for (const an::SampleType storage : {an::SampleType::UInt32, an::SampleType::Int32})
    {
        EXPECT_EQ(an::checkComputePrecision(storage, an::OutputPrecisionContract::ExactStoredValues,
                                            an::ComputePrecision::Float64,
                                            an::Float64Justification::ExactIntegerCodeValues),
                  an::PrecisionError::None)
            << an::sampleTypeName(storage);
        EXPECT_EQ(an::checkComputePrecision(storage, an::OutputPrecisionContract::NormalizedOrLossy,
                                            an::ComputePrecision::Float64,
                                            an::Float64Justification::ExactIntegerCodeValues),
                  an::PrecisionError::JustificationNotApplicable)
            << an::sampleTypeName(storage);
    }
    EXPECT_EQ(an::checkComputePrecision(an::SampleType::UInt16, an::OutputPrecisionContract::ExactStoredValues,
                                        an::ComputePrecision::Float64,
                                        an::Float64Justification::ExactIntegerCodeValues),
              an::PrecisionError::JustificationNotApplicable);
}

TEST(ComputePrecisionTest, AReasonGivenForFloat32IsRefusedRatherThanIgnored)
{
    for (const an::Float64Justification justification : kAllJustifications)
    {
        EXPECT_EQ(an::checkComputePrecision(an::SampleType::UInt16, an::OutputPrecisionContract::NormalizedOrLossy,
                                            an::ComputePrecision::Float32, justification),
                  an::PrecisionError::JustificationWithoutFloat64)
            << an::float64JustificationName(justification);
    }
}

TEST(ComputePrecisionTest, EveryArgumentIsCheckedForMembershipBeforeAnyRuleInterpretsIt)
{
    EXPECT_EQ(an::checkComputePrecision(static_cast<an::SampleType>(200),
                                        an::OutputPrecisionContract::NormalizedOrLossy, an::ComputePrecision::Float32,
                                        std::nullopt),
              an::PrecisionError::UnknownSampleType);
    EXPECT_EQ(an::checkComputePrecision(an::SampleType::UInt16, an::OutputPrecisionContract::NormalizedOrLossy,
                                        static_cast<an::ComputePrecision>(200), std::nullopt),
              an::PrecisionError::UnknownComputePrecision);
    EXPECT_EQ(an::checkComputePrecision(an::SampleType::UInt16, static_cast<an::OutputPrecisionContract>(200),
                                        an::ComputePrecision::Float32, std::nullopt),
              an::PrecisionError::UnknownOutputContract);
    EXPECT_EQ(an::checkComputePrecision(an::SampleType::UInt16, an::OutputPrecisionContract::NormalizedOrLossy,
                                        an::ComputePrecision::Float64, static_cast<an::Float64Justification>(200)),
              an::PrecisionError::UnknownJustification);
}

TEST(ComputePrecisionTest, TheDefaultIsAlwaysAPrecisionTheRuleAccepts)
{
    for (const an::SampleType storage : kAllSampleTypes)
    {
        for (const an::OutputPrecisionContract contract :
             {an::OutputPrecisionContract::ExactStoredValues, an::OutputPrecisionContract::NormalizedOrLossy})
        {
            const an::ComputePrecision precision = an::defaultComputePrecision(storage, contract);
            const std::optional<an::Float64Justification> justification =
                precision == an::ComputePrecision::Float64
                    ? std::optional<an::Float64Justification>{storage == an::SampleType::Float64
                                                                  ? an::Float64Justification::StoredInFloat64
                                                                  : an::Float64Justification::ExactIntegerCodeValues}
                    : std::nullopt;
            EXPECT_EQ(an::checkComputePrecision(storage, contract, precision, justification), an::PrecisionError::None)
                << an::sampleTypeName(storage) << " " << an::outputPrecisionContractName(contract);
        }
    }
}

TEST(ComputePrecisionTest, AWorkingImageOfAPrecisionIsStoredAsThatWidth)
{
    EXPECT_EQ(an::workingSampleTypeOf(an::ComputePrecision::Float32), an::SampleType::Float32);
    EXPECT_EQ(an::workingSampleTypeOf(an::ComputePrecision::Float64), an::SampleType::Float64);
}

TEST(ComputePrecisionTest, EveryVocabularyHasItsOwnNamesAndAStrangerSaysSo)
{
    constexpr an::ComputePrecision kPrecisions[] = {an::ComputePrecision::Float32, an::ComputePrecision::Float64};
    for (const an::ComputePrecision left : kPrecisions)
    {
        EXPECT_TRUE(an::isComputePrecision(left));
        for (const an::ComputePrecision right : kPrecisions)
        {
            EXPECT_EQ(std::strcmp(an::computePrecisionName(left), an::computePrecisionName(right)) == 0, left == right);
        }
    }

    constexpr an::OutputPrecisionContract kContracts[] = {an::OutputPrecisionContract::ExactStoredValues,
                                                          an::OutputPrecisionContract::NormalizedOrLossy};
    for (const an::OutputPrecisionContract left : kContracts)
    {
        EXPECT_TRUE(an::isOutputPrecisionContract(left));
        for (const an::OutputPrecisionContract right : kContracts)
        {
            EXPECT_EQ(std::strcmp(an::outputPrecisionContractName(left), an::outputPrecisionContractName(right)) == 0,
                      left == right);
        }
    }

    for (const an::Float64Justification left : kAllJustifications)
    {
        EXPECT_TRUE(an::isFloat64Justification(left));
        for (const an::Float64Justification right : kAllJustifications)
        {
            EXPECT_EQ(std::strcmp(an::float64JustificationName(left), an::float64JustificationName(right)) == 0,
                      left == right);
        }
    }

    for (const an::PrecisionError left : kAllErrors)
    {
        for (const an::PrecisionError right : kAllErrors)
        {
            EXPECT_EQ(std::strcmp(an::precisionErrorName(left), an::precisionErrorName(right)) == 0, left == right);
        }
    }

    EXPECT_EQ(an::kPrecisionErrorCount, sizeof(kAllErrors) / sizeof(kAllErrors[0]));
    EXPECT_STREQ(an::computePrecisionName(static_cast<an::ComputePrecision>(200)), "UnknownComputePrecision");
    EXPECT_STREQ(an::outputPrecisionContractName(static_cast<an::OutputPrecisionContract>(200)),
                 "UnknownOutputPrecisionContract");
    EXPECT_STREQ(an::float64JustificationName(static_cast<an::Float64Justification>(200)),
                 "UnknownFloat64Justification");
    EXPECT_STREQ(an::precisionErrorName(static_cast<an::PrecisionError>(200)), "UnknownPrecisionError");
}

} // namespace
