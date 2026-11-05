// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The helpers the pointwise and tone processes share, and the processes' own maps from a dialog's values to the
/// engine's: samples held as fractions of full scale and narrowed back, the validity a value rule states judged on the
/// stored samples, a sample that holds no number counted out, the nonlinear record a result keeps, what a history step
/// says of its channel, the exposure factor, the saturation's hue ranges and the HSL and LCh adjustments.

#include "Test_ProcessesPointwise/PointwiseTestSupport.h"

#include "processes/AdjustHSLProcess.h"
#include "processes/AdjustLChProcess.h"
#include "processes/ColorSaturationProcess.h"
#include "processes/ExposureCorrectionProcess.h"
#include "processes/PointwiseProcessUtils.h"

#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::pointwise;
using an::tests::viewer::Channels;

/// A one-channel planar image of a representation holding codes, in a row.
template <typename SampleT>
[[nodiscard]] std::optional<an::Image<SampleT>> planeOf(const an::EngineContext& engine,
                                                        const std::vector<SampleT>& codes)
{
    const std::optional<an::ImageDescriptor> described = an::tests::viewer::describeImage(
        engine, an::kSampleTypeOf<SampleT>, static_cast<std::uint32_t>(codes.size()), 1u, Channels::Grey);
    EXPECT_TRUE(described.has_value());
    if (!described.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        auto row = access->view().row(0u, 0u);
        for (std::size_t i = 0u; i < codes.size(); ++i)
        {
            (*row)[i] = codes[i];
        }
    }
    return std::move(*image);
}

/// The codes of a one-channel image.
template <typename SampleT> [[nodiscard]] std::vector<SampleT> codesOf(const an::Image<SampleT>& image)
{
    const auto view = image.view();
    const auto row = view->row(0u, 0u);
    return std::vector<SampleT>(row->begin(), row->end());
}

/// The working values a set of samples holds.
[[nodiscard]] std::vector<double> workingValuesOf(const process_utils::NormalizedSamples& samples)
{
    std::vector<double> values;
    const std::uint32_t width = samples.layout().extent().width();
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        if (samples.sampleType() == an::SampleType::Float64)
        {
            values.push_back(static_cast<const double*>(samples.samples())[x]);
        }
        else
        {
            values.push_back(static_cast<const float*>(samples.samples())[x]);
        }
    }
    return values;
}

/// Runs a scale and a bias over working samples in place, under their validity.
[[nodiscard]] an::Status scaleWorking(const an::ExecutionContext& execution, process_utils::NormalizedSamples& samples,
                                      double scale, double bias)
{
    const an::Result<an::PointwiseProgram> program =
        an::buildScaleBiasProgram(process_utils::normalizedPlanShape(samples.sampleType(), 1u), scale, bias);
    if (!program.hasValue())
    {
        return program.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = samples.samples(), .layout = &samples.layout()}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *program, sources,
        an::PointwiseMutableImageView{.samples = samples.samples(), .layout = &samples.layout()}, samples.validity());
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// Opens a one-channel image's samples for work.
template <typename SampleT>
[[nodiscard]] an::Result<process_utils::NormalizedSamples> openPlane(const an::consumer::OperationScope& scope,
                                                                     an::Image<SampleT>& image,
                                                                     const an::KernelValidityView& validity = {})
{
    std::optional<an::ImageWriteAccess<SampleT>> access = image.write();
    return process_utils::NormalizedSamples::open(scope.engine(), scope.execution(), *image.descriptor(),
                                                  access->view().data(), access->view().layout(), validity);
}

class PointwiseUnitsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// A process destination over an image of the session, its metadata the one given.
[[nodiscard]] std::optional<an::consumer::ImageAsset> assetWithMetadata(const an::consumer::EngineSession& session,
                                                                        an::ImageMetadata metadata)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, 4u, 3u, Channels::Rgb);
    an::Result<an::Image<float>> image = an::Image<float>::createFilled(
        scope->engine(), *described, an::StorageArrangement::Planar, 0.25f, std::move(metadata));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// The metadata the cases' sources carry: a linear meaning, the cards given, and an unrelated card.
[[nodiscard]] an::ImageMetadata metadataWith(an::TransferFunction transfer, std::vector<an::CodecCard> state)
{
    an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "");
    state.push_back(*object);
    an::Result<an::CodecMetadata> cards = an::CodecMetadata::create(an::SourceEncoding{}, std::move(state));
    an::ImageSemanticsRequest meaning = an::ImageSemantics{}.toRequest();
    meaning.transferFunction = transfer;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    return an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                     *cards, std::nullopt);
}

[[nodiscard]] an::CodecCard card(const char* keyword, const char* value)
{
    return *an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, "");
}

/// The metadata of what an asset holds.
[[nodiscard]] an::ImageMetadata metadataOf(const an::consumer::AssetWriteLease& lease)
{
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease.image());
}

/// Runs a chain as the helper ran it before it worked in bands: the whole plane set opened as one working copy, the
/// fused chain run over it under the samples' validity and the copy narrowed back, in place.
[[nodiscard]] an::Status runOverWholePlanes(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described,
                                            void* samples, const an::ImageLayout& layout,
                                            const an::KernelValidityView& validity, const an::PointwiseProgram& chain)
{
    an::Result<process_utils::NormalizedSamples> normalized =
        process_utils::NormalizedSamples::open(scope.engine(), scope.execution(), described, samples, layout, validity);
    if (!normalized.hasValue())
    {
        return normalized.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = normalized->samples(), .layout = &normalized->layout()}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        scope.execution(), chain, sources,
        an::PointwiseMutableImageView{.samples = normalized->samples(), .layout = &normalized->layout()},
        normalized->validity());
    if (!ran.hasValue())
    {
        return ran.status();
    }
    return normalized->commit(scope.execution());
}

/// One way of building the chains a case runs: its name, and the chain for a working shape.
struct NamedChain
{
    const char* name = "";
    process_utils::PointwiseChainBuilder build;
};

