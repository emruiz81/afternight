// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for calibration planning and its fused execution.

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.backend = backend;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageDescriptor descriptorOf(an::SampleType sampleType, std::uint32_t width, std::uint32_t height,
                                               an::ChannelSchema schema = an::ChannelSchema::Mono,
                                               an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                               an::CfaDescriptor cfa = {})
{
    const std::uint32_t channels = schema == an::ChannelSchema::Rgb ? 3u : 1u;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = sampleType,
                                             .channelSchema = schema,
                                             .colorSpace = schema == an::ChannelSchema::Rgb
                                                               ? (domain == an::NumericDomain::EncodedNonlinear
                                                                      ? an::ColorSpace::DisplayRgb
                                                                      : an::ColorSpace::LinearRgb)
                                                               : an::ColorSpace::Unspecified,
                                             .numericDomain = domain,
                                             .cfa = cfa,
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ImageLayout layoutOf(const an::ImageDescriptor& descriptor,
                                       an::StorageArrangement arrangement = an::StorageArrangement::Planar,
                                       std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageLayoutRequest> request = an::layoutRequestFor(descriptor, arrangement, 1u);
    EXPECT_TRUE(request.has_value());
    if (rowPaddingSamples == 0u)
    {
        const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
        EXPECT_TRUE(layout.has_value());
        return *layout;
    }
    const std::uint64_t row = request->extent.width() * request->samplesPerPixel * request->elementSizeBytes +
                              static_cast<std::uint64_t>(rowPaddingSamples) * request->elementSizeBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(*request, row, row * request->extent.height(), an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::CalibrationFrameView frameOf(const void* samples, const an::ImageLayout& layout,
                                               const an::ImageDescriptor& descriptor,
                                               const an::KernelValidityView& validity = {})
{
    const an::Result<an::CalibrationFrameView> made =
        an::CalibrationFrameView::create(samples, layout.totalBytes(), layout, descriptor, validity);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::CalibrationFrameView{};
}

TEST(ImageCalibrationTest, AFrameViewKeepsWhatDescribesTheFrameRatherThanPointingAtIt)
{
    const std::array<float, 8> samples{};
    an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 4u, 2u);
    an::ImageLayout layout = layoutOf(descriptor);
    const an::CalibrationFrameView view = frameOf(samples.data(), layout, descriptor);

    // The samples are borrowed and the description is copied, so the only lifetime a caller keeps
    // is the buffer's. Both of these used to be what the view read through.
    descriptor = descriptorOf(an::SampleType::Float64, 2u, 4u);
    layout = layoutOf(descriptor);

    EXPECT_EQ(view.descriptor().sampleType(), an::SampleType::Float32);
    EXPECT_EQ(view.layout().extent().width(), 4u);
    EXPECT_EQ(view.layout().extent().height(), 2u);
    EXPECT_EQ(view.samples(), samples.data());
}

TEST(ImageCalibrationTest, BiasDarkFlatAndPedestalExecuteInOneDeclaredPass)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 4u, 2u);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::array<float, 8> input{10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f};
    const std::array<float, 8> bias{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    const std::array<float, 8> dark{2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 4.0f, 4.0f, 4.0f};
    const std::array<float, 8> flat{2.0f, 4.0f, 5.0f, 10.0f, 2.0f, 4.0f, 5.0f, 10.0f};
    std::array<float, 8> output{};

    const an::CalibrationFrameView light = frameOf(input.data(), layout, descriptor);
    const an::CalibrationFrameView biasFrame = frameOf(bias.data(), layout, descriptor);
    const an::CalibrationFrameView darkFrame = frameOf(dark.data(), layout, descriptor);
    const an::CalibrationFrameView flatFrame = frameOf(flat.data(), layout, descriptor);
    const an::CalibrationMasters masters{.bias = biasFrame, .dark = darkFrame, .flat = flatFrame};
    const an::CalibrationRequest request{.pedestal = 0.5,
                                         .darkScaling = an::DarkScalingMode::Fixed,
                                         .darkScale = 1.5,
                                         .flatNormalization = an::FlatNormalizationMode::None};

    const an::Result<an::CalibrationPlan> planned = an::prepareImageCalibration(execution, light, masters, request);
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_TRUE(planned->usesBias());
    EXPECT_TRUE(planned->usesDark());
    EXPECT_TRUE(planned->usesFlat());
    EXPECT_DOUBLE_EQ(planned->effectiveDarkScale(), 1.5);
    EXPECT_EQ(planned->precision(), an::ComputePrecision::Float64);
    EXPECT_EQ(planned->float64Justification(), an::Float64Justification::CalibrationDivide);

    const an::Result<an::CalibrationResult> calibrated =
        an::calibrateImage(execution, *planned, light, masters, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        float expected = static_cast<float>(static_cast<double>(input[index]) + request.pedestal);
        expected = static_cast<float>(static_cast<double>(expected) - static_cast<double>(bias[index]));
        const float scaledDark = static_cast<float>(static_cast<double>(dark[index]) * request.darkScale);
        expected = static_cast<float>(static_cast<double>(expected) - static_cast<double>(scaledDark));
        expected = static_cast<float>(static_cast<double>(expected) / static_cast<double>(flat[index]));
        EXPECT_FLOAT_EQ(output[index], expected) << index;
    }
    EXPECT_EQ(calibrated->imagePasses, 1u);
    EXPECT_EQ(calibrated->samplesWritten, input.size());
    EXPECT_EQ(calibrated->nonFiniteOutputs, 0u);
    EXPECT_GE(calibrated->plannedWorkers, 1u);
    EXPECT_NE(calibrated->backend.instructionSet, an::InstructionSet::Automatic);
}

TEST(ImageCalibrationTest, CentralFlatNormalizationIsPerRgbChannelAndReusable)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);
    const an::ImageDescriptor descriptor =
        descriptorOf(an::SampleType::Float64, 6u, 6u, an::ChannelSchema::Rgb, an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = layoutOf(descriptor, an::StorageArrangement::Interleaved);
    std::vector<double> light(6u * 6u * 3u, 20.0);
    std::vector<double> flat(light.size(), 1.0);
    for (std::size_t pixel = 0u; pixel < 36u; ++pixel)
    {
        flat[3u * pixel] = 2.0;
        flat[3u * pixel + 1u] = 4.0;
        flat[3u * pixel + 2u] = 5.0;
    }
    std::vector<double> output(light.size(), 0.0);
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    const an::CalibrationFrameView flatFrame = frameOf(flat.data(), layout, descriptor);
    const an::CalibrationMasters masters{.flat = flatFrame};

    const an::Result<an::CalibrationPlan> planned = an::prepareImageCalibration(
        execution, input, masters, an::CalibrationRequest{.flatNormalization = an::FlatNormalizationMode::CentralMean});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->flatNormalizationGroupCount(), 3u);
    EXPECT_DOUBLE_EQ(planned->flatNormalizationFactor(0u), 2.0);
    EXPECT_DOUBLE_EQ(planned->flatNormalizationFactor(1u), 4.0);
    EXPECT_DOUBLE_EQ(planned->flatNormalizationFactor(2u), 5.0);

    const an::Result<an::CalibrationResult> calibrated =
        an::calibrateImage(execution, *planned, input, masters, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    for (std::size_t pixel = 0u; pixel < 36u; ++pixel)
    {
        EXPECT_DOUBLE_EQ(output[3u * pixel], 20.0);
        EXPECT_DOUBLE_EQ(output[3u * pixel + 1u], 20.0);
        EXPECT_DOUBLE_EQ(output[3u * pixel + 2u], 20.0);
    }
}

