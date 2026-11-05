// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The geometry and filter-array support the windows share, held directly: the interpolation and reducer choices, the
/// names of a mosaic's planes, the mosaics the filter-array windows take apart, the debayer of an X-Trans image, and
/// what a geometry step does to an image's validity and to a floating result's range.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"

#include "processes/CfaProcessSupport.h"
#include "processes/GeometryProcessSupport.h"

#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QComboBox>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::geometry;

/// A six-by-six arrangement, as a camera with a tabulated filter array carries.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// A one-channel description of a size, representation and filter array.
[[nodiscard]] an::ImageDescriptor monoDescriptor(std::uint32_t width, std::uint32_t height, an::CfaDescriptor cfa,
                                                 an::SampleType type = an::SampleType::UInt16)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    an::ImageDescriptorRequest request{.extent = *extent,
                                       .sampleType = type,
                                       .channelSchema = an::ChannelSchema::Mono,
                                       .colorSpace = an::ColorSpace::Unspecified,
                                       .numericDomain = an::NumericDomain::NormalizedLinear,
                                       .cfa = cfa,
                                       .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                       .pixelAspectRatio = 1.0};
    return *an::ImageDescriptor::create(request);
}

/// A Float32 grey image of a size whose samples a function decides, under a validity rule and an optional mask.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
greyAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
          const std::function<float(std::uint32_t, std::uint32_t)>& value, an::NumericDomain domain,
          an::ValidityRule rule = an::ValidityRule::AllValid, bool maskFirstPixel = false)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    an::ImageDescriptorRequest request =
        monoDescriptor(width, height, an::CfaDescriptor{}, an::SampleType::Float32).toRequest();
    request.numericDomain = domain;
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    an::ImageSemanticsRequest meaning;
    meaning.validity = *an::ValidityDescriptor::create({.rule = rule});
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *an::ImageSemantics::create(meaning),
                                  an::AstrometricSolution{}, an::Provenance{}, an::CodecMetadata{}, std::nullopt);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            auto row = access->view().row(0u, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (*row)[x] = value(x, y);
            }
        }
    }
    std::optional<an::ValidityMask> mask;
    if (rule == an::ValidityRule::ExplicitMask)
    {
        an::Result<an::ValidityMask> made = an::ValidityMask::create(scope->engine(), described->extent());
        EXPECT_TRUE(made.hasValue());
        std::optional<an::ValidityMaskView<std::uint8_t>> view = made->view();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (void)view->setValid(0u, x, y, !(maskFirstPixel && x == 0u && y == 0u));
            }
        }
        mask.emplace(std::move(*made));
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)), std::move(mask));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// Runs steps over an asset and hands back the result, still an asset.
[[nodiscard]] std::optional<an::consumer::ImageAsset> transformed(const an::consumer::EngineSession& session,
                                                                  const an::consumer::ImageAsset& asset,
                                                                  std::vector<an::ui::processes::GeometryStep> steps,
                                                                  bool mosaicAsGrey = false)
{
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(scope.hasValue() && lease.hasValue());
    if (!scope.hasValue() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> made =
        an::ui::processes::transformImage(*scope, an::ui::ProcessSource::of(*lease), steps, "a case", mosaicAsGrey);
    lease->release();
    scope->finish(made.hasValue() ? an::Status::success() : made.status());
    if (!made.hasValue())
    {
        ADD_FAILURE() << made.status().message();
        return std::nullopt;
    }
    return std::move(*made);
}

/// An RGGB mosaic of a size and representation whose codes a function decides.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
mosaicAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
            const std::function<SampleT(std::uint32_t, std::uint32_t)>& code)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    const an::ImageDescriptor described = monoDescriptor(
        width, height, *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u), an::kSampleTypeOf<SampleT>);
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        scope->engine(), described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, {});
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            auto row = access->view().row(0u, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (*row)[x] = code(x, y);
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// The hydrogen-alpha and OIII pair extracted from a mosaic, as samples.
[[nodiscard]] std::vector<fixtures::FixtureSamples> extractedPair(const an::consumer::EngineSession& session,
                                                                  const an::consumer::ImageAsset& mosaic)
{
    std::vector<fixtures::FixtureSamples> pair;
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = mosaic.read();
    if (!scope.hasValue() || !lease.hasValue())
    {
        ADD_FAILURE() << "the extraction was not admitted";
        return pair;
    }
    an::Result<std::vector<an::consumer::ImageAsset>> made = an::ui::processes::extractHydrogenAlphaAndOxygenIII(
        *scope, an::ui::ProcessSource::of(*lease), an::NarrowbandExtractionScaling::Native);
    lease->release();
    scope->finish(made.hasValue() ? an::Status::success() : made.status());
    if (!made.hasValue())
    {
        ADD_FAILURE() << made.status().message();
        return pair;
    }
    for (const an::consumer::ImageAsset& product : *made)
    {
        std::optional<fixtures::FixtureSamples> samples = assetSamples(product);
        if (samples.has_value())
        {
            pair.push_back(std::move(*samples));
        }
    }
    return pair;
}

class GeometryUnits : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
};

} // namespace