/// A chain of scales, biases and clamps, which an eight- or sixteen-bit plane takes as a lookup, and one with a power
/// in it, which no plane does.
[[nodiscard]] std::vector<NamedChain> chainsUnderTest()
{
    return {
        NamedChain{.name = "brightness and contrast",
                   .build = [](const an::PointwisePlanShape& shape) -> an::Result<process_utils::PointwiseChain> {
                       const an::ClampInterval unit{.low = 0.0, .high = 1.0};
                       process_utils::PointwiseChain chain;
                       chain.push_back(*an::buildBrightnessProgram(shape, 0.137));
                       chain.push_back(*an::buildClampProgram(shape, unit));
                       chain.push_back(*an::buildContrastProgram(shape, 1.61, 0.5));
                       chain.push_back(*an::buildClampProgram(shape, unit));
                       return chain;
                   }},
        NamedChain{.name = "a power",
                   .build = [](const an::PointwisePlanShape& shape) -> an::Result<process_utils::PointwiseChain> {
                       an::TonePlanShape tone;
                       tone.sampleType = shape.sampleType;
                       tone.inputDomain = an::NumericDomain::NormalizedLinear;
                       tone.outputDomain = an::NumericDomain::EncodedNonlinear;
                       tone.channelCount = shape.channelCount;
                       tone.precision = shape.precision;
                       tone.precisionJustification = shape.precisionJustification;
                       an::Result<an::PowerProgramPlan> power =
                           an::buildGammaProgram(tone, 1.7, an::GammaDirection::Encode);
                       if (!power.hasValue())
                       {
                           return power.status();
                       }
                       process_utils::PointwiseChain chain;
                       chain.push_back(*an::buildClampProgram(shape, an::ClampInterval{.low = 0.0, .high = 1.0}));
                       chain.push_back(power->program());
                       return chain;
                   }},
    };
}

/// Every sample of a planar image, row by row, without the padding after a row.
[[nodiscard]] std::vector<std::byte> samplesOf(const an::consumer::AnyImage& image)
{
    std::vector<std::byte> bytes;
    std::visit(
        [&](const auto& typed) {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            for (std::uint32_t channel = 0u; channel < view->channelCount(); ++channel)
            {
                for (std::uint32_t y = 0u; y < view->height(); ++y)
                {
                    const auto row = view->row(channel, y);
                    const auto* first = reinterpret_cast<const std::byte*>(row->data());
                    bytes.insert(bytes.end(), first, first + row->size() * sizeof(SampleT));
                }
            }
        },
        image);
    return bytes;
}

/// A colour planar image of a representation, three channels of codes spread over its whole range, its ends and zero
/// among them; or of floating values about the unit interval, past it on both sides.
[[nodiscard]] std::optional<an::consumer::AnyImage> spreadImage(const an::EngineContext& engine, an::SampleType type,
                                                                std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(engine, type, width, height, Channels::Rgb);
    if (!described.has_value())
    {
        return std::nullopt;
    }
    std::optional<an::consumer::AnyImage> made;
    (void)an::dispatchSampleType(type, [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
            engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
        if (!image.hasValue())
        {
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            std::uint64_t state = 0x9E3779B97F4A7C15ull;
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                for (std::uint32_t y = 0u; y < height; ++y)
                {
                    auto row = access->view().row(channel, y);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        state = state * 6364136223846793005ull + 1442695040888963407ull;
                        const std::uint64_t bits = state >> 11u;
                        if constexpr (std::is_floating_point_v<SampleT>)
                        {
                            (*row)[x] =
                                static_cast<SampleT>(-0.25 + 1.5 * static_cast<double>(bits % 1000003u) / 1000003.0);
                        }
                        else if ((x + y + channel) % 11u == 0u)
                        {
                            const std::array<SampleT, 3u> ends{(std::numeric_limits<SampleT>::lowest)(),
                                                               (std::numeric_limits<SampleT>::max)(), SampleT{0}};
                            (*row)[x] = ends[(x + channel) % 3u];
                        }
                        else
                        {
                            (*row)[x] = static_cast<SampleT>(bits);
                        }
                    }
                }
            }
        }
        made.emplace(std::move(*image));
    });
    return made;
}

/// A copy of an image, its own storage, laid out as the image is.
[[nodiscard]] std::optional<an::consumer::AnyImage> copyOfImage(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) -> std::optional<an::consumer::AnyImage> {
            auto copy = typed.clone();
            if (!copy.hasValue())
            {
                return std::nullopt;
            }
            return an::consumer::AnyImage(std::move(*copy));
        },
        image);
}

/// The first sample and layout of an image, for writing.
struct WrittenSamples
{
    void* samples = nullptr;
    std::optional<an::ImageLayout> layout;
    std::optional<an::ImageDescriptor> described;
};

[[nodiscard]] WrittenSamples writtenOf(an::consumer::AnyImage& image)
{
    return std::visit(
        [](auto& typed) {
            WrittenSamples written;
            auto access = typed.write();
            written.samples = access->view().data();
            written.layout = access->view().layout();
            written.described = typed.descriptor();
            return written;
        },
        image);
}

/// Every sample of an image set to one byte pattern that no result of these chains holds in every byte.
void poison(an::consumer::AnyImage& image)
{
    const WrittenSamples written = writtenOf(image);
    std::memset(written.samples, 0xA5, static_cast<std::size_t>(written.layout->totalBytes()));
}

/// A colour planar image of a signed eight- or sixteen-bit representation, 256 pixels square, every channel holding
/// every code of the representation - each code as often as the next - each channel from another code onwards.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::AnyImage> everyCodeImage(const an::EngineContext& engine)
{
    using CodeT = std::make_unsigned_t<SampleT>;
    constexpr std::uint32_t kSide = 256u;
    constexpr std::uint32_t kCodes = std::uint32_t{1} << (8u * sizeof(SampleT));
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(engine, an::kSampleTypeOf<SampleT>, kSide, kSide, Channels::Rgb);
    if (!described.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < kSide; ++y)
            {
                auto row = access->view().row(channel, y);
                for (std::uint32_t x = 0u; x < kSide; ++x)
                {
                    const std::uint32_t bits = (y * kSide + x + channel * 0x5555u) % kCodes;
                    (*row)[x] = std::bit_cast<SampleT>(static_cast<CodeT>(bits));
                }
            }
        }
    }
    return an::consumer::AnyImage(std::move(*image));
}

} // namespace

