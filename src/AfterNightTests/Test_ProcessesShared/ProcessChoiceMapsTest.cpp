// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The closed maps from the text a dialog offers - and a standalone tool persists - to what the engine is asked for:
/// every text names one of the engine's choices, every name the engine offers is reached from a text, and a text no
/// dialog offers reads as the setting's default and says it was not known. The demosaic texts and the demosaic setting
/// are one choice spelled twice, and read as one method.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/DemosaicTextMaps.h"
#include "processes/ProcessCommonUtils.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QString>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace
{

using an::tests::processes::InstalledContext;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessTestContext;

/// How many enumerators an enumeration that counts from zero has, by its last one.
template <typename EnumT> [[nodiscard]] constexpr std::size_t enumeratorCount(EnumT last) noexcept
{
    return static_cast<std::size_t>(last) + 1u;
}

} // namespace

// Each text the debayer, stacking and registration dialogs offer names one of the engine's methods: "Bilinear" the
// fast gradient-weighted estimate the engine names Fast, "AHD" the luminance-only homogeneity the engine publishes as
// LuminanceAhd, and the rest the methods of their names.
TEST(ProcessChoiceMapsTest, EveryDemosaicTextNamesItsMethod)
{
    struct Case
    {
        QString text;
        an::DemosaicMethod method;
    };
    for (const Case& item : {Case{process_utils::DEMOSAIC_BILINEAR, an::DemosaicMethod::Fast},
                             Case{process_utils::DEMOSAIC_VNG4, an::DemosaicMethod::Vng4},
                             Case{process_utils::DEMOSAIC_AHD, an::DemosaicMethod::LuminanceAhd},
                             Case{process_utils::DEMOSAIC_AMAZE, an::DemosaicMethod::Amaze},
                             Case{process_utils::DEMOSAIC_LMMSE, an::DemosaicMethod::Lmmse},
                             Case{process_utils::DEMOSAIC_RCD, an::DemosaicMethod::Rcd}})
    {
        const process_utils::DemosaicLabelChoice choice = process_utils::demosaicChoiceFromLabel(item.text);
        EXPECT_EQ(choice.method, item.method) << item.text.toStdString();
        EXPECT_TRUE(choice.known) << item.text.toStdString();
    }

    // A text is compared exactly; one no dialog offers - a method name the engine does not carry among them - reads as
    // RCD, the setting's default, and says it was not known.
    for (const QString& text : {QStringLiteral("bilinear"), QStringLiteral("Default"), QString(), QStringLiteral("VNG"),
                                QStringLiteral("DCB"), QStringLiteral("HPHD"), QStringLiteral("IGV")})
    {
        const process_utils::DemosaicLabelChoice choice = process_utils::demosaicChoiceFromLabel(text);
        EXPECT_EQ(choice.method, an::DemosaicMethod::Rcd) << text.toStdString();
        EXPECT_FALSE(choice.known) << text.toStdString();
    }
}

// A demosaic setting is offered in a dialog by its text, and reading that text gives the method the setting itself is
// read as on every open path: the two tables are one choice spelled twice. A name neither table holds reads as RCD in
// both.
TEST(ProcessChoiceMapsTest, TheTextOfADemosaicSettingNamesTheMethodTheSettingIsReadAs)
{
    for (const char* key : {"bilinear", "vng4", "ahd", "amaze", "dcb", "hphd", "igv", "lmmse", "rcd", " AMaZE ",
                            "BILINEAR", "unknown", ""})
    {
        const QString setting = QString::fromLatin1(key);
        const an::ui::DemosaicChoice read = an::ui::demosaicMethodFromSettingsKey(setting);
        const process_utils::DemosaicLabelChoice offered =
            process_utils::demosaicChoiceFromLabel(process_utils::demosaicLabelFromSettingsKey(setting));
        EXPECT_EQ(offered.method, read.method) << key;
        EXPECT_TRUE(offered.known) << key;
    }
}