// The resize and rotate dialogs offer their interpolations in one order, Auto first under the text that says what it
// chooses, each item carrying its own name.
TEST_F(GeometryUnits, TheInterpolationChoicesAreOfferedAutoFirstEachUnderItsName)
{
    QComboBox combo;
    an::ui::processes::fillInterpolationChoices(&combo, QStringLiteral("Auto (shown)"));
    const QStringList names{"Auto",
                            "Nearest Neighbor",
                            "Bilinear",
                            "Bicubic",
                            "Pixel Area Relation",
                            "Lanczos4",
                            "Bit Exact Bilinear",
                            "Bit Exact Nearest Neighbor"};
    ASSERT_EQ(combo.count(), names.size());
    EXPECT_EQ(combo.itemText(0), QStringLiteral("Auto (shown)"));
    for (int index = 0; index < combo.count(); ++index)
    {
        EXPECT_EQ(combo.itemData(index).toString(), names[index]);
    }
    an::ui::processes::selectInterpolationName(&combo, QStringLiteral("Bicubic"));
    EXPECT_EQ(an::ui::processes::selectedInterpolationName(&combo), QStringLiteral("Bicubic"));
    EXPECT_EQ(an::ui::processes::resolveResizeInterpolation(QStringLiteral("Bit Exact Bilinear"), true).interpolation,
              an::KernelInterpolation::LinearExact);
    const an::ui::processes::ResolvedInterpolation unknown =
        an::ui::processes::resolveRotationInterpolation(QStringLiteral("Sinc"));
    EXPECT_EQ(unknown.interpolation, an::KernelInterpolation::Lanczos4);
    EXPECT_EQ(unknown.label, QStringLiteral("Lanczos4")) << "the step names what ran, not the text it was given";
}

// The integer resample dialogs' reducers, an unknown text reading as Average, and a reduction that discards an
// incomplete block at the edge.
TEST_F(GeometryUnits, TheFourReducersDiscardAnIncompleteBlock)
{
    EXPECT_EQ(an::ui::processes::integerReductionLabels(),
              (std::vector<QString>{"Average", "Median", "Maximum", "Minimum"}));
    EXPECT_EQ(an::ui::processes::integerReductionFromLabel(QStringLiteral("Median")), an::IntegerReduction::Median);
    EXPECT_EQ(an::ui::processes::integerReductionFromLabel(QStringLiteral("Maximum")), an::IntegerReduction::Maximum);
    EXPECT_EQ(an::ui::processes::integerReductionFromLabel(QStringLiteral("Minimum")), an::IntegerReduction::Minimum);
    EXPECT_EQ(an::ui::processes::integerReductionFromLabel(QStringLiteral("Mode")), an::IntegerReduction::Average);
    const an::IntegerResampleRequest reduce =
        an::ui::processes::integerResampleRequest(3u, false, an::IntegerReduction::Average);
    const std::array<an::ui::processes::GeometryStep, 1u> steps{reduce};
    const an::Result<an::ImageDescriptor> result =
        an::ui::processes::geometryResultDescriptor(monoDescriptor(13u, 8u, an::CfaDescriptor{}), steps);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->extent().width(), 4u);
    EXPECT_EQ(result->extent().height(), 2u);
}