TEST(ImageCalibrationTest, BayerFlatNormalizationKeepsTheFourSitesSeparate)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(cfa.has_value());
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 6u, 6u, an::ChannelSchema::Mono,
                                                        an::NumericDomain::NormalizedLinear, *cfa);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> light(36u, 12.0f);
    std::vector<float> flat(36u, 0.0f);
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 6u; ++x)
        {
            flat[static_cast<std::size_t>(y) * 6u + x] = static_cast<float>(1u + (y & 1u) * 2u + (x & 1u));
        }
    }
    std::vector<float> output(36u, 0.0f);
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    const an::CalibrationFrameView flatFrame = frameOf(flat.data(), layout, descriptor);
    const an::CalibrationMasters masters{.flat = flatFrame};
    const an::Result<an::CalibrationPlan> planned = an::prepareImageCalibration(
        execution, input, masters, an::CalibrationRequest{.flatNormalization = an::FlatNormalizationMode::CentralMean});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->flatNormalizationGroupCount(), 4u);
    for (std::uint32_t group = 0u; group < 4u; ++group)
    {
        EXPECT_DOUBLE_EQ(planned->flatNormalizationFactor(group), static_cast<double>(group + 1u));
    }
    ASSERT_TRUE(
        an::calibrateImage(execution, *planned, input, masters, output.data(), layout.totalBytes(), layout).hasValue());
    for (float value : output)
    {
        EXPECT_FLOAT_EQ(value, 12.0f);
    }
}