// Every interpolation the resize, rotate and registration dialogs offer names one of the engine's, and every one the
// engine offers is named. "Auto" is the dialog's to resolve before it asks; read here it is Lanczos-4 and not known.
TEST(ProcessChoiceMapsTest, EveryInterpolationTextNamesAKernelAndEveryKernelHasOne)
{
    struct Case
    {
        QString text;
        an::KernelInterpolation interpolation;
    };
    std::set<an::KernelInterpolation> reached;
    for (const Case& item :
         {Case{process_utils::INTERP_NEAREST_NEIGHBOR, an::KernelInterpolation::Nearest},
          Case{process_utils::INTERP_BILINEAR, an::KernelInterpolation::Linear},
          Case{process_utils::INTERP_BICUBIC, an::KernelInterpolation::Cubic},
          Case{process_utils::INTERP_AREA, an::KernelInterpolation::Area},
          Case{process_utils::INTERP_LANCZOS4, an::KernelInterpolation::Lanczos4},
          Case{process_utils::INTERP_BIT_EXACT_BILINEAR, an::KernelInterpolation::LinearExact},
          Case{process_utils::INTERP_BIT_EXACT_NEAREST_NEIGHBOR, an::KernelInterpolation::NearestExact}})
    {
        const process_utils::InterpolationChoice choice = process_utils::interpolationFromLabel(item.text);
        EXPECT_EQ(choice.interpolation, item.interpolation) << item.text.toStdString();
        EXPECT_TRUE(choice.known) << item.text.toStdString();
        reached.insert(choice.interpolation);
    }
    EXPECT_EQ(reached.size(), enumeratorCount(an::KernelInterpolation::NearestExact));

    for (const QString& text : {process_utils::INTERP_AUTO, QStringLiteral("lanczos4"), QString()})
    {
        const process_utils::InterpolationChoice choice = process_utils::interpolationFromLabel(text);
        EXPECT_EQ(choice.interpolation, an::KernelInterpolation::Lanczos4) << text.toStdString();
        EXPECT_FALSE(choice.known) << text.toStdString();
    }
}

// Every transform the registration dialog offers names one of the engine's kinds, the polynomial ones among them, and
// every kind is named; anything else reads as a similarity and is not known.
TEST(ProcessChoiceMapsTest, EveryTransformTextNamesAKindAndEveryKindHasOne)
{
    struct Case
    {
        const char* text;
        an::RegistrationTransformKind kind;
    };
    std::set<an::RegistrationTransformKind> reached;
    for (const Case& item : {Case{"Similarity", an::RegistrationTransformKind::Similarity},
                             Case{"Affine", an::RegistrationTransformKind::Affine},
                             Case{"Homography", an::RegistrationTransformKind::Projective},
                             Case{"Polynomial 2", an::RegistrationTransformKind::Polynomial2},
                             Case{"Polynomial 3", an::RegistrationTransformKind::Polynomial3},
                             Case{"Polynomial 4", an::RegistrationTransformKind::Polynomial4}})
    {
        const process_utils::TransformChoice choice =
            process_utils::transformKindFromLabel(QString::fromLatin1(item.text));
        EXPECT_EQ(choice.kind, item.kind) << item.text;
        EXPECT_TRUE(choice.known) << item.text;
        reached.insert(choice.kind);
    }
    EXPECT_EQ(reached.size(), enumeratorCount(an::RegistrationTransformKind::Polynomial4));

    for (const char* text : {"Auto", "homography", ""})
    {
        const process_utils::TransformChoice choice = process_utils::transformKindFromLabel(QString::fromLatin1(text));
        EXPECT_EQ(choice.kind, an::RegistrationTransformKind::Similarity) << text;
        EXPECT_FALSE(choice.known) << text;
    }
}