// A mosaic's planes are named after their position and the filter over it, under the mosaic's own phase.
TEST_F(GeometryUnits, PlaneNamesFollowTheArrangementAndPhase)
{
    const auto names = [](an::CfaPattern pattern, std::uint32_t x, std::uint32_t y) {
        return an::ui::processes::mosaicPlaneNames(*an::CfaDescriptor::create(pattern, x, y));
    };
    EXPECT_EQ(names(an::CfaPattern::Rggb, 0u, 0u),
              (std::array<std::string, 4u>{"CFA0 - Red", "CFA1 - Green1", "CFA2 - Green2", "CFA3 - Blue"}));
    EXPECT_EQ(names(an::CfaPattern::Bggr, 0u, 0u),
              (std::array<std::string, 4u>{"CFA0 - Blue", "CFA1 - Green1", "CFA2 - Green2", "CFA3 - Red"}));
    EXPECT_EQ(names(an::CfaPattern::Gbrg, 0u, 0u),
              (std::array<std::string, 4u>{"CFA0 - Green1", "CFA1 - Blue", "CFA2 - Red", "CFA3 - Green2"}));
    EXPECT_EQ(names(an::CfaPattern::Grbg, 0u, 0u),
              (std::array<std::string, 4u>{"CFA0 - Green1", "CFA1 - Red", "CFA2 - Blue", "CFA3 - Green2"}));
    EXPECT_EQ(names(an::CfaPattern::Rggb, 1u, 0u), names(an::CfaPattern::Grbg, 0u, 0u))
        << "an origin one column in is the arrangement that starts there";
}

// The filter-array windows take apart a one-channel mosaic of one of the four two-by-two arrangements, and refuse a
// tabulated one, an image that states none and a colour image, each by name.
TEST_F(GeometryUnits, OnlyATwoByTwoMosaicIsTakenApart)
{
    EXPECT_TRUE(an::ui::processes::checkBayerMosaic(
                    monoDescriptor(8u, 8u, *an::CfaDescriptor::create(an::CfaPattern::Gbrg, 0u, 0u)))
                    .ok());
    const an::Status tabulated =
        an::ui::processes::checkBayerMosaic(monoDescriptor(12u, 12u, *an::CfaDescriptor::create(kCameraTable, 0u, 0u)));
    EXPECT_EQ(tabulated.code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(tabulated.message()).find("not a two-by-two arrangement"), std::string::npos)
        << tabulated.message();
    const an::Status grey = an::ui::processes::checkBayerMosaic(monoDescriptor(8u, 8u, an::CfaDescriptor{}));
    EXPECT_EQ(grey.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::ui::processes::checkBayerMosaic(std::nullopt).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::ui::processes::representationRank(an::SampleType::Int16) <
                  an::ui::processes::representationRank(an::SampleType::UInt16),
              true);
    EXPECT_EQ(an::ui::processes::representationRank(an::SampleType::UInt32) <
                  an::ui::processes::representationRank(an::SampleType::Float32),
              true);
}