// Float64 and the two 32-bit integers are worked on in Float64, whose significand holds their codes; the rest in
// Float32.
TEST(PointwiseUnits, TheWorkingRepresentationHoldsWhatTheStoredOneCarries)
{
    using an::SampleType;
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::UInt8), SampleType::Float32);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::Int8), SampleType::Float32);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::UInt16), SampleType::Float32);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::Int16), SampleType::Float32);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::UInt32), SampleType::Float64);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::Int32), SampleType::Float64);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::Float32), SampleType::Float32);
    EXPECT_EQ(process_utils::normalizedWorkingType(SampleType::Float64), SampleType::Float64);
    // A tone program reads an unsigned eight- or sixteen-bit plane directly, as a table over its codes.
    EXPECT_EQ(process_utils::normalizedToneShape(SampleType::UInt16, 1u).sampleType, SampleType::UInt16);
    EXPECT_EQ(process_utils::normalizedToneShape(SampleType::UInt8, 1u).sampleType, SampleType::UInt8);
    EXPECT_EQ(process_utils::normalizedToneShape(SampleType::Int16, 1u).sampleType, SampleType::Float32);
    EXPECT_EQ(process_utils::normalizedToneShape(SampleType::Int32, 1u).sampleType, SampleType::Float64);
}

// An unsigned integer is a fraction of its largest value and a signed one is counted from its smallest; untouched,
// every code comes back as it was.
TEST_F(PointwiseUnitsTest, IntegersAreFractionsOfFullScaleAndComeBackUnchanged)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());

    std::optional<an::Image<std::uint16_t>> unsignedPlane =
        planeOf<std::uint16_t>(scope->engine(), {0u, 1u, 32767u, 65534u, 65535u});
    ASSERT_TRUE(unsignedPlane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *unsignedPlane);
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        EXPECT_EQ(samples->sampleType(), an::SampleType::Float32);
        const std::vector<double> working = workingValuesOf(*samples);
        EXPECT_EQ(working[0], 0.0);
        EXPECT_FLOAT_EQ(static_cast<float>(working[2]), 32767.0f / 65535.0f);
        EXPECT_EQ(working[4], 1.0);
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    EXPECT_EQ(codesOf(*unsignedPlane), (std::vector<std::uint16_t>{0u, 1u, 32767u, 65534u, 65535u}));

    std::optional<an::Image<std::int16_t>> signedPlane = planeOf<std::int16_t>(scope->engine(), {-32768, -1, 0, 32767});
    ASSERT_TRUE(signedPlane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *signedPlane);
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        const std::vector<double> working = workingValuesOf(*samples);
        EXPECT_EQ(working[0], 0.0);
        EXPECT_NEAR(working[2], 32768.0 / 65535.0, 1.0e-7);
        EXPECT_NEAR(working[3], 1.0, 1.0e-7);
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    EXPECT_EQ(codesOf(*signedPlane), (std::vector<std::int16_t>{-32768, -1, 0, 32767}));

    constexpr std::int32_t kLowest = (std::numeric_limits<std::int32_t>::min)();
    constexpr std::int32_t kHighest = (std::numeric_limits<std::int32_t>::max)();
    std::optional<an::Image<std::int32_t>> widePlane =
        planeOf<std::int32_t>(scope->engine(), {kLowest, -1, 0, kHighest});
    ASSERT_TRUE(widePlane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *widePlane);
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        EXPECT_EQ(samples->sampleType(), an::SampleType::Float64);
        const std::vector<double> working = workingValuesOf(*samples);
        EXPECT_EQ(working[0], 0.0);
        EXPECT_NEAR(working[2], 2147483648.0 / 4294967295.0, 1.0e-15);
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    EXPECT_EQ(codesOf(*widePlane), (std::vector<std::int32_t>{kLowest, -1, 0, kHighest}));
}

// A result is narrowed to the nearest code, not truncated toward zero.
TEST_F(PointwiseUnitsTest, AResultIsNarrowedToTheNearestCode)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());
    std::optional<an::Image<std::uint8_t>> plane = planeOf<std::uint8_t>(scope->engine(), {9u, 10u, 254u});
    ASSERT_TRUE(plane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *plane);
        ASSERT_TRUE(samples.hasValue());
        ASSERT_TRUE(scaleWorking(scope->execution(), *samples, 0.3, 0.0).ok());
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    // 2.7, 3.0 and 76.2 codes; truncated, the first would be 2.
    EXPECT_EQ(codesOf(*plane), (std::vector<std::uint8_t>{3u, 3u, 76u}));

    std::optional<an::Image<std::int8_t>> signedPlane = planeOf<std::int8_t>(scope->engine(), {-128, 0, 100});
    ASSERT_TRUE(signedPlane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *signedPlane);
        ASSERT_TRUE(samples.hasValue());
        // Three tenths of every value as a fraction of full scale, counted from the smallest code: 0, 38.4 and 68.4
        // codes above it.
        ASSERT_TRUE(scaleWorking(scope->execution(), *samples, 0.3, 0.0).ok());
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    EXPECT_EQ(codesOf(*signedPlane), (std::vector<std::int8_t>{-128, -90, -60}));
}

// Floating samples are fractions of full scale already and are worked on where they are.
TEST_F(PointwiseUnitsTest, FloatingSamplesAreWorkedOnWhereTheyAre)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());
    std::optional<an::Image<float>> plane = planeOf<float>(scope->engine(), {-0.5f, 0.25f, 1.5f});
    ASSERT_TRUE(plane.has_value());
    std::optional<an::ImageWriteAccess<float>> access = plane->write();
    void* stored = access->view().data();
    an::Result<process_utils::NormalizedSamples> samples = process_utils::NormalizedSamples::open(
        scope->engine(), scope->execution(), *plane->descriptor(), stored, access->view().layout(), {});
    ASSERT_TRUE(samples.hasValue());
    EXPECT_EQ(samples->samples(), stored);
    EXPECT_EQ(workingValuesOf(*samples), (std::vector<double>{-0.5, 0.25, 1.5})) << "nothing is held to an interval";
    ASSERT_TRUE(samples->commit(scope->execution()).ok());
}