TEST(ImageCalibrationTest, OneChannelFlatCalibratesEveryRgbChannel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor rgbDescriptor = descriptorOf(an::SampleType::Float32, 2u, 1u, an::ChannelSchema::Rgb);
    const an::ImageDescriptor monoDescriptor = descriptorOf(an::SampleType::Float32, 2u, 1u);
    const an::ImageLayout rgbLayout = layoutOf(rgbDescriptor, an::StorageArrangement::Interleaved);
    const an::ImageLayout monoLayout = layoutOf(monoDescriptor);
    const std::array<float, 6> light{2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f};
    const std::array<float, 2> flat{2.0f, 4.0f};
    std::array<float, 6> output{};
    const an::CalibrationFrameView input = frameOf(light.data(), rgbLayout, rgbDescriptor);
    const an::CalibrationFrameView flatFrame = frameOf(flat.data(), monoLayout, monoDescriptor);
    const an::CalibrationMasters masters{.flat = flatFrame};
    const an::Result<an::CalibrationPlan> planned = an::prepareImageCalibration(
        execution, input, masters, an::CalibrationRequest{.flatNormalization = an::FlatNormalizationMode::None});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    const an::Result<an::CalibrationResult> calibrated =
        an::calibrateImage(execution, *planned, input, masters, output.data(), rgbLayout.totalBytes(), rgbLayout);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    EXPECT_EQ(output, (std::array<float, 6>{1.0f, 2.0f, 3.0f, 2.0f, 2.5f, 3.0f}));
}

TEST(ImageCalibrationTest, AZeroFlatProducesNanAndTheTransitionStatesFiniteValidity)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 3u, 1u);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::array<float, 3> light{2.0f, 4.0f, 6.0f};
    const std::array<float, 3> flat{1.0f, 0.0f, 2.0f};
    std::array<float, 3> output{};
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    const an::CalibrationFrameView flatFrame = frameOf(flat.data(), layout, descriptor);
    const an::CalibrationMasters masters{.flat = flatFrame};
    const an::Result<an::CalibrationPlan> planned = an::prepareImageCalibration(
        execution, input, masters, an::CalibrationRequest{.flatNormalization = an::FlatNormalizationMode::None});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::Result<an::CalibrationResult> calibrated =
        an::calibrateImage(execution, *planned, input, masters, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    EXPECT_EQ(calibrated->nonFiniteOutputs, 1u);
    EXPECT_TRUE(std::isnan(output[1]));

    const an::Result<an::ImageSemantics> source =
        an::ImageSemantics::create(an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Unspecified});
    ASSERT_TRUE(source.hasValue());
    const an::Result<an::MetadataTransition> transition = an::imageCalibrationTransition(*planned, *source);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_EQ(transition->semantics->validity().rule(), an::ValidityRule::FiniteOnly);
    EXPECT_EQ(transition->semantics->transferFunction(), an::TransferFunction::Linear);
    EXPECT_TRUE(transition->semantics->processingState().flatFielded);
    ASSERT_TRUE(transition->record.has_value());
    EXPECT_EQ(transition->record->operation(), "an::calibrateImage");
    EXPECT_FALSE(transition->record->detail().empty());
}