// The debayer asked the engine for its Bayer method under an X-Trans image's own table, which the engine refused, and
// read a named two-by-two pattern over such an image as the filters at its origin. From metadata demosaics it by the
// method's tabulated counterpart, and a named pattern is refused, naming the arrangement.
TEST_F(GeometryUnits, DebayerReadsAnXTransArrayFromTheImageAndRefusesANamedBayerPatternOverIt)
{
    const std::optional<an::consumer::ImageAsset> mosaic = an::tests::viewer::xtransMosaicAsset(session());
    ASSERT_TRUE(mosaic.has_value());
    const auto debayer = [&](const QString& pattern) -> an::Result<std::vector<an::consumer::ImageAsset>> {
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
        an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
        if (!scope.hasValue() || !lease.hasValue())
        {
            return an::Status::failure(an::ErrorCode::Internal, "the debayer was not admitted");
        }
        an::Result<std::vector<an::consumer::ImageAsset>> made = an::ui::processes::debayerImage(
            *scope, an::ui::ProcessSource::of(*lease), an::DemosaicMethod::Rcd,
            an::ui::bayerPatternChoiceFromText(pattern), an::ui::processes::DebayerOutput::Combined);
        lease->release();
        scope->finish(made.hasValue() ? an::Status::success() : made.status());
        return made;
    };

    const an::Result<std::vector<an::consumer::ImageAsset>> own = debayer(an::ui::kBayerPatternFromImageText);
    ASSERT_TRUE(own.hasValue()) << own.status().message();
    ASSERT_EQ(own->size(), 1u);
    const std::optional<an::ImageDescriptor> colour = own->front().descriptor();
    ASSERT_TRUE(colour.has_value());
    EXPECT_EQ(colour->extent().channelCount(), 3u);
    EXPECT_FALSE(colour->isMosaiced());
    EXPECT_TRUE(an::tests::viewer::lineageMentions(own->front(), "MarkesteijnThreePass"));

    const an::Result<std::vector<an::consumer::ImageAsset>> named = debayer(an::ui::kBayerPatternRggbText);
    ASSERT_FALSE(named.hasValue());
    EXPECT_EQ(named.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(named.status().message()).find("X-Trans"), std::string::npos) << named.status().message();
}