// A rule an integer's value decides is judged on the stored samples - a signed zero is the middle of the range, not
// the bottom - and carried as a mask, so a pixel it rejects comes back as it was.
TEST_F(PointwiseUnitsTest, AValueRuleIsJudgedOnTheStoredSamples)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroIsNoData.has_value());

    std::optional<an::Image<std::int16_t>> plane = planeOf<std::int16_t>(scope->engine(), {0, -32768, 100, 0});
    ASSERT_TRUE(plane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples =
            openPlane(*scope, *plane, an::KernelValidityView{.descriptor = *zeroIsNoData});
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        EXPECT_EQ(samples->validity().descriptor.rule(), an::ValidityRule::ExplicitMask);
        ASSERT_NE(samples->validity().mask, nullptr);
        // Everything valid moves to the top of the range.
        ASSERT_TRUE(scaleWorking(scope->execution(), *samples, 0.0, 1.0).ok());
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    EXPECT_EQ(codesOf(*plane), (std::vector<std::int16_t>{0, 32767, 32767, 0}));

    // A rule about finiteness admits every integer.
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    std::optional<an::Image<std::uint16_t>> other = planeOf<std::uint16_t>(scope->engine(), {0u, 7u});
    an::Result<process_utils::NormalizedSamples> samples =
        openPlane(*scope, *other, an::KernelValidityView{.descriptor = *finite});
    ASSERT_TRUE(samples.hasValue());
    EXPECT_EQ(samples->validity().descriptor.rule(), an::ValidityRule::AllValid);
}

// An integer image every sample of which is valid is worked on as one: no mask is judged for it, so an operation over
// it takes the executor's path for an all-valid image rather than reading a mask pixel by pixel.
TEST_F(PointwiseUnitsTest, AnIntegerImageWhoseEverySampleIsValidIsWorkedOnWithoutAMask)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());
    std::optional<an::Image<std::uint16_t>> unsignedPlane = planeOf<std::uint16_t>(scope->engine(), {0u, 8u, 65535u});
    ASSERT_TRUE(unsignedPlane.has_value());
    {
        an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *unsignedPlane);
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        EXPECT_EQ(samples->validity().descriptor.rule(), an::ValidityRule::AllValid);
        EXPECT_EQ(samples->validity().mask, nullptr) << "a mask was judged for an image every sample of which is valid";
        ASSERT_TRUE(scaleWorking(scope->execution(), *samples, 0.5, 0.0).ok());
        ASSERT_TRUE(samples->commit(scope->execution()).ok());
    }
    // 0, 4 and 32767.5 codes, the half to the nearest even code.
    EXPECT_EQ(codesOf(*unsignedPlane), (std::vector<std::uint16_t>{0u, 4u, 32768u}));

    std::optional<an::Image<std::int8_t>> signedPlane = planeOf<std::int8_t>(scope->engine(), {-128, 0, 127});
    ASSERT_TRUE(signedPlane.has_value());
    an::Result<process_utils::NormalizedSamples> samples = openPlane(*scope, *signedPlane);
    ASSERT_TRUE(samples.hasValue()) << samples.status().message();
    EXPECT_EQ(samples->validity().descriptor.rule(), an::ValidityRule::AllValid);
    EXPECT_EQ(samples->validity().mask, nullptr);
    ASSERT_TRUE(samples->commit(scope->execution()).ok());
}

// A chain of scales, biases and clamps over a camera frame - an unsigned sixteen-bit image every sample of which is
// valid - is answered once for each code and every sample looked up, so it holds no working copy of the frame: under a
// memory budget that holds the frame but not a Float32 copy of it, it still runs, and every sample is what the chain
// makes of its code as a fraction of full scale, narrowed to the nearest code.
TEST(PointwiseUnits, AChainOverACameraFrameHoldsNoWorkingCopyOfIt)
{
    constexpr std::uint32_t kWidth = 1024u;
    constexpr std::uint32_t kHeight = 512u;
    constexpr std::uint64_t kFrameBytes = std::uint64_t{kWidth} * kHeight * 3u * sizeof(std::uint16_t);
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 4u;
    // The frame and room for the table, well short of the frame's Float32 copy of twice its size.
    settings.limits.memoryBudgetBytes = kFrameBytes + (std::uint64_t{2} << 20u);
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    an::Result<an::ui::ExecutionScope> scope =
        an::ui::ExecutionScope::admit(*session, an::ui::ExecutionRequest{}, an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();

    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::UInt16, kWidth, kHeight, Channels::Rgb);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<std::uint16_t>> frame = an::Image<std::uint16_t>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    std::optional<an::ImageWriteAccess<std::uint16_t>> access = frame->write();
    ASSERT_TRUE(access.has_value());
    const an::ImageLayout layout = access->view().layout();
    const auto codeAt = [](std::uint32_t channel, std::uint32_t x, std::uint32_t y) {
        return static_cast<std::uint16_t>((x * 131u + y * 7919u + channel * 21737u) % 65536u);
    };
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            auto row = access->view().row(channel, y);
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                (*row)[x] = codeAt(channel, x, y);
            }
        }
    }

    constexpr double kBrightness = 0.1;
    const an::ChannelScopeInput input{
        .descriptor = &*described, .samples = access->view().data(), .layout = &layout, .validity = {}};
    const an::ChannelScopeOutput output{.descriptor = &*described, .samples = access->view().data(), .layout = &layout};
    const an::Status ran = process_utils::runNormalizedChain(
        *scope, input, output, [](const an::PointwisePlanShape& shape) -> an::Result<process_utils::PointwiseChain> {
            process_utils::PointwiseChain chain;
            chain.push_back(*an::buildBrightnessProgram(shape, kBrightness));
            chain.push_back(*an::buildClampProgram(shape, an::ClampInterval{.low = 0.0, .high = 1.0}));
            return chain;
        });
    ASSERT_TRUE(ran.ok()) << ran.message();

    // The code as a fraction of full scale, a scale by one and the bias in Float32, the clamp, and the nearest code.
    const auto expected = [](std::uint16_t code) {
        const float working = static_cast<float>(static_cast<double>(code) * (1.0 / 65535.0));
        const float biased = working * 1.0f + static_cast<float>(kBrightness);
        const float held = biased < 0.0f ? 0.0f : (biased > 1.0f ? 1.0f : biased);
        return an::convertSample<float, std::uint16_t>(held, an::ConversionPolicy{}).value;
    };
    std::size_t differ = 0u;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const auto row = access->view().row(channel, y);
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                differ += (*row)[x] == expected(codeAt(channel, x, y)) ? 0u : 1u;
            }
        }
    }
    EXPECT_EQ(differ, 0u);
}