TEST(ImageCalibrationTest, MasterDomainsGeometryAndCfaPhaseMustMatchTheLight)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor lightDescriptor = descriptorOf(an::SampleType::Float32, 4u, 4u);
    const an::ImageLayout lightLayout = layoutOf(lightDescriptor);
    const std::array<float, 16> values{};
    const an::CalibrationFrameView light = frameOf(values.data(), lightLayout, lightDescriptor);

    const an::ImageDescriptor wrongExtent = descriptorOf(an::SampleType::Float32, 5u, 4u);
    const an::ImageLayout wrongExtentLayout = layoutOf(wrongExtent);
    const std::array<float, 20> wide{};
    const an::CalibrationFrameView wideBias = frameOf(wide.data(), wrongExtentLayout, wrongExtent);
    EXPECT_EQ(
        an::prepareImageCalibration(execution, light, an::CalibrationMasters{.bias = wideBias}, {}).status().code(),
        an::ErrorCode::IncompatibleArguments);

    const an::ImageDescriptor wrongDomain =
        descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Mono, an::NumericDomain::PhysicalScaled);
    const an::ImageLayout wrongDomainLayout = layoutOf(wrongDomain);
    const an::CalibrationFrameView normalizedBias = frameOf(values.data(), wrongDomainLayout, wrongDomain);
    EXPECT_EQ(an::prepareImageCalibration(execution, light, an::CalibrationMasters{.bias = normalizedBias}, {})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const std::optional<an::CfaDescriptor> shifted = an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u);
    ASSERT_TRUE(shifted.has_value());
    const an::ImageDescriptor wrongCfa = descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Mono,
                                                      an::NumericDomain::NormalizedLinear, *shifted);
    const an::ImageLayout wrongCfaLayout = layoutOf(wrongCfa);
    const an::CalibrationFrameView shiftedDark = frameOf(values.data(), wrongCfaLayout, wrongCfa);
    EXPECT_EQ(
        an::prepareImageCalibration(execution, light, an::CalibrationMasters{.dark = shiftedDark}, {}).status().code(),
        an::ErrorCode::IncompatibleArguments);

    const an::ImageDescriptor rgbDescriptor = descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Rgb);
    an::ImageDescriptorRequest displayRequest = rgbDescriptor.toRequest();
    displayRequest.colorSpace = an::ColorSpace::DisplayRgb;
    const std::optional<an::ImageDescriptor> displayDescriptor = an::ImageDescriptor::create(displayRequest);
    ASSERT_TRUE(displayDescriptor.has_value());
    const an::ImageLayout rgbLayout = layoutOf(rgbDescriptor);
    const std::array<float, 48> rgbValues{};
    const an::CalibrationFrameView rgbLight = frameOf(rgbValues.data(), rgbLayout, rgbDescriptor);
    const an::CalibrationFrameView displayBias = frameOf(rgbValues.data(), rgbLayout, *displayDescriptor);
    EXPECT_EQ(an::prepareImageCalibration(execution, rgbLight, an::CalibrationMasters{.bias = displayBias}, {})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ImageCalibrationTest, CalibrationNeedsFloatingLinearArithmeticAndAtLeastOneComponent)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor integerDescriptor = descriptorOf(an::SampleType::UInt16, 4u, 4u);
    const an::ImageLayout integerLayout = layoutOf(integerDescriptor);
    const std::array<std::uint16_t, 16> integers{};
    EXPECT_EQ(an::CalibrationFrameView::create(integers.data(), sizeof(integers), integerLayout, integerDescriptor)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);

    const an::ImageDescriptor floatDescriptor = descriptorOf(an::SampleType::Float32, 4u, 4u);
    const an::ImageLayout floatLayout = layoutOf(floatDescriptor);
    const std::array<float, 16> floats{};
    const an::CalibrationFrameView light = frameOf(floats.data(), floatLayout, floatDescriptor);
    EXPECT_EQ(an::prepareImageCalibration(execution, light, {}, {}).status().code(), an::ErrorCode::InvalidArgument);

    const an::ImageDescriptor encodedDescriptor =
        descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Rgb, an::NumericDomain::EncodedNonlinear);
    const an::ImageLayout encodedLayout = layoutOf(encodedDescriptor);
    EXPECT_EQ(an::CalibrationFrameView::create(floats.data(), sizeof(floats), encodedLayout, encodedDescriptor)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);
}