// Every stack a dialog offers, and the second spelling two of its rejections have, names a combination and a
// rejection - a rejection is an average of what it keeps - and every combination and every rejection the engine offers
// is named.
TEST(ProcessChoiceMapsTest, EveryStackTextNamesACombinationAndARejection)
{
    struct Case
    {
        const char* text;
        an::StackCombination combination;
        an::StackRejection rejection;
    };
    std::set<an::StackCombination> combinations;
    std::set<an::StackRejection> rejections;
    for (const Case& item :
         {Case{"Maximum", an::StackCombination::Maximum, an::StackRejection::None},
          Case{"Minimum", an::StackCombination::Minimum, an::StackRejection::None},
          Case{"Average", an::StackCombination::Average, an::StackRejection::None},
          Case{"Sum", an::StackCombination::Sum, an::StackRejection::None},
          Case{"Median", an::StackCombination::Median, an::StackRejection::None},
          Case{"Auto-Adaptive Weighted Average", an::StackCombination::AutoAdaptiveWeightedAverage,
               an::StackRejection::None},
          Case{"Sigma Clipping", an::StackCombination::Average, an::StackRejection::SigmaClipping},
          Case{"MAD Sigma Clipping", an::StackCombination::Average, an::StackRejection::MadSigmaClipping},
          Case{"MAD Clipping", an::StackCombination::Average, an::StackRejection::MadSigmaClipping},
          Case{"Median Sigma", an::StackCombination::Average, an::StackRejection::MedianSigmaReplacement},
          Case{"Winsorized Sigma", an::StackCombination::Average, an::StackRejection::WinsorizedSigmaClipping},
          Case{"Winsorized Sigma Clipping", an::StackCombination::Average, an::StackRejection::WinsorizedSigmaClipping},
          Case{"Linear Fit", an::StackCombination::Average, an::StackRejection::LinearFitClipping},
          Case{"Percentile", an::StackCombination::Average, an::StackRejection::PercentileClipping},
          Case{"GESDT", an::StackCombination::Average, an::StackRejection::GeneralizedEsdTest}})
    {
        const process_utils::StackMethodChoice choice =
            process_utils::stackMethodFromLabel(QStringLiteral("  %1 ").arg(QString::fromLatin1(item.text)));
        EXPECT_EQ(choice.combination, item.combination) << item.text;
        EXPECT_EQ(choice.rejection, item.rejection) << item.text;
        EXPECT_TRUE(choice.known) << item.text;
        combinations.insert(choice.combination);
        rejections.insert(choice.rejection);
    }
    EXPECT_EQ(combinations.size(), enumeratorCount(an::StackCombination::AutoAdaptiveWeightedAverage));
    EXPECT_EQ(rejections.size(), enumeratorCount(an::StackRejection::GeneralizedEsdTest));

    for (const char* text : {"sigma clipping", "Kappa-Sigma", ""})
    {
        const process_utils::StackMethodChoice choice = process_utils::stackMethodFromLabel(QString::fromLatin1(text));
        EXPECT_EQ(choice.combination, an::StackCombination::Average) << text;
        EXPECT_EQ(choice.rejection, an::StackRejection::None) << text;
        EXPECT_FALSE(choice.known) << text;
    }
}

// Every normalization a dialog offers, in either capitalization a stored setting may hold, names one of the engine's
// methods or none, and every method is named; anything else reads as none and is not known.
TEST(ProcessChoiceMapsTest, EveryNormalizationTextNamesAMethodOrNone)
{
    struct Case
    {
        const char* text;
        std::optional<an::NormalizationMethod> method;
    };
    std::set<an::NormalizationMethod> reached;
    for (const Case& item : {Case{"None", std::nullopt}, Case{"Additive", an::NormalizationMethod::Additive},
                             Case{"Multiplicative", an::NormalizationMethod::Multiplicative},
                             Case{"Additive with Scaling", an::NormalizationMethod::AdditiveScaling},
                             Case{"Additive With Scaling", an::NormalizationMethod::AdditiveScaling},
                             Case{"Multiplicative with Scaling", an::NormalizationMethod::MultiplicativeScaling},
                             Case{"Multiplicative With Scaling", an::NormalizationMethod::MultiplicativeScaling}})
    {
        const process_utils::NormalizationChoice choice =
            process_utils::normalizationFromLabel(QStringLiteral(" %1").arg(QString::fromLatin1(item.text)));
        EXPECT_EQ(choice.method, item.method) << item.text;
        EXPECT_TRUE(choice.known) << item.text;
        if (choice.method.has_value())
        {
            reached.insert(*choice.method);
        }
    }
    EXPECT_EQ(reached.size(), enumeratorCount(an::NormalizationMethod::MultiplicativeScaling));

    for (const char* text : {"none", "Additive Scaling", ""})
    {
        const process_utils::NormalizationChoice choice =
            process_utils::normalizationFromLabel(QString::fromLatin1(text));
        EXPECT_FALSE(choice.method.has_value()) << text;
        EXPECT_FALSE(choice.known) << text;
    }
}

// A process that writes a new sequence asks for its path in the session's temporary directory, named for the
// container it writes; without the temporary-file service there is no path.
TEST(ProcessChoiceMapsTest, ANewSequenceIsGivenAPathForItsContainerInTheTemporaryDirectory)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);

    const QString serPath = context->nextTempFilePathForNewSequence(an::SequenceFormat::Ser);
    EXPECT_EQ(QString::fromStdString(process_utils::createTempSequenceFilePath(an::SequenceFormat::Ser)), serPath);
    EXPECT_TRUE(serPath.endsWith(QStringLiteral(".ser")));
    const QString listPath = context->nextTempFilePathForNewSequence(an::SequenceFormat::ImageList);
    EXPECT_EQ(QString::fromStdString(process_utils::createTempSequenceFilePath(an::SequenceFormat::ImageList)),
              listPath);
    EXPECT_TRUE(listPath.endsWith(QStringLiteral(".anseq")));
    EXPECT_TRUE(context->isTempFile(listPath.toStdString()));

    context->lendTemporaryFiles = false;
    EXPECT_TRUE(process_utils::createTempSequenceFilePath(an::SequenceFormat::Ser).empty());
}