// A chain run band by band, each band by one worker in a working band of its own, writes what the whole plane set's
// working copy would have: every sample of every representation, over frames whose rows are not a vector's width and
// over every fixture scene, with every pixel valid, under a mask, and under a rule an integer's value decides; for a
// chain an eight- or sixteen-bit plane takes as a lookup - a signed one by its codes' bit patterns - and for one with a
// power, which no plane does. Read from the source into a destination that starts holding none of it, every sample is
// written.
TEST_F(PointwiseUnitsTest, AChainRunInBandsWritesWhatTheWholePlanesWorkingCopyWould)
{
    const an::consumer::EngineSession& session = m_context->engineSession();
    an::Result<an::ui::ExecutionScope> scope =
        an::ui::ExecutionScope::admit(session, an::ui::ExecutionRequest{}, an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const an::EngineContext& engine = scope->engine();

    struct Input
    {
        std::string name;
        an::consumer::AnyImage image;
    };
    std::vector<Input> inputs;
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        // Wide and tall enough for a sixteen-bit lookup, with rows that are not a whole number of any vector.
        std::optional<an::consumer::AnyImage> image = spreadImage(engine, type, 301u, 233u);
        ASSERT_TRUE(image.has_value()) << an::sampleTypeName(type);
        inputs.push_back(Input{.name = std::string("spread ") + an::sampleTypeName(type), .image = std::move(*image)});
    }
    for (const std::vector<an::tests::fixtures::FixtureImage>* catalogue :
         {&an::tests::fixtures::standardFixtures(), &an::tests::fixtures::behaviorFixtures()})
    {
        for (const an::tests::fixtures::FixtureImage& fixture : *catalogue)
        {
            std::string error;
            const std::optional<an::tests::fixtures::FixtureSamples> samples =
                an::tests::fixtures::materializeFixture(fixture, error);
            ASSERT_TRUE(samples.has_value()) << fixture.name << ": " << error;
            an::Result<an::consumer::AnyImage> image =
                an::tests::fixtures::engine::makeEngineImage(engine, fixture, *samples);
            ASSERT_TRUE(image.hasValue()) << fixture.name << ": " << image.status().message();
            inputs.push_back(Input{.name = fixture.name, .image = std::move(*image)});
        }
    }

    const std::optional<an::ValidityDescriptor> explicitMask =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(explicitMask.has_value() && zeroIsNoData.has_value());
    std::size_t compared = 0u;
    for (const Input& input : inputs)
    {
        const std::optional<an::ImageDescriptor> described =
            std::visit([](const auto& typed) { return typed.descriptor(); }, input.image);
        ASSERT_TRUE(described.has_value());
        const an::SampleType stored = described->sampleType();
        const an::ImageLayout inputLayout =
            std::visit([](const auto& typed) { return typed.view()->layout(); }, input.image);
        const void* inputSamples =
            std::visit([](const auto& typed) -> const void* { return typed.view()->data(); }, input.image);
        const std::uint32_t width = inputLayout.extent().width();
        const std::uint32_t height = inputLayout.extent().height();

        // A pixel in thirteen is masked out; under the value rule, a code of zero is no data.
        const std::optional<an::ImageExtent> maskExtent = an::ImageExtent::create(width, height, 1u, engine.limits());
        ASSERT_TRUE(maskExtent.has_value());
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *maskExtent);
        ASSERT_TRUE(mask.hasValue()) << mask.status().message();
        {
            std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
            ASSERT_TRUE(bits.has_value());
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if ((x * 7u + y * 3u) % 13u != 0u)
                    {
                        bits->data()[y * bits->rowStrideBytes() + x / 8u] |= static_cast<std::uint8_t>(1u << (x % 8u));
                    }
                }
            }
        }
        const std::optional<an::ValidityMaskView<const std::uint8_t>> maskView = std::as_const(*mask).view();
        std::vector<std::pair<const char*, an::KernelValidityView>> validities{
            {"every pixel valid", an::KernelValidityView{}},
            {"a mask", an::KernelValidityView{.descriptor = *explicitMask, .mask = &*maskView, .maskPlane = 0u}}};
        if (!an::isFloatingSampleType(stored))
        {
            validities.emplace_back("zero is no data", an::KernelValidityView{.descriptor = *zeroIsNoData});
        }

        for (const NamedChain& named : chainsUnderTest())
        {
            an::Result<process_utils::PointwiseChain> chain = named.build(process_utils::normalizedPlanShape(
                process_utils::normalizedWorkingType(stored), inputLayout.extent().channelCount()));
            ASSERT_TRUE(chain.hasValue()) << chain.status().message();
            an::Result<an::PointwiseProgram> fused = an::fusePointwisePrograms(*chain);
            ASSERT_TRUE(fused.hasValue()) << fused.status().message();
            for (const auto& [rule, validity] : validities)
            {
                SCOPED_TRACE(::testing::Message() << input.name << ", " << named.name << ", " << rule);
                std::optional<an::consumer::AnyImage> expected = copyOfImage(input.image);
                ASSERT_TRUE(expected.has_value());
                const WrittenSamples whole = writtenOf(*expected);
                ASSERT_TRUE(
                    runOverWholePlanes(*scope, *described, whole.samples, *whole.layout, validity, *fused).ok());

                // The framework's copy of the source, which the chain reads the source into.
                std::optional<an::consumer::AnyImage> actual = copyOfImage(input.image);
                ASSERT_TRUE(actual.has_value());
                const WrittenSamples banded = writtenOf(*actual);
                const an::ChannelScopeInput scopeInput{
                    .descriptor = &*described, .samples = inputSamples, .layout = &inputLayout, .validity = validity};
                const an::ChannelScopeOutput scopeOutput{
                    .descriptor = &*described, .samples = banded.samples, .layout = &*banded.layout};
                const an::Status ran = process_utils::runNormalizedChain(*scope, scopeInput, scopeOutput, named.build);
                ASSERT_TRUE(ran.ok()) << ran.message();
                EXPECT_EQ(samplesOf(*actual), samplesOf(*expected));
                ++compared;

                if (validity.descriptor.rule() == an::ValidityRule::AllValid)
                {
                    std::optional<an::consumer::AnyImage> unwritten = copyOfImage(input.image);
                    ASSERT_TRUE(unwritten.has_value());
                    poison(*unwritten);
                    const WrittenSamples fresh = writtenOf(*unwritten);
                    const an::ChannelScopeOutput freshOutput{
                        .descriptor = &*described, .samples = fresh.samples, .layout = &*fresh.layout};
                    const an::Status wrote =
                        process_utils::runNormalizedChain(*scope, scopeInput, freshOutput, named.build);
                    ASSERT_TRUE(wrote.ok()) << wrote.message();
                    EXPECT_EQ(samplesOf(*unwritten), samplesOf(*expected)) << "a sample was left unwritten";
                }
            }
        }
    }
    EXPECT_GT(compared, 100u);
}