// A step keeps an image's validity rule where it wrote every sample from the source's, carries an explicit mask with
// the samples, and marks a sample it could not write invalid in a mask of its own.
TEST_F(GeometryUnits, AStepKeepsTheRuleCarriesTheMaskAndMarksWhatItCouldNotWrite)
{
    const auto ramp = [](std::uint32_t x, std::uint32_t y) { return 0.1f + 0.01f * static_cast<float>(x + 3u * y); };
    // Every sample valid, a crop: the rule stays and no mask is carried.
    std::optional<an::consumer::ImageAsset> plain =
        greyAsset(session(), 8u, 6u, ramp, an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(plain.has_value());
    std::optional<an::consumer::ImageAsset> cropped =
        transformed(session(), *plain, {an::CropRequest{.x = 1u, .y = 1u, .width = 4u, .height = 3u}});
    ASSERT_TRUE(cropped.has_value());
    {
        an::Result<an::consumer::AssetReadLease> lease = cropped->read();
        ASSERT_TRUE(lease.hasValue());
        EXPECT_EQ(lease->validity(), nullptr);
        const an::ImageMetadata& metadata =
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
        EXPECT_EQ(metadata.semantics().validity().rule(), an::ValidityRule::AllValid);
    }
    // An explicit mask whose first pixel is invalid, a crop from the origin: the mask comes with the samples.
    std::optional<an::consumer::ImageAsset> masked =
        greyAsset(session(), 8u, 6u, ramp, an::NumericDomain::NormalizedLinear, an::ValidityRule::ExplicitMask, true);
    ASSERT_TRUE(masked.has_value());
    std::optional<an::consumer::ImageAsset> maskedCrop =
        transformed(session(), *masked, {an::CropRequest{.x = 0u, .y = 0u, .width = 4u, .height = 3u}});
    ASSERT_TRUE(maskedCrop.has_value());
    {
        an::Result<an::consumer::AssetReadLease> lease = maskedCrop->read();
        ASSERT_TRUE(lease.hasValue());
        ASSERT_NE(lease->validity(), nullptr) << "the explicit mask is carried";
        const std::optional<an::ValidityMaskView<const std::uint8_t>> view = lease->validity()->view();
        EXPECT_EQ(view->test(0u, 0u, 0u), std::optional<bool>(false));
        EXPECT_EQ(view->test(0u, 1u, 0u), std::optional<bool>(true));
    }
    // A sample holding no number under a rule that calls it invalid: a resize that needs every neighbour cannot write
    // the samples beside it, and marks them invalid in a mask.
    std::optional<an::consumer::ImageAsset> holed = greyAsset(
        session(), 8u, 6u,
        [ramp](std::uint32_t x, std::uint32_t y) {
            return x == 3u && y == 3u ? std::numeric_limits<float>::quiet_NaN() : ramp(x, y);
        },
        an::NumericDomain::NormalizedLinear, an::ValidityRule::FiniteOnly);
    ASSERT_TRUE(holed.has_value());
    std::optional<an::consumer::ImageAsset> resized =
        transformed(session(), *holed, {an::ui::processes::resizeRequest(16u, 12u, an::KernelInterpolation::Linear)});
    ASSERT_TRUE(resized.has_value());
    {
        // A sample beside the hole is invalid under what the result states - marked in a mask where the step left it
        // unwritten, or holding no number under the source's own rule - and one far from it is valid.
        const std::optional<fixtures::FixtureSamples> made = assetSamples(*resized);
        ASSERT_TRUE(made.has_value());
        an::Result<an::consumer::AssetReadLease> lease = resized->read();
        ASSERT_TRUE(lease.hasValue());
        const an::ImageMetadata& metadata =
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
        const an::ValidityRule rule = metadata.semantics().validity().rule();
        const auto valid = [&](std::uint32_t x, std::uint32_t y) {
            if (rule == an::ValidityRule::ExplicitMask)
            {
                return lease->validity() != nullptr && lease->validity()->view()->test(0u, x, y).value_or(false);
            }
            EXPECT_EQ(rule, an::ValidityRule::FiniteOnly) << "the source's rule is kept";
            return std::isfinite(made->value(x, y, 0u));
        };
        EXPECT_FALSE(valid(7u, 7u)) << "a sample beside the hole";
        EXPECT_TRUE(valid(0u, 0u)) << "a sample far from it";
    }
}

// A resize or a rotation of a floating image in a normalized domain is held to the unit interval, and one in a
// physical domain is not; a crop never is.
TEST_F(GeometryUnits, ANormalizedFloatingResultIsHeldToTheUnitInterval)
{
    const auto step = [](std::uint32_t x, std::uint32_t) { return x < 4u ? 0.0f : 1.0f; };
    for (const an::NumericDomain domain : {an::NumericDomain::NormalizedLinear, an::NumericDomain::PhysicalScaled})
    {
        SCOPED_TRACE(domain == an::NumericDomain::NormalizedLinear ? "normalized" : "physical");
        std::optional<an::consumer::ImageAsset> edge = greyAsset(session(), 8u, 6u, step, domain);
        ASSERT_TRUE(edge.has_value());
        std::optional<an::consumer::ImageAsset> resized = transformed(
            session(), *edge, {an::ui::processes::resizeRequest(24u, 18u, an::KernelInterpolation::Lanczos4)});
        ASSERT_TRUE(resized.has_value());
        const std::optional<fixtures::FixtureSamples> samples = assetSamples(*resized);
        ASSERT_TRUE(samples.has_value());
        double low = 1.0;
        double high = 0.0;
        for (std::uint32_t y = 0u; y < samples->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples->width(); ++x)
            {
                low = (std::min)(low, samples->value(x, y, 0u));
                high = (std::max)(high, samples->value(x, y, 0u));
            }
        }
        if (domain == an::NumericDomain::NormalizedLinear)
        {
            EXPECT_GE(low, 0.0);
            EXPECT_LE(high, 1.0);
        }
        else
        {
            EXPECT_TRUE(low < 0.0 || high > 1.0) << "Lanczos rings past a physical step, which is kept";
        }
    }
}