TEST(ImageCalibrationTest, BorrowedExtentsAndThePreparedDescriptorAreRechecked)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 4u, 4u);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::array<float, 16> light{};
    std::array<float, 16> output{};

    EXPECT_EQ(
        an::CalibrationFrameView::create(light.data(), layout.totalBytes() - 1u, layout, descriptor).status().code(),
        an::ErrorCode::InvalidArgument);

    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    EXPECT_EQ(input.bufferBytes(), layout.totalBytes());
    const an::Result<an::CalibrationPlan> planned =
        an::prepareImageCalibration(execution, input, {}, an::CalibrationRequest{.pedestal = 0.5});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(an::calibrateImage(execution, *planned, input, {}, output.data(), layout.totalBytes() - 1u, layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    an::ImageDescriptorRequest changedRequest = descriptor.toRequest();
    changedRequest.coordinateOrigin = an::CoordinateOrigin::BottomLeft;
    const std::optional<an::ImageDescriptor> changedDescriptor = an::ImageDescriptor::create(changedRequest);
    ASSERT_TRUE(changedDescriptor.has_value());
    const an::CalibrationFrameView changedInput = frameOf(light.data(), layout, *changedDescriptor);
    EXPECT_EQ(an::calibrateImage(execution, *planned, changedInput, {}, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ImageCalibrationTest, ValidityAliasingAndCancellationAreReturnedOutcomes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 3u, 1u);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::array<float, 3> light{1.0f, 2.0f, 3.0f};

    std::array<std::uint8_t, 1> maskBytes{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, descriptor.extent());
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    ASSERT_TRUE(writableMask->setValid(0u, 1u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    const an::ValidityDescriptor validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    const an::KernelValidityView validityView{.descriptor = validity, .mask = &mask};
    an::KernelValidityView shiftedValidity = validityView;
    shiftedValidity.maskPlane = 1u;
    EXPECT_EQ(an::CalibrationFrameView::create(light.data(), layout.totalBytes(), layout, descriptor, shiftedValidity)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor, validityView);
    const an::Result<an::CalibrationPlan> planned =
        an::prepareImageCalibration(execution, input, {}, an::CalibrationRequest{.pedestal = 0.5});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    std::array<float, 3> output{};
    const an::Result<an::CalibrationResult> calibrated =
        an::calibrateImage(execution, *planned, input, {}, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    EXPECT_FLOAT_EQ(output[0], 1.5f);
    EXPECT_TRUE(std::isnan(output[1]));
    EXPECT_FLOAT_EQ(output[2], 3.5f);
    EXPECT_EQ(calibrated->invalidInputs, 1u);
    EXPECT_EQ(calibrated->path, an::CalibrationExecutionPath::Reference);

    EXPECT_EQ(
        an::calibrateImage(execution, *planned, input, {}, light.data(), layout.totalBytes(), layout).status().code(),
        an::ErrorCode::Aliasing);

    std::stop_source stopped;
    stopped.request_stop();
    const an::ExecutionContext cancelled = makeExecution(engine, an::ComputeBackend::Automatic, stopped.get_token());
    EXPECT_EQ(
        an::prepareImageCalibration(cancelled, input, {}, an::CalibrationRequest{.pedestal = 0.5}).status().code(),
        an::ErrorCode::Cancelled);
}

TEST(ImageCalibrationTest, OptimizedDarkScalingRecoversTheVarianceMinimum)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = an::kCalibrationDarkOptimizationSide;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kSide, kSide);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> dark(static_cast<std::size_t>(kSide) * kSide, 0.0f);
    std::vector<float> light(dark.size(), 0.0f);
    for (std::size_t index = 0u; index < dark.size(); ++index)
    {
        dark[index] = static_cast<float>((index * 37u) % 251u) / 251.0f;
        light[index] = 3.0f + 1.25f * dark[index];
    }
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    const an::CalibrationFrameView darkFrame = frameOf(dark.data(), layout, descriptor);
    const an::Result<an::CalibrationPlan> planned =
        an::prepareImageCalibration(execution, input, an::CalibrationMasters{.dark = darkFrame},
                                    an::CalibrationRequest{.darkScaling = an::DarkScalingMode::OptimizeCentralRegion});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_NEAR(planned->effectiveDarkScale(), 1.25, 0.002);
    EXPECT_EQ(planned->darkOptimizationSamples(), static_cast<std::uint64_t>(kSide) * kSide);
    EXPECT_GT(planned->darkOptimizationEvaluations(), 1u);
}

TEST(ImageCalibrationTest, ReferenceAndCompiledFusedRunsStoreTheSameBits)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext referenceExecution = makeExecution(engine, an::ComputeBackend::Reference);
    const an::ExecutionContext compiledExecution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 257u, 3u);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> light(257u * 3u, 0.0f);
    std::vector<float> bias(light.size(), 0.0f);
    std::vector<float> dark(light.size(), 0.0f);
    std::vector<float> flat(light.size(), 0.0f);
    for (std::size_t index = 0u; index < light.size(); ++index)
    {
        light[index] = static_cast<float>((index * 19u) % 401u) / 17.0f;
        bias[index] = static_cast<float>((index * 7u) % 31u) / 101.0f;
        dark[index] = static_cast<float>((index * 11u) % 47u) / 113.0f;
        flat[index] = index % 97u == 0u ? 0.0f : 0.5f + static_cast<float>(index % 13u) / 17.0f;
    }
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    const an::CalibrationFrameView biasFrame = frameOf(bias.data(), layout, descriptor);
    const an::CalibrationFrameView darkFrame = frameOf(dark.data(), layout, descriptor);
    const an::CalibrationFrameView flatFrame = frameOf(flat.data(), layout, descriptor);
    const an::CalibrationMasters masters{.bias = biasFrame, .dark = darkFrame, .flat = flatFrame};
    const an::CalibrationRequest request{
        .pedestal = 0.25, .darkScale = 0.875, .flatNormalization = an::FlatNormalizationMode::None};
    const an::Result<an::CalibrationPlan> referencePlan =
        an::prepareImageCalibration(referenceExecution, input, masters, request);
    const an::Result<an::CalibrationPlan> compiledPlan =
        an::prepareImageCalibration(compiledExecution, input, masters, request);
    ASSERT_TRUE(referencePlan.hasValue());
    ASSERT_TRUE(compiledPlan.hasValue());
    std::vector<float> reference(light.size(), 0.0f);
    std::vector<float> compiled(light.size(), 0.0f);
    const an::Result<an::CalibrationResult> referenceRun = an::calibrateImage(
        referenceExecution, *referencePlan, input, masters, reference.data(), layout.totalBytes(), layout);
    const an::Result<an::CalibrationResult> compiledRun = an::calibrateImage(
        compiledExecution, *compiledPlan, input, masters, compiled.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(referenceRun.hasValue()) << referenceRun.status().message();
    ASSERT_TRUE(compiledRun.hasValue()) << compiledRun.status().message();
    for (std::size_t index = 0u; index < reference.size(); ++index)
    {
        if (std::isnan(reference[index]))
        {
            EXPECT_TRUE(std::isnan(compiled[index])) << index;
        }
        else
        {
            EXPECT_EQ(compiled[index], reference[index]) << index;
        }
    }
    EXPECT_EQ(referenceRun->path, an::CalibrationExecutionPath::Reference);
    if (compiledRun->backend.instructionSet != an::InstructionSet::Scalar)
    {
        EXPECT_EQ(compiledRun->path, an::CalibrationExecutionPath::CompiledFused);
    }
}

TEST(ImageCalibrationTest, EveryCompiledRowStoresTheScalarRowsBitsForHostileSamplesAndParameters)
{
    // The compiled row is held to the scalar row's bits, not its values: a signed zero, a not-a-number
    // payload, a subnormal quotient and an overflow to infinity all count. Every sample below is one
    // of those, an extreme, or an ordinary value, and the three parameters the row takes at Float64
    // are each tried both at a value Float32 holds exactly and at one it does not.
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kWidth = 263u;
    constexpr std::uint32_t kHeight = 6u;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::uint64_t state = 0x6ca1u;
    const auto next = [&state]() {
        state += 0x9e3779b97f4a7c15ull;
        std::uint64_t mixed = state;
        mixed = (mixed ^ (mixed >> 30u)) * 0xbf58476d1ce4e5b9ull;
        mixed = (mixed ^ (mixed >> 27u)) * 0x94d049bb133111ebull;
        return mixed ^ (mixed >> 31u);
    };
    const auto hostile = [&next]() {
        const std::uint64_t draw = next();
        const auto bits = static_cast<std::uint32_t>(draw >> 32u);
        switch (draw % 12u)
        {
        case 0u:
            return std::bit_cast<float>(bits); // any pattern: signalling and quiet NaNs included
        case 1u:
            return std::bit_cast<float>(bits & 0x807FFFFFu); // a subnormal or a signed zero
        case 2u:
            return (bits & 1u) != 0u ? -0.0f : 0.0f;
        case 3u:
            return (bits & 1u) != 0u ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
        case 4u:
            return std::bit_cast<float>((bits & 0x80000000u) | 0x7F000000u | (bits & 0x007FFFFFu)); // near the top
        case 5u:
            return std::bit_cast<float>((bits & 0x80000000u) | 0x00800000u | (bits & 0x007FFFFFu)); // smallest normals
        default:
            return static_cast<float>(static_cast<double>(bits) / 4294967296.0 * 3.0 - 0.5);
        }
    };
    std::vector<float> light(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<float> bias(light.size());
    std::vector<float> dark(light.size());
    std::vector<float> flat(light.size());
    for (std::size_t index = 0u; index < light.size(); ++index)
    {
        light[index] = hostile();
        bias[index] = hostile();
        dark[index] = hostile();
        const auto x = static_cast<std::uint32_t>(index % kWidth);
        const auto y = static_cast<std::uint32_t>(index / kWidth);
        // The central third is where a flat's normalization is measured, and it has to be finite there.
        const bool central = x >= kWidth / 3u && x < 2u * (kWidth / 3u) && y >= kHeight / 3u && y < 2u * (kHeight / 3u);
        flat[index] = central ? 0.75f + static_cast<float>(next() % 1000u) / 997.0f : hostile();
    }
    const an::CalibrationFrameView input = frameOf(light.data(), layout, descriptor);
    const an::CalibrationMasters masters{.bias = frameOf(bias.data(), layout, descriptor),
                                         .dark = frameOf(dark.data(), layout, descriptor),
                                         .flat = frameOf(flat.data(), layout, descriptor)};

    std::vector<an::InstructionSet> compiledSets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const auto candidate = static_cast<an::InstructionSet>(index);
        if (candidate != an::InstructionSet::Automatic && candidate != an::InstructionSet::Scalar &&
            an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            compiledSets.push_back(candidate);
        }
    }
    if (compiledSets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    const an::ExecutionContext referenceExecution = makeExecution(engine, an::ComputeBackend::Reference);

    std::uint32_t compared = 0u;
    for (const double pedestal : {0.0, 0.1})
    {
        for (const double darkScale : {1.0, 0.875, 1.0 / 3.0})
        {
            for (const an::FlatNormalizationMode normalization :
                 {an::FlatNormalizationMode::None, an::FlatNormalizationMode::CentralMean})
            {
                const an::CalibrationRequest request{
                    .pedestal = pedestal, .darkScale = darkScale, .flatNormalization = normalization};
                const an::Result<an::CalibrationPlan> plan =
                    an::prepareImageCalibration(referenceExecution, input, masters, request);
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                std::vector<float> reference(light.size(), 0.0f);
                const an::Result<an::CalibrationResult> referenceRun = an::calibrateImage(
                    referenceExecution, *plan, input, masters, reference.data(), layout.totalBytes(), layout);
                ASSERT_TRUE(referenceRun.hasValue()) << referenceRun.status().message();
                ASSERT_EQ(referenceRun->path, an::CalibrationExecutionPath::Reference);
                for (const an::InstructionSet instructionSet : compiledSets)
                {
                    an::ExecutionOptions options;
                    options.maxConcurrency = 3u;
                    options.backend = an::ComputeBackend::Cpu;
                    options.instructionSet = instructionSet;
                    options.backendFallback = an::BackendFallback::Forbidden;
                    an::Result<an::ExecutionContext> compiledExecution = an::ExecutionContext::create(engine, options);
                    ASSERT_TRUE(compiledExecution.hasValue()) << compiledExecution.status().message();
                    std::vector<float> compiled(light.size(), 0.0f);
                    const an::Result<an::CalibrationResult> compiledRun = an::calibrateImage(
                        *compiledExecution, *plan, input, masters, compiled.data(), layout.totalBytes(), layout);
                    ASSERT_TRUE(compiledRun.hasValue()) << compiledRun.status().message();
                    ASSERT_EQ(compiledRun->path, an::CalibrationExecutionPath::CompiledFused);
                    ASSERT_EQ(compiledRun->backend.instructionSet, instructionSet);
                    EXPECT_EQ(compiledRun->nonFiniteOutputs, referenceRun->nonFiniteOutputs);
                    for (std::size_t index = 0u; index < reference.size(); ++index)
                    {
                        ASSERT_EQ(std::bit_cast<std::uint32_t>(compiled[index]),
                                  std::bit_cast<std::uint32_t>(reference[index]))
                            << "sample " << index << " under pedestal " << pedestal << ", dark scale " << darkScale
                            << ", normalization " << an::flatNormalizationModeName(normalization) << ", set "
                            << static_cast<unsigned>(instructionSet) << ": light " << light[index] << " bias "
                            << bias[index] << " dark " << dark[index] << " flat " << flat[index];
                    }
                    ++compared;
                }
            }
        }
    }
    EXPECT_EQ(compared, 12u * compiledSets.size());
}

} // namespace

TEST(ImageCalibrationTest, ReplacementFlatMustKeepPreparedChannelsBeforeAnyWrite)
{
    const auto engine = makeEngine();
    const auto rgb = descriptorOf(an::SampleType::Float32, 2u, 2u, an::ChannelSchema::Rgb);
    const auto mono = descriptorOf(an::SampleType::Float32, 2u, 2u);
    for (const auto arrangement : {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
    {
        const auto rgbLayout = layoutOf(rgb, arrangement);
        const auto monoLayout = layoutOf(mono);
        const std::vector<float> light(12u, 40.0f), rgbFlat(12u, 2.0f), monoFlat(4u, 2.0f);
        const auto source = frameOf(light.data(), rgbLayout, rgb);
        const std::array flats{frameOf(rgbFlat.data(), rgbLayout, rgb), frameOf(monoFlat.data(), monoLayout, mono)};
        for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
        {
            const auto execution = makeExecution(engine, backend);
            for (std::size_t original = 0u; original < flats.size(); ++original)
            {
                const auto plan = an::prepareImageCalibration(execution, source, {.flat = flats[original]},
                                                              {.flatNormalization = an::FlatNormalizationMode::None});
                ASSERT_TRUE(plan.hasValue());
                std::vector<float> output(12u, -1.0f);
                const auto refused = an::calibrateImage(execution, *plan, source, {.flat = flats[1u - original]},
                                                        output.data(), rgbLayout.totalBytes(), rgbLayout);
                EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
                EXPECT_EQ(output, std::vector<float>(12u, -1.0f));
                const auto accepted = an::calibrateImage(execution, *plan, source, {.flat = flats[original]},
                                                         output.data(), rgbLayout.totalBytes(), rgbLayout);
                ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
                EXPECT_EQ(output, std::vector<float>(12u, 20.0f));
            }
        }
    }
}