// A signed eight- or sixteen-bit plane is looked up by its codes' bit patterns, and every code comes out as the band
// path makes it: through a chain of scales, biases and clamps, each channel of a plane holding every code is written
// code for code as the same chain run in bands writes it - where every sample is opened as a fraction of full scale,
// counted from the representation's smallest value - so a table built or read with the wrong sign would show as codes
// wrapped to the other half of the range.
TEST_F(PointwiseUnitsTest, ASignedPlaneHoldingEveryCodeIsLookedUpAsTheBandPathWorksIt)
{
    const an::consumer::EngineSession& session = m_context->engineSession();
    an::Result<an::ui::ExecutionScope> scope =
        an::ui::ExecutionScope::admit(session, an::ui::ExecutionRequest{}, an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const an::EngineContext& engine = scope->engine();

    // A mask that admits every pixel: the same samples as every pixel valid, which a lookup is never taken under, so
    // the chain runs in bands.
    const std::optional<an::ImageExtent> maskExtent = an::ImageExtent::create(256u, 256u, 1u, engine.limits());
    ASSERT_TRUE(maskExtent.has_value());
    an::Result<an::ValidityMask> admitAll = an::ValidityMask::create(engine, *maskExtent);
    ASSERT_TRUE(admitAll.hasValue()) << admitAll.status().message();
    {
        std::optional<an::ValidityMaskView<std::uint8_t>> bits = admitAll->view();
        ASSERT_TRUE(bits.has_value());
        std::memset(bits->data(), 0xFF, static_cast<std::size_t>(bits->spannedByteCount()));
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> admitted = std::as_const(*admitAll).view();
    const std::optional<an::ValidityDescriptor> explicitMask =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(admitted.has_value() && explicitMask.has_value());
    const an::KernelValidityView inBands{.descriptor = *explicitMask, .mask = &*admitted, .maskPlane = 0u};

    const std::vector<NamedChain> chains = chainsUnderTest();
    const NamedChain& scalesBiasesAndClamps = chains.front();
    const auto compareCodes = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        std::optional<an::consumer::AnyImage> source = everyCodeImage<SampleT>(engine);
        ASSERT_TRUE(source.has_value());
        const auto& typed = std::get<an::Image<SampleT>>(*source);
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        ASSERT_TRUE(described.has_value());
        const an::ImageLayout sourceLayout = typed.view()->layout();
        const void* sourceSamples = typed.view()->data();

        // Looked up, into a destination that starts holding none of it.
        std::optional<an::consumer::AnyImage> looked = copyOfImage(*source);
        ASSERT_TRUE(looked.has_value());
        poison(*looked);
        const WrittenSamples lookedSamples = writtenOf(*looked);
        const an::Status lookedUp = process_utils::runNormalizedChain(
            *scope,
            an::ChannelScopeInput{
                .descriptor = &*described, .samples = sourceSamples, .layout = &sourceLayout, .validity = {}},
            an::ChannelScopeOutput{
                .descriptor = &*described, .samples = lookedSamples.samples, .layout = &*lookedSamples.layout},
            scalesBiasesAndClamps.build);
        ASSERT_TRUE(lookedUp.ok()) << lookedUp.message();

        // Run in bands over the same samples.
        std::optional<an::consumer::AnyImage> banded = copyOfImage(*source);
        ASSERT_TRUE(banded.has_value());
        const WrittenSamples bandedSamples = writtenOf(*banded);
        const an::Status ranInBands = process_utils::runNormalizedChain(
            *scope,
            an::ChannelScopeInput{
                .descriptor = &*described, .samples = sourceSamples, .layout = &sourceLayout, .validity = inBands},
            an::ChannelScopeOutput{
                .descriptor = &*described, .samples = bandedSamples.samples, .layout = &*bandedSamples.layout},
            scalesBiasesAndClamps.build);
        ASSERT_TRUE(ranInBands.ok()) << ranInBands.message();

        const auto in = typed.view();
        const auto out = std::get<an::Image<SampleT>>(*looked).view();
        const auto reference = std::get<an::Image<SampleT>>(*banded).view();
        std::uint64_t differ = 0u;
        std::uint64_t moved = 0u;
        std::optional<std::string> first;
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < 256u; ++y)
            {
                const auto codes = in->row(channel, y);
                const auto answers = out->row(channel, y);
                const auto expected = reference->row(channel, y);
                for (std::uint32_t x = 0u; x < 256u; ++x)
                {
                    moved += (*expected)[x] != (*codes)[x] ? 1u : 0u;
                    if ((*answers)[x] == (*expected)[x])
                    {
                        continue;
                    }
                    if (!first.has_value())
                    {
                        first = ::testing::PrintToString(static_cast<int>((*codes)[x])) + " became " +
                                ::testing::PrintToString(static_cast<int>((*answers)[x])) + " rather than " +
                                ::testing::PrintToString(static_cast<int>((*expected)[x]));
                    }
                    ++differ;
                }
            }
        }
        EXPECT_EQ(differ, 0u) << "first: " << first.value_or("");
        EXPECT_GT(moved, 3u * 256u * 128u) << "the chain moves most codes";
    };
    compareCodes.template operator()<std::int8_t>();
    compareCodes.template operator()<std::int16_t>();
}