// A mosaic read as grey forgets its filter array; read as it is, a resize refuses it by name.
TEST_F(GeometryUnits, AMosaicReadAsGreyForgetsItsFilterArray)
{
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples("mosaic-rggb-uint16");
    ASSERT_TRUE(scene.has_value());
    const fixtures::FixtureImage* image = fixtures::fixtureNamed("mosaic-rggb-uint16");
    ASSERT_NE(image, nullptr);
    an::Result<an::consumer::ImageAsset> mosaic =
        an::tests::fixtures::engine::makeImageAsset(session(), *image, *scene);
    ASSERT_TRUE(mosaic.hasValue());
    const std::vector<an::ui::processes::GeometryStep> steps{
        an::ui::processes::resizeRequest(12u, 8u, an::KernelInterpolation::Area)};
    std::optional<an::consumer::ImageAsset> grey = transformed(session(), *mosaic, steps, true);
    ASSERT_TRUE(grey.has_value());
    EXPECT_FALSE(grey->descriptor()->isMosaiced());
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
    ASSERT_TRUE(scope.hasValue() && lease.hasValue());
    const an::Result<an::consumer::ImageAsset> refused =
        an::ui::processes::transformImage(*scope, an::ui::ProcessSource::of(*lease), steps, "a case");
    EXPECT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    lease->release();
    scope->finish(an::Status::success());
}

// The resize the dialog asks for repeats the edge sample and guards against dark ringing; the rotation turns clockwise
// about the centre onto a black border.
TEST_F(GeometryUnits, TheResizeRepeatsTheEdgeAndTheRotationTurnsClockwise)
{
    const an::ResizeRequest resize = an::ui::processes::resizeRequest(18u, 12u, an::KernelInterpolation::Cubic);
    EXPECT_EQ(resize.width, 18u);
    EXPECT_EQ(resize.height, 12u);
    EXPECT_EQ(resize.sampling.interpolation, an::KernelInterpolation::Cubic);
    EXPECT_EQ(resize.sampling.border, an::KernelBorderMode::Clamp);
    EXPECT_EQ(resize.sampling.invalid, an::SamplingInvalidPolicy::RequireAll);
    EXPECT_EQ(resize.ringing, an::ResizeRingingPolicy::SuppressDarkRinging);
    const an::RotationRequest rotation =
        an::ui::processes::rotationRequest(90.0, true, an::KernelInterpolation::Linear);
    EXPECT_NEAR(rotation.angleRadians, 1.5707963267948966, 1e-12) << "a positive angle turns clockwise";
    EXPECT_EQ(rotation.canvas, an::RotationCanvas::ExpandToFit);
    EXPECT_EQ(rotation.sampling.border, an::KernelBorderMode::Constant);
    EXPECT_EQ(rotation.sampling.constantBorder, 0.0f);
    EXPECT_EQ(an::ui::processes::rotationRequest(5.0, false, an::KernelInterpolation::Linear).canvas,
              an::RotationCanvas::Preserve);
    const an::IntegerResampleRequest enlarge =
        an::ui::processes::integerResampleRequest(3u, true, an::IntegerReduction::Median);
    EXPECT_EQ(enlarge.direction, an::IntegerResampleDirection::Enlarge);
    EXPECT_EQ(enlarge.factor, 3u);
    const an::IntegerResampleRequest reduce =
        an::ui::processes::integerResampleRequest(2u, false, an::IntegerReduction::Maximum);
    EXPECT_EQ(reduce.direction, an::IntegerResampleDirection::Reduce);
    EXPECT_EQ(reduce.reduction, an::IntegerReduction::Maximum);
    EXPECT_EQ(reduce.remainder, an::IntegerRemainderPolicy::DiscardPartialEdges);
}