// An operation that refuses a sample holding no number works under a rule that leaves such a pixel as it was.
TEST(PointwiseUnits, ASampleHoldingNoNumberIsCountedOutOnlyWhereEverySampleWasValid)
{
    EXPECT_EQ(process_utils::skippingNonFinite(an::KernelValidityView{}).descriptor.rule(),
              an::ValidityRule::FiniteOnly);
    const std::optional<an::ValidityDescriptor> zero =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    EXPECT_EQ(process_utils::skippingNonFinite(an::KernelValidityView{.descriptor = *zero}).descriptor.rule(),
              an::ValidityRule::ZeroIsNoData);
}

// Under a rule other than all-valid a floating sample holding no number is counted out with what the rule rejects, into
// a mask the samples keep, so an operation that refuses one leaves it as it was rather than refusing the run.
TEST_F(PointwiseUnitsTest, ASampleHoldingNoNumberIsCountedOutUnderEveryRule)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroIsNoData.has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::optional<an::Image<float>> plane = planeOf<float>(scope->engine(), {0.0f, nan, 0.5f, inf});
    ASSERT_TRUE(plane.has_value());
    an::Result<process_utils::NormalizedSamples> samples =
        openPlane(*scope, *plane, an::KernelValidityView{.descriptor = *zeroIsNoData});
    ASSERT_TRUE(samples.hasValue());
    const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope->execution());
    ASSERT_TRUE(skipping.hasValue()) << skipping.status().message();
    EXPECT_EQ(skipping->descriptor.rule(), an::ValidityRule::ExplicitMask);
    ASSERT_NE(skipping->mask, nullptr);
    std::vector<bool> admitted;
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        admitted.push_back(skipping->mask->test(0u, x, 0u).value_or(true));
    }
    EXPECT_EQ(admitted, (std::vector<bool>{false, false, true, false}));
    ASSERT_TRUE(samples->commit(scope->execution()).ok());
}

// An integer image under a rule other than all-valid keeps its own validity: an integer is always a finite number, so
// no mask of the finite samples is made or combined for it, and the view answered is the image's own.
TEST_F(PointwiseUnitsTest, AnIntegerImageUnderARuleKeepsItsOwnValidity)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroIsNoData.has_value());
    std::optional<an::Image<std::uint16_t>> plane = planeOf<std::uint16_t>(scope->engine(), {0u, 7u, 65535u, 9u});
    ASSERT_TRUE(plane.has_value());
    an::Result<process_utils::NormalizedSamples> samples =
        openPlane(*scope, *plane, an::KernelValidityView{.descriptor = *zeroIsNoData});
    ASSERT_TRUE(samples.hasValue());
    const an::KernelValidityView own = samples->validity();
    const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope->execution());
    ASSERT_TRUE(skipping.hasValue()) << skipping.status().message();
    EXPECT_EQ(skipping->descriptor.rule(), own.descriptor.rule());
    EXPECT_EQ(skipping->mask, own.mask) << "the view answered holds another mask than the image's own";
    EXPECT_EQ(skipping->maskPlane, own.maskPlane);
    ASSERT_TRUE(samples->commit(scope->execution()).ok());
}

// A result whose cards say linear is recorded nonlinear: its cards restated in a new image, its meaning no longer
// linear, its other cards kept and its lineage naming what ran.
TEST_F(PointwiseUnitsTest, AResultIsRecordedNonlinearOnItsCardsAndLineage)
{
    const an::consumer::EngineSession& session = m_context->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        assetWithMetadata(session, metadataWith(an::TransferFunction::Linear, {card("ANLIN", "'linear'")}));
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::ui::ExecutionScope> scope =
        an::ui::ExecutionScope::admit(session, an::ui::ExecutionRequest{}, an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    EXPECT_EQ(&scope->session(), &session);
    an::Result<an::ui::ProcessDestination> destination = an::ui::ProcessDestination::copyOf(
        session, scope->engine(), scope->execution(), an::ui::ProcessSource::of(*lease));
    ASSERT_TRUE(destination.hasValue());
    const std::uint64_t copied = destination->asset().assetIdentity();

    ASSERT_TRUE(process_utils::recordNonlinearResult(
                    *scope, *destination,
                    process_utils::NonlinearResultRecord{.stateOperation = "gamma-correction",
                                                         .engineOperation = "an::buildGammaProgram",
                                                         .detail = "gamma 2.2"})
                    .ok());
    EXPECT_NE(destination->asset().assetIdentity(), copied) << "the cards are restated in a new image";
    const an::ImageMetadata result = metadataOf(destination->asset());
    const an::ProcessingStateRecord state = an::readProcessingStateCards(result.codec());
    EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
    EXPECT_EQ(state.assignmentSource, an::StateAssignmentSource::Process);
    EXPECT_EQ(state.operation, "gamma-correction");
    EXPECT_EQ(result.semantics().transferFunction(), an::TransferFunction::Unspecified);
    ASSERT_FALSE(result.provenance().entries().empty());
    EXPECT_EQ(result.provenance().entries().back().operation(), "an::buildGammaProgram");
    EXPECT_EQ(result.provenance().entries().back().detail(), "gamma 2.2");
    bool keptObject = false;
    for (const an::CodecCard& kept : result.codec().cards())
    {
        keptObject = keptObject || kept.keyword() == "OBJECT";
    }
    EXPECT_TRUE(keptObject);
    scope->finish(an::Status::success());
}

// A result already recorded as this process left it - its cards unchanged and its meaning not linear - is recorded in
// place: the same image, one more lineage entry.
TEST_F(PointwiseUnitsTest, AResultAlreadyRecordedNonlinearIsRecordedInPlace)
{
    const an::consumer::EngineSession& session = m_context->engineSession();
    std::optional<an::consumer::ImageAsset> seed =
        assetWithMetadata(session, metadataWith(an::TransferFunction::Unspecified, {}));
    ASSERT_TRUE(seed.has_value());
    an::Result<an::ui::ExecutionScope> scope =
        an::ui::ExecutionScope::admit(session, an::ui::ExecutionRequest{}, an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue());
    an::Result<an::consumer::AssetReadLease> lease = seed->read();
    an::Result<an::ui::ProcessDestination> first = an::ui::ProcessDestination::copyOf(
        session, scope->engine(), scope->execution(), an::ui::ProcessSource::of(*lease));
    ASSERT_TRUE(first.hasValue());
    const process_utils::NonlinearResultRecord record{
        .stateOperation = "colorize", .engineOperation = "an::colorizeImage", .detail = "first"};
    ASSERT_TRUE(process_utils::recordNonlinearResult(*scope, *first, record).ok());
    an::Result<an::consumer::ImageAsset> recorded = first->release();
    ASSERT_TRUE(recorded.hasValue());

    an::Result<an::consumer::AssetReadLease> again = recorded->read();
    an::Result<an::ui::ProcessDestination> second = an::ui::ProcessDestination::copyOf(
        session, scope->engine(), scope->execution(), an::ui::ProcessSource::of(*again));
    ASSERT_TRUE(second.hasValue());
    const std::uint64_t identity = second->asset().assetIdentity();
    const std::size_t entries = metadataOf(second->asset()).provenance().entries().size();
    ASSERT_TRUE(process_utils::recordNonlinearResult(*scope, *second, record).ok());
    EXPECT_EQ(second->asset().assetIdentity(), identity);
    EXPECT_EQ(metadataOf(second->asset()).provenance().entries().size(), entries + 1u);
    scope->finish(an::Status::success());
}

// A history step names the channel of a colour target it ran on, and nothing of a grey one.
TEST_F(PointwiseUnitsTest, AStepNamesTheChannelOfAColourTargetAlone)
{
    std::optional<an::ui::ImageDocument> colour =
        an::tests::viewer::makeDocument<float>(m_context->engineSession(), 4u, 3u, Channels::Rgb,
                                               [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    std::optional<an::ui::ImageDocument> grey =
        greyDocument(m_context->engineSession(), 4u, 3u, [](std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(colour.has_value() && grey.has_value());
    ViewHandle colourView = makeTargetView("colour", std::move(*colour));
    ViewHandle greyView = makeTargetView("grey", std::move(*grey));
    EXPECT_TRUE(process_utils::holdsColourImage(colourView.get()));
    EXPECT_FALSE(process_utils::holdsColourImage(greyView.get()));
    EXPECT_FALSE(process_utils::holdsColourImage(nullptr));
    EXPECT_EQ(process_utils::channelStepSuffix(colourView.get(), an::ChannelScope::CieLightness),
              " in channel: CIE L* (Lightness)");
    EXPECT_EQ(process_utils::channelStepSuffix(colourView.get(), an::ChannelScope::Whole), " in channel: RGB");
    EXPECT_EQ(process_utils::channelStepSuffix(greyView.get(), an::ChannelScope::Whole), "");
    dropView(*m_context, colourView);
    dropView(*m_context, greyView);
}

// The exposure factor, in single precision: one plus a fifth of a positive setting, one plus 0.02 plus a fifth of a
// negative one.
TEST(PointwiseUnits, TheExposureFactorIsOnePlusAFifthOfTheSetting)
{
    EXPECT_EQ(ExposureCorrectionProcess::exposureFactor(0.0), 1.0f);
    EXPECT_EQ(ExposureCorrectionProcess::exposureFactor(1.5), 1.0f + 1.5f / 5.0f);
    EXPECT_EQ(ExposureCorrectionProcess::exposureFactor(-1.0), 1.0f + (0.02f + -1.0f / 5.0f));
    EXPECT_EQ(ExposureCorrectionProcess::exposureFactor(5.0), 2.0f);
}

// The saturation's selector names seven hue ranges, the second crossing zero.
TEST(PointwiseUnits, TheSaturationsSelectorNamesSevenHueRanges)
{
    const std::array<std::pair<double, double>, 7u> expected{
        {{0.0, 360.0}, {346.0, 20.0}, {21.0, 60.0}, {61.0, 200.0}, {170.0, 200.0}, {201.0, 280.0}, {281.0, 345.0}}};
    for (int selector = 0; selector < 7; ++selector)
    {
        const SaturationHueRange range = ColorSaturationProcess::hueRange(selector);
        EXPECT_EQ(range.minimumDegrees, expected[static_cast<std::size_t>(selector)].first) << selector;
        EXPECT_EQ(range.maximumDegrees, expected[static_cast<std::size_t>(selector)].second) << selector;
    }
    EXPECT_EQ(ColorSaturationProcess::hueRange(7).maximumDegrees, 360.0) << "an unknown entry names every hue";
    EXPECT_EQ(ColorSaturationProcess::hueRange(-1).minimumDegrees, 0.0);
}

// The HSL and LCh dialogs' values become the engine's adjustments: a hue in turns or degrees, a scale of one plus a
// twenty-fifth of a positive setting or a hundredth of a negative one, and a lightness a hundredth of its setting in
// HSL and its setting in CIE units.
TEST(PointwiseUnits, TheHslAndLchSettingsBecomeTheEnginesAdjustments)
{
    const an::HslAdjustment hsl = AdjustHSLProcess::hslAdjustment(AdjustHSLParams{30.0, 40.0, -10.0});
    EXPECT_DOUBLE_EQ(hsl.hueOffsetTurns, 30.0 / 360.0);
    EXPECT_DOUBLE_EQ(hsl.saturationScale, 1.0 + 40.0 / 25.0);
    EXPECT_DOUBLE_EQ(hsl.lightnessOffset, -0.1);
    EXPECT_DOUBLE_EQ(AdjustHSLProcess::hslAdjustment(AdjustHSLParams{0.0, -50.0, 0.0}).saturationScale, 0.5);
    EXPECT_DOUBLE_EQ(AdjustHSLProcess::hslAdjustment(AdjustHSLParams{}).saturationScale, 1.0);

    const an::LchAdjustment lch = AdjustLChProcess::lchAdjustment(AdjustLChParams{10.0, 30.0, -20.0});
    EXPECT_DOUBLE_EQ(lch.lightnessOffset, 10.0);
    EXPECT_DOUBLE_EQ(lch.chromaScale, 1.0 + 30.0 / 25.0);
    EXPECT_DOUBLE_EQ(lch.hueOffsetDegrees, -20.0);
    EXPECT_DOUBLE_EQ(AdjustLChProcess::lchAdjustment(AdjustLChParams{0.0, -40.0, 0.0}).chromaScale, 0.6);
}