// A resize keeps the source's pixel aspect exactly, whatever the rounding of the scales it is planned with, so the
// image it makes can be rotated: a 6000 by 4000 frame reduced with its proportions kept to 295 by 196 or to 64 by 42
// states square pixels, and a frame whose pixels are not square keeps its own aspect.
TEST_F(GeometryUnits, AResizeKeepsTheSourcesPixelAspectExactly)
{
    const an::ImageDescriptor square = monoDescriptor(6000u, 4000u, an::CfaDescriptor{}, an::SampleType::Float32);
    an::ImageDescriptorRequest stretchedRequest = square.toRequest();
    stretchedRequest.pixelAspectRatio = 1.25;
    const std::optional<an::ImageDescriptor> stretched = an::ImageDescriptor::create(stretchedRequest);
    ASSERT_TRUE(stretched.has_value());
    struct Case
    {
        const an::ImageDescriptor* source;
        std::uint32_t width;
        std::uint32_t height;
    };
    for (const Case& item : {Case{&square, 295u, 196u}, Case{&square, 64u, 42u}, Case{&*stretched, 290u, 190u},
                             Case{&*stretched, 293u, 194u}})
    {
        SCOPED_TRACE(std::to_string(item.width) + "x" + std::to_string(item.height) + " at aspect " +
                     std::to_string(item.source->pixelAspectRatio()));
        const std::array<an::ui::processes::GeometryStep, 1u> steps{
            an::ui::processes::resizeRequest(item.width, item.height, an::KernelInterpolation::Area)};
        const an::Result<an::ImageDescriptor> resized =
            an::ui::processes::geometryResultDescriptor(*item.source, steps);
        ASSERT_TRUE(resized.hasValue()) << resized.status().message();
        EXPECT_EQ(resized->extent().width(), item.width);
        EXPECT_EQ(resized->extent().height(), item.height);
        EXPECT_EQ(resized->pixelAspectRatio(), item.source->pixelAspectRatio());
        if (item.source->pixelAspectRatio() == 1.0)
        {
            const an::Result<an::GeometryPlan> rotation = an::ui::processes::planGeometryStep(
                *resized, an::ui::processes::rotationRequest(30.0, false, an::KernelInterpolation::Lanczos4));
            EXPECT_TRUE(rotation.hasValue()) << rotation.status().message();
        }
    }
}

// The hydrogen-alpha and OIII pair is worked as fractions of full scale and narrowed back the way they were read: a
// signed mosaic's codes are counted from its smallest value, so a signed mosaic and the unsigned one whose codes stand
// for the same fractions make the same pair, half the range apart.
TEST_F(GeometryUnits, TheNarrowbandPairOfASignedMosaicIsCountedFromItsSmallestValue)
{
    const auto code = [](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::uint16_t>(4000u + 2500u * ((x * 7u + y * 3u) % 11u) + 900u * (x % 2u));
    };
    const std::optional<an::consumer::ImageAsset> unsignedMosaic = mosaicAsset<std::uint16_t>(session(), 8u, 6u, code);
    const std::optional<an::consumer::ImageAsset> signedMosaic =
        mosaicAsset<std::int16_t>(session(), 8u, 6u, [&code](std::uint32_t x, std::uint32_t y) {
            return static_cast<std::int16_t>(static_cast<int>(code(x, y)) - 32768);
        });
    ASSERT_TRUE(unsignedMosaic.has_value() && signedMosaic.has_value());
    const std::vector<fixtures::FixtureSamples> fromUnsigned = extractedPair(session(), *unsignedMosaic);
    const std::vector<fixtures::FixtureSamples> fromSigned = extractedPair(session(), *signedMosaic);
    ASSERT_EQ(fromUnsigned.size(), 2u);
    ASSERT_EQ(fromSigned.size(), 2u);
    for (std::size_t product = 0u; product < 2u; ++product)
    {
        ASSERT_EQ(fromSigned[product].width(), fromUnsigned[product].width());
        ASSERT_EQ(fromSigned[product].height(), fromUnsigned[product].height());
        for (std::uint32_t y = 0u; y < fromSigned[product].height(); ++y)
        {
            for (std::uint32_t x = 0u; x < fromSigned[product].width(); ++x)
            {
                EXPECT_NEAR(fromSigned[product].value(x, y, 0u), fromUnsigned[product].value(x, y, 0u) - 32768.0, 1.0)
                    << "product " << product << " at " << x << ", " << y;
            }
        }
    }
}
