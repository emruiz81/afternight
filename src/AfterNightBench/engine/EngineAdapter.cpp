// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "EngineAdapter.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Color/LrgbCombination.h>
#include <AfterNightEngine/Algorithms/Masks/MaskOverlay.h>
#include <AfterNightEngine/Algorithms/Pointwise/BlendComposition.h>
#include <AfterNightEngine/Algorithms/Pointwise/ExternalToolHandoff.h>
#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>
#include <AfterNightEngine/Algorithms/Pointwise/ImageDifference.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Pointwise/StatisticalStretch.h>
#include <AfterNightEngine/Algorithms/Spatial/DrawingPrimitives.h>
#include <AfterNightEngine/Algorithms/Stars/StarMaskRecombination.h>

// The process identifier the pipeline scratch directory is named after. Each platform declares it in
// a header of its own and neither arrives with anything else this file includes, so both are named
// here. The Windows spelling is the C runtime's `_getpid` rather than `GetCurrentProcessId`, which
// would mean including <windows.h> into a translation unit that would then have to defend itself
// against that header's `min` and `max` macros for the sake of a directory name.
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "DumpCodecScenarios.h"
#include "PreviewGammaScenarios.h"
#include "SequenceScenarios.h"
#include "SnapshotScenarios.h"
#include "StretchScenarios.h"
#include "HistogramStretchOracle.h"
#include "AsinhStretchOracle.h"
#include "GeneralizedHyperbolicStretchOracle.h"
#include "MidtonesTransferStretchOracle.h"
#include "PresentationOracle.h"
#include "PresentationScenarios.h"
#include "ChannelScopeOracle.h"
#include "ChannelScopeScenarios.h"
#include "CombinationOracle.h"
#include "StackingNumericsOracle.h"
#include "StackingNumericsScenarios.h"
#include "CombinationScenarios.h"
#include "CurveScenarios.h"
#include "DifferenceOracle.h"
#include "MaskRenderingOracle.h"
#include "MaskRenderingScenarios.h"
#include "StatisticalStretchOracle.h"
#include "StreamingScenarios.h"
#include "SerSequenceScenarios.h"
#include "BenchVideo.h"
#include "FfmpegSequenceScenarios.h"
#include "VideoWriteScenarios.h"
#include <AfterNightEngine/IO/AviSequence.h>
#include <AfterNightEngine/IO/FfmpegSink.h>
#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/FrameCache.h>
#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/IO/DummySource.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/IO/SerSequence.h>
#include <fstream>
#include <iterator>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/ImageSnapshot.h>

#include "RawCodecFixture.h"
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Synthesis/NoiseGeneration.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include "BasicStatisticsOracle.h"
#include "BenchJson.h"
#include "BilinearResampleOracle.h"
#include "CalibrationFixture.h"
#include "CapturedStatistics.h"
#include "NormalizationFixture.h"
#include "ColorTransformOracle.h"
#include "CometDetectionFixture.h"
#include "CosmeticCorrectionOracle.h"
#include "AutostretchOracle.h"
#include "AutostretchScenarios.h"
#include "DemosaicCaptureScenarios.h"
#include "DemosaicOracle.h"
#include "NormalizationPairOracle.h"
#include "NormalizationPairScenarios.h"
#include "GammaTransferOracle.h"
#include "HistogramOracle.h"
#include "MedianFilterOracle.h"
#include "NoiseReductionOracle.h"
#include "OrderStatisticsOracle.h"
#include "PhaseCorrelationOracle.h"
#include "PipelineFixture.h"
#include "PipelineScenario.h"
#include "PipelineScratch.h"
#include "PipelineStageRow.h"
#include "CatalogFixture.h"
#include "RegistrationFixture.h"
#include "SipFixture.h"
#include "DrizzleGeometryFixture.h"
#include "DrizzleAccumulationFixture.h"
#include "DrizzleExecutionFixture.h"
#include "CodecLookupFixture.h"
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/OutputTransaction.h>

#include <filesystem>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include "PhotometricWeightingFixture.h"
#include "AperturePhotometryFixture.h"
#include "ColorFitFixture.h"
#include "SpectrophotometricFixture.h"
#include "PlateSolveFixture.h"
#include "StarFieldFixture.h"
#include "StarReductionAlignmentFixture.h"
#include "BackgroundModelOracle.h"
#include "RestorationOracle.h"
#include "SeparableConvolutionOracle.h"
#include "StackingFixture.h"
#include "StackingOracle.h"
#include "Digest.h"
#include "FixtureBridge.h"
#include "ProcessClock.h"
#include "ResidentMemory.h"

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandColorMap.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandExtraction.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandRgbCombination.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogAliases.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogText.h>
#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePixmap.h>
#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/CometComposite.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/AperturePhotometry.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Algorithms/Stars/StarReduction.h>
#include <AfterNightEngine/Algorithms/Demosaic/CfaChannels.h>
#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ResampleKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/TotalVariationDenoise.h>
#include <AfterNightEngine/Algorithms/Masks/RangeSelection.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathLowering.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Algorithms/CosmeticCorrection/CosmeticCorrection.h>
#include <AfterNightEngine/Algorithms/Background/AutoBackground.h>
#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>
#include <AfterNightEngine/Algorithms/Deconvolution/Deconvolution.h>
#include <AfterNightEngine/Algorithms/Detail/DarkStructureEnhance.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include <AfterNightEngine/Algorithms/Registration/PhaseCorrelation.h>
#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>
#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/LibraryInfo.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Interop/OpenCvAdapter.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// The runner's half of every scenario: the catalogue this engine is measured on, and the mapping of
// each row onto the engine's calls.
//
// A scenario is the same contract on every run: the same identity, the same parameters, the same
// declared input, the same work counts, built here from the same numbers rather than copied from a
// file, so two runs that name a row measured the same work.
//
// The output digest is taken over the converted samples exactly as they sit in memory, so two runs
// compare byte for byte rather than through a tolerance. That is only meaningful because the
// destination buffer is tightly packed: a layout with padding would digest whatever the allocator
// left between the rows.

namespace an::bench::engine
{
namespace
{

/// The dimensions every smoke scenario uses.
constexpr std::uint32_t kSmokeWidth = 1024;

/// The other dimension of the same square.
constexpr std::uint32_t kSmokeHeight = 1024;

/// The seed every smoke scenario uses.
constexpr std::uint64_t kSmokeSeed = 20260822;

/// Odd and byte-unaligned on purpose, so the production statistics benchmark exercises ROI row
/// strides and a packed-mask bit offset rather than accidentally becoming a contiguous reduction.
constexpr std::uint32_t kStatisticsRegionX = 113u;
constexpr std::uint32_t kStatisticsRegionY = 97u;
constexpr std::uint32_t kStatisticsRegionWidth = 797u;
constexpr std::uint32_t kStatisticsRegionHeight = 701u;
constexpr std::uint32_t kStatisticsMaskPeriod = 17u;

/// How many samples separate one convolution oracle probe from the next.
///
/// Coprime with the smoke width so the probes are not one column, and large enough that an
/// independent long-double evaluation of a production-sized plane stays affordable.
constexpr std::uint32_t kConvolutionProbeStride = 259u;

/// How many interior positions separate one median-filter oracle probe from the next.
///
/// A different stride from the convolution one, and a prime, so that the two oracles do not
/// sample the same lattice of the same fixture.
constexpr std::uint32_t kMedianProbeStride = 197u;

/// Coprime stride through the odd bilinear-resize output.
constexpr std::uint32_t kResampleProbeStride = 263u;

/// Coprime stride through the gamma-transferred plane.
constexpr std::uint32_t kGammaProbeStride = 269u;

/// Distance between the pixels the colour oracle checks, chosen coprime with the frame width so
/// that the probes do not land in one column.
constexpr std::uint32_t kColorProbeStride = 331u;

/// The descriptor a smoke scenario declares for its input.
///
/// @param type what the samples are.
/// @param channels how many channels.
/// @return the descriptor.
[[nodiscard]] SampleDescriptor smokeDescriptor(SampleType type, std::uint32_t channels)
{
    SampleDescriptor descriptor;
    descriptor.width = kSmokeWidth;
    descriptor.height = kSmokeHeight;
    descriptor.channels = channels;
    descriptor.sampleType = type;
    descriptor.layout = Layout::Planar;
    // Explicit rather than native, so a fixture digest means the same thing on either host.
    descriptor.byteOrder = ByteOrder::Little;
    return descriptor;
}

/// One chain the fused executor is weighed against per-operation execution on.
struct FusionChain
{
    /// Which chain, for the scenario parameters and the report.
    const char* name;

    /// The identity of the row that runs it in one image pass.
    const char* fusedId;

    /// The identity of the row that runs one image pass per step.
    const char* stagedId;

    /// How many steps the chain has, which is how many passes the staged row makes.
    std::uint32_t steps;
};

/// The three chains the fusion rows measure: one from each named group.
///
/// Every one reads and writes the normalized linear domain, which is not a simplification but a
/// requirement of the measurement. A step that changes the numeric domain - a gamma encode - has no
/// well-typed single-step form, because the program that carried it would have to declare a
/// transition its neighbours do not make. That is worth knowing about the intermediate form and it
/// is not what these rows are measuring.
///
/// @return the chains, in a fixed order.
[[nodiscard]] std::span<const FusionChain> fusionChains()
{
    static constexpr FusionChain kChains[] = {
        {"scale_clamp_ladder", "fusion.pointwise_chain_fused.f32", "fusion.pointwise_chain_staged.f32", 6u},
        {"color_matrix_exposure_clamp", "fusion.color_chain_fused.f32", "fusion.color_chain_staged.f32", 4u},
        {"expression_scale_bias", "fusion.expression_chain_fused.f32", "fusion.expression_chain_staged.f32", 2u}};
    return std::span<const FusionChain>(kChains);
}

/// One smoke scenario, built from the shared smoke dimensions and seed.
///
/// @param id the row's stable identity.
/// @param group the taxonomy group.
/// @param input what the scenario declares its input to be.
/// @param corpusId the generator or dataset the fixture comes from.
/// @param correctness how the row's output is judged.
/// @param tolerance what that judgement permits.
/// @return the scenario.
/// One real camera table, the mosaic the tabulated estimates are measured on.
constexpr an::CfaColorEntries kBenchCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// The estimate a scenario parameter names.
///
/// @param name the parameter value.
/// @return the method, or nothing when the parameter names none.
[[nodiscard]] std::optional<an::DemosaicMethod> demosaicMethodNamed(const std::string& name)
{
    constexpr an::DemosaicMethod kMethods[] = {an::DemosaicMethod::Bilinear,
                                               an::DemosaicMethod::Fast,
                                               an::DemosaicMethod::Rcd,
                                               an::DemosaicMethod::Amaze,
                                               an::DemosaicMethod::XTransFast,
                                               an::DemosaicMethod::MarkesteijnOnePass,
                                               an::DemosaicMethod::MarkesteijnThreePass,
                                               an::DemosaicMethod::Lmmse,
                                               an::DemosaicMethod::Vng4,
                                               an::DemosaicMethod::LuminanceAhd};
    for (const an::DemosaicMethod method : kMethods)
    {
        if (name == an::demosaicMethodName(method))
        {
            return method;
        }
    }
    return std::nullopt;
}

/// The mosaic a scenario parameter names.
///
/// @param name the parameter value.
/// @return the descriptor, or nothing when the parameter names none.
[[nodiscard]] std::optional<an::CfaDescriptor> mosaicNamed(const std::string& name)
{
    if (name == "XTrans")
    {
        return an::CfaDescriptor::create(kBenchCameraTable, 0u, 0u);
    }
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        if (name == an::cfaPatternName(pattern))
        {
            return an::CfaDescriptor::create(pattern, 0u, 0u);
        }
    }
    return std::nullopt;
}

/// One mosaic's repeating block, as the oracle's engine-independent colour indices.
///
/// @param cfa the mosaic.
/// @return the entries, row-major.
[[nodiscard]] std::vector<std::uint8_t> mosaicBlockEntries(const an::CfaDescriptor& cfa)
{
    const std::uint32_t size = an::cfaPatternBlockSize(cfa.pattern());
    std::vector<std::uint8_t> entries(static_cast<std::size_t>(size) * size, 0u);
    for (std::uint32_t row = 0u; row < size; ++row)
    {
        for (std::uint32_t column = 0u; column < size; ++column)
        {
            // The phase belongs to the block description rather than to the entries, so the entries
            // are read at the mosaic's own origin and the oracle applies the phase itself.
            const std::uint32_t x = (column + size - cfa.originX() % size) % size;
            const std::uint32_t y = (row + size - cfa.originY() % size) % size;
            entries[static_cast<std::size_t>(row) * size + column] = static_cast<std::uint8_t>(cfa.colorAt(x, y));
        }
    }
    return entries;
}

/// One demosaic row of the catalogue.
struct DemosaicScenario
{
    /// The row's stable identity.
    const char* id;

    /// Which estimate, as the runner's own parameter.
    const char* method;

    /// Which mosaic it estimates.
    const char* pattern;
};

/// The demosaic rows of the catalogue.
///
/// @return one entry per estimate.
[[nodiscard]] std::vector<DemosaicScenario> demosaicScenarios()
{
    return {
        {"demosaic.bilinear.f32", "Bilinear", "Rggb"},
        {"demosaic.fast.f32", "Fast", "Rggb"},
        {"demosaic.rcd.f32", "Rcd", "Rggb"},
        {"demosaic.direct_fast.f32", "Fast", "Rggb"},
        {"demosaic.direct_rcd.f32", "Rcd", "Rggb"},
        {"demosaic.amaze.f32", "Amaze", "Rggb"},
        {"demosaic.xtrans_fast.f32", "XTransFast", "XTrans"},
        {"demosaic.markesteijn_one.f32", "MarkesteijnOnePass", "XTrans"},
        {"demosaic.markesteijn_three.f32", "MarkesteijnThreePass", "XTrans"},
        {"demosaic.lmmse.f32", "Lmmse", "Rggb"},
        {"demosaic.vng4.f32", "Vng4", "Rggb"},
        {"demosaic.luminance_ahd.f32", "LuminanceAhd", "Rggb"},
    };
}

[[nodiscard]] ScenarioSpec smokeScenario(const std::string& id, const std::string& group, const SampleDescriptor& input,
                                         const std::string& corpusId, CorrectnessClass correctness, double tolerance)
{
    ScenarioSpec scenario;
    scenario.id = id;
    scenario.group = group;
    scenario.operationVersion = 1;
    scenario.inputSource = InputSource::Synthetic;
    scenario.input = input;
    scenario.corpusId = corpusId;
    scenario.seed = kSmokeSeed;
    scenario.correctnessClass = correctness;
    scenario.tolerance = tolerance;
    scenario.bytesProcessed = input.totalBytes();
    scenario.elementsProcessed = std::uint64_t{input.width} * input.height * input.channels;
    scenario.allowedBackends = {BackendId::Unspecified};
    return scenario;
}

/// The value of a scenario parameter, or a fallback.
///
/// @param scenario the scenario to read.
/// @param name the parameter.
/// @param fallback what to answer when the scenario states none.
/// @return the value, which is empty only when both the parameter and the fallback are.
[[nodiscard]] std::string parameterValue(const ScenarioSpec& scenario, const char* name, const char* fallback)
{
    const auto at = scenario.parameters.find(name);
    return at == scenario.parameters.end() ? std::string{fallback} : at->second;
}

/// A finite numeric scenario parameter, or why it could not be read.
[[nodiscard]] bool doubleParameter(const ScenarioSpec& scenario, const char* name, double fallback, double& out,
                                   std::string& error)
{
    const auto at = scenario.parameters.find(name);
    if (at == scenario.parameters.end())
    {
        out = fallback;
        return true;
    }
    const char* const first = at->second.data();
    const char* const last = first + at->second.size();
    const auto parsed = std::from_chars(first, last, out, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite(out))
    {
        error = "scenario '" + scenario.id + "' has invalid numeric parameter '" + name + "'";
        return false;
    }
    return true;
}

/// Wall time, CPU time and resident peak for one repetition of `body`.
///
/// The body receives false so output hashing stays outside all three measurements: the fixture is
/// already built and the digest is taken separately, so what is measured is the operation and
/// nothing around it.
///
/// @tparam BodyT what to measure.
/// @param body the operation, called with false to run without digesting.
/// @param timings where the two clocks are appended.
/// @param sampler the resident-set sampler, started and stopped around the same region.
/// @param peakBytes the largest peak seen so far, raised when this repetition beats it.
template <typename BodyT>
void timeOnce(const ScenarioSpec& scenario, BodyT&& body, TimingSamples& timings, ResidentMemorySampler& sampler,
              std::uint64_t& peakBytes)
{
    const std::uint32_t batch = timedBatchIterations(scenario).value_or(1u);
    waitForWarmWorkersToPark();
    sampler.start();
    const double cpuStart = processCpuMilliseconds();
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0u; index < batch; ++index)
    {
        body(false);
    }
    const auto finish = std::chrono::steady_clock::now();
    const double cpuFinish = processCpuMilliseconds();
    sampler.stop();
    const double scale = 1.0 / static_cast<double>(batch);
    timings.wallMs.push_back(std::chrono::duration<double, std::milli>(finish - start).count() * scale);
    timings.cpuMs.push_back((cpuFinish - cpuStart) * scale);
    peakBytes = (std::max)(peakBytes, sampler.peakBytes());
}

/// The execution options requested for this benchmark run.
///
/// A forced backend is exact: fallback is forbidden so an unavailable implementation produces a
/// typed failure and the harness records a skip instead of measuring another implementation under
/// the requested label.
///
/// @param options the run, including any exact backend request.
/// @return the engine execution options.
[[nodiscard]] an::ExecutionOptions executionOptionsForRun(const RunOptions& options)
{
    an::ExecutionOptions execution;
    if (!options.forcedBackend.has_value())
    {
        return execution;
    }

    execution.backendFallback = an::BackendFallback::Forbidden;
    switch (*options.forcedBackend)
    {
    case BackendId::Scalar:
        execution.backend = an::ComputeBackend::Reference;
        execution.instructionSet = an::InstructionSet::Scalar;
        break;
    case BackendId::Sse2:
        execution.backend = an::ComputeBackend::Cpu;
        execution.instructionSet = an::InstructionSet::Sse2;
        break;
    case BackendId::Sse42:
        execution.backend = an::ComputeBackend::Cpu;
        execution.instructionSet = an::InstructionSet::Sse42;
        break;
    case BackendId::Avx:
        execution.backend = an::ComputeBackend::Cpu;
        execution.instructionSet = an::InstructionSet::Avx;
        break;
    case BackendId::Avx2:
        execution.backend = an::ComputeBackend::Cpu;
        execution.instructionSet = an::InstructionSet::Avx2;
        break;
    case BackendId::Avx512:
        execution.backend = an::ComputeBackend::Cpu;
        execution.instructionSet = an::InstructionSet::Avx512Skylake;
        break;
    case BackendId::Neon:
        execution.backend = an::ComputeBackend::Cpu;
        execution.instructionSet = an::InstructionSet::Neon;
        break;
    case BackendId::Gpu:
        execution.backend = an::ComputeBackend::Gpu;
        break;
    case BackendId::Unspecified:
    case BackendId::CompilerVectorized:
    case BackendId::PortableSimd:
        break;
    }
    return execution;
}

/// The execution options a measured operation runs under.
///
/// None, which is what every ordinary measurement is: an operation nobody can stop. A run that
/// probes cancellation gives the operation the probe's stop token as well, and only such a run, so
/// an ordinary measurement is exactly what it was before the probe existed.
///
/// @param options the run, which says whether it probes.
/// @param probe the stop source the probe requests.
/// @return the options.
[[nodiscard]] an::ExecutionOptions measuredExecutionOptions(const RunOptions& options, const std::stop_source& probe)
{
    an::ExecutionOptions execution = executionOptionsForRun(options);
    if (options.cancelAtFraction.has_value())
    {
        execution.stopToken = probe.get_token();
    }
    return execution;
}

/// Requests a stop part of the way through one more call of a measured operation, and records what
/// happened.
///
/// The request is made `fraction` of the way through the operation's median time from the ordinary
/// run, by a thread that sleeps most of the way and spins the rest, because a sleep alone can land a
/// scheduler tick late and a tick is longer than many of these operations. What is recorded is
/// measured rather than planned: when the request was actually made, when the call returned, and
/// the failure it returned, by code.
///
/// @param fraction how far into the operation the stop is requested.
/// @param out the record the probe's counters go into.
/// @param body the operation.
/// @param failure whatever the body writes when it fails. Cleared afterwards: an operation that
///        stopped when asked is the probe's result, not a failure of the scenario.
/// @param probe the stop source the operation's execution was created with.
void probeCancellation(double fraction, ResultRecord& out, const std::function<std::string(bool)>& body,
                       std::string& failure, std::stop_source& probe)
{
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](Clock::duration span) {
        return std::chrono::duration<double, std::milli>(span).count();
    };
    std::vector<double> times = out.timings.wallMs;
    std::sort(times.begin(), times.end());
    const double typicalMs = times.empty() ? 0.0 : times[times.size() / 2u];
    const auto delay =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(typicalMs * fraction));

    (void)takeLastDescribedEngineFailure();
    waitForWarmWorkersToPark();
    std::atomic<bool> returned{false};
    bool requested = false;
    Clock::time_point requestedAt{};
    const Clock::time_point start = Clock::now();
    const Clock::time_point target = start + delay;
    std::thread stopper([&] {
        // A sleep lasts a whole scheduler tick, which is 15.6 ms on a Windows host left at its default
        // timer resolution; sleeping to within one of them would land most requests a tick late.
        while (!returned.load(std::memory_order_acquire) && Clock::now() + std::chrono::milliseconds(20) < target)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while (!returned.load(std::memory_order_acquire) && Clock::now() < target)
        {
        }
        if (!returned.load(std::memory_order_acquire))
        {
            requestedAt = Clock::now();
            requested = true;
            probe.request_stop();
        }
    });
    body(false);
    const Clock::time_point finished = Clock::now();
    returned.store(true, std::memory_order_release);
    stopper.join();

    // A request the stopper made after the call had already returned stopped nothing.
    const bool duringCall = requested && requestedAt <= finished;
    const std::optional<DescribedEngineFailure> described = takeLastDescribedEngineFailure();
    out.counters["cancelProbeFraction"] = fraction;
    out.counters["cancelProbeTypicalMs"] = typicalMs;
    out.counters["cancelProbeCallMs"] = milliseconds(finished - start);
    out.counters["cancelProbeDuringCall"] = duringCall ? 1.0 : 0.0;
    if (duringCall)
    {
        out.counters["cancelProbeRequestedMs"] = milliseconds(requestedAt - start);
        out.counters["cancelProbeLatencyMs"] = milliseconds(finished - requestedAt);
    }
    out.counters["cancelProbeFailed"] = failure.empty() ? 0.0 : 1.0;
    out.counters["cancelProbeCancelled"] = described.has_value() && described->cancelled ? 1.0 : 0.0;
    out.counters["cancelProbeErrorCode"] = described.has_value() ? static_cast<double>(described->code) : -1.0;
    failure.clear();
}

/// Warms, times and digests one measured body, and reports what the record needs.
///
/// The tail every runner ends with, written once. It is a free function rather than a member
/// because it touches nothing of the adapter: a scenario, the options, the record, and the body.
///
/// @param scenario what is being measured.
/// @param options the warm-up and repetition counts, and whether the run probes cancellation.
/// @param out the record the timings, peak memory and digest go into.
/// @param skipReason where a failure the body reported is written.
/// @param body the operation, called with false to run and with true to digest.
/// @param failure whatever the body wrote when it could not run.
/// @param probe the stop source the operation's execution was created with, or null for an
///        operation this runner does not probe. Requested only after the ordinary measurement and
///        digest, and only by a run that probes.
/// @return true when the scenario produced a measurement.
[[nodiscard]] bool finishScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                  std::string& skipReason, const std::function<std::string(bool)>& body,
                                  std::string& failure, std::stop_source* probe = nullptr)
{
    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    if (probe != nullptr && options.cancelAtFraction.has_value())
    {
        probeCancellation(*options.cancelAtFraction, out, body, failure, *probe);
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

/// The transfer/layout scenario one identity names, or null when it is not one of them.
///
/// @param id the scenario identity.
/// @return the row, which outlives the call, or null.
[[nodiscard]] const TransferBandwidthScenario* findTransferScenario(const std::string& id)
{
    static const std::vector<TransferBandwidthScenario> kTable = transferBandwidthScenarios();
    for (const TransferBandwidthScenario& entry : kTable)
    {
        if (id == entry.id)
        {
            return &entry;
        }
    }
    return nullptr;
}

/// What the destination of one transfer looks like, as a benchmark descriptor.
///
/// A repack is the only member of the family whose destination has a different arrangement from its
/// source; the rest write the shape they read, and a byte swap writes the buffer it read. The
/// strides are cleared so the bridge derives tight ones, which is what makes a digest over the
/// destination mean the samples rather than whatever the allocator left between the rows.
///
/// @param source what the fixture is.
/// @param operation which operation.
/// @return the destination descriptor.
[[nodiscard]] SampleDescriptor transferDestinationDescriptor(const SampleDescriptor& source,
                                                             TransferOperation operation)
{
    SampleDescriptor destination = source;
    destination.rowStrideBytes = 0u;
    destination.planeStrideBytes = 0u;
    if (operation == TransferOperation::Deinterleave)
    {
        destination.layout = Layout::Planar;
    }
    else if (operation == TransferOperation::Interleave || operation == TransferOperation::ComposeChannels)
    {
        destination.layout = Layout::Interleaved;
    }
    else if (operation == TransferOperation::ExtractChannel)
    {
        // The one destination in the family that is not the same picture as the source: a channel on
        // its own is one channel, and one channel is planar whatever the source was.
        destination.channels = 1u;
        destination.layout = Layout::Planar;
    }
    return destination;
}

/// The engine-context configuration for one benchmark invocation.
///
/// The thread override is applied at construction because a context's concurrency is immutable;
/// reporting the requested number without configuring it would make a scaling sweep fiction.
[[nodiscard]] an::EngineContextOptions benchmarkEngineOptions(const RunOptions& options)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = options.maxThreads;
    return engineOptions;
}

} // namespace

std::string AfterNightEngineAdapter::runnerVersion() const
{
    return "AfterNightEngineBench 26";
}

void AfterNightEngineAdapter::describeEngine(BuildManifest& manifest) const
{
    const an::LibraryInfo info = an::libraryInfo();
    manifest.compiler = info.compiler;
    manifest.configuration = info.configuration;
    manifest.dependencyVersions["AfterNightEngine"] = an::engineVersionString();
    manifest.dependencyVersions["AfterNightEngineAbi"] = std::to_string(an::engineAbiGeneration());
}

bool AfterNightEngineAdapter::prepare(const RunOptions& options, std::string& error)
{
    if (options.corpusManifest.empty())
    {
        // Synthetic fixtures only, which is the ordinary state on a machine that holds no corpus.
        return true;
    }

    CorpusManifest manifest;
    if (const auto parsed = json::readCorpusFile(options.corpusManifest, manifest); !parsed)
    {
        error = "corpus manifest " + options.corpusManifest + ": " + parsed.errors.front();
        return false;
    }
    if (const std::vector<std::string> problems = manifest.validate(); !problems.empty())
    {
        error = "corpus manifest " + options.corpusManifest + ": " + problems.front();
        return false;
    }

    m_fixtures.useCorpus(std::move(manifest), resolveCorpusRoot(options.corpusRoot));
    return true;
}

std::vector<ScenarioSpec> AfterNightEngineAdapter::scenarios() const
{
    std::vector<ScenarioSpec> set;
    const auto drizzleGeometry = drizzleGeometryScenarios();
    const auto drizzleAccumulation = drizzleAccumulationScenarios();
    set.insert(set.end(), drizzleAccumulation.begin(), drizzleAccumulation.end());
    const auto drizzleExecution = drizzleExecutionScenarios();
    set.insert(set.end(), drizzleExecution.begin(), drizzleExecution.end());
    set.push_back(codecLookupScenario());
    ScenarioSpec fits;
    fits.id = "codec.fits_roundtrip.f32";
    fits.group = "codec";
    fits.input.width = fits.input.height = 1024u;
    fits.input.channels = 1u;
    fits.input.sampleType = SampleType::Float32;
    fits.input.layout = Layout::Planar;
    fits.input.byteOrder = ByteOrder::Little;
    fits.corpusId = "synthetic.stars";
    fits.seed = 20260822u;
    fits.parameters["format"] = "FITS";
    fits.correctnessClass = CorrectnessClass::BitExact;
    fits.bytesProcessed = fits.input.totalBytes();
    fits.elementsProcessed = std::uint64_t{fits.input.width} * fits.input.height;
    fits.allowedBackends = {BackendId::Unspecified};
    set.push_back(fits);
    const auto dumps = dumpCodecScenarios();
    set.insert(set.end(), dumps.begin(), dumps.end());
    const auto snapshots = snapshotScenarios();
    set.insert(set.end(), snapshots.begin(), snapshots.end());
    const auto sequences = sequenceScenarios();
    set.insert(set.end(), sequences.begin(), sequences.end());
    const auto streams = streamingScenarios();
    set.insert(set.end(), streams.begin(), streams.end());
    const auto serSequences = serSequenceScenarios();
    set.insert(set.end(), serSequences.begin(), serSequences.end());
    const auto ffmpegSequences = ffmpegSequenceScenarios();
    set.insert(set.end(), ffmpegSequences.begin(), ffmpegSequences.end());
    const auto videoWrites = videoWriteScenarios();
    set.insert(set.end(), videoWrites.begin(), videoWrites.end());

    // The same round trip through the other container this engine writes. One scenario per format
    // rather than one parameterized row, because a comparison reads a format's own name.
    ScenarioSpec xisf = fits;
    xisf.id = "codec.xisf_roundtrip.f32";
    xisf.parameters["format"] = "XISF";
    set.push_back(xisf);
    // The three raster containers, through the same save-and-decode shape as the two above. Their
    // fixture is sixteen-bit rather than floating because that is what these formats hold, and the
    // lossy one is held within a stated distance of its input rather than bit for bit.
    ScenarioSpec png = fits;
    png.id = "codec.png_roundtrip.u16";
    png.parameters["format"] = "PNG";
    png.input.sampleType = SampleType::UInt16;
    png.bytesProcessed = png.input.totalBytes();
    set.push_back(png);

    ScenarioSpec tiff = png;
    tiff.id = "codec.tiff_roundtrip.u16";
    tiff.parameters["format"] = "TIFF";
    set.push_back(tiff);

    ScenarioSpec jpeg = png;
    jpeg.id = "codec.jpeg_roundtrip.u8";
    jpeg.parameters["format"] = "JPEG";
    jpeg.input.sampleType = SampleType::UInt8;
    jpeg.bytesProcessed = jpeg.input.totalBytes();
    jpeg.correctnessClass = CorrectnessClass::EstimatorTolerance;
    jpeg.tolerance = 8.0 / 255.0;
    set.push_back(jpeg);
    const auto rawCodecs = rawCodecScenarios();
    set.insert(set.end(), rawCodecs.begin(), rawCodecs.end());
    set.push_back(imageListWriteScenario());
    set.insert(set.end(), drizzleGeometry.begin(), drizzleGeometry.end());

    // Conversion: the boundary case every codec path goes through. Its answer is exact rather than
    // within a tolerance - a 16-bit code divided by 65535 in double rounds to the same float as the
    // exact quotient for every one of the 65536 values.
    ScenarioSpec conversion =
        smokeScenario("conversion.u16_to_f32", "conversion", smokeDescriptor(SampleType::UInt16, 1), "synthetic.ramp",
                      CorrectnessClass::ExactAfterMapping, 0.0);
    conversion.parameters["destination"] = "Float32";
    set.push_back(conversion);

    ScenarioSpec reverseConversion =
        smokeScenario("conversion.f32_to_u16", "conversion", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gradient", CorrectnessClass::LsbDistribution, 1.0);
    reverseConversion.parameters["destination"] = "UInt16";
    set.push_back(reverseConversion);

    ScenarioSpec storage =
        smokeScenario("storage.allocate_first_touch.f32", "storage", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    storage.parameters["value"] = "0.5";
    storage.workUnit = "samples";
    set.push_back(storage);

    ScenarioSpec pixelAccess =
        smokeScenario("pixel_access.generic_read_write.f32", "pixel-access", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    pixelAccess.parameters["scale"] = "0.5";
    pixelAccess.parameters["bias"] = "0.25";
    pixelAccess.bytesProcessed *= 2u;
    pixelAccess.workUnit = "samples";
    set.push_back(pixelAccess);

    ScenarioSpec openCvView =
        smokeScenario("interop.opencv_view.u16", "interop", smokeDescriptor(SampleType::UInt16, 1), "synthetic.ramp",
                      CorrectnessClass::BitExact, 0.0);
    openCvView.parameters["copy"] = "false";
    openCvView.workUnit = "samples";
    set.push_back(openCvView);

    ScenarioSpec reduction =
        smokeScenario("reduction.basic_statistics.f32", "reduction", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-9);
    reduction.parameters["channel"] = "0";
    reduction.parameters["passes"] = "2";
    reduction.bytesProcessed *= 2u;
    reduction.workUnit = "samples";
    set.push_back(reduction);

    ScenarioSpec maskedStatistics =
        smokeScenario("statistics.basic_masked_roi.f32", "statistics", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-9);
    maskedStatistics.parameters["channel"] = "0";
    maskedStatistics.parameters["passes"] = "2";
    maskedStatistics.parameters["regionX"] = "113";
    maskedStatistics.parameters["regionY"] = "97";
    maskedStatistics.parameters["regionWidth"] = "797";
    maskedStatistics.parameters["regionHeight"] = "701";
    maskedStatistics.parameters["maskPeriod"] = "17";
    maskedStatistics.elementsProcessed = static_cast<std::uint64_t>(kStatisticsRegionWidth) * kStatisticsRegionHeight;
    maskedStatistics.bytesProcessed = maskedStatistics.elementsProcessed * sizeof(float) * 2u;
    maskedStatistics.workUnit = "samples";
    set.push_back(maskedStatistics);

    ScenarioSpec order = smokeScenario("order.median_mad.f32", "statistics", smokeDescriptor(SampleType::Float32, 1),
                                       "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-4);
    order.parameters["channel"] = "0";
    order.parameters["estimators"] = "median,mad";
    order.workUnit = "samples";
    set.push_back(order);

    // The same estimator over 4 and 16 megapixel fields, past the size where an exact selection
    // needs its largest scratch, and over the Eagle and Carina captures. A capture's codes are carried
    // into the unit interval by 65535, the normalization a sixteen-bit frame is read with, in the
    // neutral fixture layer, so every run ranks the same samples; those samples lie on a grid of
    // codes, which a continuous synthetic field never does. Beside them, the complete statistics of
    // each capture: the engine's basic and advanced statistics together.
    for (const bool sixteen : {false, true})
    {
        SampleDescriptor field = smokeDescriptor(SampleType::Float32, 1);
        field.width = 4096u;
        field.height = sixteen ? 4096u : 1024u;
        ScenarioSpec wider =
            smokeScenario(sixteen ? "order.median_mad.16mp.f32" : "order.median_mad.4mp.f32", "statistics", field,
                          "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-4);
        wider.parameters = order.parameters;
        wider.workUnit = "samples";
        set.push_back(wider);
    }
    for (const bool complete : {false, true})
    {
        for (const bool wide : {false, true})
        {
            SampleDescriptor frame = smokeDescriptor(SampleType::UInt16, 1);
            frame.width = wide ? 8288u : 3008u;
            frame.height = wide ? 5644u : 3004u;
            const std::string capture = wide ? "carina" : "eagle";
            ScenarioSpec captured =
                smokeScenario((complete ? "statistics.full." : "order.median_mad.") + capture + ".f32", "statistics",
                              frame, wide ? "carina.asi294mm.red.light" : "eagle.qhy533m.ha.light",
                              CorrectnessClass::EstimatorTolerance, 1e-4);
            captured.inputSource = InputSource::Corpus;
            captured.parameters["channel"] = "0";
            captured.parameters["estimators"] = complete ? "complete" : "median,mad";
            captured.parameters["samples"] = "codes-over-65535";
            captured.bytesProcessed = captured.elementsProcessed * sizeof(float);
            captured.workUnit = "samples";
            set.push_back(captured);
        }
    }

    // Robust estimators over the same masked region the basic-statistics row uses, so the two
    // rows differ in what they estimate rather than in what they select. The declared work is one
    // read of the selection; how many reads an estimator actually needs is reported per run as a
    // counter, because that number is a property of the implementation being measured.
    for (const bool compacted : {false, true})
    {
        ScenarioSpec robust =
            smokeScenario(compacted ? "statistics.robust_masked_roi_compact.f32" : "statistics.robust_masked_roi.f32",
                          "statistics", smokeDescriptor(SampleType::Float32, 1), "synthetic.gaussian_noise",
                          CorrectnessClass::EstimatorTolerance, 1e-4);
        robust.parameters["channel"] = "0";
        robust.parameters["estimators"] = "median,mad";
        robust.parameters["regionX"] = "113";
        robust.parameters["regionY"] = "97";
        robust.parameters["regionWidth"] = "797";
        robust.parameters["regionHeight"] = "701";
        robust.parameters["maskPeriod"] = "17";
        robust.parameters["roiStrategy"] = compacted ? "compact" : "strided";
        robust.elementsProcessed = static_cast<std::uint64_t>(kStatisticsRegionWidth) * kStatisticsRegionHeight;
        robust.bytesProcessed = robust.elementsProcessed * sizeof(float);
        robust.workUnit = "samples";
        if (compacted)
        {
        }
        set.push_back(robust);
    }

    // The clipped estimator under both reading strategies. The masked-region rows above measure the
    // choice where an estimator reads its selection five times; these measure it where the same
    // selection is read once per clipping iteration, which is the axis a compaction is supposed to
    // pay for. Two points on that axis is what makes the choice a measurement.
    for (const bool compacted : {false, true})
    {
        ScenarioSpec clipped = smokeScenario(compacted ? "statistics.sigma_clipped_location_scale_compact.f32"
                                                       : "statistics.sigma_clipped_location_scale.f32",
                                             "statistics", smokeDescriptor(SampleType::Float32, 1),
                                             "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-4);
        clipped.parameters["channel"] = "0";
        clipped.parameters["lowSigma"] = "3";
        clipped.parameters["highSigma"] = "3";
        clipped.parameters["maximumIterations"] = "5";
        clipped.parameters["roiStrategy"] = compacted ? "compact" : "strided";
        clipped.workUnit = "samples";
        set.push_back(clipped);
    }

    // The histogram a screen stretch is read off. Sixteen-bit codes into a bin each maps every code
    // to its own bin, so this row is exact rather than approximate.
    ScenarioSpec fullScale =
        smokeScenario("histogram.full_scale.u16", "histogram", smokeDescriptor(SampleType::UInt16, 1),
                      "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    fullScale.parameters["channel"] = "0";
    fullScale.parameters["bins"] = "65536";
    fullScale.parameters["domain"] = "representation-full-scale";
    fullScale.workUnit = "samples";
    set.push_back(fullScale);

    // The other half of what an autostretch asks for: the same masked region the statistics rows
    // use, counted into a display-sized histogram, accumulated, and read at three percentiles.
    ScenarioSpec percentiles =
        smokeScenario("histogram.masked_roi_percentiles.f32", "histogram", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::BitExact, 0.0);
    percentiles.parameters["channel"] = "0";
    percentiles.parameters["bins"] = "1024";
    percentiles.parameters["domain"] = "stated";
    percentiles.parameters["lowest"] = "0";
    percentiles.parameters["highest"] = "1";
    percentiles.parameters["quantiles"] = "0.001,0.5,0.999";
    percentiles.parameters["regionX"] = "113";
    percentiles.parameters["regionY"] = "97";
    percentiles.parameters["regionWidth"] = "797";
    percentiles.parameters["regionHeight"] = "701";
    percentiles.parameters["maskPeriod"] = "17";
    percentiles.elementsProcessed = static_cast<std::uint64_t>(kStatisticsRegionWidth) * kStatisticsRegionHeight;
    percentiles.bytesProcessed = percentiles.elementsProcessed * sizeof(float);
    percentiles.workUnit = "samples";
    set.push_back(percentiles);

    ScenarioSpec blur =
        smokeScenario("stencil.separable_gaussian.f32", "filter", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::EstimatorTolerance, 1e-4);
    blur.parameters["sigma"] = "1.5";
    blur.parameters["border"] = "reflect-excluding-edge";
    blur.workUnit = "samples";
    set.push_back(blur);

    // The published neighbourhood operations, measured where a caller reaches them rather than at
    // the kernel below them. The exact row is production; the box row is the retained real-time
    // preview entry point. Both are measured against the same independent oracle, so the report
    // shows the approximation's latency saving and accuracy cost without treating it as production.
    ScenarioSpec exactBlur =
        smokeScenario("filter.gaussian_blur.f32", "filter", smokeDescriptor(SampleType::Float32, 1), "synthetic.stars",
                      CorrectnessClass::EstimatorTolerance, 1e-4);
    exactBlur.parameters["sigma"] = "2.5";
    exactBlur.parameters["border"] = "reflect-excluding-edge";
    exactBlur.parameters["method"] = "separable-taps";
    exactBlur.workUnit = "samples";
    set.push_back(exactBlur);

    ScenarioSpec boxBlur = exactBlur;
    boxBlur.id = "filter.gaussian_blur_box.f32";
    boxBlur.parameters["method"] = "repeated-box";
    boxBlur.parameters["purpose"] = "realtime-preview";
    // Three box averages are an approximation of the Gaussian rather than a faster route to it, so
    // the tolerance the engine is held to here is the approximation's own error and not a
    // numerical one. The engine is measured against the Gaussian, which is what makes "how much
    // accuracy does the fast path cost" a number in the report.
    boxBlur.tolerance = 5e-2;
    set.push_back(boxBlur);

    // A narrow blur is not evidence for the scale at which a direct Gaussian becomes expensive.
    // Keep two wide production rows and their explicitly preview-only counterparts in the shared
    // catalogue so the implementation choice is measured at 81 and 241 taps as well.
    for (const std::pair<const char*, const char*> scale : {std::pair{"sigma10", "10"}, std::pair{"sigma30", "30"}})
    {
        ScenarioSpec wideExact = exactBlur;
        wideExact.id = std::string("filter.gaussian_blur_") + scale.first + ".f32";
        wideExact.parameters["sigma"] = scale.second;
        set.push_back(wideExact);

        ScenarioSpec widePreview = boxBlur;
        widePreview.id = std::string("filter.gaussian_blur_box_") + scale.first + ".preview.f32";
        widePreview.parameters["sigma"] = scale.second;
        set.push_back(widePreview);
    }

    ScenarioSpec box = smokeScenario("filter.box_5x5.f32", "filter", smokeDescriptor(SampleType::Float32, 1),
                                     "synthetic.stars", CorrectnessClass::EstimatorTolerance, 1e-6);
    box.parameters["radius"] = "2";
    box.parameters["border"] = "reflect-excluding-edge";
    box.workUnit = "samples";
    set.push_back(box);

    ScenarioSpec dense =
        smokeScenario("filter.dense_convolution.f32", "filter", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::EstimatorTolerance, 1e-4);
    dense.parameters["sigma"] = "2.5";
    dense.parameters["border"] = "reflect-excluding-edge";
    dense.workUnit = "samples";
    set.push_back(dense);

    // The median pair, twice: the automatic network/interior plus truncated-edge path, and the same
    // picture through the general selection, which isolates the path the automatic one avoids.
    for (const std::uint32_t radius : {1u, 2u, 3u, 4u})
    {
        const std::string side = std::to_string(2u * radius + 1u);
        const std::string window = side + "x" + side;
        ScenarioSpec median =
            smokeScenario("filter.median_" + window + ".f32", "filter", smokeDescriptor(SampleType::Float32, 1),
                          "synthetic.stars", CorrectnessClass::AbsoluteRelative, 1e-6);
        median.parameters["radius"] = std::to_string(radius);
        median.parameters["method"] = "automatic";
        median.parameters["probes"] = "full-image-truncated";
        median.workUnit = "samples";
        set.push_back(median);

        ScenarioSpec selection = median;
        selection.id = "filter.median_" + window + "_selection.f32";
        selection.parameters["method"] = "selection";
        set.push_back(selection);
    }

    // The middle rank of a disc-shaped structuring element, which the morphological selection ranks:
    // the cells of the five-by-five square within the disc the product's circular element draws, held
    // to the sorted members of the same truncated element.
    ScenarioSpec footprint =
        smokeScenario("filter.footprint_rank_disc_5x5.f32", "filter", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::AbsoluteRelative, 1e-6);
    footprint.parameters["radius"] = "2";
    footprint.parameters["element"] = "disc";
    footprint.parameters["quantile"] = "0.5";
    footprint.parameters["probes"] = "full-image-truncated";
    footprint.workUnit = "samples";
    set.push_back(footprint);

    // The multiscale round trip: decompose into layers and add them back. Its oracle is the input
    // itself, so a difference is a transform that lost or invented energy rather than a tolerance
    // argument, and the engine is measured against that definition.
    ScenarioSpec roundTrip =
        smokeScenario("multiscale.starlet_roundtrip.f32", "multiscale", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::EstimatorTolerance, 1e-5);
    roundTrip.parameters["algorithm"] = "starlet";
    roundTrip.parameters["scaling"] = "bspline5";
    roundTrip.parameters["layers"] = "5";
    roundTrip.parameters["reconstruct"] = "unit-weights";
    roundTrip.workUnit = "samples";
    set.push_back(roundTrip);

    ScenarioSpec multiscaleLinear = roundTrip;
    multiscaleLinear.id = "multiscale.mlt_decompose.f32";
    multiscaleLinear.parameters["algorithm"] = "multiscale-linear";
    multiscaleLinear.parameters["layers"] = "4";
    multiscaleLinear.parameters.erase("reconstruct");
    set.push_back(multiscaleLinear);

    // The bilateral filter, whose arithmetic the engine owns rather than an imaging library. It is
    // measured against the harness's own definition of the weight, so the report says what the
    // engine's arithmetic costs and how far it is from the definition.
    ScenarioSpec bilateral =
        smokeScenario("nr.bilateral.f32", "noise-reduction", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 5e-3);
    bilateral.parameters["spatialSigma"] = "2";
    bilateral.parameters["rangeSigma"] = "0.05";
    bilateral.parameters["radius"] = "3";
    bilateral.parameters["border"] = "reflect-excluding-edge";
    bilateral.workUnit = "samples";
    set.push_back(bilateral);

    // The two total-variation methods, run natively rather than through an interpreter, so the rows
    // measure the method alone.
    ScenarioSpec chambolle =
        smokeScenario("nr.total_variation_chambolle.f32", "noise-reduction", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-5);
    chambolle.parameters["method"] = "chambolle";
    chambolle.parameters["weight"] = "0.1";
    chambolle.parameters["iterations"] = "30";
    chambolle.workUnit = "samples";
    set.push_back(chambolle);

    ScenarioSpec bregman = chambolle;
    bregman.id = "nr.total_variation_bregman.f32";
    bregman.parameters["method"] = "split-bregman";
    bregman.parameters["weight"] = "10";
    // The split reaches the same minimum by a different route, so its correctness is the functional
    // rather than the other method's intermediate states.
    bregman.tolerance = 2e-2;
    set.push_back(bregman);

    // The noise generator, whose correctness is a property of a distribution rather than of a
    // stream: two generators that draw different numbers from the same distribution are both right.
    ScenarioSpec noise =
        smokeScenario("synthesis.noise_gaussian.f32", "synthesis", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gradient", CorrectnessClass::EstimatorTolerance, 2e-2);
    noise.parameters["kind"] = "gaussian";
    noise.parameters["variance"] = "0.0025";
    noise.parameters["seed"] = "20260920";
    noise.workUnit = "samples";
    set.push_back(noise);

    ScenarioSpec chromatic =
        smokeScenario("nr.scnr_average_neutral.f32", "noise-reduction", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.stars", CorrectnessClass::EstimatorTolerance, 1e-5);
    chromatic.parameters["method"] = "average-neutral";
    chromatic.parameters["amount"] = "1";
    chromatic.parameters["whiteLevel"] = "1";
    chromatic.parameters["preserveLightness"] = "false";
    chromatic.workUnit = "samples";
    set.push_back(chromatic);

    ScenarioSpec banding =
        smokeScenario("nr.banding_horizontal.f32", "noise-reduction", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gradient", CorrectnessClass::EstimatorTolerance, 1e-5);
    banding.parameters["axis"] = "horizontal";
    banding.parameters["amount"] = "1";
    banding.parameters["protectHighlights"] = "false";
    banding.workUnit = "samples";
    set.push_back(banding);

    // The impulse rejection, over a truncated window, so the measurement is about which samples were
    // replaced rather than about what happens at a border.
    ScenarioSpec impulse =
        smokeScenario("nr.impulse_rejection.f32", "noise-reduction", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-5);
    impulse.parameters["radius"] = "1";
    impulse.parameters["highThreshold"] = "0.2";
    impulse.parameters["border"] = "truncated";
    impulse.workUnit = "samples";
    set.push_back(impulse);

    // The cosmetic pair: derive thresholds and find the defects, then repair the defects a stated
    // map names. The harness plants the defects and states the answer, so the engine's own
    // detection never decides what it is measured against; the repair row's marked set comes from
    // the harness too, which is what lets its outputs compare bit for bit.
    ScenarioSpec cosmeticDetect =
        smokeScenario("cosmetic.detect_map.f32", "cosmetic", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 2e-2);
    cosmeticDetect.parameters["coldSigma"] = "3";
    cosmeticDetect.parameters["hotSigma"] = "3";
    cosmeticDetect.parameters["implants"] = "lattice48-extremes";
    cosmeticDetect.workUnit = "samples";
    set.push_back(cosmeticDetect);

    ScenarioSpec cosmeticCorrect =
        smokeScenario("cosmetic.correct_map.f32", "cosmetic", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::BitExact, 0.0);
    cosmeticCorrect.parameters["hotRadius"] = "1";
    cosmeticCorrect.parameters["coldRadius"] = "2";
    cosmeticCorrect.parameters["rank"] = "linear-middle-mean";
    cosmeticCorrect.parameters["implants"] = "lattice48-extremes";
    cosmeticCorrect.workUnit = "samples";
    set.push_back(cosmeticCorrect);

    // The complete arithmetic path in one row: the same deterministic bias, dark and flat are
    // prepared outside the measurement, then each timed call writes one output.
    ScenarioSpec calibration =
        smokeScenario("calibration.bias_dark_flat.f32", "calibration", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    calibration.parameters["bias"] = "binary-pattern-v1";
    calibration.parameters["dark"] = "binary-pattern-v1";
    calibration.parameters["flat"] = "binary-pattern-v1";
    calibration.parameters["darkScale"] = "0.875";
    calibration.parameters["flatNormalization"] = "none";
    calibration.parameters["pedestal"] = "0";
    calibration.workUnit = "samples";
    set.push_back(calibration);

    // The same calibration over two captured lights of 9 and 47 megapixels, whose five planes do not fit
    // in a processor's cache as the synthetic row's do. The runner calibrates the frame's codes carried
    // into the unit interval by a power of two, which is exact, against the synthetic row's masters.
    for (const bool wide : {false, true})
    {
        SampleDescriptor frame = smokeDescriptor(SampleType::UInt16, 1);
        frame.width = wide ? 8288u : 3008u;
        frame.height = wide ? 5644u : 3004u;
        ScenarioSpec captured = smokeScenario(
            wide ? "calibration.bias_dark_flat.carina.f32" : "calibration.bias_dark_flat.eagle.f32", "calibration",
            frame, wide ? "carina.asi294mm.red.light" : "eagle.qhy533m.ha.light", CorrectnessClass::BitExact, 0.0);
        captured.inputSource = InputSource::Corpus;
        captured.parameters = calibration.parameters;
        captured.parameters["light"] = "codes-over-65536";
        captured.bytesProcessed = captured.elementsProcessed * sizeof(float);
        captured.workUnit = "samples";
        set.push_back(captured);
    }

    // Global normalization as one frame is brought to a measured reference. The target is an exact
    // affine image of the reference inside an exact-zero border, so the answer is the reference
    // itself and the run is scored against it; the border is what the zero-is-no-data rule exists
    // for. The runner copies the target and writes it in place, so the clocked region has one shape
    // in every run.
    for (const bool real : {false, true})
    {
        SampleDescriptor frameDescriptor = smokeDescriptor(real ? SampleType::UInt16 : SampleType::Float32, 1);
        if (real)
        {
            frameDescriptor.width = 3008u;
            frameDescriptor.height = 3004u;
        }
        ScenarioSpec normalizeFrame = smokeScenario(
            real ? "normalization.global_frame.real.f32" : "normalization.global_frame.f32", "normalization",
            frameDescriptor, real ? "eagle.qhy533m.ha.light" : "synthetic.gaussian_noise",
            CorrectnessClass::EstimatorTolerance, 1e-3);
        if (real)
        {
            normalizeFrame.inputSource = InputSource::Corpus;
            normalizeFrame.parameters["conversion"] = "u16-to-f32";
        }
        normalizeFrame.parameters["method"] = "AdditiveScaling";
        normalizeFrame.parameters["estimator"] = "IterativeKSigma";
        normalizeFrame.parameters["validity"] = "zero-is-no-data";
        normalizeFrame.parameters["target"] = "affine-border-v1";
        normalizeFrame.bytesProcessed = normalizeFrame.elementsProcessed * sizeof(float) * 2u;
        normalizeFrame.workUnit = "samples";
        set.push_back(normalizeFrame);
    }

    // The apply half alone, with fixed coefficients, so the write path is measured bit for bit
    // apart from the estimators.
    ScenarioSpec normalizeApply =
        smokeScenario("normalization.global_apply.f32", "normalization", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::BitExact, 0.0);
    normalizeApply.parameters["method"] = "AdditiveScaling";
    normalizeApply.parameters["scale"] = "1.03125";
    normalizeApply.parameters["offset"] = "-0.0078125";
    normalizeApply.parameters["validity"] = "zero-is-no-data";
    normalizeApply.bytesProcessed = normalizeApply.elementsProcessed * sizeof(float) * 2u;
    normalizeApply.workUnit = "samples";
    set.push_back(normalizeApply);

    // Local normalization as one frame's background surface is replaced by a fitted reference's.
    // The target adds a plane to the scaled reference inside the same exact-zero border, which no
    // global match removes, so the answer is again the reference and the run is scored against
    // it to within what a node grid follows of a plane. The runner fits the reference once outside
    // the clocked region and fit, scale and apply the target inside it.
    for (const bool real : {false, true})
    {
        SampleDescriptor frameDescriptor = smokeDescriptor(real ? SampleType::UInt16 : SampleType::Float32, 1);
        if (real)
        {
            frameDescriptor.width = 3008u;
            frameDescriptor.height = 3004u;
        }
        ScenarioSpec localFrame = smokeScenario(
            real ? "normalization.local_frame.real.f32" : "normalization.local_frame.f32", "normalization",
            frameDescriptor, real ? "eagle.qhy533m.ha.light" : "synthetic.gaussian_noise",
            CorrectnessClass::EstimatorTolerance, 1e-2);
        if (real)
        {
            localFrame.inputSource = InputSource::Corpus;
            localFrame.parameters["conversion"] = "u16-to-f32";
        }
        localFrame.parameters["grid"] = "8";
        localFrame.parameters["interpolation"] = "Bicubic";
        localFrame.parameters["estimator"] = "Percentile";
        localFrame.parameters["scale"] = "ResidualRatio";
        localFrame.parameters["validity"] = "zero-is-no-data";
        localFrame.parameters["target"] = "gradient-border-v1";
        localFrame.bytesProcessed = localFrame.elementsProcessed * sizeof(float) * 2u;
        localFrame.workUnit = "samples";
        set.push_back(localFrame);
    }

    // The local apply alone, through fixed plane grids, so the interpolating write path is measured
    // apart from the estimators against a closed-form answer.
    ScenarioSpec localApply =
        smokeScenario("normalization.local_apply.f32", "normalization", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 1e-6);
    localApply.parameters["grid"] = "8";
    localApply.parameters["interpolation"] = "Bicubic";
    localApply.parameters["scale"] = "1.03125";
    localApply.parameters["grids"] = "plane-v1";
    localApply.parameters["validity"] = "zero-is-no-data";
    localApply.bytesProcessed = localApply.elementsProcessed * sizeof(float) * 2u;
    localApply.workUnit = "samples";
    set.push_back(localApply);

    // Frame-level weighting consumes a compact measurement table rather than image pixels. A
    // one-row Float32 fixture supplies deterministic entropy for those measurements, so the normal
    // fixture digest still proves that every run saw the same input table.
    SampleDescriptor weightingInput = smokeDescriptor(SampleType::Float32, 1);
    weightingInput.width = 4096u;
    weightingInput.height = 1u;
    ScenarioSpec weighting = smokeScenario("stacking.photometric_weighting.frames", "stacking", weightingInput,
                                           "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    weighting.parameters["model"] = "zero-point-confidence-alignment-v1";
    weighting.parameters["uncertaintyPenaltyPerMagnitude"] = "5";
    weighting.parameters["alignmentPenalty"] = "4";
    weighting.parameters["minimumRawWeight"] = "0.01";
    weighting.parameters["fixtureMapping"] = "photometric-frame-v1";
    weighting.parameters[kTimedBatchParameter] = "4096";
    weighting.workUnit = "frames";
    set.push_back(weighting);

    // The stacks: eight frames of the smoke fixture combined in memory through the executor under
    // every combination, rejection family, weighting, the global normalization pass and a caller
    // row cap. The rows are declared from one header, so each paired row measures the same
    // complete stack on both sides against the same oracle.
    for (ScenarioSpec& stacking : stackingModeScenarios())
    {
        set.push_back(std::move(stacking));
    }

    // Aperture photometry plants its own three-plane field from the scenario seed and measures every
    // planted star with fixed radii, so the fixture digest is taken over the planted samples and the
    // row is scored against the analytic flux inside the aperture rather than the engine's own answer.
    SampleDescriptor apertureInput = smokeDescriptor(SampleType::Float32, 3);
    apertureInput.width = kAperturePhotometryFixtureWidth;
    apertureInput.height = kAperturePhotometryFixtureHeight;
    ScenarioSpec aperture = smokeScenario("photometry.aperture.rgb.f32", "photometry", apertureInput,
                                          "synthetic.uniform_noise", CorrectnessClass::EstimatorTolerance, 0.03);
    aperture.parameters["fixtureMapping"] = kAperturePhotometryFixtureMapping;
    aperture.parameters["apertureRadius"] = "4";
    aperture.parameters["annulus"] = "8-14";
    aperture.parameters["neighbors"] = "none";
    aperture.parameters[kTimedBatchParameter] = "4";
    aperture.elementsProcessed = std::uint64_t{kAperturePhotometryFixtureWidth / kAperturePhotometryFixtureCell - 2u} *
                                 (kAperturePhotometryFixtureHeight / kAperturePhotometryFixtureCell - 2u);
    aperture.workUnit = "stars";
    set.push_back(aperture);

    // The colour-gain fit consumes a compact sample table rather than pixels. A one-row Float32
    // fixture supplies deterministic entropy for the samples, and the planted channel factors are
    // what the recovered gains are held to.
    SampleDescriptor colorFitInput = smokeDescriptor(SampleType::Float32, 1);
    colorFitInput.width = 4096u;
    colorFitInput.height = 1u;
    ScenarioSpec colorFit = smokeScenario("color.photometric.fit.samples", "color", colorFitInput,
                                          "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    colorFit.parameters["fixtureMapping"] = kColorFitFixtureMapping;
    colorFit.parameters["model"] = "pcc-gaia-bp-rp-v1";
    colorFit.parameters["minimumSamples"] = "10";
    colorFit.parameters[kTimedBatchParameter] = "256";
    colorFit.workUnit = "samples";
    set.push_back(colorFit);

    // Synthetic photometry integrates a table of linear spectra through a constant response. The
    // one-row Float32 fixture is the entropy the spectra are derived from, and every count and
    // uncertainty has an analytic answer the row is scored against.
    SampleDescriptor syntheticInput = smokeDescriptor(SampleType::Float32, 1);
    syntheticInput.width = 4096u;
    syntheticInput.height = 1u;
    ScenarioSpec synthetic = smokeScenario("spectrophotometry.synthetic.rgb.f32", "spectrophotometry", syntheticInput,
                                           "synthetic.uniform_noise", CorrectnessClass::EstimatorTolerance, 1.0e-6);
    synthetic.parameters["fixtureMapping"] = kSyntheticPhotometryFixtureMapping;
    synthetic.parameters["recipe"] = "spcc-synthetic-photometry-v2";
    synthetic.parameters["response"] = "constant-2-1-0.5";
    synthetic.parameters["spectra"] = std::to_string(kSyntheticPhotometryFixtureSpectra);
    synthetic.parameters[kTimedBatchParameter] = "644";
    synthetic.elementsProcessed = kSyntheticPhotometryFixtureSpectra;
    synthetic.workUnit = "spectra";
    set.push_back(synthetic);

    // The spectrophotometric fit consumes colour samples whose catalog ratios span a stated range
    // and whose image ratios are the catalog's times two planted scales, with a deterministic
    // minority of gross outliers the repeated-median line is expected to clip.
    SampleDescriptor spccFitInput = smokeDescriptor(SampleType::Float32, 1);
    spccFitInput.width = 4096u;
    spccFitInput.height = 1u;
    ScenarioSpec spccFit = smokeScenario("color.spectrophotometric.fit.samples", "color", spccFitInput,
                                         "synthetic.uniform_noise", CorrectnessClass::EstimatorTolerance, 1.0e-9);
    spccFit.parameters["fixtureMapping"] = kSpectrophotometricFitFixtureMapping;
    spccFit.parameters["policy"] = "iterative-sigma-clip-refit";
    spccFit.parameters["minimumSamples"] = "35";
    spccFit.parameters[kTimedBatchParameter] = "113";
    spccFit.workUnit = "samples";
    set.push_back(spccFit);

    // The apply writes stated gains to the planted three-plane field with no sky neutralization,
    // so the row times the fused gain-and-offset pass alone; it is scored by how close each
    // channel's sky median lands to the planted sky times its gain.
    SampleDescriptor applyInput = smokeDescriptor(SampleType::Float32, 3);
    applyInput.width = kAperturePhotometryFixtureWidth;
    applyInput.height = kAperturePhotometryFixtureHeight;
    ScenarioSpec spccApply = smokeScenario("color.spectrophotometric.apply.rgb.f32", "color", applyInput,
                                           "synthetic.uniform_noise", CorrectnessClass::EstimatorTolerance, 1.0e-3);
    spccApply.parameters["fixtureMapping"] = kSpectrophotometricApplyFixtureMapping;
    spccApply.parameters["gains"] = "0.64-0.8-1";
    spccApply.parameters["background"] = "none";
    spccApply.parameters[kTimedBatchParameter] = "1106";
    spccApply.elementsProcessed =
        std::uint64_t{kAperturePhotometryFixtureWidth} * kAperturePhotometryFixtureHeight * 3u;
    spccApply.workUnit = "samples";
    set.push_back(spccApply);

    for (ScenarioSpec& narrowband : narrowbandCompositionScenarios())
    {
        set.push_back(std::move(narrowband));
    }

    // A catalog is a text file and a table of names and directions rather than pixels. The one-row
    // Float32 fixture is only the deterministic entropy the shared mapping turns into that text, so
    // the recorded fixture digest still proves every run read the same catalog.
    SampleDescriptor catalogInput = smokeDescriptor(SampleType::Float32, 1);
    catalogInput.width = 16384u;
    catalogInput.height = 1u;
    ScenarioSpec catalogParse = smokeScenario("catalog.parse.star_csv", "catalog", catalogInput,
                                              "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    catalogParse.parameters["schema"] = "bright-star-v1";
    catalogParse.parameters["entries"] = "16384";
    catalogParse.parameters["fixtureMapping"] = "catalog-text-v1";
    catalogParse.parameters[kTimedBatchParameter] = "16";
    catalogParse.workUnit = "entries";
    set.push_back(catalogParse);

    // The lookup a plate solve and an annotation both make: sixty-four cones over the whole sphere
    // against one already-built catalog, so the row measures the search and not the load.
    ScenarioSpec catalogQuery = smokeScenario("catalog.query.region.star", "catalog", catalogInput,
                                              "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    catalogQuery.parameters["schema"] = "bright-star-v1";
    catalogQuery.parameters["entries"] = "16384";
    catalogQuery.parameters["cones"] = "64";
    catalogQuery.parameters["radiusDegrees"] = "10";
    catalogQuery.parameters["magnitudeLimit"] = "20";
    catalogQuery.parameters["fixtureMapping"] = "catalog-text-v1";
    catalogQuery.parameters[kTimedBatchParameter] = "238";
    catalogQuery.workUnit = "cones";
    set.push_back(catalogQuery);

    // The distortion map a registration or drizzle pass needs before it can resample a solved
    // frame: one inverse correction for every pixel of the destination grid. The model carries no
    // written inverse, so the engine solves the forward polynomial by Newton iteration at every
    // pixel, with the first pixel centre at one half.
    SampleDescriptor sipMapInput = smokeDescriptor(SampleType::Float32, 1);
    sipMapInput.width = kSipFixtureWidth;
    sipMapInput.height = kSipFixtureHeight;
    ScenarioSpec sipMap = smokeScenario("astrometry.sip.inverse_map.f32", "astrometry", sipMapInput,
                                        "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    sipMap.parameters["order"] = "2";
    sipMap.parameters["inverseModel"] = "absent";
    sipMap.parameters["seed"] = "authoritative";
    sipMap.parameters["fixtureMapping"] = kSipFixtureMapping;
    sipMap.parameters[kTimedBatchParameter] = "63";
    sipMap.workUnit = "pixels";
    set.push_back(sipMap);

    // One exact-hint solve over a catalog and image field built from engine-neutral points.
    // Catalog construction is outside timing; the row measures query, matching, robust fit,
    // recentering, and WCS production together, which is the unit a caller actually pays for.
    SampleDescriptor plateInput = smokeDescriptor(SampleType::Float32, 1);
    plateInput.width = 42u;
    plateInput.height = 1u;
    ScenarioSpec plateSolve = smokeScenario("astrometry.plate_solve.hinted.points", "astrometry", plateInput,
                                            "synthetic.uniform_noise", CorrectnessClass::EstimatorTolerance, 3.5);
    plateSolve.parameters["fixture"] = "linear-hinted-v1";
    plateSolve.parameters["stars"] = "42";
    plateSolve.parameters["catalog"] = "prebuilt";
    plateSolve.parameters["search"] = "exact-hint";
    plateSolve.parameters["fixtureMapping"] = "plate-solve-v1";
    plateSolve.parameters[kTimedBatchParameter] = "16";
    plateSolve.bytesProcessed = 42u * 4u * sizeof(double);
    plateSolve.workUnit = "stars";
    set.push_back(plateSolve);

    // The coordinate map a warp of a registered frame consumes: the source position of every pixel
    // of the canvas. The transform carries a second-order polynomial, so no matrix inverts it and
    // every pixel is its own Newton solve of the composed model - which is what registration
    // planning actually costs on a plate whose field is curved. The engine puts the first pixel
    // centre at one half.
    SampleDescriptor registrationMapInput = smokeDescriptor(SampleType::Float32, 1);
    registrationMapInput.width = kRegistrationFixtureWidth;
    registrationMapInput.height = kRegistrationFixtureHeight;
    ScenarioSpec registrationMap =
        smokeScenario("registration.transform.destination_map.f32", "registration", registrationMapInput,
                      "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    registrationMap.parameters["model"] = "affine";
    registrationMap.parameters["polynomialOrder"] = "2";
    registrationMap.parameters["seed"] = "authoritative";
    registrationMap.parameters["fixtureMapping"] = kRegistrationFixtureMapping;
    registrationMap.parameters[kTimedBatchParameter] = "12";
    registrationMap.workUnit = "pixels";
    set.push_back(registrationMap);

    // Blind moving-source detection is the image-sized hot path of comet tracking. The row sees seven
    // already star-aligned planes: three static stars, one per-frame impulse and one coherent moving
    // Gaussian, and its answer is compared with the planted truth in half-pixel coordinates.
    SampleDescriptor cometInput = smokeDescriptor(SampleType::Float32, 1);
    cometInput.width = kCometDetectionFixtureWidth;
    cometInput.height = kCometDetectionFixtureHeight;
    ScenarioSpec cometDetection = smokeScenario("registration.comet.detect.frames.f32", "registration", cometInput,
                                                "synthetic.uniform_noise", CorrectnessClass::EstimatorTolerance, 0.75);
    cometDetection.parameters["frames"] = std::to_string(kCometDetectionFixtureFrameCount);
    cometDetection.parameters["fixtureMapping"] = kCometDetectionFixtureMapping;
    cometDetection.parameters["background"] = "temporal-median";
    cometDetection.parameters["track"] = "linear";
    cometDetection.parameters["minimumPositiveResidual"] = "0.06";
    cometDetection.parameters["detectionSigma"] = "4";
    cometDetection.parameters["minimumIntegratedSignalToNoise"] = "5";
    cometDetection.parameters["componentArea"] = "3-48";
    cometDetection.parameters["minimumDetections"] = "5";
    cometDetection.parameters["maximumTrackResidualPixels"] = "1.5";
    cometDetection.parameters["minimumMotionPixels"] = "15";
    cometDetection.parameters["maximumCandidates"] = "8";
    cometDetection.parameters[kTimedBatchParameter] = "1";
    cometDetection.elementsProcessed =
        std::uint64_t{kCometDetectionFixtureWidth} * kCometDetectionFixtureHeight * kCometDetectionFixtureFrameCount;
    cometDetection.bytesProcessed = cometDetection.elementsProcessed * sizeof(float);
    cometDetection.workUnit = "samples";
    set.push_back(cometDetection);

    // The tile resolution the shipping Gaia provider asks before it reads anything: whole-sphere
    // cones at order five, in standard RING numbering and with the exact maximum-pixel radius.
    SampleDescriptor healpixInput = smokeDescriptor(SampleType::Float32, 1);
    healpixInput.width = static_cast<std::uint32_t>(kCatalogFixtureQueryCount);
    healpixInput.height = 1u;
    ScenarioSpec healpixQuery = smokeScenario("catalog.healpix.query_disc.ring", "catalog", healpixInput,
                                              "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    healpixQuery.parameters["order"] = std::to_string(kHealpixFixtureOrder);
    healpixQuery.parameters["cones"] = std::to_string(kCatalogFixtureQueryCount);
    healpixQuery.parameters["radiusDegrees"] = std::to_string(kHealpixFixtureQueryRadiusDegrees);
    healpixQuery.parameters["coverage"] = "conservative-overlap";
    healpixQuery.parameters["fixtureMapping"] = "healpix-cones-v1";
    healpixQuery.parameters[kTimedBatchParameter] = "1998";
    healpixQuery.workUnit = "cones";
    set.push_back(healpixQuery);

    // The deep-sky table, whose load also decides which of the rows that name one object a query
    // answers with. That decision is what this row measures: the engine reaches the candidates
    // through the name they share rather than comparing every row with every other.
    SampleDescriptor deepSkyInput = smokeDescriptor(SampleType::Float32, 1);
    deepSkyInput.width = static_cast<std::uint32_t>(kDeepSkyFixtureEntryCount);
    deepSkyInput.height = 1u;
    ScenarioSpec deepSkyBuild = smokeScenario("catalog.build.deep_sky", "catalog", deepSkyInput,
                                              "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    deepSkyBuild.parameters["schema"] = "deep-sky-v1";
    deepSkyBuild.parameters["entries"] = "13000";
    deepSkyBuild.parameters["aliases"] = "110";
    deepSkyBuild.parameters["fixtureMapping"] = "catalog-deep-sky-v1";
    deepSkyBuild.parameters[kTimedBatchParameter] = "12";
    deepSkyBuild.workUnit = "entries";
    set.push_back(deepSkyBuild);

    // Star detection: the entry point every alignment, plate-solving, quality and colour path
    // starts from. Its quality cannot be read from a comparison of two implementations - they
    // disagree in the last digit of every fitted parameter and a handful of marginal stars fall
    // either side of a threshold - so the synthetic row plants its stars first and scores the
    // engine against the list that produced the pixels. The two real captures have no truth and
    // are scored on the median relative residual of the fits the engine accepted, which is a
    // property of the engine's own answer and is dimensionless, so it is comparable.
    //
    // The smoothing the candidate scan runs against is the engine's own decision, which is why
    // the parameter names the decision rather than a method: the planted row is what says whether
    // that decision changed which stars were found.
    {
        SampleDescriptor planted = smokeDescriptor(SampleType::Float32, 1);
        planted.width = 1024u;
        planted.height = 1024u;
        ScenarioSpec detect = smokeScenario("stars.detect.planted.f32", "stars", planted, "synthetic.uniform_noise",
                                            CorrectnessClass::EstimatorTolerance, 0.05);
        detect.parameters["field"] = "planted-star-v1";
        detect.parameters["cell"] = "48";
        detect.parameters["profile"] = "Gaussian";
        detect.parameters["radius"] = "5";
        detect.parameters["sigma"] = "1";
        detect.parameters["roundness"] = "0.5";
        detect.parameters["smoothing"] = "engine-default";
        detect.workUnit = "samples";
        set.push_back(detect);
    }
    for (const bool wide : {false, true})
    {
        SampleDescriptor frame = smokeDescriptor(SampleType::UInt16, 1);
        frame.width = wide ? 8288u : 3008u;
        frame.height = wide ? 5644u : 3004u;
        ScenarioSpec detect = smokeScenario(wide ? "stars.detect.carina.f32" : "stars.detect.eagle.f32", "stars", frame,
                                            wide ? "carina.asi294mm.red.light" : "eagle.qhy533m.ha.light",
                                            CorrectnessClass::EstimatorTolerance, 0.2);
        detect.inputSource = InputSource::Corpus;
        detect.parameters["conversion"] = "u16-to-f32";
        detect.parameters["profile"] = "Gaussian";
        detect.parameters["radius"] = "5";
        detect.parameters["sigma"] = "1";
        detect.parameters["roundness"] = "0.5";
        detect.parameters["smoothing"] = "engine-default";
        detect.workUnit = "samples";
        set.push_back(detect);
    }

    // The star reduction over two fixture-built unit-domain planes. The independent oracle is the
    // equation, not the engine's output.
    ScenarioSpec starReduction =
        smokeScenario("stars.reduce.strong.f32", "stars", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.uniform_noise", CorrectnessClass::AbsoluteRelative, 1e-6);
    starReduction.parameters["fixture"] = "target-starless-v1";
    starReduction.parameters["method"] = "Star";
    starReduction.parameters["strength"] = "Strong";
    starReduction.parameters["iterations"] = "2";
    starReduction.bytesProcessed = starReduction.elementsProcessed * sizeof(float) * 3u;
    starReduction.workUnit = "samples";
    set.push_back(starReduction);

    // Repeated registration normally reuses its target catalogue. This row therefore prepares the
    // reference outside timing and measures source triangle generation, matching and model fitting.
    SampleDescriptor alignmentInput = smokeDescriptor(SampleType::Float32, 1);
    alignmentInput.width = 40u;
    alignmentInput.height = 1u;
    ScenarioSpec alignment =
        smokeScenario("registration.star_alignment.prepared.points", "registration", alignmentInput,
                      "synthetic.uniform_noise", CorrectnessClass::GeometricResidual, 0.05);
    alignment.parameters["fixture"] = "similarity-outliers-v1";
    alignment.parameters["points"] = "40";
    alignment.parameters["referenceOutliers"] = "3";
    alignment.parameters["model"] = "Similarity";
    alignment.parameters["reference"] = "prepared";
    alignment.bytesProcessed = (40u * 2u + 43u * 2u) * sizeof(double);
    alignment.workUnit = "stars";
    set.push_back(alignment);

    // The patch denoiser, scored by how much of the harness's own noise estimate survives it. Both
    // engines measure the frame themselves and neither measurement decides the row.
    ScenarioSpec patchDenoise =
        smokeScenario("nr.patch_denoise.f32", "noise-reduction", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gaussian_noise", CorrectnessClass::EstimatorTolerance, 0.95);
    patchDenoise.parameters["sigma"] = "measured";
    patchDenoise.parameters["whiteLevel"] = "1";
    patchDenoise.parameters["iterations"] = "1";
    patchDenoise.parameters["finishing"] = "none";
    patchDenoise.workUnit = "samples";
    set.push_back(patchDenoise);

    // A real public-domain frame exercises the guided finishing stage. Keeping this separate from
    // the synthetic estimator row makes both
    // the input provenance and the extra stage visible in every result record.
    SampleDescriptor guidedDescriptor = smokeDescriptor(SampleType::Float32, 1);
    guidedDescriptor.width = 512u;
    guidedDescriptor.height = 512u;
    ScenarioSpec guidedDenoise = smokeScenario("nr.patch_denoise_guided.real.f32", "noise-reduction", guidedDescriptor,
                                               "public.uit.scaled.f32", CorrectnessClass::EstimatorTolerance, 0.95);
    guidedDenoise.inputSource = InputSource::Corpus;
    guidedDenoise.parameters["sigma"] = "measured";
    guidedDenoise.parameters["whiteLevel"] = "1";
    guidedDenoise.parameters["iterations"] = "1";
    guidedDenoise.parameters["finishing"] = "guided-dual-domain";
    guidedDenoise.workUnit = "samples";
    set.push_back(std::move(guidedDenoise));

    // The same operation over two captures, whole and as the 512 x 512 crop at each one's centre. A
    // patch denoise's cost depends on how its frame divides into tiles, so the public frame above says
    // nothing about a capture of another size. `prepare_patch_denoise_inputs.py` derives all four from
    // the verified raw dumps.
    struct GuidedCapture
    {
        const char* id;
        const char* corpusId;
        std::uint32_t width;
        std::uint32_t height;
    };
    for (const GuidedCapture& capture : std::array<GuidedCapture, 4u>{
             {{"nr.patch_denoise_guided.eagle.crop512.f32", "patch_denoise.eagle.crop512.unit_f32.v1", 512u, 512u},
              {"nr.patch_denoise_guided.carina.crop512.f32", "patch_denoise.carina.crop512.unit_f32.v1", 512u, 512u},
              {"nr.patch_denoise_guided.eagle.full.f32", "patch_denoise.eagle.full.unit_f32.v1", 3008u, 3004u},
              {"nr.patch_denoise_guided.carina.full.f32", "patch_denoise.carina.full.unit_f32.v1", 8288u, 5644u}}})
    {
        SampleDescriptor captureDescriptor = smokeDescriptor(SampleType::Float32, 1);
        captureDescriptor.width = capture.width;
        captureDescriptor.height = capture.height;
        ScenarioSpec captureDenoise = smokeScenario(capture.id, "noise-reduction", captureDescriptor, capture.corpusId,
                                                    CorrectnessClass::EstimatorTolerance, 0.95);
        captureDenoise.inputSource = InputSource::Corpus;
        captureDenoise.parameters["sigma"] = "measured";
        captureDenoise.parameters["whiteLevel"] = "1";
        captureDenoise.parameters["iterations"] = "1";
        captureDenoise.parameters["finishing"] = "guided-dual-domain";
        captureDenoise.workUnit = "samples";
        set.push_back(std::move(captureDenoise));
    }

    // The two deconvolutions, each against a frame the harness blurred by a kernel the harness
    // built, so the unblurred frame is the exact answer and an engine that returns its input scores
    // exactly one.
    for (const auto& [id, method] : std::array<std::pair<const char*, const char*>, 2u>{
             {{"deconvolution.richardson_lucy.f32", "richardson-lucy"}, {"deconvolution.wiener.f32", "wiener"}}})
    {
        ScenarioSpec deconvolve = smokeScenario(id, "restoration", smokeDescriptor(SampleType::Float32, 1),
                                                "synthetic.stars", CorrectnessClass::EstimatorTolerance, 0.95);
        deconvolve.parameters["method"] = method;
        deconvolve.parameters["psf"] = "gaussian";
        deconvolve.parameters["fwhm"] = "3";
        deconvolve.parameters["iterations"] = "10";
        deconvolve.parameters["regularization"] = "0.001";
        deconvolve.parameters["taper"] = "off";
        deconvolve.workUnit = "samples";
        set.push_back(std::move(deconvolve));
    }

    // The two background removals, over a frame the harness added a stated gradient to. Both are
    // scored on how much of that gradient's large-scale variation survives, measured as a spread of
    // tile medians so the stars a background model has to ignore do not decide the number.
    ScenarioSpec backgroundModel =
        smokeScenario("background.model_subtract.f32", "background", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::EstimatorTolerance, 0.35);
    backgroundModel.parameters["model"] = "polynomial";
    backgroundModel.parameters["degree"] = "2";
    backgroundModel.parameters["samplesPerLine"] = "20";
    backgroundModel.parameters["tolerance"] = "1";
    backgroundModel.parameters["correction"] = "subtract";
    backgroundModel.workUnit = "samples";
    set.push_back(backgroundModel);

    ScenarioSpec autoGradient =
        smokeScenario("background.auto_gradient.f32", "background", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::EstimatorTolerance, 0.35);
    autoGradient.parameters["model"] = "polynomial";
    autoGradient.parameters["degree"] = "2";
    autoGradient.parameters["downsample"] = "4";
    autoGradient.parameters["protection"] = "off";
    autoGradient.parameters["correction"] = "subtract";
    autoGradient.workUnit = "samples";
    set.push_back(autoGradient);

    // The dark-structure enhancement, over a frame the harness cut a soft-edged well into. Scored
    // on how much more prominent the well became, which is a property of the answer rather than of
    // the engine's mask.
    ScenarioSpec darkStructure =
        smokeScenario("detail.dark_structure.f32", "detail", smokeDescriptor(SampleType::Float32, 1), "synthetic.stars",
                      CorrectnessClass::EstimatorTolerance, 0.95);
    darkStructure.parameters["removedLayers"] = "8";
    darkStructure.parameters["midtones"] = "0.8";
    darkStructure.parameters["iterations"] = "1";
    darkStructure.workUnit = "samples";
    set.push_back(darkStructure);

    // The translation measurement, against a cyclically shifted copy of the same sky - the one
    // input whose exact answer the harness can state - through the engine's own transforms.
    ScenarioSpec phase =
        smokeScenario("registration.phase_correlation.f32", "registration", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.stars", CorrectnessClass::GeometricResidual, 0.5);
    phase.parameters["shiftX"] = "7";
    phase.parameters["shiftY"] = "-4";
    phase.parameters["window"] = "hann";
    phase.parameters["normalization"] = "subtract-mean";
    phase.workUnit = "samples";
    set.push_back(phase);

    for (const auto& [suffix, interpolation] : std::array<std::pair<const char*, const char*>, 7u>{
             {{"nearest_neighbor", "nearest_neighbor"},
              {"bilinear", "bilinear"},
              {"bicubic", "bicubic"},
              {"area", "area"},
              {"lanczos4", "lanczos4"},
              {"bit_exact_bilinear", "bit_exact_bilinear"},
              {"bit_exact_nearest_neighbor", "bit_exact_nearest_neighbor"}}})
    {
        ScenarioSpec resize = smokeScenario(std::string("resample.resize_") + suffix + ".f32", "geometry",
                                            smokeDescriptor(SampleType::Float32, 1), "synthetic.gradient",
                                            CorrectnessClass::EstimatorTolerance, 2e-5);
        resize.parameters["outputWidth"] = "997";
        resize.parameters["outputHeight"] = "773";
        resize.parameters["interpolation"] = interpolation;
        resize.parameters["border"] = "clamp";
        resize.elementsProcessed = 997u * 773u;
        resize.bytesProcessed += resize.elementsProcessed * sizeof(float);
        resize.workUnit = "samples";
        set.push_back(std::move(resize));
    }

    SampleDescriptor tileInput = smokeDescriptor(SampleType::Float32, 3);
    tileInput.width = 1003u;
    tileInput.height = 769u;
    ScenarioSpec tileRoundTrip = smokeScenario("geometry.tile_roundtrip.f32", "geometry", tileInput,
                                               "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    tileRoundTrip.parameters["grid"] = "3";
    tileRoundTrip.parameters["scalarAuthority"] = "facade";
    tileRoundTrip.elementsProcessed = std::uint64_t{tileInput.width} * tileInput.height * tileInput.channels * 2u;
    tileRoundTrip.bytesProcessed = tileInput.totalBytes() * 4u;
    tileRoundTrip.workUnit = "bytes";
    set.push_back(std::move(tileRoundTrip));

    ScenarioSpec multiply =
        smokeScenario("pointwise.mul_scalar.f32", "pointwise", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    multiply.parameters["scalar"] = "1.5";
    set.push_back(multiply);
    ScenarioSpec add = smokeScenario("pointwise.add_images.f32", "pointwise", smokeDescriptor(SampleType::Float32, 3),
                                     "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
    add.parameters["rightScale"] = "0.5";
    set.push_back(add);

    ScenarioSpec fused =
        smokeScenario("pointwise.fused_scale_clamp_chain.f32", "pointwise", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    fused.parameters["factor0"] = "1.5";
    fused.parameters["factor1"] = "0.5";
    fused.parameters["factor2"] = "1.25";
    fused.parameters["clamp"] = "0,1";
    fused.elementsProcessed *= 6u;
    fused.workUnit = "pointwise-ops";
    set.push_back(fused);

    // The fused intermediate form, weighed against running one optimized kernel per operation.
    //
    // Six rows, three chains, two ways of executing each. Both ways build the same program from the
    // same public entry points and hand it to the same compiled backend; the fused row runs it in one
    // image pass, and the staged row runs one single-step program per step with an intermediate frame
    // between them, which is what a caller who never had a fusable form would write. The three chains
    // are one from each group: a scale and clamp ladder, a colour matrix with an exposure and a clamp
    // after it, and the lowered form of an expression.
    for (const FusionChain& chain : fusionChains())
    {
        for (const bool fused : {true, false})
        {
            ScenarioSpec fusion =
                smokeScenario(fused ? chain.fusedId : chain.stagedId, "fusion", smokeDescriptor(SampleType::Float32, 3),
                              "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
            fusion.parameters["chain"] = chain.name;
            fusion.parameters["path"] = fused ? "fused" : "staged";
            fusion.parameters["steps"] = std::to_string(chain.steps);
            fusion.elementsProcessed *= chain.steps;
            fusion.workUnit = "pointwise-ops";
            set.push_back(fusion);
        }
    }

    const auto stretches = stretchScenarios();
    set.insert(set.end(), stretches.begin(), stretches.end());

    const auto presentations = presentationScenarios();
    set.insert(set.end(), presentations.begin(), presentations.end());
    const auto autostretches = autostretchScenarios();
    set.insert(set.end(), autostretches.begin(), autostretches.end());
    const auto normalizationPairs = normalizationPairScenarios();
    set.insert(set.end(), normalizationPairs.begin(), normalizationPairs.end());

    const auto scopes = channelScopeScenarios();
    set.insert(set.end(), scopes.begin(), scopes.end());

    const auto maskRendering = maskRenderingScenarios();
    set.insert(set.end(), maskRendering.begin(), maskRendering.end());

    const auto combinations = combinationScenarios();
    set.insert(set.end(), combinations.begin(), combinations.end());

    const auto stackingNumerics = stackingNumericsScenarios();
    set.insert(set.end(), stackingNumerics.begin(), stackingNumerics.end());

    // Gamma: the transfer function that decides what a vector transcendental is worth. The engine
    // applies a compiled polynomial in Float32 lanes, an approximation at the accepted tier, and the
    // row is checked against an independent oracle that raises every sample in long double.
    ScenarioSpec gamma =
        smokeScenario("pointwise.gamma_transfer.f32", "pointwise", smokeDescriptor(SampleType::Float32, 1),
                      "synthetic.gradient", CorrectnessClass::AbsoluteRelative, 1e-5);
    gamma.parameters["gamma"] = "2.2";
    gamma.workUnit = "samples";
    set.push_back(gamma);

    ScenarioSpec previewGamma = gamma;
    previewGamma.id = "pointwise.gamma_transfer.preview.f32";
    previewGamma.parameters["purpose"] = "realtime-preview";
    previewGamma.parameters["tableEntries"] = "1024";
    previewGamma.tolerance = 5e-2;
    set.push_back(previewGamma);
    const std::vector<ScenarioSpec> previewRows = previewGammaScenarios(previewGamma);
    set.insert(set.end(), previewRows.begin(), previewRows.end());

    // The tone curves, and a gamma followed by a curve, as the product applies them.
    const auto curves = curveScenarios();
    set.insert(set.end(), curves.begin(), curves.end());

    // Colour: encoded sRGB into the perceptual space, in the unit packing, with the transfer function
    // and the packing stated. The engine is not the oracle - the harness evaluates the standard's own
    // definitions in long double and the row is measured against that.
    ScenarioSpec color = smokeScenario("color.rgb_to_lab.f32", "color", smokeDescriptor(SampleType::Float32, 3),
                                       "synthetic.gradient", CorrectnessClass::AbsoluteRelative, 1e-5);
    color.parameters["source"] = "srgb";
    color.parameters["destination"] = "lab";
    color.parameters["packing"] = "unit";
    color.workUnit = "pixels";
    color.elementsProcessed = std::uint64_t{color.input.width} * color.input.height;
    set.push_back(color);

    ScenarioSpec previewColor = color;
    previewColor.id = "color.rgb_to_lab.preview.f32";
    previewColor.parameters["purpose"] = "realtime-preview";
    previewColor.parameters["tableEntries"] = "1024";
    previewColor.tolerance = 1e-2;
    set.push_back(previewColor);

    // The same conversion as one call that plans, allocates the image it returns, converts into it
    // and releases it: this row times all four, where the first times the conversion alone into a
    // destination it keeps.
    ScenarioSpec newImageColor = color;
    newImageColor.id = "color.rgb_to_lab.new_image.f32";
    newImageColor.parameters["timed"] = "plan-allocate-convert-release";
    set.push_back(newImageColor);

    // All three over a real colour frame, the one the real-data audit measured: NGC 253 demosaiced and
    // sRGB-encoded outside timing by `prepare_color_inputs.py`, every sample past the transfer
    // function's linear segment.
    for (const ScenarioSpec& base : {color, newImageColor, previewColor})
    {
        ScenarioSpec capture = base;
        capture.id = base.id.substr(0u, base.id.size() - 4u) + ".ngc253.f32";
        capture.inputSource = InputSource::Corpus;
        capture.corpusId = "color.ngc253.srgb.f32.v1";
        capture.input.width = 5544u;
        capture.input.height = 3684u;
        capture.bytesProcessed = capture.input.totalBytes();
        capture.elementsProcessed = std::uint64_t{capture.input.width} * capture.input.height;
        set.push_back(std::move(capture));
    }

    // Masks: the soft screened range selection. Nonzero fuzziness exercises both edge ramps rather
    // than only the binary special case.
    ScenarioSpec selection =
        smokeScenario("masks.range_selection.f32", "masks", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    selection.parameters["lowerLimit"] = "0.25";
    selection.parameters["upperLimit"] = "0.75";
    selection.parameters["fuzziness"] = "0.4";
    selection.parameters["screening"] = "true";
    selection.workUnit = "samples";
    set.push_back(selection);

    // The packed-mask family: build a mask from a threshold, combine two of them and count what
    // survives, which is one bit per pixel rather than one sample.
    ScenarioSpec packed =
        smokeScenario("masks.packed_combine_count.f32", "masks", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    packed.parameters["lowerLimit"] = "0.25";
    packed.parameters["upperLimit"] = "0.75";
    packed.workUnit = "pixels";
    packed.elementsProcessed = std::uint64_t{packed.input.width} * packed.input.height;
    set.push_back(packed);

    // Expressions: what a caller pays to turn a script into a frame. The timed body includes the
    // parse and the compile, because that is what running an expression once costs.
    //
    // Three scenarios rather than one, because they measure three different things. The first names
    // the specialized evaluator. The second requires the lowering into the pointwise executor, so
    // that path stays independently visible. The third is a non-lowerable random draw, whose every
    // draw is derived from the execution's stated seed and the pixel, so its answer is reproducible.
    ScenarioSpec interpreted =
        smokeScenario("expression.evaluate_chain.f32", "expression", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    interpreted.parameters["expression"] = "$T * 0.5 + 0.25";
    interpreted.parameters["path"] = "evaluator";
    interpreted.workUnit = "samples";
    set.push_back(interpreted);

    ScenarioSpec fusedExpression = interpreted;
    fusedExpression.id = "expression.evaluate_chain_fused.f32";
    fusedExpression.parameters["path"] = "fused";
    set.push_back(fusedExpression);

    ScenarioSpec random =
        smokeScenario("expression.evaluate_random.f32", "expression", smokeDescriptor(SampleType::Float32, 3),
                      "synthetic.gradient", CorrectnessClass::BitExact, 0.0);
    random.parameters["expression"] = "$T + random() * 0.05";
    random.parameters["path"] = "evaluator";
    random.parameters["seed"] = "7";
    random.workUnit = "samples";
    set.push_back(random);

    // Pipelines: the integration chain over the reference dataset, which is where the end-to-end
    // wall time and peak-memory ceiling are read from. It is corpus-backed and skips by name on a
    // machine without the dataset, and its declaration comes from the shared contract.
    set.push_back(pipelineScenario());
    // The pipeline's stages on their own, in both storage states, from the same shared contract.
    for (ScenarioSpec& stage : pipelineStageScenarios())
        set.push_back(std::move(stage));
    // The final stack again, over the same lights rewritten as XISF, which this engine reads band by band as it reads
    // the FITS files.
    for (ScenarioSpec& stage : xisfStackStreamScenarios())
        set.push_back(std::move(stage));

    // The demosaic family. Seven estimates over a two-by-two mosaic, with Fast and RCD also measured
    // over sixteen-bit code values, and three estimates over a six-by-six tabulated mosaic.
    //
    // The correctness each row is held to is a re-mosaic residual, which needs no reference output: a
    // reconstruction that changed a sample the sensor measured changed a measurement rather than
    // estimating the two it did not have.
    for (const DemosaicScenario& estimate : demosaicScenarios())
    {
        ScenarioSpec demosaic = smokeScenario(estimate.id, "demosaic", smokeDescriptor(SampleType::Float32, 1),
                                              "synthetic.stars", CorrectnessClass::RmsPsnr, 1e-3);
        demosaic.parameters["method"] = estimate.method;
        demosaic.parameters["pattern"] = estimate.pattern;
        demosaic.parameters["whiteLevel"] = "1";
        if (demosaic.id == "demosaic.lmmse.f32" || demosaic.id == "demosaic.vng4.f32")
        {
            // These estimates have only a scalar authority, so the row names it rather than leaving
            // the backend to the dispatch table. The luminance-only AHD estimate has compiled passes
            // and is measured as it is dispatched.
            demosaic.parameters["scalarAuthority"] = "facade";
        }
        if (std::string(estimate.method) == "Vng4")
        {
            demosaic.parameters[kDemosaicQualityOracleParameter] = kVng4DefinitionOracle;
        }
        demosaic.workUnit = "pixels";
        set.push_back(demosaic);
    }

    // The direct rows run the scalar authority of Fast and RCD over sixteen-bit code values, the
    // domain a raw decoder hands them over in; their output is the same every time it is computed.
    for (ScenarioSpec& scenario : set)
    {
        if (scenario.id == "demosaic.direct_fast.f32" || scenario.id == "demosaic.direct_rcd.f32")
        {
            scenario.correctnessClass = CorrectnessClass::BitExact;
            scenario.tolerance = 0.0;
            scenario.parameters["scalarAuthority"] = "code-domain";
            scenario.parameters["inputScale"] = "65535";
            scenario.parameters["whiteLevel"] = "65536";
            scenario.parameters["clipFraction"] = "0.9999847412109375";
        }
    }
    const std::vector<ScenarioSpec> demosaicCaptures = demosaicCaptureScenarios(set);
    set.insert(set.end(), demosaicCaptures.begin(), demosaicCaptures.end());

    ScenarioSpec superPixel = smokeScenario("cfa.super_pixel.f32", "demosaic", smokeDescriptor(SampleType::Float32, 1),
                                            "synthetic.stars", CorrectnessClass::BitExact, 0.0);
    superPixel.parameters["pattern"] = "Rggb";
    superPixel.workUnit = "pixels";
    set.push_back(superPixel);

    ScenarioSpec split = smokeScenario("cfa.split_planes.f32", "demosaic", smokeDescriptor(SampleType::Float32, 1),
                                       "synthetic.stars", CorrectnessClass::BitExact, 0.0);
    split.parameters["pattern"] = "Rggb";
    split.workUnit = "pixels";
    set.push_back(split);

    // The transfer/layout family: the memory-bandwidth floor every other operation sits on.
    for (const TransferBandwidthScenario& transfer : transferBandwidthScenarios())
    {
        SampleDescriptor input = smokeDescriptor(transfer.sampleType, transfer.channels);
        input.layout = transfer.inputLayout;
        ScenarioSpec bandwidth =
            smokeScenario(transfer.id, "transfer", input, "synthetic.uniform_noise", CorrectnessClass::BitExact, 0.0);
        bandwidth.workUnit = "bytes";
        bandwidth.bytesProcessed = transfer.bytesMoved(input.totalBytes());
        set.push_back(bandwidth);
    }

    return set;
}

bool AfterNightEngineAdapter::runFrameSetScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                  ResultRecord& out, std::string& skipReason)
{
    FixtureSet frames;
    if (!m_fixtures.acquireSet(scenario.corpusId, scenario.input, frames, skipReason))
    {
        // The dataset is host-local and this machine does not have it, or it is incomplete. Either
        // way the scenario skips by name rather than measuring something smaller.
        return false;
    }

    // Under the process temporary directory, which the run protocol points at persistent storage: the
    // calibrated and aligned intermediates are roughly twice the light set, and a host whose temporary
    // directory is a memory filesystem fills it partway through alignment and produces a pipeline that
    // kept a third of its lights.
    PipelineScratch scratch("afternight-bench-pipeline-", options.keptPipelineScratch);
    if (!scratch.ok())
    {
        skipReason = "'" + scenario.id + "': " + scratch.error();
        return false;
    }

    const auto codec = an::createFitsCodec();
    if (!codec)
    {
        skipReason = describeEngineFailure("the FITS codec could not be built", codec.status());
        return false;
    }
    // XISF beside it, for the rows that rewrite a frame set as XISF; every other row reads and writes FITS alone.
    const auto xisfCodec = an::createXisfCodec();
    if (!xisfCodec)
    {
        skipReason = describeEngineFailure("the XISF codec could not be built", xisfCodec.status());
        return false;
    }
    const std::array codecs{*codec, *xisfCodec};
    an::EngineContextOptions settings = benchmarkEngineOptions(options);
    settings.codecRegistry = *an::CodecRegistry::create(codecs);
    an::Result<an::EngineContext> engine = an::EngineContext::create(settings);
    if (!engine.hasValue())
    {
        skipReason = describeEngineFailure("the engine context could not be built", engine.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = describeEngineFailure("the pipeline could not be started", execution.status());
        return false;
    }

    out.fixtureDigest = frames.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));

    if (isMasterBiasStageScenario(scenario))
    {
        // The pipeline's first stage on its own, in the storage state the row declares. The plane is
        // in the frames' code units.
        StageRowMeasurement measurement(scenario, 1.0);
        if (!measurement.condition().has_value())
        {
            skipReason = "stage '" + scenario.id + "' declares no storage state this runner knows";
            return false;
        }
        PipelineStageTiming timing;
        std::uint64_t frameReads = 0u;
        std::string digest;
        std::string error;
        if (!runMasterBiasStage(*engine, *execution, frames, scenario, measurement.hooks(), timing, frameReads, digest,
                                error))
        {
            skipReason = "stage '" + scenario.id + "': " + error;
            return false;
        }
        measurement.record(timing, digest, "masterBias", out);
        out.counters["stageFrameReads"] = static_cast<double>(frameReads);
        return true;
    }
    if (isXisfStackStreamScenario(scenario))
    {
        // The final stack over the aligned lights rewritten as XISF in the scratch directory, checked against the
        // FITS files they were made from.
        StageRowMeasurement measurement(scenario, 1.0);
        if (!measurement.condition().has_value())
        {
            skipReason = "stage '" + scenario.id + "' declares no storage state this runner knows";
            return false;
        }
        PipelineStageTiming timing;
        XisfStackStreamFigures figures;
        std::uint64_t frameReads = 0u;
        std::string digest;
        std::string error;
        if (!runXisfStackStreamStage(*engine, *execution, frames, scenario, scratch.path(), measurement.hooks(), timing,
                                     figures, frameReads, digest, error))
        {
            skipReason = "stage '" + scenario.id + "': " + error;
            return false;
        }
        measurement.record(timing, digest, "stack", out);
        out.counters["stageFrameReads"] = static_cast<double>(frameReads);
        out.counters["stage.wholeFrameReadMs"] = figures.wholeReadMs;
        if (figures.reservedMaster)
        {
            // The master written as it was combined, beside the same stack handing it over whole and writing it.
            out.counters["stage.peakHeldBytes"] = static_cast<double>(figures.peakHeldBytes);
            out.counters["stage.plannedPeakBytes"] = static_cast<double>(figures.plannedPeakBytes);
            out.counters["stage.masterBytes"] = static_cast<double>(figures.masterBytes);
            out.counters["stage.wholeThenWriteMs"] = figures.wholeThenWriteMs;
            out.counters["stage.wholePeakHeldBytes"] = static_cast<double>(figures.wholePeakHeldBytes);
            out.counters["stage.wholePlannedPeakBytes"] = static_cast<double>(figures.wholePlannedPeakBytes);
        }
        return true;
    }
    if (isFinalStackStageScenario(scenario))
    {
        // The pipeline's last stage on its own, over the aligned lights one pipeline run produced.
        // The plane is in the lights' own units.
        StageRowMeasurement measurement(scenario, 1.0);
        if (!measurement.condition().has_value())
        {
            skipReason = "stage '" + scenario.id + "' declares no storage state this runner knows";
            return false;
        }
        PipelineStageTiming timing;
        std::uint64_t frameReads = 0u;
        std::string digest;
        std::string error;
        if (!runFinalStackStage(*engine, *execution, frames, scenario, measurement.hooks(), timing, frameReads, digest,
                                error))
        {
            skipReason = "stage '" + scenario.id + "': " + error;
            return false;
        }
        measurement.record(timing, digest, "stack", out);
        out.counters["stageFrameReads"] = static_cast<double>(frameReads);
        return true;
    }

    // One repetition, always. A pipeline over the reference dataset runs for minutes and writes
    // gigabytes; repeating it inside one process would measure a warm page cache rather than the
    // engine, and the controller already spreads trials across processes for exactly this reason.
    PipelineStageTimings timings;
    std::string digest;
    std::string error;

    ResidentMemorySampler sampler;
    const double cpuStart = processCpuMilliseconds();
    const auto started = std::chrono::steady_clock::now();
    sampler.start();
    const bool completed =
        runIntegrationPipeline(*engine, *execution, frames, scenario, scratch.path(), timings, digest, error);
    sampler.stop();
    const auto finished = std::chrono::steady_clock::now();
    const double cpuFinish = processCpuMilliseconds();

    if (!completed)
    {
        skipReason = "pipeline '" + scenario.id + "': " + error;
        return false;
    }
    if (timings.residuals.count == 0u)
    {
        skipReason = "pipeline '" + scenario.id +
                     "': the registration reported no per-light residual, so the run measured no geometry";
        return false;
    }

    out.timings.wallMs.push_back(std::chrono::duration<double, std::milli>(finished - started).count());
    out.timings.cpuMs.push_back(cpuFinish - cpuStart);
    out.memory.peakResidentBytes = sampler.peakBytes();
    out.outputDigest = digest;
    // The row is scored on the geometry the pipeline fitted rather than on samples two runs are not
    // required to reproduce: see the shared contract.
    out.correctnessMetric = timings.residuals.medianPixels;
    timings.writeCounters(out);
    return true;
}

bool AfterNightEngineAdapter::run(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                  std::string& skipReason)
{
    if (scenario.inputSource == InputSource::CorpusFrameSet)
    {
        return runFrameSetScenario(scenario, options, out, skipReason);
    }

    const std::optional<std::uint32_t> timedBatch = timedBatchIterations(scenario);
    if (!timedBatch.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' has an invalid timedBatch parameter";
        return false;
    }
    out.counters[kTimedBatchParameter] = static_cast<double>(*timedBatch);

    if (scenario.group == "stretch")
        return runStretch(scenario, options, out, skipReason);
    if (scenario.group == "presentation")
        return runPresentation(scenario, options, out, skipReason);
    if (scenario.group == "autostretch")
        return runAutostretch(scenario, options, out, skipReason);
    if (scenario.id == kChannelScopeLabLightness)
        return runChannelScope(scenario, options, out, skipReason);
    if (scenario.id == kImageDifference)
        return runImageDifference(scenario, options, out, skipReason);
    if (scenario.id == kMaskRenderTint)
        return runMaskOverlay(scenario, options, out, skipReason);
    if (scenario.id == kDabBlend)
        return runDabBlend(scenario, options, out, skipReason);
    if (scenario.id == kLrgbCombine)
        return runLrgbCombination(scenario, options, out, skipReason);
    if (scenario.id == kBlendModes)
        return runBlendModes(scenario, options, out, skipReason);
    if (scenario.id == kStarRecombine)
        return runStarRecombination(scenario, options, out, skipReason);
    if (scenario.id == kEdgeTighten)
        return runEdgeTighten(scenario, options, out, skipReason);
    if (scenario.id == kExternalHandoff)
        return runExternalHandoff(scenario, options, out, skipReason);
    if (scenario.id == kCometComposite)
        return runCometComposite(scenario, options, out, skipReason);
    if (scenario.id == kRejectionMapStatistics)
        return runRejectionMapStatistics(scenario, options, out, skipReason);
    if (scenario.id == "io.codec_format.path_lookup")
        return runCodecLookup(scenario, options, out, skipReason);
    if (isVideoWriteScenario(scenario.id))
        return runVideoWrite(scenario, options, out, skipReason);
    if (isSerSequenceScenario(scenario.id))
        return runSerSequence(scenario, options, out, skipReason);
    if (isFfmpegSequenceScenario(scenario.id))
        return runFfmpegSequence(scenario, options, out, skipReason);
    if (scenario.id == "sequence.image_list_write.8x_f32")
        return runImageListWrite(scenario, options, out, skipReason);
    if (scenario.group == "sequence")
        return runSequence(scenario, options, out, skipReason);
    if (scenario.group == "streaming")
        return runStreaming(scenario, options, out, skipReason);
    if (isSnapshotScenario(scenario.id))
        return runSnapshotRoundTrip(scenario, options, out, skipReason);
    if (scenario.id == "codec.fits_roundtrip.f32" || scenario.id == "codec.xisf_roundtrip.f32" ||
        isRawCodecScenario(scenario.id) || scenario.id == "codec.png_roundtrip.u16" ||
        scenario.id == "codec.tiff_roundtrip.u16" || scenario.id == "codec.jpeg_roundtrip.u8" ||
        scenario.id.rfind("codec.dump_", 0u) == 0u)
        return runCodecRoundTrip(scenario, options, out, skipReason);

    if (const TransferBandwidthScenario* const transfer = findTransferScenario(scenario.id))
    {
        return runTransfer(*transfer, scenario, options, out, skipReason);
    }
    if (scenario.id == "storage.allocate_first_touch.f32" || scenario.id == "pixel_access.generic_read_write.f32" ||
        scenario.id == "interop.opencv_view.u16")
    {
        return runPerformanceAnchor(scenario, options, out, skipReason);
    }
    if (scenario.id == "pointwise.fused_scale_clamp_chain.f32" || scenario.id == "pointwise.mul_scalar.f32" ||
        scenario.id == "pointwise.add_images.f32")
    {
        return runPointwise(scenario, options, out, skipReason);
    }
    if (scenario.group == "fusion")
    {
        return runFusionComparison(scenario, options, out, skipReason);
    }
    if (scenario.id == "reduction.basic_statistics.f32" || scenario.id == "statistics.basic_masked_roi.f32")
    {
        return runReduction(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("order.median_mad.", 0u) == 0u)
    {
        return runOrderStatistics(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("statistics.full.", 0u) == 0u)
    {
        return runCompleteStatistics(scenario, options, out, skipReason);
    }
    if (scenario.id == "histogram.full_scale.u16" || scenario.id == "histogram.masked_roi_percentiles.f32")
    {
        return runHistogram(scenario, options, out, skipReason);
    }
    if (scenario.id == "statistics.robust_masked_roi.f32" ||
        scenario.id == "statistics.robust_masked_roi_compact.f32" ||
        scenario.id == "statistics.sigma_clipped_location_scale.f32" ||
        scenario.id == "statistics.sigma_clipped_location_scale_compact.f32")
    {
        return runRobustStatistics(scenario, options, out, skipReason);
    }
    if (scenario.id == "stencil.separable_gaussian.f32")
    {
        return runSeparableConvolution(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("filter.gaussian_blur", 0u) == 0u)
    {
        return runGaussianBlur(scenario, options, out, skipReason);
    }
    if (scenario.id == "filter.box_5x5.f32")
    {
        return runBoxFilter(scenario, options, out, skipReason);
    }
    if (scenario.id == "filter.dense_convolution.f32")
    {
        return runDenseConvolution(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("filter.median_", 0u) == 0u)
    {
        return runMedianFilter(scenario, options, out, skipReason);
    }
    if (scenario.id == "filter.footprint_rank_disc_5x5.f32")
    {
        return runFootprintRank(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("multiscale.", 0u) == 0u)
    {
        return runMultiscaleTransform(scenario, options, out, skipReason);
    }
    if (scenario.id == "cosmetic.detect_map.f32")
    {
        return runCosmeticDetection(scenario, options, out, skipReason);
    }
    if (scenario.id == "cosmetic.correct_map.f32")
    {
        return runCosmeticCorrection(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("calibration.bias_dark_flat.", 0u) == 0u)
    {
        return runCalibration(scenario, options, out, skipReason);
    }
    if (scenario.id == "normalization.global_frame.f32" || scenario.id == "normalization.global_frame.real.f32")
    {
        return runGlobalNormalizationFrame(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("normalization.global_pair.", 0u) == 0u)
    {
        return runGlobalNormalizationPair(scenario, options, out, skipReason);
    }
    if (scenario.id == "normalization.global_apply.f32")
    {
        return runGlobalNormalizationApply(scenario, options, out, skipReason);
    }
    if (scenario.id == "normalization.local_frame.f32" || scenario.id == "normalization.local_frame.real.f32")
    {
        return runLocalNormalizationFrame(scenario, options, out, skipReason);
    }
    if (scenario.id == "normalization.local_apply.f32")
    {
        return runLocalNormalizationApply(scenario, options, out, skipReason);
    }
    if (scenario.id == "stacking.photometric_weighting.frames")
    {
        return runPhotometricWeighting(scenario, options, out, skipReason);
    }
    if (scenario.group == "stacking")
    {
        return runStackScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "photometry.aperture.rgb.f32")
    {
        return runAperturePhotometry(scenario, options, out, skipReason);
    }
    if (scenario.id == "color.photometric.fit.samples")
    {
        return runColorFit(scenario, options, out, skipReason);
    }
    if (scenario.id == "spectrophotometry.synthetic.rgb.f32")
    {
        return runSyntheticPhotometry(scenario, options, out, skipReason);
    }
    if (scenario.id == "color.spectrophotometric.fit.samples")
    {
        return runSpectrophotometricFit(scenario, options, out, skipReason);
    }
    if (scenario.id == "color.spectrophotometric.apply.rgb.f32")
    {
        return runSpectrophotometricApply(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("color.narrowband.", 0u) == 0u)
    {
        return runNarrowbandComposition(scenario, options, out, skipReason);
    }
    if (scenario.id == "catalog.parse.star_csv" || scenario.id == "catalog.query.region.star")
    {
        return runCatalogScenario(scenario, options, out, skipReason);
    }
    if (scenario.id.starts_with("drizzle.execute."))
        return runDrizzleExecutionScenario(scenario, options, out, skipReason);
    if (scenario.id.starts_with("drizzle.accumulation."))
        return runDrizzleAccumulationScenario(scenario, options, out, skipReason);
    if (scenario.id.starts_with("drizzle.geometry."))
        return runDrizzleGeometryScenario(scenario, options, out, skipReason);
    if (scenario.id == "astrometry.sip.inverse_map.f32")
    {
        return runSipInverseMapScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "astrometry.plate_solve.hinted.points")
    {
        return runPlateSolveScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "registration.transform.destination_map.f32")
    {
        return runRegistrationMapScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "registration.comet.detect.frames.f32")
    {
        return runCometDetectionScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "catalog.healpix.query_disc.ring")
    {
        return runHealpixQueryScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "catalog.build.deep_sky")
    {
        return runDeepSkyCatalogScenario(scenario, options, out, skipReason);
    }
    if (scenario.id == "stars.reduce.strong.f32")
    {
        return runStarReduction(scenario, options, out, skipReason);
    }
    if (scenario.id == "registration.star_alignment.prepared.points")
    {
        return runStarAlignment(scenario, options, out, skipReason);
    }
    if (scenario.group == "stars")
    {
        return runStarDetection(scenario, options, out, skipReason);
    }
    if (scenario.id == "registration.phase_correlation.f32")
    {
        return runPhaseCorrelation(scenario, options, out, skipReason);
    }
    if (scenario.id.starts_with("nr.patch_denoise"))
    {
        return runPatchDenoise(scenario, options, out, skipReason);
    }
    if (scenario.id == "deconvolution.richardson_lucy.f32" || scenario.id == "deconvolution.wiener.f32")
    {
        return runDeconvolution(scenario, options, out, skipReason);
    }
    if (scenario.id == "background.model_subtract.f32" || scenario.id == "background.auto_gradient.f32")
    {
        return runBackground(scenario, options, out, skipReason);
    }
    if (scenario.id == "detail.dark_structure.f32")
    {
        return runDarkStructure(scenario, options, out, skipReason);
    }
    if (scenario.id == "nr.bilateral.f32")
    {
        return runBilateralFilter(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("pointwise.curve_", 0u) == 0u)
    {
        return runCurveInterpolation(scenario, options, out, skipReason);
    }
    if (scenario.id == "nr.total_variation_chambolle.f32" || scenario.id == "nr.total_variation_bregman.f32")
    {
        return runTotalVariation(scenario, options, out, skipReason);
    }
    if (scenario.id == "synthesis.noise_gaussian.f32")
    {
        return runNoiseGeneration(scenario, options, out, skipReason);
    }
    if (scenario.id == "nr.scnr_average_neutral.f32")
    {
        return runChromaticNoise(scenario, options, out, skipReason);
    }
    if (scenario.id == "nr.banding_horizontal.f32")
    {
        return runBandingSuppression(scenario, options, out, skipReason);
    }
    if (scenario.id == "nr.impulse_rejection.f32")
    {
        return runImpulseRejection(scenario, options, out, skipReason);
    }
    if (scenario.id.starts_with("resample.resize_") && scenario.id.ends_with(".f32"))
    {
        return runResize(scenario, options, out, skipReason);
    }
    if (scenario.id == "geometry.tile_roundtrip.f32")
    {
        return runTileRoundTrip(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("pointwise.gamma_transfer.", 0u) == 0u || scenario.id == "pointwise.gamma_curve_fused.f32")
    {
        return runGammaTransfer(scenario, options, out, skipReason);
    }
    if (scenario.id.rfind("color.rgb_to_lab.", 0u) == 0u)
    {
        return runColorTransform(scenario, options, out, skipReason);
    }
    if (scenario.id == "masks.range_selection.f32" || scenario.id == "masks.packed_combine_count.f32")
    {
        return runMaskOperations(scenario, options, out, skipReason);
    }
    if (scenario.group == "demosaic")
    {
        return scenario.id.rfind("cfa.", 0u) == 0u ? runCfaChannels(scenario, options, out, skipReason)
                                                   : runDemosaic(scenario, options, out, skipReason);
    }

    if (scenario.group == "expression")
    {
        return runExpression(scenario, options, out, skipReason);
    }
    if (scenario.id != "conversion.u16_to_f32" && scenario.id != "conversion.f32_to_u16")
    {
        skipReason = "AfterNightEngine does not implement scenario '" + scenario.id + "'";
        return false;
    }
    const bool forward = scenario.id == "conversion.u16_to_f32";
    const std::string destinationName = forward ? "Float32" : "UInt16";
    if (parameterValue(scenario, "destination", destinationName.c_str()) != destinationName)
    {
        skipReason = "scenario '" + scenario.id + "' requests a destination this runner does not implement";
        return false;
    }
    if (scenario.input.sampleType != (forward ? SampleType::UInt16 : SampleType::Float32) ||
        scenario.input.layout != Layout::Planar)
    {
        skipReason = "scenario '" + scenario.id + "' declares an input this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }

    an::EngineContextOptions engineOptions = benchmarkEngineOptions(options);
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }

    // Both sides of the conversion go through the one bridge, and the destination is described as a
    // fixture of its own rather than derived from the source by an element size. That is what makes
    // this a use of the bridge rather than a second implementation of it: the destination's shape,
    // strides and meaning are stated in the benchmark vocabulary and translated by the same rules,
    // so a fixture the bridge would refuse cannot reach the engine through this side either.
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }

    SampleDescriptor destinationDescriptor = fixture.descriptor;
    destinationDescriptor.sampleType = forward ? SampleType::Float32 : SampleType::UInt16;
    destinationDescriptor.rowStrideBytes = 0u;
    destinationDescriptor.planeStrideBytes = 0u;
    const std::optional<BridgedFixture> destinationShape =
        bridgeFixture(destinationDescriptor, semantics, engine->limits(), destinationDescriptor.totalBytes());
    if (!destinationShape.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(destinationDescriptor, semantics, engine->limits(), destinationDescriptor.totalBytes()),
            destinationDescriptor);
        return false;
    }

    const an::ImageLayout& sourceLayout = source->layout;
    const an::ImageLayout& destinationLayout = destinationShape->layout;

    an::Result<an::ImageStorageBytes> destination = an::ImageStorageBytes::create(
        *engine, destinationLayout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    std::string conversionFailure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        an::ConversionCounters counters;
        an::ConversionPolicy policy;
        // The narrowing row measures truncation toward zero, the rounding of a multiply by the
        // destination maximum and a C++ cast; the engine's unbiased NearestEven default stays
        // intact for ordinary callers.
        if (!forward)
        {
            policy.rounding = an::RoundingMode::TowardZero;
        }
        const an::Status converted = an::convertImageSamples(
            *execution, forward ? an::SampleType::UInt16 : an::SampleType::Float32, fixture.bytes.data(), sourceLayout,
            forward ? an::SampleType::Float32 : an::SampleType::UInt16, destination->data(), destinationLayout, policy,
            counters);
        if (converted.failed() && conversionFailure.empty())
        {
            conversionFailure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", converted);
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        if (!forward)
        {
            const auto* source = reinterpret_cast<const float*>(fixture.bytes.data());
            const auto* produced = reinterpret_cast<const std::uint16_t*>(destination->data());
            const std::uint64_t samples = static_cast<std::uint64_t>(fixture.descriptor.width) *
                                          fixture.descriptor.height * fixture.descriptor.channels;
            double worst = 0.0;
            for (std::uint64_t index = 0; index < samples; ++index)
            {
                const std::uint16_t oracle =
                    static_cast<std::uint16_t>(65535.0f * (std::clamp)(source[index], 0.0f, 1.0f));
                worst = (std::max)(worst, static_cast<double>(
                                              std::abs(static_cast<int>(produced[index]) - static_cast<int>(oracle))));
            }
            out.correctnessMetric = worst;
        }
        return Sha256::of(destination->data(), static_cast<std::size_t>(destinationLayout.totalBytes()));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0;
    for (std::uint32_t index = 0; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);

    if (!conversionFailure.empty())
    {
        skipReason = conversionFailure;
        return false;
    }

    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runPerformanceAnchor(const ScenarioSpec& scenario, const RunOptions& options,
                                                   ResultRecord& out, std::string& skipReason)
{
    const bool storageAnchor = scenario.id == "storage.allocate_first_touch.f32";
    const bool pixelAnchor = scenario.id == "pixel_access.generic_read_write.f32";
    const bool openCvAnchor = scenario.id == "interop.opencv_view.u16";
    if ((!storageAnchor && !pixelAnchor && !openCvAnchor) || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        (openCvAnchor ? scenario.input.sampleType != SampleType::UInt16
                      : scenario.input.sampleType != SampleType::Float32))
    {
        skipReason = "scenario '" + scenario.id + "' declares a performance anchor this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = describeEngineFailure("the engine context could not be built", engine.status());
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> bridge =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!bridge.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }

    out.fixtureDigest = fixture.digest;
    out.threadCount = 1u;
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;

    std::string operationFailure;
    std::function<std::string(bool)> body;
    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    if (storageAnchor)
    {
        body = [&, sampleCount](bool produceDigest) {
            an::Result<an::ImageStorage<float>> storage =
                an::ImageStorage<float>::createFilled(*engine, bridge->layout, 0.5f, an::kDefaultBufferAlignmentBytes);
            if (!storage.hasValue())
            {
                if (operationFailure.empty())
                {
                    operationFailure = describeEngineFailure("the storage anchor could not allocate", storage.status());
                }
                return std::string{};
            }
            return produceDigest ? Sha256::of(storage->data(), sampleCount * sizeof(float)) : std::string{};
        };
    }
    else if (pixelAnchor)
    {
        an::Result<an::ImageStorage<float>> allocated = an::ImageStorage<float>::create(
            *engine, bridge->layout, an::StorageInitialization::Uninitialized, an::kDefaultBufferAlignmentBytes);
        if (!allocated.hasValue())
        {
            skipReason =
                describeEngineFailure("the pixel-access destination could not be allocated", allocated.status());
            return false;
        }
        auto destination = std::make_shared<an::ImageStorage<float>>(std::move(*allocated));
        const auto* const source = reinterpret_cast<const float*>(fixture.bytes.data());
        body = [source, destination, sampleCount](bool produceDigest) {
            float* const output = destination->data();
            for (std::size_t index = 0u; index < sampleCount; ++index)
            {
                output[index] = source[index] * 0.5f + 0.25f;
            }
            return produceDigest ? Sha256::of(output, sampleCount * sizeof(float)) : std::string{};
        };
    }
    else
    {
        an::Result<an::ImageStorage<std::uint16_t>> allocated = an::ImageStorage<std::uint16_t>::create(
            *engine, bridge->layout, an::StorageInitialization::Uninitialized, an::kDefaultBufferAlignmentBytes);
        if (!allocated.hasValue())
        {
            skipReason = describeEngineFailure("the OpenCV-view storage could not be allocated", allocated.status());
            return false;
        }
        auto storage = std::make_shared<an::ImageStorage<std::uint16_t>>(std::move(*allocated));
        std::memcpy(storage->data(), fixture.bytes.data(), fixture.bytes.size());
        const std::optional<an::PlaneView<const std::uint16_t>> plane = an::PlaneView<const std::uint16_t>::create(
            std::span<const std::uint16_t>{storage->data(), sampleCount}, scenario.input.width, scenario.input.height,
            1u, bridge->layout.rowStrideBytes());
        if (!plane.has_value())
        {
            skipReason = "the OpenCV-view source could not be described as a plane";
            return false;
        }
        const an::ExternalBufferContract contract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                                  .libraryDomain = an::NumericDomain::NormalizedLinear,
                                                  .ownership = an::BufferOwnership::EngineOwned,
                                                  .scope = an::BorrowScope::SingleCall,
                                                  .mutability = an::BufferMutability::ReadOnly};
        body = [&, storage, plane, contract, sampleCount](bool produceDigest) {
            const std::optional<an::OpenCvMatDescription> description =
                an::describeAsOpenCvMat(*plane, contract, 1u, an::kOpenCvTypeLayout4);
            if (!description.has_value() || description->data != storage->data())
            {
                operationFailure = "the OpenCV adapter did not return the engine storage's zero-copy description";
                return std::string{};
            }
            return produceDigest ? Sha256::of(storage->data(), sampleCount * sizeof(std::uint16_t)) : std::string{};
        };
    }

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!operationFailure.empty())
    {
        skipReason = operationFailure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runPointwise(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                           std::string& skipReason)
{
    const bool fusedChain = scenario.id == "pointwise.fused_scale_clamp_chain.f32";
    const bool multiply = scenario.id == "pointwise.mul_scalar.f32";
    const bool add = scenario.id == "pointwise.add_images.f32";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u || (!fusedChain && !multiply && !add) ||
        (fusedChain && parameterValue(scenario, "clamp", "0,1") != "0,1"))
    {
        skipReason = "scenario '" + scenario.id + "' declares a pointwise shape this runner does not implement";
        return false;
    }
    std::array<double, 3u> factors{};
    double scalar = 1.5;
    double rightScale = 0.5;
    if ((fusedChain && (!doubleParameter(scenario, "factor0", 1.5, factors[0], skipReason) ||
                        !doubleParameter(scenario, "factor1", 0.5, factors[1], skipReason) ||
                        !doubleParameter(scenario, "factor2", 1.25, factors[2], skipReason))) ||
        (multiply && !doubleParameter(scenario, "scalar", 1.5, scalar, skipReason)) ||
        (add && !doubleParameter(scenario, "rightScale", 0.5, rightScale, skipReason)))
    {
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    // These rows read engine storage, copied from the fixture before the clock. The fixture's own
    // buffer starts at another offset within its
    // page than engine storage does, and a processor that matches a load to an earlier store by the low
    // twelve bits of their addresses makes each load of a source 48 bytes short of its destination,
    // modulo 4 KiB, wait for the store before it: a cost of where the benchmark keeps its fixture.
    an::Result<an::ImageStorage<float>> input = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Uninitialized, an::kDefaultBufferAlignmentBytes);
    if (!input.hasValue())
    {
        skipReason = std::string("the source could not be allocated: ") + input.status().message();
        return false;
    }
    std::memcpy(input->data(), fixture.bytes.data(), fixture.bytes.size());

    an::PointwisePlanShape shape;
    shape.channelCount = scenario.input.channels;
    const auto makeProgram = [&]() -> an::Result<an::PointwiseProgram> {
        if (multiply)
        {
            return an::buildScaleBiasProgram(shape, scalar, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
        }
        if (add)
        {
            const an::Result<an::PointwiseProgram> sum =
                an::buildBinaryArithmeticProgram(shape, an::BinaryArithmeticOperation::Add);
            const an::Result<an::PointwiseProgram> scale =
                an::buildScaleBiasProgram(shape, rightScale, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
            if (!sum.hasValue())
            {
                return sum.status();
            }
            if (!scale.hasValue())
            {
                return scale.status();
            }
            const std::array<an::PointwiseProgram, 2u> stages{*sum, *scale};
            return an::fusePointwisePrograms(stages);
        }
        an::PointwiseProgramBuilder builder;
        builder.channels(scenario.input.channels).sources(1u).readingSamples(an::SampleType::Float32);
        for (const double factor : factors)
        {
            builder.append(an::pointwiseScale(factor)).append(an::pointwiseClamp(0.0, 1.0));
        }
        return builder.build();
    };
    an::Result<an::PointwiseProgram> program = makeProgram();
    if (!program.hasValue())
    {
        skipReason = std::string("the pointwise program could not be built: ") + program.status().message();
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const std::array<an::PointwiseImageView, 2u> sourceStorage{
        an::PointwiseImageView{.samples = input->data(), .layout = &source->layout},
        an::PointwiseImageView{.samples = input->data(), .layout = &source->layout}};
    const std::span<const an::PointwiseImageView> sources(sourceStorage.data(), program->sourceCount());
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            *execution, *program, sources,
            an::PointwiseMutableImageView{.samples = destination->data(), .layout = &source->layout},
            an::KernelValidityView{});
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.counters["validPixels"] = static_cast<double>(ran->run.selection.valid);
        out.counters["samplesConverted"] = static_cast<double>(ran->conversion.samplesConverted);
        out.counters["saturatedSamples"] = static_cast<double>(ran->conversion.saturatedSamples);
        return Sha256::of(destination->data(), static_cast<std::size_t>(source->layout.totalBytes()));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

namespace
{

/// The single-step program that runs one step of a chain on its own.
///
/// The staged path is not a different implementation of anything: each step is put into a program of
/// its own, with the chain's representation, channel count and domains, and executed by the same
/// entry point the fused chain uses. A table the step reads travels with it, because a lookup step
/// whose table was left behind would refuse rather than run.
///
/// @param chain the whole program, for its shape and its tables.
/// @param step which step to isolate.
/// @return the one-step program, or whatever the builder refused.
[[nodiscard]] an::Result<an::PointwiseProgram> isolateStep(const an::PointwiseProgram& chain, std::uint32_t step)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(chain.channelCount())
        .sources(chain.sourceCount())
        .readingSamples(chain.sourceSampleType())
        .readingDomain(chain.inputDomain())
        .writingDomain(chain.outputDomain())
        .computingIn(chain.precision(), chain.precisionJustification())
        .promising(chain.outputContract());
    for (std::uint32_t index = 0u; index < chain.tableCount(); ++index)
    {
        (void)builder.addTable(chain.table(index));
    }
    builder.append(chain.steps()[step]);
    return builder.build();
}

} // namespace

bool AfterNightEngineAdapter::runFusionComparison(const ScenarioSpec& scenario, const RunOptions& options,
                                                  ResultRecord& out, std::string& skipReason)
{
    const std::string chainName = parameterValue(scenario, "chain", "");
    const bool fused = parameterValue(scenario, "path", "fused") == "fused";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u)
    {
        skipReason = "scenario '" + scenario.id + "' declares a shape this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }

    // Two destinations rather than one: the staged path writes an intermediate frame between every
    // pair of steps, and paying for that frame is part of what it costs.
    an::Result<an::ImageStorage<float>> first = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    an::Result<an::ImageStorage<float>> second = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!first.hasValue() || !second.hasValue())
    {
        skipReason = std::string("the destinations could not be allocated: ") +
                     (first.hasValue() ? second.status().message() : first.status().message());
        return false;
    }

    an::PointwisePlanShape shape;
    shape.channelCount = scenario.input.channels;
    const auto makeChain = [&]() -> an::Result<an::PointwiseProgram> {
        if (chainName == "scale_clamp_ladder")
        {
            an::PointwiseProgramBuilder builder;
            builder.channels(shape.channelCount).sources(1u).readingSamples(an::SampleType::Float32);
            for (const double factor : {1.5, 0.5, 1.25})
            {
                builder.append(an::pointwiseScale(factor)).append(an::pointwiseClamp(0.0, 1.0));
            }
            return builder.build();
        }
        if (chainName == "color_matrix_exposure_clamp")
        {
            // The primaries change a caller would run inside a chain it was already executing: sRGB
            // to Display P3 in linear light, which is one channel matrix.
            const an::ColorEncoding srgb{.primaries = an::RgbPrimaries::Srgb};
            const an::ColorEncoding displayP3{.primaries = an::RgbPrimaries::DisplayP3};
            const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(srgb, displayP3);
            if (!plan.hasValue())
            {
                return plan.status();
            }
            const std::optional<an::ColorMatrix3> matrix = plan->linearRgbMatrix();
            if (!matrix.has_value())
            {
                return an::Status::failure(an::ErrorCode::InvalidArgument);
            }
            const an::Result<an::PointwiseProgram> matrixProgram = an::buildColorMatrixProgram(shape, *matrix);
            const an::Result<an::PointwiseProgram> exposure = an::buildExposureProgram(shape, 0.5);
            const an::Result<an::PointwiseProgram> clamp =
                an::buildClampProgram(shape, an::ClampInterval{.low = 0.0, .high = 1.0});
            if (!matrixProgram.hasValue())
            {
                return matrixProgram.status();
            }
            if (!exposure.hasValue())
            {
                return exposure.status();
            }
            if (!clamp.hasValue())
            {
                return clamp.status();
            }
            const std::array<an::PointwiseProgram, 3u> stages{*matrixProgram, *exposure, *clamp};
            return an::fusePointwisePrograms(stages);
        }
        if (chainName == "expression_scale_bias")
        {
            return an::buildScaleBiasProgram(shape, 0.5, 0.25, std::nullopt);
        }
        return an::Status::failure(an::ErrorCode::InvalidArgument);
    };

    an::Result<an::PointwiseProgram> chain = makeChain();
    if (!chain.hasValue())
    {
        skipReason = std::string("the chain could not be built: ") + chain.status().message();
        return false;
    }
    const std::uint32_t stepCount = static_cast<std::uint32_t>(chain->steps().size());

    std::vector<an::PointwiseProgram> isolated;
    if (!fused)
    {
        isolated.reserve(stepCount);
        for (std::uint32_t index = 0u; index < stepCount; ++index)
        {
            an::Result<an::PointwiseProgram> one = isolateStep(*chain, index);
            if (!one.hasValue())
            {
                skipReason = std::string("step ") + std::to_string(index) +
                             " has no single-step form: " + one.status().message();
                return false;
            }
            isolated.push_back(std::move(*one));
        }
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const void* readFrom = fixture.bytes.data();
        void* writeTo = first->data();
        if (fused)
        {
            const an::PointwiseImageView bound{.samples = readFrom, .layout = &source->layout};
            const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                *execution, *chain, std::span<const an::PointwiseImageView>{&bound, 1u},
                an::PointwiseMutableImageView{.samples = writeTo, .layout = &source->layout}, an::KernelValidityView{});
            if (!ran.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
                }
                return std::string{};
            }
        }
        else
        {
            for (std::uint32_t index = 0u; index < stepCount; ++index)
            {
                const an::PointwiseImageView bound{.samples = readFrom, .layout = &source->layout};
                const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                    *execution, isolated[index], std::span<const an::PointwiseImageView>{&bound, 1u},
                    an::PointwiseMutableImageView{.samples = writeTo, .layout = &source->layout},
                    an::KernelValidityView{});
                if (!ran.hasValue())
                {
                    if (failure.empty())
                    {
                        failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
                    }
                    return std::string{};
                }
                readFrom = writeTo;
                writeTo = (writeTo == first->data()) ? second->data() : first->data();
            }
            writeTo = const_cast<void*>(readFrom);
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.counters["steps"] = static_cast<double>(stepCount);
        out.counters["imagePasses"] = static_cast<double>(fused ? 1u : stepCount);
        return Sha256::of(writeTo, static_cast<std::size_t>(source->layout.totalBytes()));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runReduction(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                           std::string& skipReason)
{
    const bool maskedRegion = scenario.id == "statistics.basic_masked_roi.f32";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "channel", "0") != "0" ||
        parameterValue(scenario, "passes", "2") != "2" ||
        (maskedRegion &&
         (parameterValue(scenario, "regionX", "113") != "113" || parameterValue(scenario, "regionY", "97") != "97" ||
          parameterValue(scenario, "regionWidth", "797") != "797" ||
          parameterValue(scenario, "regionHeight", "701") != "701" ||
          parameterValue(scenario, "maskPeriod", "17") != "17")))
    {
        skipReason = "scenario '" + scenario.id + "' declares a reduction shape this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                fixture.bytes.size() / sizeof(float)};
    const BasicStatisticsOracleWindow oracleWindow{.sourceWidth = scenario.input.width,
                                                   .sourceHeight = scenario.input.height,
                                                   .originX = kStatisticsRegionX,
                                                   .originY = kStatisticsRegionY,
                                                   .width = kStatisticsRegionWidth,
                                                   .height = kStatisticsRegionHeight,
                                                   .excludedEvery = kStatisticsMaskPeriod};
    const std::optional<BasicStatisticsOracle> oracle = maskedRegion
                                                            ? computeBasicStatisticsOracle(fixtureSamples, oracleWindow)
                                                            : computeBasicStatisticsOracle(fixtureSamples);
    if (!oracle.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' has no finite non-empty oracle input";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    an::StatisticsSelection statisticsSelection;
    std::vector<std::uint8_t> maskBytes;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    if (maskedRegion)
    {
        const std::uint64_t maskByteCount =
            an::packedMaskRowBytes(source->layout.extent().width()) * source->layout.extent().height();
        maskBytes.resize(static_cast<std::size_t>(maskByteCount), std::uint8_t{0});
        const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
            an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, source->layout.extent());
        const std::optional<an::ValidityDescriptor> descriptor =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
        if (!writableMask.has_value() || !descriptor.has_value())
        {
            skipReason = "scenario '" + scenario.id + "' could not build its checked validity mask";
            return false;
        }
        for (std::uint32_t y = kStatisticsRegionY; y < kStatisticsRegionY + kStatisticsRegionHeight; ++y)
        {
            for (std::uint32_t x = kStatisticsRegionX; x < kStatisticsRegionX + kStatisticsRegionWidth; ++x)
            {
                if (!writableMask->setValid(0u, x, y, oracleWindow.selects(x, y)))
                {
                    skipReason = "scenario '" + scenario.id + "' could not populate its checked validity mask";
                    return false;
                }
            }
        }
        mask = writableMask->constView();
        statisticsSelection.validity = *descriptor;
        statisticsSelection.mask = &*mask;
        statisticsSelection.region = an::StatisticsRegion{.originX = kStatisticsRegionX,
                                                          .originY = kStatisticsRegionY,
                                                          .width = kStatisticsRegionWidth,
                                                          .height = kStatisticsRegionHeight};
    }

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::BasicStatistics> statistics = an::computeImageBasicStatistics(
            *execution, an::SampleType::Float32, fixture.bytes.data(), source->layout, 0u, statisticsSelection);
        if (!statistics.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", statistics.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const BasicStatisticsOracle actual{.count = static_cast<std::size_t>(statistics->selection.valid),
                                           .minimum = statistics->minimum,
                                           .maximum = statistics->maximum,
                                           .mean = statistics->mean,
                                           .populationVariance = statistics->populationVariance};
        out.correctnessMetric = basicStatisticsError(actual, *oracle);
        out.counters["validSamples"] = static_cast<double>(actual.count);
        out.counters["selectedPixels"] = static_cast<double>(statistics->selection.total);
        out.counters["maximumAdditionDepth"] = static_cast<double>(statistics->sumError.maximumAdditionDepth);
        out.counters["sumErrorBound"] = statistics->sumError.absoluteError;
        out.counters["centeredMomentErrorBound"] = statistics->centeredMomentError.absoluteError;
        out.counters["populationStandardDeviation"] = statistics->populationStandardDeviation;
        return Sha256::of(basicStatisticsText(actual));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runOrderStatistics(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    const bool captured = scenario.inputSource == InputSource::Corpus;
    if (scenario.input.sampleType != (captured ? SampleType::UInt16 : SampleType::Float32) ||
        scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        parameterValue(scenario, "channel", "0") != "0" ||
        parameterValue(scenario, "estimators", "median,mad") != "median,mad" ||
        (captured && parameterValue(scenario, "samples", "") != "codes-over-65535"))
    {
        skipReason = "scenario '" + scenario.id + "' declares an estimator shape this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    // A capture is ranked as the unit-interval samples the neutral fixture layer makes of its codes.
    std::vector<float> capturedSamples;
    SampleDescriptor descriptor = fixture.descriptor;
    const void* samples = fixture.bytes.data();
    std::size_t sampleBytes = fixture.bytes.size();
    if (captured)
    {
        capturedSamples = makeUnitIntervalCapture(fixture.bytes);
        descriptor.sampleType = SampleType::Float32;
        descriptor.rowStrideBytes = 0u;
        descriptor.planeStrideBytes = 0u;
        samples = capturedSamples.data();
        sampleBytes = capturedSamples.size() * sizeof(float);
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(descriptor);
    const std::optional<BridgedFixture> source = bridgeFixture(descriptor, semantics, engine->limits(), sampleBytes);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(checkFixture(descriptor, semantics, engine->limits(), sampleBytes),
                                                  descriptor);
        return false;
    }
    const std::span<const float> fixtureSamples{static_cast<const float*>(samples),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::optional<OrderStatisticsOracle> oracle =
        captured ? computeCapturedOrderStatisticsOracle(fixture.bytes) : computeOrderStatisticsOracle(fixtureSamples);
    if (!oracle.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' has no finite non-empty oracle input";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const an::KernelPlaneSource view{.sampleType = an::SampleType::Float32,
                                     .samples = samples,
                                     .layout = &source->layout,
                                     .plane = 0u,
                                     .sampleInPixel = 0u};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
            *execution, view, std::span<const double>{}, std::span<double>{},
            an::PlaneOrderRequest{.medianAbsoluteDeviation = true}, an::KernelValidityView{});
        if (!selected.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", selected.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const OrderStatisticsOracle actual{.count = static_cast<std::size_t>(selected->run.selection.valid),
                                           .median = selected->median,
                                           .medianAbsoluteDeviation = selected->medianAbsoluteDeviation};
        out.correctnessMetric = orderStatisticsError(actual, *oracle);
        out.counters["validSamples"] = static_cast<double>(actual.count);
        out.counters["deterministicChunks"] = static_cast<double>(selected->deterministicChunks);
        out.counters["plannedWorkers"] = static_cast<double>(selected->plannedWorkers);
        out.counters["planeScans"] = static_cast<double>(selected->planeScans);
        out.counters["peakScratchBytes"] = static_cast<double>(selected->peakScratchBytes);
        return Sha256::of(orderStatisticsText(actual));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runCompleteStatistics(const ScenarioSpec& scenario, const RunOptions& options,
                                                    ResultRecord& out, std::string& skipReason)
{
    if (scenario.inputSource != InputSource::Corpus || scenario.input.sampleType != SampleType::UInt16 ||
        scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        parameterValue(scenario, "channel", "0") != "0" ||
        parameterValue(scenario, "estimators", "complete") != "complete" ||
        parameterValue(scenario, "samples", "") != "codes-over-65535")
    {
        skipReason = "scenario '" + scenario.id + "' declares an estimator shape this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const std::vector<float> samples = makeUnitIntervalCapture(fixture.bytes);
    SampleDescriptor descriptor = fixture.descriptor;
    descriptor.sampleType = SampleType::Float32;
    descriptor.rowStrideBytes = 0u;
    descriptor.planeStrideBytes = 0u;
    const std::size_t sampleBytes = samples.size() * sizeof(float);
    const FixtureSemantics semantics = defaultFixtureSemantics(descriptor);
    const std::optional<BridgedFixture> source = bridgeFixture(descriptor, semantics, engine->limits(), sampleBytes);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(checkFixture(descriptor, semantics, engine->limits(), sampleBytes),
                                                  descriptor);
        return false;
    }
    const std::optional<BasicStatisticsOracle> basicOracle = computeBasicStatisticsOracle(samples);
    const std::optional<OrderStatisticsOracle> orderOracle = computeCapturedOrderStatisticsOracle(fixture.bytes);
    if (!basicOracle.has_value() || !orderOracle.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' has no finite non-empty oracle input";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    // The basic reduction and the advanced statistics, both retained, as one complete statistics
    // call composes them; the advanced statistics repeat the reduction for their own centres.
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::BasicStatistics> basic =
            an::computeImageBasicStatistics(*execution, an::SampleType::Float32, samples.data(), source->layout, 0u);
        if (!basic.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", basic.status());
            }
            return std::string{};
        }
        const an::Result<an::AdvancedStatistics> advanced =
            an::computeImageAdvancedStatistics(*execution, an::SampleType::Float32, samples.data(), source->layout, 0u);
        if (!advanced.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", advanced.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const CompleteStatistics actual{.count = static_cast<std::size_t>(basic->selection.valid),
                                        .minimum = basic->minimum,
                                        .maximum = basic->maximum,
                                        .mean = basic->mean,
                                        .populationVariance = basic->populationVariance,
                                        .median = advanced->median,
                                        .medianAbsoluteDeviation = advanced->medianAbsoluteDeviation,
                                        .averageAbsoluteDeviation = advanced->averageAbsoluteDeviation,
                                        .sumOfSquares = advanced->sumOfSquares,
                                        .meanOfSquares = advanced->meanOfSquares,
                                        .biweightMidvariance = advanced->biweightMidvariance,
                                        .backgroundNoise = advanced->backgroundNoise,
                                        .location = advanced->iterativeKsigmaLocation,
                                        .scale = advanced->iterativeKsigmaScale};
        out.correctnessMetric = completeStatisticsError(actual, *basicOracle, *orderOracle);
        out.counters["validSamples"] = static_cast<double>(actual.count);
        out.counters["advancedPlaneScans"] = static_cast<double>(advanced->planeScans);
        out.counters["advancedPeakScratchBytes"] = static_cast<double>(advanced->peakScratchBytes);
        return Sha256::of(completeStatisticsText(actual));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runRobustStatistics(const ScenarioSpec& scenario, const RunOptions& options,
                                                  ResultRecord& out, std::string& skipReason)
{
    const bool clipping = scenario.id == "statistics.sigma_clipped_location_scale.f32" ||
                          scenario.id == "statistics.sigma_clipped_location_scale_compact.f32";
    const bool compacted = scenario.id == "statistics.robust_masked_roi_compact.f32" ||
                           scenario.id == "statistics.sigma_clipped_location_scale_compact.f32";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "channel", "0") != "0" ||
        parameterValue(scenario, "roiStrategy", "strided") != (compacted ? "compact" : "strided") ||
        (clipping &&
         (parameterValue(scenario, "lowSigma", "3") != "3" || parameterValue(scenario, "highSigma", "3") != "3" ||
          parameterValue(scenario, "maximumIterations", "5") != "5")) ||
        (!clipping &&
         (parameterValue(scenario, "estimators", "median,mad") != "median,mad" ||
          parameterValue(scenario, "regionX", "113") != "113" || parameterValue(scenario, "regionY", "97") != "97" ||
          parameterValue(scenario, "regionWidth", "797") != "797" ||
          parameterValue(scenario, "regionHeight", "701") != "701" ||
          parameterValue(scenario, "maskPeriod", "17") != "17")))
    {
        skipReason = "scenario \'" + scenario.id + "\' declares an estimator shape this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                fixture.bytes.size() / sizeof(float)};
    const BasicStatisticsOracleWindow oracleWindow{.sourceWidth = scenario.input.width,
                                                   .sourceHeight = scenario.input.height,
                                                   .originX = kStatisticsRegionX,
                                                   .originY = kStatisticsRegionY,
                                                   .width = kStatisticsRegionWidth,
                                                   .height = kStatisticsRegionHeight,
                                                   .excludedEvery = kStatisticsMaskPeriod};
    std::optional<OrderStatisticsOracle> rankOracle;
    std::optional<SigmaClippedOracle> clipOracle;
    if (clipping)
    {
        clipOracle = computeSigmaClippedOracle(fixtureSamples, 3.0, 3.0, 5u);
    }
    else
    {
        rankOracle = computeOrderStatisticsOracle(fixtureSamples, oracleWindow);
    }
    if (!rankOracle.has_value() && !clipOracle.has_value())
    {
        skipReason = "scenario \'" + scenario.id + "\' has no finite non-empty oracle input";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    an::StatisticsSelection statisticsSelection;
    std::vector<std::uint8_t> maskBytes;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    if (!clipping)
    {
        const std::uint64_t maskByteCount =
            an::packedMaskRowBytes(source->layout.extent().width()) * source->layout.extent().height();
        maskBytes.resize(static_cast<std::size_t>(maskByteCount), std::uint8_t{0});
        const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
            an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, source->layout.extent());
        const std::optional<an::ValidityDescriptor> descriptor =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
        if (!writableMask.has_value() || !descriptor.has_value())
        {
            skipReason = "scenario \'" + scenario.id + "\' could not build its checked validity mask";
            return false;
        }
        for (std::uint32_t y = kStatisticsRegionY; y < kStatisticsRegionY + kStatisticsRegionHeight; ++y)
        {
            for (std::uint32_t x = kStatisticsRegionX; x < kStatisticsRegionX + kStatisticsRegionWidth; ++x)
            {
                if (!writableMask->setValid(0u, x, y, oracleWindow.selects(x, y)))
                {
                    skipReason = "scenario \'" + scenario.id + "\' could not populate its checked validity mask";
                    return false;
                }
            }
        }
        mask = writableMask->constView();
        statisticsSelection.validity = *descriptor;
        statisticsSelection.mask = &*mask;
        statisticsSelection.region = an::StatisticsRegion{.originX = kStatisticsRegionX,
                                                          .originY = kStatisticsRegionY,
                                                          .width = kStatisticsRegionWidth,
                                                          .height = kStatisticsRegionHeight};
    }

    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    request.roiStrategy =
        compacted ? an::RoiSelectionStrategy::CompactSelection : an::RoiSelectionStrategy::StridedSource;
    if (clipping)
    {
        request.sigmaClipping = an::SigmaClippingRequest{.lowSigma = 3.0, .highSigma = 3.0, .maximumIterations = 5u};
    }

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::RobustStatistics> statistics =
            an::computeImageRobustStatistics(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                             0u, statisticsSelection, request);
        if (!statistics.hasValue())
        {
            if (failure.empty())
            {
                failure =
                    describeEngineFailure("the engine failed to run \'" + scenario.id + "\'", statistics.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.counters["validSamples"] = static_cast<double>(statistics->selection.valid);
        out.counters["selectedPixels"] = static_cast<double>(statistics->selection.total);
        out.counters["planeScans"] = static_cast<double>(statistics->planeScans);
        out.counters["peakScratchBytes"] = static_cast<double>(statistics->peakScratchBytes);
        out.counters["compactedSelection"] =
            statistics->roiStrategy == an::RoiSelectionStrategy::CompactSelection ? 1.0 : 0.0;
        if (!clipping)
        {
            const OrderStatisticsOracle actual{.count = static_cast<std::size_t>(statistics->selection.valid),
                                               .median = statistics->median,
                                               .medianAbsoluteDeviation = statistics->medianAbsoluteDeviation};
            out.correctnessMetric = orderStatisticsError(actual, *rankOracle);
            return Sha256::of(orderStatisticsText(actual));
        }
        if (!statistics->clipped.has_value())
        {
            failure = "the engine produced no clipped estimator for \'" + scenario.id + "\'";
            return std::string{};
        }
        out.counters["clippingIterations"] = static_cast<double>(statistics->clipped->iterations);
        out.counters["clippingConverged"] = statistics->clipped->converged ? 1.0 : 0.0;
        const SigmaClippedOracle actual{.retained = static_cast<std::size_t>(statistics->clipped->selection.valid),
                                        .location = statistics->clipped->location,
                                        .scale = statistics->clipped->scale,
                                        .mean = statistics->clipped->mean,
                                        .standardDeviation = statistics->clipped->standardDeviation};
        out.correctnessMetric = sigmaClippedError(actual, *clipOracle);
        return Sha256::of(sigmaClippedText(actual));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runHistogram(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                           std::string& skipReason)
{
    const bool fullScale = scenario.id == "histogram.full_scale.u16";
    const std::size_t binCount = fullScale ? 65536u : 1024u;
    if (scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        parameterValue(scenario, "channel", "0") != "0" ||
        scenario.input.sampleType != (fullScale ? SampleType::UInt16 : SampleType::Float32) ||
        parameterValue(scenario, "bins", "65536") != (fullScale ? "65536" : "1024") ||
        parameterValue(scenario, "domain", "stated") != (fullScale ? "representation-full-scale" : "stated") ||
        (!fullScale &&
         (parameterValue(scenario, "lowest", "0") != "0" || parameterValue(scenario, "highest", "1") != "1" ||
          parameterValue(scenario, "quantiles", "0.001,0.5,0.999") != "0.001,0.5,0.999" ||
          parameterValue(scenario, "regionX", "113") != "113" || parameterValue(scenario, "regionY", "97") != "97" ||
          parameterValue(scenario, "regionWidth", "797") != "797" ||
          parameterValue(scenario, "regionHeight", "701") != "701" ||
          parameterValue(scenario, "maskPeriod", "17") != "17")))
    {
        skipReason = "scenario '" + scenario.id + "' declares a histogram shape this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }

    const BasicStatisticsOracleWindow oracleWindow{.sourceWidth = scenario.input.width,
                                                   .sourceHeight = scenario.input.height,
                                                   .originX = kStatisticsRegionX,
                                                   .originY = kStatisticsRegionY,
                                                   .width = kStatisticsRegionWidth,
                                                   .height = kStatisticsRegionHeight,
                                                   .excludedEvery = kStatisticsMaskPeriod};
    std::optional<HistogramOracle> oracle;
    std::vector<std::uint64_t> oracleCounts;
    if (fullScale)
    {
        const std::span<const std::uint16_t> codes{reinterpret_cast<const std::uint16_t*>(fixture.bytes.data()),
                                                   fixture.bytes.size() / sizeof(std::uint16_t)};
        oracle = computeFullScaleHistogramOracle(codes, binCount);
    }
    else
    {
        const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                    fixture.bytes.size() / sizeof(float)};
        oracle = computeWindowedHistogramOracle(fixtureSamples, oracleWindow, 0.0, 1.0, binCount, oracleCounts);
    }
    if (!oracle.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' has no valid oracle input";
        return false;
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    an::StatisticsSelection statisticsSelection;
    std::vector<std::uint8_t> maskBytes;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    if (!fullScale)
    {
        const std::uint64_t maskByteCount =
            an::packedMaskRowBytes(source->layout.extent().width()) * source->layout.extent().height();
        maskBytes.resize(static_cast<std::size_t>(maskByteCount), std::uint8_t{0});
        const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
            an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, source->layout.extent());
        const std::optional<an::ValidityDescriptor> descriptor =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
        if (!writableMask.has_value() || !descriptor.has_value())
        {
            skipReason = "scenario '" + scenario.id + "' could not build its checked validity mask";
            return false;
        }
        for (std::uint32_t y = kStatisticsRegionY; y < kStatisticsRegionY + kStatisticsRegionHeight; ++y)
        {
            for (std::uint32_t x = kStatisticsRegionX; x < kStatisticsRegionX + kStatisticsRegionWidth; ++x)
            {
                if (!writableMask->setValid(0u, x, y, oracleWindow.selects(x, y)))
                {
                    skipReason = "scenario '" + scenario.id + "' could not populate its checked validity mask";
                    return false;
                }
            }
        }
        mask = writableMask->constView();
        statisticsSelection.validity = *descriptor;
        statisticsSelection.mask = &*mask;
        statisticsSelection.region = an::StatisticsRegion{.originX = kStatisticsRegionX,
                                                          .originY = kStatisticsRegionY,
                                                          .width = kStatisticsRegionWidth,
                                                          .height = kStatisticsRegionHeight};
    }

    an::ImageHistogramRequest request;
    request.domain = fullScale ? an::HistogramDomain::RepresentationFullScale : an::HistogramDomain::Stated;
    request.lowest = 0.0;
    request.highest = 1.0;
    std::vector<std::uint64_t> bins(binCount, 0u);
    std::vector<std::uint64_t> cumulative(binCount, 0u);
    const std::array<double, 3u> quantiles{0.001, 0.5, 0.999};
    std::array<double, 3u> values{};

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::ImageHistogram> histogram = an::computeImageHistogram(
            *execution,
            scenario.input.sampleType == SampleType::UInt16 ? an::SampleType::UInt16 : an::SampleType::Float32,
            fixture.bytes.data(), source->layout, 0u, statisticsSelection, request, bins);
        if (!histogram.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", histogram.status());
            }
            return std::string{};
        }
        if (!fullScale)
        {
            const an::Status accumulated = an::accumulateCumulativeCounts(bins, cumulative);
            if (accumulated.failed())
            {
                if (failure.empty())
                {
                    failure =
                        describeEngineFailure("the engine failed to accumulate '" + scenario.id + "'", accumulated);
                }
                return std::string{};
            }
            for (std::size_t index = 0u; index < quantiles.size(); ++index)
            {
                const an::Result<double> value = an::histogramQuantileValue(*histogram, cumulative, quantiles[index],
                                                                            an::HistogramQuantileRule::Interpolated);
                if (!value.hasValue())
                {
                    if (failure.empty())
                    {
                        failure = describeEngineFailure(
                            "the engine failed to read a percentile of '" + scenario.id + "'", value.status());
                    }
                    return std::string{};
                }
                values[index] = *value;
            }
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::optional<HistogramOracle> actual = summariseHistogram(bins);
        if (!actual.has_value())
        {
            failure = "the engine produced no bins for '" + scenario.id + "'";
            return std::string{};
        }
        out.correctnessMetric = histogramError(*actual, *oracle);
        out.counters["validSamples"] = static_cast<double>(histogram->selection.valid);
        out.counters["countedSamples"] = static_cast<double>(histogram->counted);
        out.counters["belowRange"] = static_cast<double>(histogram->belowRange);
        out.counters["aboveRange"] = static_cast<double>(histogram->aboveRange);
        out.counters["deterministicChunks"] = static_cast<double>(histogram->deterministicChunks);
        out.counters["plannedWorkers"] = static_cast<double>(histogram->plannedWorkers);
        out.counters["partialBytes"] = static_cast<double>(histogram->partialBytes);
        std::string text = histogramText(*actual);
        if (!fullScale)
        {
            double worst = 0.0;
            for (std::size_t index = 0u; index < quantiles.size(); ++index)
            {
                const double expected = histogramOracleQuantile(oracleCounts, 0.0, 1.0, quantiles[index]);
                worst = (std::max)(worst, std::abs(values[index] - expected));
                out.counters["quantile" + std::to_string(index)] = values[index];
            }
            // The bins have to agree exactly and the percentiles read out of them have to agree to
            // the resolution of a bin, so the row reports whichever of the two is further out.
            out.correctnessMetric = (std::max)(out.correctnessMetric.value_or(0.0), worst);
            text += histogramPercentileText(values);
        }
        return Sha256::of(text);
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runSeparableConvolution(const ScenarioSpec& scenario, const RunOptions& options,
                                                      ResultRecord& out, std::string& skipReason)
{
    double sigma = 1.5;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "border", "reflect-excluding-edge") != "reflect-excluding-edge")
    {
        skipReason = "scenario '" + scenario.id + "' declares a filter shape this runner does not implement";
        return false;
    }
    if (!doubleParameter(scenario, "sigma", 1.5, sigma, skipReason) || !(sigma > 0.0))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' states a standard deviation this runner cannot filter with";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::uint32_t radius = gaussianRadiusForSigma(sigma);
    const std::vector<double> taps = gaussianSeparableTaps(radius, sigma);
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<ConvolutionProbe> probes = computeSeparableGaussianOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, taps, kConvolutionProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const std::optional<an::PlaneView<const float>> input = an::PlaneView<const float>::create(
        fixtureSamples, scenario.input.width, scenario.input.height, 1u, source->layout.rowStrideBytes());
    const std::optional<an::PlaneView<float>> output = an::PlaneView<float>::create(
        std::span<float>{destination->data(), static_cast<std::size_t>(scenario.elementsProcessed)},
        scenario.input.width, scenario.input.height, 1u, source->layout.rowStrideBytes());
    if (!input.has_value() || !output.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not describe its planes";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const an::SeparableStencilRequest filter{
        .horizontalTaps = taps,
        .verticalTaps = taps,
        .borders = an::StencilBorderPolicy{.border = an::KernelBorderMode::ReflectExcludingEdge}};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::StencilExecutionResult> ran =
            an::convolveSeparablePlane(*execution, *input, *output, filter, an::KernelValidityView{});
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = separableConvolutionError(produced, scenario.input.width, probes);
        out.counters["tapsPerAxis"] = static_cast<double>(taps.size());
        out.counters["tileRows"] = static_cast<double>(ran->tileRows);
        out.counters["tileCount"] = static_cast<double>(ran->tileCount);
        out.counters["concurrentTiles"] = static_cast<double>(ran->concurrentTiles);
        out.counters["scratchBytes"] = static_cast<double>(ran->scratchBytes);
        return Sha256::of(separableConvolutionText(produced, scenario.input.width, probes));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runDemosaic(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                          std::string& skipReason)
{
    const std::string methodName = parameterValue(scenario, "method", "");
    const std::string patternName = parameterValue(scenario, "pattern", "");
    std::optional<an::DemosaicMethod> method = demosaicMethodNamed(methodName);
    std::optional<an::CfaDescriptor> cfa = mosaicNamed(patternName);
    double whiteLevel = 1.0;
    double clipFraction = 1.0;
    double inputScale = 1.0;
    const std::string scalarAuthority = parameterValue(scenario, "scalarAuthority", "");
    const bool rtprocessScalar = scalarAuthority == "code-domain";
    const bool facadeScalar = scalarAuthority == "facade";
    // A capture's codes are carried into the unit interval before the clock, as the statistics rows do.
    const bool captured = scenario.inputSource == InputSource::Corpus;
    if (scenario.input.sampleType != (captured ? SampleType::UInt16 : SampleType::Float32) ||
        (captured && (rtprocessScalar || parameterValue(scenario, "samples", "") != "codes-over-65535")) ||
        scenario.input.layout != Layout::Planar || scenario.input.channels != 1u || !method.has_value() ||
        !cfa.has_value() || !doubleParameter(scenario, "whiteLevel", 1.0, whiteLevel, skipReason) ||
        !(whiteLevel > 0.0) || !doubleParameter(scenario, "clipFraction", 1.0, clipFraction, skipReason) ||
        !(clipFraction > 0.0) || clipFraction > 1.0 ||
        !doubleParameter(scenario, "inputScale", 1.0, inputScale, skipReason) || !(inputScale > 0.0))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a demosaic shape this runner does not implement";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(scenario.input.width) * scenario.input.height;
    std::vector<float> scaledMosaic;
    const std::uint8_t* sourceBytes = fixture.bytes.data();
    SampleDescriptor descriptor = fixture.descriptor;
    std::size_t sourceByteCount = fixture.bytes.size();
    if (captured)
    {
        scaledMosaic = makeUnitIntervalCapture(fixture.bytes);
        if (scaledMosaic.size() != pixels)
        {
            skipReason = "scenario '" + scenario.id + "' names a capture that is not one sixteen-bit code a pixel";
            return false;
        }
        sourceBytes = reinterpret_cast<const std::uint8_t*>(scaledMosaic.data());
        sourceByteCount = scaledMosaic.size() * sizeof(float);
        descriptor.sampleType = SampleType::Float32;
        descriptor.rowStrideBytes = 0u;
        descriptor.planeStrideBytes = 0u;
    }
    if (rtprocessScalar)
    {
        const auto* const samples = reinterpret_cast<const float*>(fixture.bytes.data());
        scaledMosaic.resize(pixels);
        for (std::size_t index = 0u; index < pixels; ++index)
        {
            scaledMosaic[index] = samples[index] * static_cast<float>(inputScale);
        }
        sourceBytes = reinterpret_cast<const std::uint8_t*>(scaledMosaic.data());
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(descriptor, semantics, engine->limits(), sourceByteCount);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(descriptor, semantics, engine->limits(), sourceByteCount), descriptor);
        return false;
    }
    const std::optional<an::ImageExtent> colourExtent =
        an::ImageExtent::create(scenario.input.width, scenario.input.height, 3u, engine->limits());
    if (!colourExtent.has_value())
    {
        skipReason = "the destination extent could not be built";
        return false;
    }
    const an::ImageLayoutRequest colourRequest{.extent = *colourExtent,
                                               .elementSizeBytes = 4u,
                                               .samplesPerPixel = 1u,
                                               .planeCount = 3u,
                                               .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> colourLayout = an::ImageLayout::create(colourRequest, engine->limits());
    if (!colourLayout.has_value())
    {
        skipReason = "the destination layout could not be built";
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, *colourLayout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    an::DemosaicRequest request{};
    request.method = *method;
    request.domain.whiteLevel = whiteLevel;
    request.domain.clipFraction = clipFraction;
    const an::Result<an::DemosaicPlan> plan = an::planDemosaic(request, *cfa);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the demosaic could not be planned", plan.status());
        return false;
    }
    an::ExecutionOptions executionOptions = executionOptionsForRun(options);
    if (rtprocessScalar || facadeScalar)
    {
        executionOptions.backend = an::ComputeBackend::Reference;
        executionOptions.backendFallback = an::BackendFallback::Forbidden;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptions);
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = rtprocessScalar || facadeScalar ? BackendId::Scalar : BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    const std::vector<std::uint8_t> blockEntries = mosaicBlockEntries(*cfa);
    const MosaicBlock block{.entries = blockEntries,
                            .size = an::cfaPatternBlockSize(cfa->pattern()),
                            .originX = cfa->originX(),
                            .originY = cfa->originY()};
    const std::span<const float> mosaicSamples{reinterpret_cast<const float*>(sourceBytes), pixels};
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::DemosaicResult> ran =
            an::demosaicImage(*execution, an::SampleType::Float32, sourceBytes, source->layout, destination->data(),
                              *colourLayout, plan.value());
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> red{destination->data(), pixels};
        const std::span<const float> green{destination->data() + pixels, pixels};
        const std::span<const float> blue{destination->data() + 2u * pixels, pixels};
        recordDemosaicEvidence(scenario, red, green, blue, mosaicSamples, width, height, block, out);
        out.counters["tiles"] = static_cast<double>(ran->tileCount);
        out.counters["imagePasses"] = static_cast<double>(ran->imagePasses);
        out.counters["borderWidth"] = static_cast<double>(ran->borderWidth);
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        out.counters["plannedWorkers"] = static_cast<double>(ran->plannedWorkers);
        out.counters["readInPlace"] = ran->readInPlace ? 1.0 : 0.0;
        return rtprocessScalar ? demosaicExactDigest(red, green, blue)
                               : Sha256::of(demosaicProbeText(red, green, blue, width, height, kDemosaicProbeStride));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runCfaChannels(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    const bool superPixel = scenario.id == "cfa.super_pixel.f32";
    const std::optional<an::CfaDescriptor> cfa = mosaicNamed(parameterValue(scenario, "pattern", ""));
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || !cfa.has_value() || cfa->hasFilterTable())
    {
        skipReason = "scenario '" + scenario.id + "' declares a mosaic this runner does not rearrange";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    const std::uint32_t blockSize =
        superPixel ? an::cfaSuperPixelBlockSize(cfa->pattern()) : an::cfaPatternBlockSize(cfa->pattern());
    const std::uint32_t channels = superPixel ? 3u : an::cfaBlockPlaneCount(cfa->pattern());
    const std::uint32_t blockColumns = scenario.input.width / blockSize;
    const std::uint32_t blockRows = scenario.input.height / blockSize;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(blockColumns, blockRows, channels, engine->limits());
    if (!extent.has_value())
    {
        skipReason = "the destination extent could not be built";
        return false;
    }
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, engine->limits());
    if (!layout.has_value())
    {
        skipReason = "the destination layout could not be built";
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, *layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const std::size_t produced = static_cast<std::size_t>(blockColumns) * blockRows * channels;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::CfaChannelResult> ran =
            superPixel ? an::superPixelCfa(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                           *cfa, destination->data(), *layout)
                       : an::splitCfaBlockPlanes(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                 source->layout, *cfa, destination->data(), *layout);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = 0.0;
        out.counters["blockSize"] = static_cast<double>(ran->blockSize);
        out.counters["trailingColumns"] = static_cast<double>(ran->trailingColumns);
        out.counters["trailingRows"] = static_cast<double>(ran->trailingRows);
        const std::span<const float> samples{destination->data(), produced};
        return Sha256::of(demosaicProbeText(samples, samples, samples, blockColumns, blockRows * channels, 17u));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runGaussianBlur(const ScenarioSpec& scenario, const RunOptions& options,
                                              ResultRecord& out, std::string& skipReason)
{
    double sigma = 2.5;
    const std::string method = parameterValue(scenario, "method", "separable-taps");
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "border", "reflect-excluding-edge") != "reflect-excluding-edge" ||
        (method != "separable-taps" && method != "repeated-box") ||
        (method == "repeated-box" && parameterValue(scenario, "purpose", "") != "realtime-preview"))
    {
        skipReason = "scenario '" + scenario.id + "' declares a blur shape this runner does not implement";
        return false;
    }
    if (!doubleParameter(scenario, "sigma", 2.5, sigma, skipReason) || !(sigma > 0.0))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' states a standard deviation this runner cannot blur with";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    // Both rows are measured against the Gaussian itself, evaluated by the harness. The radius the
    // oracle truncates at is the one the engine's plan derives from four sigmas, so the engine is not
    // filtering with a wider kernel than the reference.
    const std::uint32_t radius = gaussianRadiusForSigma(sigma);
    const std::vector<double> taps = gaussianSeparableTaps(radius, sigma);
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<ConvolutionProbe> probes = computeSeparableGaussianOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, taps, kConvolutionProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    an::GaussianRequest request{};
    request.sigmaX = sigma;
    request.sigmaY = sigma;
    const bool realtimePreview = method == "repeated-box";
    const an::Result<an::GaussianPlan> plan =
        realtimePreview
            ? an::planRealtimePreviewGaussianBlur(an::RealtimePreviewGaussianRequest{.sigmaX = sigma, .sigmaY = sigma})
            : an::planGaussianBlur(request);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the blur could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::SpatialFilterResult> ran =
            realtimePreview
                ? an::blurImageRealtimePreviewGaussian(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                       source->layout, destination->data(), source->layout, *plan)
                : an::blurImageGaussian(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                        destination->data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = separableConvolutionError(produced, scenario.input.width, probes);
        out.counters["imagePasses"] = static_cast<double>(ran->imagePasses);
        out.counters["horizontalRadius"] = static_cast<double>(ran->horizontalRadius);
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        out.counters["realizedSigma"] = plan->realizedSigmaX();
        out.counters["readInPlace"] = ran->readInPlace ? 1.0 : 0.0;
        return Sha256::of(separableConvolutionText(produced, scenario.input.width, probes));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runBoxFilter(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                           std::string& skipReason)
{
    double radiusValue = 2.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "border", "reflect-excluding-edge") != "reflect-excluding-edge" ||
        !doubleParameter(scenario, "radius", 2.0, radiusValue, skipReason) || radiusValue < 0.0 ||
        radiusValue > static_cast<double>(an::kMaxStencilRadius) || std::floor(radiusValue) != radiusValue)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a box this runner does not implement";
        }
        return false;
    }
    const auto radius = static_cast<std::uint32_t>(radiusValue);

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::uint32_t tapCount = 2u * radius + 1u;
    const std::vector<double> taps(tapCount, 1.0 / static_cast<double>(tapCount));
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<ConvolutionProbe> probes = computeSeparableGaussianOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, taps, kConvolutionProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }
    const an::Result<an::BoxFilterPlan> plan = an::planBoxFilter(
        {.radiusX = radius, .radiusY = radius, .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge}});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the box filter could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::SpatialFilterResult> ran =
            an::filterImageBox(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                               destination->data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = separableConvolutionError(produced, scenario.input.width, probes);
        out.counters["imagePasses"] = static_cast<double>(ran->imagePasses);
        out.counters["horizontalRadius"] = static_cast<double>(ran->horizontalRadius);
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        return Sha256::of(separableConvolutionText(produced, scenario.input.width, probes));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runDenseConvolution(const ScenarioSpec& scenario, const RunOptions& options,
                                                  ResultRecord& out, std::string& skipReason)
{
    double sigma = 2.5;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "border", "reflect-excluding-edge") != "reflect-excluding-edge")
    {
        skipReason = "scenario '" + scenario.id + "' declares a filter shape this runner does not implement";
        return false;
    }
    if (!doubleParameter(scenario, "sigma", 2.5, sigma, skipReason) || !(sigma > 0.0))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' states a standard deviation this runner cannot filter with";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::uint32_t radius = gaussianRadiusForSigma(sigma);
    const std::vector<double> taps = gaussianSeparableTaps(radius, sigma);
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<ConvolutionProbe> probes = computeSeparableGaussianOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, taps, kConvolutionProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }
    // The outer product of the same taps, which is the kernel the separable pair is equivalent to.
    // Handing it to the dense path is what makes the two rows a comparison of execution rather than
    // of filters.
    std::vector<double> dense(taps.size() * taps.size(), 0.0);
    for (std::size_t row = 0u; row < taps.size(); ++row)
    {
        for (std::size_t column = 0u; column < taps.size(); ++column)
        {
            dense[row * taps.size() + column] = taps[row] * taps[column];
        }
    }
    if (dense.size() > an::kMaxStencilKernelSamples)
    {
        skipReason = "scenario '" + scenario.id + "' states a kernel wider than a dense convolution may hold";
        return false;
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const an::DenseConvolutionRequest request{
        .coefficients = dense,
        .width = static_cast<std::uint32_t>(taps.size()),
        .height = static_cast<std::uint32_t>(taps.size()),
        .borders = an::StencilBorderPolicy{.border = an::KernelBorderMode::ReflectExcludingEdge}};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::SpatialFilterResult> ran =
            an::convolveImageDense(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                   destination->data(), source->layout, request);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = separableConvolutionError(produced, scenario.input.width, probes);
        out.counters["kernelSamples"] = static_cast<double>(dense.size());
        out.counters["imagePasses"] = static_cast<double>(ran->imagePasses);
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        return Sha256::of(separableConvolutionText(produced, scenario.input.width, probes));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runMedianFilter(const ScenarioSpec& scenario, const RunOptions& options,
                                              ResultRecord& out, std::string& skipReason)
{
    double radiusValue = 1.0;
    const std::string method = parameterValue(scenario, "method", "automatic");
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || (method != "automatic" && method != "network" && method != "selection"))
    {
        skipReason = "scenario '" + scenario.id + "' declares a filter shape this runner does not implement";
        return false;
    }
    if (!doubleParameter(scenario, "radius", 1.0, radiusValue, skipReason) || !(radiusValue >= 1.0) ||
        radiusValue > 8.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' states a window this runner does not measure";
        }
        return false;
    }
    const auto radius = static_cast<std::uint32_t>(radiusValue);

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<MedianProbe> probes = computeMedianFilterOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, radius, kMedianProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    an::OrderFilterRequest request{};
    request.radiusX = radius;
    request.radiusY = radius;
    request.border = an::KernelBorderMode::NotApplicable;
    request.method = method == "automatic" ? an::OrderFilterMethod::Automatic
                                           : (method == "network" ? an::OrderFilterMethod::SortingNetwork
                                                                  : an::OrderFilterMethod::SelectionKernel);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::OrderFilterResult> ran =
            an::filterImageOrderStatistic(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                          destination->data(), source->layout, request);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = medianFilterError(produced, scenario.input.width, probes);
        out.counters["windowSamples"] = static_cast<double>(ran->windowSamples);
        out.counters["networkComparators"] = static_cast<double>(ran->networkComparators);
        out.counters["networkOutputs"] = static_cast<double>(ran->networkOutputs);
        out.counters["selectionOutputs"] = static_cast<double>(ran->selectionOutputs);
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        return Sha256::of(medianFilterText(produced, scenario.input.width, probes));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runFootprintRank(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "radius", "2") != "2" ||
        parameterValue(scenario, "element", "disc") != "disc" || parameterValue(scenario, "quantile", "0.5") != "0.5")
    {
        skipReason = "scenario '" + scenario.id + "' declares a rank this runner does not implement";
        return false;
    }
    constexpr std::uint32_t kRadius = 2u;
    constexpr std::size_t kSide = 2u * kRadius + 1u;
    // The disc the product's circular element of this side draws: a cell whose squared distance from
    // the centre is at most the radius squared plus the radius.
    std::array<std::uint8_t, kSide * kSide> element{};
    for (std::size_t row = 0u; row < kSide; ++row)
    {
        for (std::size_t column = 0u; column < kSide; ++column)
        {
            const auto dy = static_cast<std::int64_t>(row) - kRadius;
            const auto dx = static_cast<std::int64_t>(column) - kRadius;
            element[row * kSide + column] = dx * dx + dy * dy <= kRadius * kRadius + kRadius ? 1u : 0u;
        }
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<MedianProbe> probes = computeFootprintMedianOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, kRadius, element, kMedianProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    an::FootprintOrderFilterRequest request{};
    request.radiusX = kRadius;
    request.radiusY = kRadius;
    request.footprint = element;
    request.quantile = 0.5;
    request.border = an::KernelBorderMode::NotApplicable;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::OrderFilterResult> ran =
            an::filterImageFootprintOrderStatistic(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                   source->layout, destination->data(), source->layout, request);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = medianFilterError(produced, scenario.input.width, probes);
        out.counters["windowSamples"] = static_cast<double>(ran->windowSamples);
        out.counters["selectionOutputs"] = static_cast<double>(ran->selectionOutputs);
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        return Sha256::of(medianFilterText(produced, scenario.input.width, probes));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runCosmeticDetection(const ScenarioSpec& scenario, const RunOptions& options,
                                                   ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "coldSigma", "3") != "3" ||
        parameterValue(scenario, "hotSigma", "3") != "3" ||
        parameterValue(scenario, "implants", "lattice48-extremes") != "lattice48-extremes")
    {
        skipReason = "scenario '" + scenario.id + "' declares a detection this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<float> samples{reinterpret_cast<float*>(fixture.bytes.data()),
                                   static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<CosmeticImplant> implants =
        plantCosmeticDefects(samples, scenario.input.width, scenario.input.height);
    if (implants.empty())
    {
        skipReason = "scenario '" + scenario.id + "' has no room for its planted defects";
        return false;
    }
    const CosmeticOracle oracle =
        computeCosmeticOracle(samples, scenario.input.width, scenario.input.height, implants, 3.0, 3.0, 1u, 2u);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    // The maps are caller-owned and every bit of them is overwritten per run, so one pair serves
    // every repetition without the answer depending on what an earlier run left behind.
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(scenario.input.width, scenario.input.height, 1u, engine->limits());
    if (!extent.has_value())
    {
        skipReason = "the map extent could not be described";
        return false;
    }
    std::vector<std::uint8_t> coldBytes(
        static_cast<std::size_t>(an::packedMaskRowBytes(scenario.input.width)) * scenario.input.height, 0u);
    std::vector<std::uint8_t> hotBytes(coldBytes.size(), 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> coldMap =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{coldBytes}, *extent);
    const std::optional<an::ValidityMaskView<std::uint8_t>> hotMap =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{hotBytes}, *extent);
    if (!coldMap.has_value() || !hotMap.has_value())
    {
        skipReason = "the deviant maps could not be described";
        return false;
    }
    const an::CosmeticDetectionOptions detection{.coldSigma = 3.0, .hotSigma = 3.0};

    std::string failure;
    std::array<an::CosmeticChannelThresholds, 1u> thresholds{};
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::CosmeticScanResult> mapped =
            an::mapImageDeviantPixels(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                      detection, &*coldMap, &*hotMap, std::span{thresholds});
        if (!mapped.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", mapped.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const double coldThreshold = static_cast<double>(thresholds[0].coldThreshold.value_or(0.0f));
        const double hotThreshold = static_cast<double>(thresholds[0].hotThreshold.value_or(0.0f));
        out.correctnessMetric =
            cosmeticDetectionError(coldThreshold, hotThreshold, mapped->coldCount, mapped->hotCount, oracle);
        out.counters["coldCount"] = static_cast<double>(mapped->coldCount);
        out.counters["hotCount"] = static_cast<double>(mapped->hotCount);
        out.counters["imagePasses"] = static_cast<double>(mapped->imagePasses);
        out.counters["workingBytes"] = static_cast<double>(mapped->workingBytes);
        out.counters["plannedWorkers"] = static_cast<double>(mapped->plannedWorkers);
        return Sha256::of(cosmeticDetectionText(coldThreshold, hotThreshold, mapped->coldCount, mapped->hotCount));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runCosmeticCorrection(const ScenarioSpec& scenario, const RunOptions& options,
                                                    ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "hotRadius", "1") != "1" ||
        parameterValue(scenario, "coldRadius", "2") != "2" ||
        parameterValue(scenario, "rank", "linear-middle-mean") != "linear-middle-mean" ||
        parameterValue(scenario, "implants", "lattice48-extremes") != "lattice48-extremes")
    {
        skipReason = "scenario '" + scenario.id + "' declares a correction this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<float> samples{reinterpret_cast<float*>(fixture.bytes.data()),
                                   static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<CosmeticImplant> implants =
        plantCosmeticDefects(samples, scenario.input.width, scenario.input.height);
    if (implants.empty())
    {
        skipReason = "scenario '" + scenario.id + "' has no room for its planted defects";
        return false;
    }
    const CosmeticOracle oracle =
        computeCosmeticOracle(samples, scenario.input.width, scenario.input.height, implants, 3.0, 3.0, 1u, 2u);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    // The marked set is the harness's rather than the engine's own detection, which is what
    // makes every repair the same question: identical positions, identical windows, and a rank
    // that is an element of the window rather than arithmetic over it.
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(scenario.input.width, scenario.input.height, 1u, engine->limits());
    if (!extent.has_value())
    {
        skipReason = "the map extent could not be described";
        return false;
    }
    const std::uint64_t rowBytes = an::packedMaskRowBytes(scenario.input.width);
    std::vector<std::uint8_t> coldBytes(static_cast<std::size_t>(rowBytes) * scenario.input.height, 0u);
    std::vector<std::uint8_t> hotBytes(coldBytes.size(), 0u);
    for (const CosmeticImplant& implant : implants)
    {
        std::vector<std::uint8_t>& bytes = implant.hot ? hotBytes : coldBytes;
        bytes[static_cast<std::size_t>(implant.y) * rowBytes + implant.x / 8u] |=
            static_cast<std::uint8_t>(1u << (implant.x % 8u));
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> coldMap =
        an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>{coldBytes}, *extent);
    const std::optional<an::ValidityMaskView<const std::uint8_t>> hotMap =
        an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>{hotBytes}, *extent);
    if (!coldMap.has_value() || !hotMap.has_value())
    {
        skipReason = "the deviant maps could not be described";
        return false;
    }

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::CosmeticCorrectionResult> corrected = an::correctImageDeviantPixels(
            *execution, an::SampleType::Float32, fixture.bytes.data(), source->layout, destination->data(),
            source->layout, &*coldMap, &*hotMap, an::CosmeticCorrectionRequest{});
        if (!corrected.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", corrected.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = cosmeticCorrectionError(produced, samples, scenario.input.width, oracle);
        out.counters["hotReplaced"] = static_cast<double>(corrected->hotReplaced);
        out.counters["coldReplaced"] = static_cast<double>(corrected->coldReplaced);
        out.counters["unresolved"] = static_cast<double>(corrected->unresolved);
        out.counters["workingBytes"] = static_cast<double>(corrected->filter.workingBytes);
        return Sha256::of(cosmeticCorrectionText(produced, scenario.input.width, oracle));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runPatchDenoise(const ScenarioSpec& scenario, const RunOptions& options,
                                              ResultRecord& out, std::string& skipReason)
{
    double whiteLevel = 1.0;
    double iterationValue = 1.0;
    const std::string finishing = parameterValue(scenario, "finishing", "none");
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "sigma", "measured") != "measured" ||
        (finishing != "none" && finishing != "guided-dual-domain") ||
        !doubleParameter(scenario, "whiteLevel", 1.0, whiteLevel, skipReason) ||
        !doubleParameter(scenario, "iterations", 1.0, iterationValue, skipReason) || iterationValue < 1.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a denoise this runner does not implement";
        }
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    an::PatchDenoiseRequest request;
    request.measureNoise = true;
    request.whiteLevel = whiteLevel;
    request.iterations = static_cast<std::uint32_t>(iterationValue);
    request.finishing = finishing == "guided-dual-domain" ? an::PatchDenoiseFinishing::GuidedDualDomain
                                                          : an::PatchDenoiseFinishing::None;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the denoise plan was refused", plan.status());
        return false;
    }

    const auto sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::span<const float> noisy{reinterpret_cast<const float*>(fixture.bytes.data()), sampleCount};
    std::vector<float> denoised(sampleCount, 0.0f);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    // This end-to-end row reports the compiled-loop backend the runner uses. The guided
    // finishing substage resolves its bounded-error exponential through the engine dispatch table;
    // that internal choice is covered separately by the differential kernel test.
    out.actualBackend = BackendId::CompilerVectorized;

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::PatchDenoiseResult> ran =
            an::denoiseImagePatches(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                    denoised.data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = restorationNoiseRatio(denoised, noisy, scenario.input.width, scenario.input.height);
        out.counters["patchGroups"] = static_cast<double>(ran->patchGroups);
        out.counters["homogeneousGroups"] = static_cast<double>(ran->homogeneousGroups);
        out.counters["inversionFailures"] = static_cast<double>(ran->inversionFailures);
        out.counters["tileCount"] = static_cast<double>(ran->tileCount);
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        out.counters["finishingRan"] = ran->finishingRan ? 1.0 : 0.0;
        out.counters["finishingPatches"] = static_cast<double>(ran->finishingPatches);
        return Sha256::of(restorationDenoiseText(denoised, noisy, scenario.input.width, scenario.input.height));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runBackground(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                            std::string& skipReason)
{
    const bool automatic = scenario.id == "background.auto_gradient.f32";
    double degreeValue = 2.0;
    double densityValue = 20.0;
    double toleranceValue = 1.0;
    double downsampleValue = 4.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "model", "polynomial") != "polynomial" ||
        parameterValue(scenario, "correction", "subtract") != "subtract" ||
        (automatic && parameterValue(scenario, "protection", "off") != "off") ||
        !doubleParameter(scenario, "degree", 2.0, degreeValue, skipReason) || degreeValue < 1.0 ||
        (!automatic &&
         (!doubleParameter(scenario, "samplesPerLine", 20.0, densityValue, skipReason) || densityValue < 2.0 ||
          !doubleParameter(scenario, "tolerance", 1.0, toleranceValue, skipReason))) ||
        (automatic &&
         (!doubleParameter(scenario, "downsample", 4.0, downsampleValue, skipReason) || downsampleValue < 1.0)))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a background removal this runner does not implement";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const auto sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> sky{reinterpret_cast<const float*>(fixture.bytes.data()), sampleCount};
    const std::vector<float> observed = backgroundGradientFrame(sky, width, height);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    std::vector<float> corrected(sampleCount, 0.0f);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;

    an::AutoBackgroundRequest automaticRequest;
    automaticRequest.model = an::AutoBackgroundModel::Polynomial;
    automaticRequest.polynomialDegree = static_cast<std::uint32_t>(degreeValue);
    automaticRequest.downsample = static_cast<std::uint32_t>(downsampleValue);
    automaticRequest.protectStructures = false;
    automaticRequest.apply.level = an::BackgroundLevel::ModelMedian;
    const an::Result<an::AutoBackgroundPlan> automaticPlan = an::planAutoBackground(automaticRequest, width, height);
    if (automatic && !automaticPlan.hasValue())
    {
        skipReason = describeEngineFailure("the automatic background plan was refused", automaticPlan.status());
        return false;
    }

    an::BackgroundSampleRequest sampling;
    sampling.samplesPerLine = static_cast<std::uint32_t>(densityValue);
    sampling.tolerance = toleranceValue;
    an::BackgroundFitRequest fit;
    fit.degree = static_cast<std::uint32_t>(degreeValue);
    an::BackgroundApplyRequest apply;
    apply.correction = an::BackgroundCorrection::Subtract;
    apply.level = an::BackgroundLevel::SampleMean;

    std::vector<an::BackgroundSample> sites;
    std::vector<double> coefficients;
    if (!automatic)
    {
        const an::Result<an::BackgroundSampleGrid> grid = an::planBackgroundSampleGrid(sampling, width, height);
        if (!grid.hasValue())
        {
            skipReason = describeEngineFailure("the background sample grid was refused", grid.status());
            return false;
        }
        sites.assign(grid->siteCount(), an::BackgroundSample{});
        const an::Result<std::uint32_t> terms = an::backgroundModelTermCount(fit, grid->siteCount());
        if (!terms.hasValue())
        {
            skipReason = describeEngineFailure("the background model was refused", terms.status());
            return false;
        }
        coefficients.assign(*terms, 0.0);
    }

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        if (automatic)
        {
            const an::Result<an::AutoBackgroundResult> ran =
                an::removeImageAutoBackground(*execution, an::SampleType::Float32, observed.data(), source->layout,
                                              corrected.data(), source->layout, *automaticPlan);
            if (!ran.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
                }
                return std::string{};
            }
            if (produceDigest)
            {
                out.counters["iterationsRun"] = static_cast<double>(ran->iterationsRun);
                out.counters["converged"] = ran->converged ? 1.0 : 0.0;
                out.counters["acceptedSamples"] = static_cast<double>(ran->acceptedSamples);
                out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
            }
        }
        else
        {
            const an::Result<an::BackgroundSampleResult> measured = an::sampleImageBackground(
                *execution, an::SampleType::Float32, observed.data(), source->layout, sampling, sites);
            if (!measured.hasValue())
            {
                if (failure.empty())
                {
                    failure =
                        describeEngineFailure("the engine failed to sample '" + scenario.id + "'", measured.status());
                }
                return std::string{};
            }
            const std::span<const an::BackgroundSample> accepted{sites.data(), measured->acceptedCount};
            const an::Result<std::uint32_t> terms =
                an::backgroundModelTermCount(fit, static_cast<std::uint32_t>(accepted.size()));
            if (!terms.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the background model was refused", terms.status());
                }
                return std::string{};
            }
            coefficients.assign(*terms, 0.0);
            const an::Result<an::BackgroundFitResult> fitted =
                an::fitBackgroundModel(*execution, fit, accepted, 0u, width, height, coefficients);
            if (!fitted.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the background fit was refused", fitted.status());
                }
                return std::string{};
            }
            an::BackgroundModelView model;
            model.fit = fit;
            model.sites = accepted;
            model.coefficients = coefficients;
            model.width = width;
            model.height = height;
            const std::array<an::BackgroundModelView, 1u> models{model};
            const an::Result<an::BackgroundApplyResult> ran =
                an::applyImageBackground(*execution, an::SampleType::Float32, observed.data(), source->layout,
                                         corrected.data(), source->layout, apply, models);
            if (!ran.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
                }
                return std::string{};
            }
            if (produceDigest)
            {
                out.counters["acceptedSites"] = static_cast<double>(measured->acceptedCount);
                out.counters["rejectedSites"] = static_cast<double>(measured->rejectedCount);
                out.counters["droppedTerms"] = static_cast<double>(fitted->droppedTerms);
                out.counters["residualRms"] = fitted->residualRootMeanSquare;
                out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
            }
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = backgroundGradientRatio(corrected, observed, width, height);
        return Sha256::of(backgroundText(corrected, width, height));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runDarkStructure(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    double layerValue = 8.0;
    double midtonesValue = 0.8;
    double iterationValue = 1.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || !doubleParameter(scenario, "removedLayers", 8.0, layerValue, skipReason) ||
        !doubleParameter(scenario, "midtones", 0.8, midtonesValue, skipReason) ||
        !doubleParameter(scenario, "iterations", 1.0, iterationValue, skipReason) || iterationValue < 1.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares an enhancement this runner does not implement";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const auto sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> sky{reinterpret_cast<const float*>(fixture.bytes.data()), sampleCount};
    const std::vector<float> observed = darkStructureFrame(sky, width, height);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    an::DarkStructureRequest request;
    request.removedLayers = static_cast<std::uint32_t>(layerValue);
    request.transfer.midtones = midtonesValue;
    request.iterations = static_cast<std::uint32_t>(iterationValue);
    const an::Result<an::DarkStructurePlan> plan = an::planDarkStructure(request);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the dark-structure plan was refused", plan.status());
        return false;
    }

    std::vector<float> enhanced(sampleCount, 0.0f);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::DarkStructureResult> ran =
            an::enhanceImageDarkStructures(*engine, *execution, an::SampleType::Float32, observed.data(),
                                           source->layout, enhanced.data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = darkStructureContrastRatio(enhanced, observed, width, height);
        out.counters["maskDegenerate"] = ran->maskDegenerate ? 1.0 : 0.0;
        out.counters["maskCoefficientsShrunk"] = static_cast<double>(ran->maskCoefficientsShrunk);
        out.counters["samplesChanged"] = static_cast<double>(ran->samplesChanged);
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        return Sha256::of(darkStructureText(enhanced, width, height));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runDeconvolution(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    double fwhm = kRestorationBlurFwhm;
    double iterationValue = 10.0;
    double regularization = 0.001;
    const std::string method = parameterValue(scenario, "method", "richardson-lucy");
    const bool iterative = method == "richardson-lucy";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || (!iterative && method != "wiener") ||
        parameterValue(scenario, "psf", "gaussian") != "gaussian" ||
        parameterValue(scenario, "taper", "off") != "off" ||
        !doubleParameter(scenario, "fwhm", kRestorationBlurFwhm, fwhm, skipReason) ||
        !doubleParameter(scenario, "iterations", 10.0, iterationValue, skipReason) || iterationValue < 1.0 ||
        !doubleParameter(scenario, "regularization", 0.001, regularization, skipReason) || regularization <= 0.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a restoration this runner does not implement";
        }
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const auto sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::span<const float> sharp{reinterpret_cast<const float*>(fixture.bytes.data()), sampleCount};
    std::uint32_t side = 0u;
    const std::vector<float> kernel = restorationBlurKernel(fwhm, side);
    const std::vector<float> observed =
        restorationBlur(sharp, scenario.input.width, scenario.input.height, kernel, side);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    std::vector<float> psf = kernel;
    const std::optional<an::PlaneView<const float>> psfView = an::PlaneView<const float>::create(
        std::span<const float>{psf}, side, side, 1u, static_cast<std::uint64_t>(side) * sizeof(float));
    if (!psfView.has_value())
    {
        skipReason = "the harness kernel does not describe a plane";
        return false;
    }
    an::RichardsonLucyRequest iterativeRequest;
    iterativeRequest.iterations = static_cast<std::uint32_t>(iterationValue);
    iterativeRequest.regularization = an::DeconvolutionRegularization::None;
    iterativeRequest.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    iterativeRequest.common.clampToWhite = false;
    an::WienerRequest directRequest;
    directRequest.regularization = regularization;
    directRequest.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    directRequest.common.clampToWhite = false;

    const an::Result<an::RichardsonLucyPlan> iterativePlan = an::planRichardsonLucy(iterativeRequest, *psfView);
    const an::Result<an::WienerPlan> directPlan = an::planWiener(directRequest, *psfView);
    if (!iterativePlan.hasValue() || !directPlan.hasValue())
    {
        skipReason = describeEngineFailure("the deconvolution plan was refused",
                                           iterativePlan.hasValue() ? directPlan.status() : iterativePlan.status());
        return false;
    }

    std::vector<float> restored(sampleCount, 0.0f);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::DeconvolutionResult> ran =
            iterative ? an::deconvolveImageRichardsonLucy(*execution, an::SampleType::Float32, observed.data(),
                                                          source->layout, restored.data(), source->layout, *psfView,
                                                          *iterativePlan)
                      : an::deconvolveImageWiener(*execution, an::SampleType::Float32, observed.data(), source->layout,
                                                  restored.data(), source->layout, *psfView, *directPlan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = restorationResidualRatio(restored, observed, sharp);
        out.counters["iterationsRun"] = static_cast<double>(ran->iterationsRun);
        out.counters["stoppedEarly"] = ran->stoppedEarly ? 1.0 : 0.0;
        out.counters["fluxScale"] = ran->fluxScale;
        out.counters["usedTransform"] = ran->usedTransform ? 1.0 : 0.0;
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        return Sha256::of(restorationText(restored, sharp, scenario.input.width));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runCalibration(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    // A captured row's frame is sixteen-bit codes, calibrated as the unit-interval light the shared
    // fixture derives from them; the synthetic row calibrates its Float32 noise as it is.
    const bool captured = scenario.id != "calibration.bias_dark_flat.f32";
    if (scenario.input.sampleType != (captured ? SampleType::UInt16 : SampleType::Float32) ||
        (captured && parameterValue(scenario, "light", "") != "codes-over-65536") ||
        scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        parameterValue(scenario, "bias", "binary-pattern-v1") != "binary-pattern-v1" ||
        parameterValue(scenario, "dark", "binary-pattern-v1") != "binary-pattern-v1" ||
        parameterValue(scenario, "flat", "binary-pattern-v1") != "binary-pattern-v1" ||
        parameterValue(scenario, "darkScale", "0.875") != "0.875" ||
        parameterValue(scenario, "flatNormalization", "none") != "none" ||
        parameterValue(scenario, "pedestal", "0") != "0")
    {
        skipReason = "scenario '" + scenario.id + "' declares a calibration this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::vector<float> capturedLight;
    SampleDescriptor lightDescriptor = fixture.descriptor;
    if (captured)
    {
        capturedLight = makeCalibrationFixtureLight(fixture.bytes);
        lightDescriptor.sampleType = SampleType::Float32;
        lightDescriptor.rowStrideBytes = 0u;
        lightDescriptor.planeStrideBytes = 0u;
    }
    const void* const lightData = captured ? static_cast<const void*>(capturedLight.data()) : fixture.bytes.data();
    const std::size_t lightBytes = captured ? capturedLight.size() * sizeof(float) : fixture.bytes.size();
    const FixtureSemantics semantics = defaultFixtureSemantics(lightDescriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(lightDescriptor, semantics, engine->limits(), lightBytes);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(lightDescriptor, semantics, engine->limits(), lightBytes), lightDescriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    CalibrationFixtureMasters masterSamples = makeCalibrationFixtureMasters(sampleCount);
    const an::Result<an::CalibrationFrameView> input =
        an::CalibrationFrameView::create(lightData, lightBytes, source->layout, source->descriptor);
    const an::Result<an::CalibrationFrameView> bias = an::CalibrationFrameView::create(
        masterSamples.bias.data(), masterSamples.bias.size() * sizeof(float), source->layout, source->descriptor);
    const an::Result<an::CalibrationFrameView> dark = an::CalibrationFrameView::create(
        masterSamples.dark.data(), masterSamples.dark.size() * sizeof(float), source->layout, source->descriptor);
    const an::Result<an::CalibrationFrameView> flat = an::CalibrationFrameView::create(
        masterSamples.flat.data(), masterSamples.flat.size() * sizeof(float), source->layout, source->descriptor);
    if (!input.hasValue() || !bias.hasValue() || !dark.hasValue() || !flat.hasValue())
    {
        const an::Status& status = !input.hasValue()  ? input.status()
                                   : !bias.hasValue() ? bias.status()
                                   : !dark.hasValue() ? dark.status()
                                                      : flat.status();
        skipReason = describeEngineFailure("the engine refused a calibration fixture", status);
        return false;
    }
    const an::CalibrationMasters masters{.bias = *bias, .dark = *dark, .flat = *flat};
    const an::CalibrationRequest request{.pedestal = 0.0,
                                         .darkScaling = an::DarkScalingMode::Fixed,
                                         .darkScale = kCalibrationFixtureDarkScale,
                                         .flatNormalization = an::FlatNormalizationMode::None};
    const an::Result<an::CalibrationPlan> plan = an::prepareImageCalibration(*execution, *input, masters, request);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the engine failed to prepare calibration", plan.status());
        return false;
    }
    std::vector<float> destination(sampleCount, 0.0f);

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::CalibrationResult> ran = an::calibrateImage(
            *execution, *plan, *input, masters, destination.data(), destination.size() * sizeof(float), source->layout);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{ran->backend});
        out.correctnessMetric = 0.0;
        out.counters["imagePasses"] = static_cast<double>(ran->imagePasses);
        out.counters["samplesWritten"] = static_cast<double>(ran->samplesWritten);
        out.counters["nonFiniteOutputs"] = static_cast<double>(ran->nonFiniteOutputs);
        out.counters["plannedWorkers"] = static_cast<double>(ran->plannedWorkers);
        out.counters["flatNormalizationGroups"] = static_cast<double>(plan->flatNormalizationGroupCount());
        return Sha256::of(destination.data(), static_cast<std::size_t>(source->layout.totalBytes()));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runGlobalNormalizationFrame(const ScenarioSpec& scenario, const RunOptions& options,
                                                          ResultRecord& out, std::string& skipReason)
{
    const bool real = scenario.id == "normalization.global_frame.real.f32";
    if (scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        scenario.input.sampleType != (real ? SampleType::UInt16 : SampleType::Float32) ||
        parameterValue(scenario, "method", "AdditiveScaling") != "AdditiveScaling" ||
        parameterValue(scenario, "estimator", "IterativeKSigma") != "IterativeKSigma" ||
        parameterValue(scenario, "validity", "zero-is-no-data") != "zero-is-no-data" ||
        parameterValue(scenario, "target", "affine-border-v1") != "affine-border-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a normalization this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(sampleCount) * sizeof(float);
    SampleDescriptor floatDescriptor = fixture.descriptor;
    floatDescriptor.sampleType = SampleType::Float32;
    floatDescriptor.rowStrideBytes = 0u;
    floatDescriptor.planeStrideBytes = 0u;
    const FixtureSemantics semantics = defaultFixtureSemantics(floatDescriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(floatDescriptor, semantics, engine->limits(), frameBytes);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(floatDescriptor, semantics, engine->limits(), frameBytes), floatDescriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    std::vector<float> samples(sampleCount, 0.0f);
    if (real)
    {
        const FixtureSemantics sourceSemantics = defaultFixtureSemantics(fixture.descriptor);
        const std::optional<BridgedFixture> integerSource =
            bridgeFixture(fixture.descriptor, sourceSemantics, engine->limits(), fixture.bytes.size());
        if (!integerSource.has_value())
        {
            skipReason = describeFixtureBridgeFailure(
                checkFixture(fixture.descriptor, sourceSemantics, engine->limits(), fixture.bytes.size()),
                fixture.descriptor);
            return false;
        }
        an::ConversionCounters counters;
        const an::Status converted = an::convertImageSamples(
            *execution, an::SampleType::UInt16, fixture.bytes.data(), integerSource->layout, an::SampleType::Float32,
            samples.data(), source->layout, an::ConversionPolicy{}, counters);
        if (converted.failed())
        {
            skipReason = describeEngineFailure("the engine could not convert the real frame", converted);
            return false;
        }
    }
    else
    {
        std::memcpy(samples.data(), fixture.bytes.data(), static_cast<std::size_t>(frameBytes));
    }
    const NormalizationFixtureFrames frames =
        makeNormalizationFixtureFrames(samples, scenario.input.width, scenario.input.height, 1u);

    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    if (!zeroRule.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not state its validity rule";
        return false;
    }
    const an::KernelValidityView validity{.descriptor = *zeroRule};
    const an::Result<an::NormalizationFrameView> referenceView = an::NormalizationFrameView::create(
        frames.reference.data(), frameBytes, source->layout, source->descriptor, validity);
    if (!referenceView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the reference frame", referenceView.status());
        return false;
    }
    const an::Result<an::NormalizationFrameStatistics> referenceStatistics =
        an::measureGlobalNormalization(*execution, *referenceView, an::ScaleEstimator::IterativeKSigma);
    if (!referenceStatistics.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not measure the reference", referenceStatistics.status());
        return false;
    }
    const an::Result<an::NormalizationReference> reference = an::combineNormalizationReferences(
        std::span<const an::NormalizationFrameStatistics>{&*referenceStatistics, 1u});
    if (!reference.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not combine the reference", reference.status());
        return false;
    }
    std::vector<float> work(sampleCount, 0.0f);
    const an::Result<an::NormalizationFrameView> workView =
        an::NormalizationFrameView::create(work.data(), frameBytes, source->layout, source->descriptor, validity);
    if (!workView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the working frame", workView.status());
        return false;
    }

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::memcpy(work.data(), frames.target.data(), static_cast<std::size_t>(frameBytes));
        const an::Result<an::NormalizationFrameStatistics> measured =
            an::measureGlobalNormalization(*execution, *workView, an::ScaleEstimator::IterativeKSigma);
        const an::Result<an::FrameNormalization> match =
            measured.hasValue()
                ? an::estimateGlobalNormalization(*reference, *measured, an::NormalizationMethod::AdditiveScaling)
                : an::Result<an::FrameNormalization>{measured.status()};
        const an::Result<an::NormalizationApplyResult> applied =
            match.hasValue()
                ? an::applyGlobalNormalization(*execution, *match, *workView, work.data(), frameBytes, source->layout)
                : an::Result<an::NormalizationApplyResult>{match.status()};
        if (!applied.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", applied.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{applied->backend});
        out.correctnessMetric = normalizationRecoveryError(work, frames);
        out.counters["samplesTransformed"] = static_cast<double>(applied->samplesTransformed);
        out.counters["samplesPreserved"] = static_cast<double>(applied->samplesPreserved);
        out.counters["noDataCollisions"] = static_cast<double>(applied->noDataCollisions);
        out.counters["plannedWorkers"] = static_cast<double>(applied->plannedWorkers);
        out.counters["planeScans"] = static_cast<double>(measured->planeScans);
        out.counters["peakScratchBytes"] = static_cast<double>(measured->peakScratchBytes);
        out.counters["clippedSamples"] = static_cast<double>(measured->channels[0].clippedSelection.valid);
        out.counters["compacted"] = measured->compacted ? 1.0 : 0.0;
        out.counters["scale"] = match->channels[0].scale;
        out.counters["offset"] = match->channels[0].offset;
        return Sha256::of(work.data(), static_cast<std::size_t>(frameBytes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runGlobalNormalizationPair(const ScenarioSpec& scenario, const RunOptions& options,
                                                         ResultRecord& out, std::string& skipReason)
{
    const std::string methodName = parameterValue(scenario, "method", "");
    const std::string targetId = parameterValue(scenario, kNormalizationTargetParameter, "");
    std::optional<an::NormalizationMethod> method;
    if (methodName == "Additive")
        method = an::NormalizationMethod::Additive;
    else if (methodName == "Multiplicative")
        method = an::NormalizationMethod::Multiplicative;
    else if (methodName == "AdditiveScaling")
        method = an::NormalizationMethod::AdditiveScaling;
    if (scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        scenario.input.sampleType != SampleType::Float32 || !method.has_value() || targetId.empty() ||
        parameterValue(scenario, "estimator", "") != "MedianAbsoluteDeviation" ||
        parameterValue(scenario, "validity", "") != "zero-is-no-data")
    {
        skipReason = "scenario '" + scenario.id + "' declares a normalization this runner does not implement";
        return false;
    }
    Fixture referenceFixture;
    Fixture targetFixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, referenceFixture, error) ||
        !m_fixtures.acquire(targetId, scenario.seed, scenario.input, targetFixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(sampleCount) * sizeof(float);
    if (referenceFixture.bytes.size() != frameBytes || targetFixture.bytes.size() != frameBytes)
    {
        skipReason = "scenario '" + scenario.id + "' names lights that are not one Float32 plane";
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(referenceFixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(referenceFixture.descriptor, semantics, engine->limits(), frameBytes);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(referenceFixture.descriptor, semantics, engine->limits(), frameBytes),
            referenceFixture.descriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    std::vector<float> reference(sampleCount);
    std::vector<float> target(sampleCount);
    std::memcpy(reference.data(), referenceFixture.bytes.data(), static_cast<std::size_t>(frameBytes));
    std::memcpy(target.data(), targetFixture.bytes.data(), static_cast<std::size_t>(frameBytes));
    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    if (!zeroRule.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not state its validity rule";
        return false;
    }
    const an::KernelValidityView validity{.descriptor = *zeroRule};
    const an::Result<an::NormalizationFrameView> referenceView =
        an::NormalizationFrameView::create(reference.data(), frameBytes, source->layout, source->descriptor, validity);
    if (!referenceView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the reference light", referenceView.status());
        return false;
    }
    // The reference is measured once, before the clock, as an integration measures its reference frame once.
    const an::Result<an::NormalizationFrameStatistics> referenceStatistics =
        an::measureGlobalNormalization(*execution, *referenceView, an::ScaleEstimator::MedianAbsoluteDeviation);
    const an::Result<an::NormalizationReference> combined =
        referenceStatistics.hasValue()
            ? an::combineNormalizationReferences(
                  std::span<const an::NormalizationFrameStatistics>{&*referenceStatistics, 1u})
            : an::Result<an::NormalizationReference>{referenceStatistics.status()};
    if (!combined.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not measure the reference light", combined.status());
        return false;
    }
    std::vector<float> work(sampleCount, 0.0f);
    const an::Result<an::NormalizationFrameView> workView =
        an::NormalizationFrameView::create(work.data(), frameBytes, source->layout, source->descriptor, validity);
    if (!workView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the working light", workView.status());
        return false;
    }

    out.fixtureDigest = Sha256::of(referenceFixture.digest + targetFixture.digest);
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::memcpy(work.data(), target.data(), static_cast<std::size_t>(frameBytes));
        const an::Result<an::NormalizationFrameStatistics> measured =
            an::measureGlobalNormalization(*execution, *workView, an::ScaleEstimator::MedianAbsoluteDeviation);
        const an::Result<an::FrameNormalization> match =
            measured.hasValue() ? an::estimateGlobalNormalization(*combined, *measured, *method)
                                : an::Result<an::FrameNormalization>{measured.status()};
        const an::Result<an::NormalizationApplyResult> applied =
            match.hasValue()
                ? an::applyGlobalNormalization(*execution, *match, *workView, work.data(), frameBytes, source->layout)
                : an::Result<an::NormalizationApplyResult>{match.status()};
        if (!applied.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", applied.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::optional<NormalizationPairMatch> defined =
            normalizationPairMatch(methodName, normalizationPairLevel(reference), normalizationPairLevel(target));
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{applied->backend});
        out.correctnessMetric =
            defined.has_value() ? normalizationPairError(work, target, *defined) : (std::numeric_limits<double>::max)();
        out.counters["samplesTransformed"] = static_cast<double>(applied->samplesTransformed);
        out.counters["samplesPreserved"] = static_cast<double>(applied->samplesPreserved);
        out.counters["noDataCollisions"] = static_cast<double>(applied->noDataCollisions);
        out.counters["scale"] = match->channels[0].scale;
        out.counters["offset"] = match->channels[0].offset;
        if (defined.has_value())
        {
            out.counters["definedScale"] = defined->scale;
            out.counters["definedOffset"] = defined->offset;
        }
        return Sha256::of(work.data(), static_cast<std::size_t>(frameBytes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runGlobalNormalizationApply(const ScenarioSpec& scenario, const RunOptions& options,
                                                          ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "method", "AdditiveScaling") != "AdditiveScaling" ||
        parameterValue(scenario, "scale", "1.03125") != "1.03125" ||
        parameterValue(scenario, "offset", "-0.0078125") != "-0.0078125" ||
        parameterValue(scenario, "validity", "zero-is-no-data") != "zero-is-no-data")
    {
        skipReason = "scenario '" + scenario.id + "' declares a normalization this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(sampleCount) * sizeof(float);
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()), sampleCount};
    const NormalizationFixtureFrames frames =
        makeNormalizationFixtureFrames(fixtureSamples, scenario.input.width, scenario.input.height, 1u);
    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    if (!zeroRule.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not state its validity rule";
        return false;
    }
    std::vector<float> work(sampleCount, 0.0f);
    const an::Result<an::NormalizationFrameView> workView = an::NormalizationFrameView::create(
        work.data(), frameBytes, source->layout, source->descriptor, an::KernelValidityView{.descriptor = *zeroRule});
    if (!workView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the working frame", workView.status());
        return false;
    }
    an::FrameNormalization match;
    match.method = an::NormalizationMethod::AdditiveScaling;
    match.estimator = an::ScaleEstimator::IterativeKSigma;
    match.channelCount = 1u;
    match.channels[0] =
        an::NormalizationChannelCoefficients{.scale = kNormalizationApplyScale, .offset = kNormalizationApplyOffset};

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::memcpy(work.data(), frames.reference.data(), static_cast<std::size_t>(frameBytes));
        const an::Result<an::NormalizationApplyResult> applied =
            an::applyGlobalNormalization(*execution, match, *workView, work.data(), frameBytes, source->layout);
        if (!applied.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", applied.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{applied->backend});
        out.correctnessMetric = 0.0;
        out.counters["samplesTransformed"] = static_cast<double>(applied->samplesTransformed);
        out.counters["samplesPreserved"] = static_cast<double>(applied->samplesPreserved);
        out.counters["noDataCollisions"] = static_cast<double>(applied->noDataCollisions);
        out.counters["plannedWorkers"] = static_cast<double>(applied->plannedWorkers);
        out.counters["imagePasses"] = static_cast<double>(applied->imagePasses);
        return Sha256::of(work.data(), static_cast<std::size_t>(frameBytes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runLocalNormalizationFrame(const ScenarioSpec& scenario, const RunOptions& options,
                                                         ResultRecord& out, std::string& skipReason)
{
    const bool real = scenario.id == "normalization.local_frame.real.f32";
    if (scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        scenario.input.sampleType != (real ? SampleType::UInt16 : SampleType::Float32) ||
        parameterValue(scenario, "grid", "8") != "8" ||
        parameterValue(scenario, "interpolation", "Bicubic") != "Bicubic" ||
        parameterValue(scenario, "estimator", "Percentile") != "Percentile" ||
        parameterValue(scenario, "scale", "ResidualRatio") != "ResidualRatio" ||
        parameterValue(scenario, "validity", "zero-is-no-data") != "zero-is-no-data" ||
        parameterValue(scenario, "target", "gradient-border-v1") != "gradient-border-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a normalization this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(sampleCount) * sizeof(float);
    SampleDescriptor floatDescriptor = fixture.descriptor;
    floatDescriptor.sampleType = SampleType::Float32;
    floatDescriptor.rowStrideBytes = 0u;
    floatDescriptor.planeStrideBytes = 0u;
    const FixtureSemantics semantics = defaultFixtureSemantics(floatDescriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(floatDescriptor, semantics, engine->limits(), frameBytes);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(floatDescriptor, semantics, engine->limits(), frameBytes), floatDescriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    std::vector<float> samples(sampleCount, 0.0f);
    if (real)
    {
        const FixtureSemantics sourceSemantics = defaultFixtureSemantics(fixture.descriptor);
        const std::optional<BridgedFixture> integerSource =
            bridgeFixture(fixture.descriptor, sourceSemantics, engine->limits(), fixture.bytes.size());
        if (!integerSource.has_value())
        {
            skipReason = describeFixtureBridgeFailure(
                checkFixture(fixture.descriptor, sourceSemantics, engine->limits(), fixture.bytes.size()),
                fixture.descriptor);
            return false;
        }
        an::ConversionCounters counters;
        const an::Status converted = an::convertImageSamples(
            *execution, an::SampleType::UInt16, fixture.bytes.data(), integerSource->layout, an::SampleType::Float32,
            samples.data(), source->layout, an::ConversionPolicy{}, counters);
        if (converted.failed())
        {
            skipReason = describeEngineFailure("the engine could not convert the real frame", converted);
            return false;
        }
    }
    else
    {
        std::memcpy(samples.data(), fixture.bytes.data(), static_cast<std::size_t>(frameBytes));
    }
    const NormalizationFixtureFrames frames = makeLocalNormalizationFixtureFrames(
        samples, scenario.input.width, scenario.input.height, 1u,
        real ? kLocalNormalizationRealFixtureGradient : kLocalNormalizationFixtureGradient);

    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    if (!zeroRule.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not state its validity rule";
        return false;
    }
    const an::KernelValidityView validity{.descriptor = *zeroRule};
    const an::Result<an::NormalizationFrameView> referenceView = an::NormalizationFrameView::create(
        frames.reference.data(), frameBytes, source->layout, source->descriptor, validity);
    if (!referenceView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the reference frame", referenceView.status());
        return false;
    }
    an::LocalNormalizationRequest request;
    request.gridSize = kLocalNormalizationFixtureGridSize;
    request.interpolation = an::LocalGridInterpolation::Bicubic;
    request.estimator = an::LocalBackgroundEstimator::Percentile;
    request.scaleMode = an::LocalScaleMode::Linked;
    const an::Result<an::LocalNormalizationPlan> plan =
        an::planLocalNormalization(request, scenario.input.width, scenario.input.height, 1u);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the local-normalization request", plan.status());
        return false;
    }
    std::vector<double> referenceGrid(static_cast<std::size_t>(plan->shape().valueCount()), 0.0);
    const an::Result<an::LocalBackgroundFitResult> referenceFit =
        an::fitLocalBackgroundGrid(*execution, *referenceView, *plan, referenceGrid);
    if (!referenceFit.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not fit the reference background", referenceFit.status());
        return false;
    }
    std::vector<float> work(sampleCount, 0.0f);
    const an::Result<an::NormalizationFrameView> workView =
        an::NormalizationFrameView::create(work.data(), frameBytes, source->layout, source->descriptor, validity);
    if (!workView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the working frame", workView.status());
        return false;
    }
    std::vector<double> targetGrid(referenceGrid.size(), 0.0);

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::memcpy(work.data(), frames.target.data(), static_cast<std::size_t>(frameBytes));
        const an::Result<an::LocalBackgroundFitResult> fitted =
            an::fitLocalBackgroundGrid(*execution, *workView, *plan, targetGrid);
        const an::Result<an::LocalScaleEstimate> scale =
            fitted.hasValue()
                ? an::estimateLocalScale(*execution, *workView, targetGrid, *referenceView, referenceGrid, *plan)
                : an::Result<an::LocalScaleEstimate>{fitted.status()};
        an::Result<an::LocalNormalizationApplyResult> applied{scale.status()};
        if (scale.hasValue())
        {
            const an::LocalNormalizationApplyRequest apply{.shape = plan->shape(),
                                                           .interpolation = request.interpolation,
                                                           .targetBackground = targetGrid,
                                                           .referenceBackground = referenceGrid,
                                                           .scale = std::span<const double>{scale->scale.data(), 1u},
                                                           .scaleGrid = {}};
            applied =
                an::applyLocalNormalization(*execution, apply, *workView, work.data(), frameBytes, source->layout);
        }
        if (!applied.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", applied.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{applied->backend});
        out.correctnessMetric = normalizationRecoveryError(work, frames);
        out.counters["samplesTransformed"] = static_cast<double>(applied->samplesTransformed);
        out.counters["samplesPreserved"] = static_cast<double>(applied->samplesPreserved);
        out.counters["noDataCollisions"] = static_cast<double>(applied->noDataCollisions);
        out.counters["plannedWorkers"] = static_cast<double>(applied->plannedWorkers);
        out.counters["applyWorkingBytes"] = static_cast<double>(applied->workingBytes);
        out.counters["fitSamplesRead"] = static_cast<double>(fitted->samplesRead);
        out.counters["fitLowSampleNodes"] = static_cast<double>(fitted->lowSampleNodes);
        out.counters["fitExpansionFallbacks"] = static_cast<double>(fitted->expansionFallbacks);
        out.counters["fitNeighbourFallbacks"] = static_cast<double>(fitted->neighbourFallbacks);
        out.counters["fitUnresolvedNodes"] = static_cast<double>(fitted->unresolvedNodes);
        out.counters["fitWorkingBytes"] = static_cast<double>(fitted->workingBytes);
        out.counters["scale"] = scale->scale[0];
        out.counters["scaleRatiosUsed"] = static_cast<double>(scale->ratiosUsed[0]);
        return Sha256::of(work.data(), static_cast<std::size_t>(frameBytes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runLocalNormalizationApply(const ScenarioSpec& scenario, const RunOptions& options,
                                                         ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "grid", "8") != "8" ||
        parameterValue(scenario, "interpolation", "Bicubic") != "Bicubic" ||
        parameterValue(scenario, "scale", "1.03125") != "1.03125" ||
        parameterValue(scenario, "grids", "plane-v1") != "plane-v1" ||
        parameterValue(scenario, "validity", "zero-is-no-data") != "zero-is-no-data")
    {
        skipReason = "scenario '" + scenario.id + "' declares a normalization this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(sampleCount) * sizeof(float);
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()), sampleCount};
    const NormalizationFixtureFrames frames =
        makeNormalizationFixtureFrames(fixtureSamples, scenario.input.width, scenario.input.height, 1u);
    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    if (!zeroRule.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not state its validity rule";
        return false;
    }
    std::vector<float> work(sampleCount, 0.0f);
    const an::Result<an::NormalizationFrameView> workView = an::NormalizationFrameView::create(
        work.data(), frameBytes, source->layout, source->descriptor, an::KernelValidityView{.descriptor = *zeroRule});
    if (!workView.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the working frame", workView.status());
        return false;
    }
    const std::vector<double> targetGrid = makeLocalNormalizationFixtureGrid(
        true, kLocalNormalizationFixtureGridSize, scenario.input.width, scenario.input.height);
    const std::vector<double> referenceGrid = makeLocalNormalizationFixtureGrid(
        false, kLocalNormalizationFixtureGridSize, scenario.input.width, scenario.input.height);
    const double scale = kNormalizationApplyScale;
    const an::LocalNormalizationApplyRequest apply{
        .shape = an::LocalNormalizationGridShape{.gridSize = kLocalNormalizationFixtureGridSize,
                                                 .width = scenario.input.width,
                                                 .height = scenario.input.height,
                                                 .channelCount = 1u},
        .interpolation = an::LocalGridInterpolation::Bicubic,
        .targetBackground = targetGrid,
        .referenceBackground = referenceGrid,
        .scale = std::span<const double>{&scale, 1u},
        .scaleGrid = {}};

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::memcpy(work.data(), frames.reference.data(), static_cast<std::size_t>(frameBytes));
        const an::Result<an::LocalNormalizationApplyResult> applied =
            an::applyLocalNormalization(*execution, apply, *workView, work.data(), frameBytes, source->layout);
        if (!applied.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", applied.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{applied->backend});
        out.correctnessMetric =
            localNormalizationApplyError(work, frames.reference, scenario.input.width, scenario.input.height, scale);
        out.counters["samplesTransformed"] = static_cast<double>(applied->samplesTransformed);
        out.counters["samplesPreserved"] = static_cast<double>(applied->samplesPreserved);
        out.counters["noDataCollisions"] = static_cast<double>(applied->noDataCollisions);
        out.counters["plannedWorkers"] = static_cast<double>(applied->plannedWorkers);
        out.counters["imagePasses"] = static_cast<double>(applied->imagePasses);
        out.counters["workingBytes"] = static_cast<double>(applied->workingBytes);
        return Sha256::of(work.data(), static_cast<std::size_t>(frameBytes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runPhotometricWeighting(const ScenarioSpec& scenario, const RunOptions& options,
                                                      ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != 4096u || scenario.input.height != 1u ||
        parameterValue(scenario, "model", "zero-point-confidence-alignment-v1") !=
            "zero-point-confidence-alignment-v1" ||
        parameterValue(scenario, "uncertaintyPenaltyPerMagnitude", "5") != "5" ||
        parameterValue(scenario, "alignmentPenalty", "4") != "4" ||
        parameterValue(scenario, "minimumRawWeight", "0.01") != "0.01" ||
        parameterValue(scenario, "fixtureMapping", "photometric-frame-v1") != "photometric-frame-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a photometric model this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> source{reinterpret_cast<const float*>(fixture.bytes.data()),
                                        fixture.bytes.size() / sizeof(float)};
    const std::vector<PhotometricWeightingFixtureSample> fixtureMeasurements = makePhotometricWeightingFixture(source);
    std::vector<an::PhotometricFrameMeasurement> measurements;
    measurements.reserve(fixtureMeasurements.size());
    for (const PhotometricWeightingFixtureSample& frame : fixtureMeasurements)
    {
        measurements.push_back(an::PhotometricFrameMeasurement{
            .zeroPointMagnitudes = frame.zeroPointMagnitudes,
            .zeroPointStandardUncertaintyMagnitudes = frame.zeroPointStandardUncertaintyMagnitudes,
            .astrometricRootMeanSquarePixels = frame.astrometricRootMeanSquarePixels});
    }
    std::vector<an::PhotometricStackWeight> weights(measurements.size());

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::PhotometricWeightingResult> result =
            an::computePhotometricStackWeights(*execution, measurements, {}, weights);
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = 0.0;
        out.counters["measuredFrames"] = static_cast<double>(result->measuredFrameCount);
        out.counters["medianSubstitutions"] = static_cast<double>(result->medianSubstitutionCount);
        out.counters["minimumApplied"] = static_cast<double>(result->minimumAppliedCount);
        out.counters["uncertaintyComplete"] = result->uncertaintyComplete ? 1.0 : 0.0;
        out.counters["effectiveFrameCount"] = result->effectiveFrameCount;
        out.counters["workingBytes"] = static_cast<double>(result->workingBytes);
        Sha256 digest;
        for (const an::PhotometricStackWeight& weight : weights)
        {
            digest.update(&weight.value, sizeof(weight.value));
        }
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

namespace
{

/// The engine's spelling of a declared combination, or nothing.
[[nodiscard]] std::optional<an::StackCombination> stackCombinationOf(std::string_view method) noexcept
{
    if (method == "Average")
        return an::StackCombination::Average;
    if (method == "Sum")
        return an::StackCombination::Sum;
    if (method == "Median")
        return an::StackCombination::Median;
    if (method == "Minimum")
        return an::StackCombination::Minimum;
    if (method == "Maximum")
        return an::StackCombination::Maximum;
    if (method == "AutoAdaptiveWeightedAverage")
        return an::StackCombination::AutoAdaptiveWeightedAverage;
    return std::nullopt;
}

/// The engine's spelling of a declared rejection family, or nothing.
[[nodiscard]] std::optional<an::StackRejection> stackRejectionOf(std::string_view rejection) noexcept
{
    if (rejection == "None")
        return an::StackRejection::None;
    if (rejection == "SigmaClipping")
        return an::StackRejection::SigmaClipping;
    if (rejection == "MadSigmaClipping")
        return an::StackRejection::MadSigmaClipping;
    if (rejection == "MedianSigmaReplacement")
        return an::StackRejection::MedianSigmaReplacement;
    if (rejection == "WinsorizedSigmaClipping")
        return an::StackRejection::WinsorizedSigmaClipping;
    if (rejection == "LinearFitClipping")
        return an::StackRejection::LinearFitClipping;
    if (rejection == "PercentileClipping")
        return an::StackRejection::PercentileClipping;
    if (rejection == "GeneralizedEsdTest")
        return an::StackRejection::GeneralizedEsdTest;
    return std::nullopt;
}

} // namespace

bool AfterNightEngineAdapter::runStackScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    const std::optional<StackScenarioDefinition> definition = stackingModeScenarioDefinition(scenario);
    const std::optional<an::StackCombination> combination =
        definition.has_value() ? stackCombinationOf(definition->method) : std::nullopt;
    const std::optional<an::StackRejection> rejection =
        definition.has_value() ? stackRejectionOf(definition->rejection) : std::nullopt;
    const std::string_view weighting = definition.has_value() ? definition->weighting : "";
    const std::string_view normalization = definition.has_value() ? definition->normalization : "";
    if (!definition.has_value() || !combination.has_value() || !rejection.has_value() ||
        (normalization != "None" && normalization != "AdditiveGlobal"))
    {
        skipReason = "scenario '" + scenario.id + "' declares a stack this runner does not implement";
        return false;
    }
    const std::uint32_t frameCount = definition->frames;
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::size_t sampleCount = static_cast<std::size_t>(width) * height;

    // The frames: every one resident, so the scenario measures the stack rather than the
    // filesystem. A plain row takes each frame from its own seed; a derived row derives every
    // frame from the first seed's fixture through the shared mapping, and reports the digest of
    // the derived set so that every run proves it stacked the same bytes.
    std::string error;
    const bool planted = definition->mapping == StackFrameMapping::PlantedOutliersOnCorpus;
    const bool distinct = definition->mapping == StackFrameMapping::PerSeed ||
                          definition->mapping == StackFrameMapping::PerCorpusFrame || planted;
    std::vector<Fixture> fixtures(distinct ? frameCount : 1u);
    std::vector<std::vector<float>> derived;
    std::vector<std::string> fixtureDigests;
    std::vector<std::span<const float>> frameSpans;
    fixtureDigests.reserve(frameCount);
    frameSpans.reserve(frameCount);
    for (std::uint32_t index = 0u; index < fixtures.size(); ++index)
    {
        if (!m_fixtures.acquire(stackScenarioFixtureId(scenario, index), scenario.seed + index, scenario.input,
                                fixtures[index], error))
        {
            skipReason = error;
            return false;
        }
    }
    if (planted)
    {
        // Each captured frame plants its own band, through the shared mapping, as the derived rows do.
        derived.reserve(frameCount);
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            const std::span<const float> capture(reinterpret_cast<const float*>(fixtures[index].bytes.data()),
                                                 sampleCount);
            derived.push_back(deriveStackFrame(definition->mapping, capture, index, width, height));
            fixtureDigests.push_back(Sha256::of(derived.back().data(), derived.back().size() * sizeof(float)));
            frameSpans.emplace_back(derived.back());
        }
    }
    else if (distinct)
    {
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            fixtureDigests.push_back(fixtures[index].digest);
            frameSpans.emplace_back(reinterpret_cast<const float*>(fixtures[index].bytes.data()), sampleCount);
        }
    }
    else
    {
        const std::span<const float> base(reinterpret_cast<const float*>(fixtures[0].bytes.data()), sampleCount);
        derived.reserve(frameCount);
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            derived.push_back(deriveStackFrame(definition->mapping, base, index, width, height));
            fixtureDigests.push_back(Sha256::of(derived.back().data(), derived.back().size() * sizeof(float)));
            frameSpans.emplace_back(derived.back());
        }
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixtures[0].descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixtures[0].descriptor, semantics, engine->limits(), fixtures[0].bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixtures[0].descriptor, semantics, engine->limits(), fixtures[0].bytes.size()),
            fixtures[0].descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    // Exact zero is not a measurement in these rows, and the run states that rule.
    const std::optional<an::ValidityDescriptor> zeroRule =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    if (!zeroRule.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not state its validity rule";
        return false;
    }
    an::StackRequest request{.input =
                                 an::StackInputDescription{.frameCount = frameCount, .descriptor = source->descriptor},
                             .combination = *combination};
    request.rejection.method = *rejection;
    if (weighting == "Explicit")
    {
        request.weighting.method = an::StackWeighting::Explicit;
    }
    else if (weighting == "Noise")
        request.weighting.method = an::StackWeighting::Noise;
    else if (weighting == "Fwhm")
        request.weighting.method = an::StackWeighting::Fwhm;
    else if (weighting == "ImageCount")
        request.weighting.method = an::StackWeighting::ImageCount;
    else if (weighting == "Photometric")
        request.weighting.method = an::StackWeighting::Photometric;
    else if (weighting.starts_with("StarCount"))
    {
        request.weighting.method = an::StackWeighting::StarCount;
        request.weighting.starCount.scaling = weighting == "StarCountLinear"        ? an::StarCountScaling::Linear
                                              : weighting == "StarCountExponential" ? an::StarCountScaling::Exponential
                                              : weighting == "StarCountRoundness"
                                                  ? an::StarCountScaling::RoundnessWeighted
                                                  : an::StarCountScaling::Quadratic;
    }
    request.localWeighting.enabled = weighting == "Local";
    if (normalization == "AdditiveGlobal")
    {
        request.normalization.scope = an::StackNormalizationScope::Global;
        request.normalization.globalMethod = an::NormalizationMethod::Additive;
        request.normalization.scaleEstimator = an::ScaleEstimator::IterativeKSigma;
        request.normalization.referenceFrame = 0u;
    }
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    if (!model.has_value())
    {
        skipReason = std::string("the stack model refused the scenario: ") +
                     an::stackModelErrorName(an::StackModel::check(request));
        return false;
    }
    std::vector<an::StackSourceBand> bands;
    bands.reserve(frameCount);
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        bands.push_back(
            an::StackSourceBand{.sampleType = an::SampleType::Float32,
                                .samples = frameSpans[index].data(),
                                .layout = &source->layout,
                                .frameIndex = index,
                                .firstRow = 0u,
                                .validity = an::StackBandValidity{.descriptor = *zeroRule, .mask = nullptr}});
    }
    // A resident frame serves any window at no cost, so the source states region access and the
    // planner walks the frames tile-major in cache-resident bands - which is the access a consumer
    // holding decoded frames would state, and the drizzle rows state the same.
    an::Result<an::ResidentStackFrameSource> frames =
        an::ResidentStackFrameSource::create(bands, an::StackSourceAccess::Region);
    if (!frames.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the resident frames", frames.status());
        return false;
    }
    // A row that plants outliers in captured lights is held to the same request over the lights as captured.
    std::vector<an::StackSourceBand> cleanBands;
    std::optional<an::ResidentStackFrameSource> cleanFrames;
    if (planted)
    {
        cleanBands = bands;
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            cleanBands[index].samples = fixtures[index].bytes.data();
        }
        an::Result<an::ResidentStackFrameSource> clean =
            an::ResidentStackFrameSource::create(cleanBands, an::StackSourceAccess::Region);
        if (!clean.hasValue())
        {
            skipReason = describeEngineFailure("the engine refused the captured frames", clean.status());
            return false;
        }
        cleanFrames.emplace(std::move(*clean));
    }
    std::vector<double> weights;
    if (weighting == "Explicit")
    {
        weights.reserve(frameCount);
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            weights.push_back(stackExplicitWeight(index));
        }
    }
    an::StackExecutionRequest executionRequest;
    executionRequest.maximumTileRows = definition->tileRows;
    executionRequest.explicitWeights = an::StackFrameWeights{.perFrame = weights};
    std::vector<an::StackFrameMeasurement> measurements(frameCount);
    for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
    {
        const auto m = stackScenarioMeasurement(frame);
        measurements[frame].starCount = m.stars;
        measurements[frame].medianRoundness = m.roundness;
        measurements[frame].fullWidth = m.fwhm;
        measurements[frame].backgroundNoise = m.noise;
        measurements[frame].integratedFrameCount = m.integratedFrames;
        measurements[frame].photometric.zeroPointMagnitudes = m.zeroPoint;
    }
    executionRequest.measurements = measurements;

    out.fixtureDigest = digestFixtureSet(fixtureDigests);
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        an::Result<an::StackResult> stacked =
            an::executeStack(*engine, *execution, *model, *frames, an::StackSourceContract{}, executionRequest,
                             an::StackExecutionOutputs{});
        if (!stacked.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", stacked.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const an::StackExecutionReport& report = stacked->report;
        if ((weighting == "Local" && !report.localWeightsMeasured) ||
            (request.weighting.method != an::StackWeighting::Uniform &&
             request.weighting.method != an::StackWeighting::Explicit &&
             report.weighting.disposition != an::StackFrameWeightingDisposition::Computed))
        {
            failure = "the declared stack weighting was not executed";
            return std::string{};
        }
        out.actualBackend = toBenchBackend(an::Result<an::BackendSelection>{report.backend});
        out.counters["frameReads"] = static_cast<double>(report.frameReads);
        out.counters["tilesCombined"] = static_cast<double>(report.tilesCombined);
        out.counters["tileRows"] = static_cast<double>(report.plan.tile.rows);
        out.counters["passesRun"] = static_cast<double>(report.passesRun);
        out.counters["samplesAdmitted"] = static_cast<double>(report.samplesAdmitted);
        out.counters["samplesExcludedByValidity"] = static_cast<double>(report.samplesExcludedByValidity);
        out.counters["rejectedLow"] = static_cast<double>(report.rejectedLow);
        out.counters["rejectedHigh"] = static_cast<double>(report.rejectedHigh);
        out.counters["elementsWithSamples"] = static_cast<double>(report.elementsWithSamples);
        out.counters["peakHeldBytes"] = static_cast<double>(report.memory.peakHeldBytes);
        out.counters["plannedPeakBytes"] = static_cast<double>(report.plan.memory.peakBytes);
        out.counters["compiledRowUsed"] = report.compiledRowUsed ? 1.0 : 0.0;
        const std::optional<an::ImageLayout> layout = stacked->image.layout();
        if (!layout.has_value())
        {
            failure = "the stack handed over no image";
            return std::string{};
        }
        const std::span<const float> resultSamples(static_cast<const float*>(stacked->image.data()), sampleCount);
        if (planted)
        {
            an::Result<an::StackResult> clean =
                an::executeStack(*engine, *execution, *model, *cleanFrames, an::StackSourceContract{}, executionRequest,
                                 an::StackExecutionOutputs{});
            if (!clean.hasValue())
            {
                failure = describeEngineFailure("the engine failed to stack the captured frames", clean.status());
                return std::string{};
            }
            std::vector<std::span<const float>> captured;
            for (std::uint32_t index = 0u; index < frameCount; ++index)
            {
                captured.emplace_back(reinterpret_cast<const float*>(fixtures[index].bytes.data()), sampleCount);
            }
            const PlantedStackMovement movement = plantedStackMovement(
                resultSamples, std::span<const float>(static_cast<const float*>(clean->image.data()), sampleCount),
                frameSpans, captured, kStackHighOutlier, kStackHighOutlier / 2.0f);
            out.correctnessMetric = movement.fraction;
            out.counters["plantedElementsJudged"] = static_cast<double>(movement.judged);
            out.counters["plantedElementsMoved"] = static_cast<double>(movement.moved);
            out.counters["plantedLargestMovement"] = movement.largest;
            out.counters["cleanRejectedLow"] = static_cast<double>(clean->report.rejectedLow);
            out.counters["cleanRejectedHigh"] = static_cast<double>(clean->report.rejectedHigh);
        }
        else
        {
            out.correctnessMetric = stackScenarioResidual(*definition, frameSpans, width, height, resultSamples);
        }
        return Sha256::of(stacked->image.data(), static_cast<std::size_t>(layout->totalBytes()));
    };
    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runAperturePhotometry(const ScenarioSpec& scenario, const RunOptions& options,
                                                    ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u || scenario.input.width != kAperturePhotometryFixtureWidth ||
        scenario.input.height != kAperturePhotometryFixtureHeight ||
        parameterValue(scenario, "fixtureMapping", kAperturePhotometryFixtureMapping) !=
            kAperturePhotometryFixtureMapping ||
        parameterValue(scenario, "apertureRadius", "4") != "4" ||
        parameterValue(scenario, "annulus", "8-14") != "8-14" ||
        parameterValue(scenario, "neighbors", "none") != "none")
    {
        skipReason = "scenario '" + scenario.id + "' declares an aperture measurement this runner does not implement";
        return false;
    }
    const AperturePhotometryFixture fixture = makeAperturePhotometryFixture(scenario.seed);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
        kAperturePhotometryFixtureWidth, kAperturePhotometryFixtureHeight, 3u, engine->limits());
    if (!extent.has_value())
    {
        skipReason = "the planted aperture field exceeds the engine's extent limits";
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Rgb,
                                                               .colorSpace = an::ColorSpace::LinearRgb,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    const std::optional<an::ImageLayoutRequest> layoutRequest =
        descriptor.has_value() ? an::layoutRequestFor(*descriptor, an::StorageArrangement::Planar, 1u) : std::nullopt;
    const std::optional<an::ImageLayout> layout =
        layoutRequest.has_value() ? an::ImageLayout::create(*layoutRequest, engine->limits()) : std::nullopt;
    if (!descriptor.has_value() || !layout.has_value())
    {
        skipReason = "the planted aperture field could not be described to the engine";
        return false;
    }
    const std::uint64_t frameBytes = fixture.samples.size() * sizeof(float);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(fixture.samples.data(), frameBytes, *layout, *descriptor);
    if (!frame.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the planted aperture field", frame.status());
        return false;
    }
    std::vector<an::ApertureTarget> targets;
    targets.reserve(fixture.stars.size());
    for (const AperturePhotometryFixtureStar& star : fixture.stars)
    {
        targets.push_back(
            an::ApertureTarget{.position = {.x = star.x, .y = star.y}, .fullWidthPixels = star.fullWidth()});
    }
    an::AperturePhotometryRequest request;
    request.radiusPolicy = an::ApertureRadiusPolicy::FixedPixels;
    request.annulusPolicy = an::AnnulusRadiusPolicy::FixedPixels;
    request.apertureRadiusPixels = kAperturePhotometryFixtureApertureRadius;
    request.annulusInnerRadiusPixels = kAperturePhotometryFixtureAnnulusInner;
    request.annulusOuterRadiusPixels = kAperturePhotometryFixtureAnnulusOuter;
    request.rejectBlends = false;
    std::vector<an::AperturePhotometryMeasurement> measurements(targets.size());

    out.fixtureDigest = Sha256::of(fixture.samples.data(), frameBytes);
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::AperturePhotometryBatchResult> result = an::measureAperturePhotometryBatch(
            *execution, *frame, targets, request, an::ApertureNeighborPolicy::None, measurements);
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        std::vector<double> fluxes(targets.size() * 3u, 0.0);
        std::vector<bool> accepted(targets.size(), false);
        Sha256 digest;
        for (std::size_t index = 0u; index < measurements.size(); ++index)
        {
            const an::AperturePhotometryMeasurement& measurement = measurements[index];
            const auto rejection = static_cast<std::uint8_t>(measurement.rejection);
            digest.update(&rejection, sizeof(rejection));
            accepted[index] = measurement.rejection == an::ApertureRejectionReason::None;
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                fluxes[index * 3u + channel] = measurement.channels[channel].flux;
                digest.update(&measurement.channels[channel].flux, sizeof(double));
                digest.update(&measurement.channels[channel].localBackground, sizeof(double));
            }
        }
        out.correctnessMetric = scoreAperturePhotometry(fixture, fluxes, accepted);
        out.counters["stars"] = static_cast<double>(targets.size());
        out.counters["accepted"] = static_cast<double>(result->acceptedCount);
        out.counters["rejected"] = static_cast<double>(result->rejectedCount);
        out.counters["workingBytes"] = static_cast<double>(result->workingBytes);
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runColorFit(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                          std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != 4096u || scenario.input.height != 1u ||
        parameterValue(scenario, "fixtureMapping", kColorFitFixtureMapping) != kColorFitFixtureMapping ||
        parameterValue(scenario, "model", "pcc-gaia-bp-rp-v1") != "pcc-gaia-bp-rp-v1" ||
        parameterValue(scenario, "minimumSamples", "10") != "10")
    {
        skipReason = "scenario '" + scenario.id + "' declares a colour fit this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> source{reinterpret_cast<const float*>(fixture.bytes.data()),
                                        fixture.bytes.size() / sizeof(float)};
    const std::vector<ColorFitFixtureSample> fixtureSamples = makeColorFitFixture(source);
    std::vector<an::PhotometricColorSample> samples;
    samples.reserve(fixtureSamples.size());
    for (const ColorFitFixtureSample& sample : fixtureSamples)
    {
        samples.push_back(an::PhotometricColorSample{.catalogMagnitude = sample.magnitudeG,
                                                     .colorIndexBpMinusRp = sample.colorIndexBpMinusRp,
                                                     .redFlux = sample.redFlux,
                                                     .greenFlux = sample.greenFlux,
                                                     .blueFlux = sample.blueFlux});
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::PhotometricColorFitResult> result =
            an::fitPhotometricColorGains(*execution, samples, an::PhotometricColorFitRequest{.minimumSamples = 10u});
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = scoreColorFit(result->gains[0], result->gains[2]);
        out.counters["usableSamples"] = static_cast<double>(result->usableCount);
        out.counters["coloredSamples"] = static_cast<double>(result->coloredCount);
        out.counters["rmsLogResidual"] = result->rmsLogResidual;
        Sha256 digest;
        for (const double gain : result->gains)
        {
            digest.update(&gain, sizeof(gain));
        }
        digest.update(&result->rmsLogResidual, sizeof(double));
        const std::uint64_t used = result->usableCount;
        digest.update(&used, sizeof(used));
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runSyntheticPhotometry(const ScenarioSpec& scenario, const RunOptions& options,
                                                     ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != 4096u || scenario.input.height != 1u ||
        parameterValue(scenario, "fixtureMapping", kSyntheticPhotometryFixtureMapping) !=
            kSyntheticPhotometryFixtureMapping ||
        parameterValue(scenario, "recipe", "spcc-synthetic-photometry-v2") != "spcc-synthetic-photometry-v2" ||
        parameterValue(scenario, "response", "constant-2-1-0.5") != "constant-2-1-0.5" ||
        parameterValue(scenario, "spectra", std::to_string(kSyntheticPhotometryFixtureSpectra).c_str()) !=
            std::to_string(kSyntheticPhotometryFixtureSpectra))
    {
        skipReason = "scenario '" + scenario.id + "' declares a synthetic photometry this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> source{reinterpret_cast<const float*>(fixture.bytes.data()),
                                        fixture.bytes.size() / sizeof(float)};
    const SyntheticPhotometryFixture spectra =
        makeSyntheticPhotometryFixture(source, kSyntheticPhotometryFixtureSpectra);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    an::ResponseProfile profile;
    profile.identifier = "bench.constant";
    profile.version = "v1";
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        profile.channels[channel].identifier = "bench.constant." + std::to_string(channel);
        profile.channels[channel].version = "v1";
        profile.channels[channel].samples = {an::SpectralSample{.wavelengthNm = kSpectrophotometricFixtureStartNm,
                                                                .value = kSyntheticPhotometryFixtureResponse[channel]},
                                             an::SpectralSample{.wavelengthNm = kSpectrophotometricFixtureEndNm,
                                                                .value = kSyntheticPhotometryFixtureResponse[channel]}};
    }
    const an::Result<an::PreparedResponse> prepared = an::prepareResponse(*execution, profile);
    if (!prepared.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the fixture response", prepared.status());
        return false;
    }
    std::vector<an::SyntheticPhotometry> predictions(spectra.count);

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::SyntheticPhotometryBatchResult> result =
            an::integrateSyntheticPhotometryTable(*execution, *prepared, spectra.flux, spectra.fluxError, predictions);
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        std::vector<double> counts(spectra.count * 3u, 0.0);
        std::vector<double> uncertainties(spectra.count * 3u, 0.0);
        Sha256 digest;
        for (std::size_t spectrum = 0u; spectrum < spectra.count; ++spectrum)
        {
            const an::SyntheticPhotometry& prediction = predictions[spectrum];
            const auto status = static_cast<std::uint8_t>(prediction.status);
            digest.update(&status, sizeof(status));
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                counts[spectrum * 3u + channel] = prediction.counts[channel];
                uncertainties[spectrum * 3u + channel] = prediction.uncertainties[channel];
                digest.update(&prediction.counts[channel], sizeof(double));
                digest.update(&prediction.uncertainties[channel], sizeof(double));
                digest.update(&prediction.relativeToGreen[channel], sizeof(double));
            }
        }
        out.correctnessMetric = scoreSyntheticPhotometry(spectra, counts, uncertainties);
        out.counters["spectra"] = static_cast<double>(spectra.count);
        out.counters["integrated"] = static_cast<double>(result->integratedCount);
        out.counters["workingBytes"] = static_cast<double>(result->workingBytes);
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runSpectrophotometricFit(const ScenarioSpec& scenario, const RunOptions& options,
                                                       ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != 4096u || scenario.input.height != 1u ||
        parameterValue(scenario, "fixtureMapping", kSpectrophotometricFitFixtureMapping) !=
            kSpectrophotometricFitFixtureMapping ||
        parameterValue(scenario, "policy", "iterative-sigma-clip-refit") != "iterative-sigma-clip-refit" ||
        parameterValue(scenario, "minimumSamples", "35") != "35")
    {
        skipReason = "scenario '" + scenario.id + "' declares a spectrophotometric fit this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> source{reinterpret_cast<const float*>(fixture.bytes.data()),
                                        fixture.bytes.size() / sizeof(float)};
    const std::vector<SpectrophotometricFitFixtureSample> fixtureSamples = makeSpectrophotometricFitFixture(source);
    std::vector<an::SpectrophotometricSample> samples;
    samples.reserve(fixtureSamples.size());
    for (std::size_t index = 0u; index < fixtureSamples.size(); ++index)
    {
        const SpectrophotometricFitFixtureSample& sample = fixtureSamples[index];
        an::SpectrophotometricSample converted;
        converted.sourceIdentifier = index + 1u;
        converted.accepted = true;
        converted.catalogRelativeToGreen = {sample.catalogRed, 1.0, sample.catalogBlue};
        converted.imageRelativeToGreen = {sample.imageRed, 1.0, sample.imageBlue};
        samples.push_back(converted);
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::SpectrophotometricFitResult> result =
            an::fitSpectrophotometricGains(*execution, samples, an::SpectrophotometricFitRequest{});
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric =
            result->accepted() ? scoreSpectrophotometricFit(result->gains[0], result->gains[2]) : 1.0;
        out.counters["usableSamples"] = static_cast<double>(result->usableCount);
        out.counters["redInliers"] = static_cast<double>(result->red.inlierCount);
        out.counters["blueInliers"] = static_cast<double>(result->blue.inlierCount);
        out.counters["verdict"] = static_cast<double>(result->verdict);
        Sha256 digest;
        for (const double gain : result->gains)
        {
            digest.update(&gain, sizeof(gain));
        }
        digest.update(&result->red.intercept, sizeof(double));
        digest.update(&result->red.slope, sizeof(double));
        digest.update(&result->blue.intercept, sizeof(double));
        digest.update(&result->blue.slope, sizeof(double));
        const std::uint64_t inliers =
            static_cast<std::uint64_t>(result->red.inlierCount) + static_cast<std::uint64_t>(result->blue.inlierCount);
        digest.update(&inliers, sizeof(inliers));
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runSpectrophotometricApply(const ScenarioSpec& scenario, const RunOptions& options,
                                                         ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u || scenario.input.width != kAperturePhotometryFixtureWidth ||
        scenario.input.height != kAperturePhotometryFixtureHeight ||
        parameterValue(scenario, "fixtureMapping", kSpectrophotometricApplyFixtureMapping) !=
            kSpectrophotometricApplyFixtureMapping ||
        parameterValue(scenario, "gains", "0.64-0.8-1") != "0.64-0.8-1" ||
        parameterValue(scenario, "background", "none") != "none")
    {
        skipReason = "scenario '" + scenario.id + "' declares an apply this runner does not implement";
        return false;
    }
    const AperturePhotometryFixture field = makeAperturePhotometryFixture(scenario.seed);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
        kAperturePhotometryFixtureWidth, kAperturePhotometryFixtureHeight, 3u, engine->limits());
    if (!extent.has_value())
    {
        skipReason = "the planted field exceeds the engine's extent limits";
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Rgb,
                                                               .colorSpace = an::ColorSpace::LinearRgb,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    const std::optional<an::ImageLayoutRequest> layoutRequest =
        descriptor.has_value() ? an::layoutRequestFor(*descriptor, an::StorageArrangement::Planar, 1u) : std::nullopt;
    const std::optional<an::ImageLayout> layout =
        layoutRequest.has_value() ? an::ImageLayout::create(*layoutRequest, engine->limits()) : std::nullopt;
    if (!descriptor.has_value() || !layout.has_value())
    {
        skipReason = "the planted field could not be described to the engine";
        return false;
    }
    const std::uint64_t frameBytes = field.samples.size() * sizeof(float);
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(field.samples.data(), frameBytes, *layout, *descriptor);
    if (!frame.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the planted field", frame.status());
        return false;
    }
    std::vector<float> destination(field.samples.size(), 0.0f);
    const an::ColorChannelGains gains{kSpectrophotometricApplyFixtureGains[0], kSpectrophotometricApplyFixtureGains[1],
                                      kSpectrophotometricApplyFixtureGains[2]};
    an::SpectrophotometricBackgroundRequest background;
    background.neutralize = false;
    const std::size_t planeSize =
        static_cast<std::size_t>(kAperturePhotometryFixtureWidth) * kAperturePhotometryFixtureHeight;

    out.fixtureDigest = Sha256::of(field.samples.data(), frameBytes);
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::SpectrophotometricApplyResult> result =
            an::applySpectrophotometricGains(*execution, *frame, destination.data(), destination.size() * sizeof(float),
                                             gains, background, an::ColorCalibrationClipping::ClampToPixelRange);
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = scoreSpectrophotometricApply(destination, planeSize, field.background,
                                                             kSpectrophotometricApplyFixtureGains);
        out.counters["samplesWritten"] = static_cast<double>(result->apply.samplesWritten);
        out.counters["samplesOutsideInterval"] = static_cast<double>(result->apply.samplesOutsideInterval);
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            out.counters["offset" + std::to_string(channel)] = result->offsets[channel];
        }
        return Sha256::of(destination.data(), destination.size() * sizeof(float));
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runNarrowbandComposition(const ScenarioSpec& scenario, const RunOptions& options,
                                                       ResultRecord& out, std::string& skipReason)
{
    const bool extraction = scenario.id == "color.narrowband.extract_bayer.f32";
    const bool palettes = scenario.id == "color.narrowband.palette_grid.f32";
    const bool mapper = scenario.id == "color.narrowband.mapper.f32";
    const bool nbrgb = scenario.id == "color.narrowband.nbrgb.f32";
    if ((!extraction && !palettes && !mapper && !nbrgb) || scenario.input.sampleType != SampleType::Float32 ||
        scenario.input.layout != Layout::Planar || scenario.input.width != 512u || scenario.input.height != 512u ||
        scenario.input.channels != (extraction ? 1u : 3u) ||
        (extraction && (scenario.input.cfaPattern != "RGGB" ||
                        parameterValue(scenario, "products", "Ha,Green,HaOIII") != "Ha,Green,HaOIII" ||
                        parameterValue(scenario, "scalings", "Native,HaUp,OiiiDown") != "Native,HaUp,OiiiDown")) ||
        (palettes && (parameterValue(scenario, "channels", "R=Ha,G=OIII,B=SII") != "R=Ha,G=OIII,B=SII" ||
                      parameterValue(scenario, "palettes", "all12") != "all12" ||
                      parameterValue(scenario, "intensities", "1,1,1") != "1,1,1")) ||
        (mapper && (parameterValue(scenario, "channels", "R=Ha,G=OIII,B=SII") != "R=Ha,G=OIII,B=SII" ||
                    parameterValue(scenario, "hues", "0,120,240") != "0,120,240" ||
                    parameterValue(scenario, "saturation", "0.75") != "0.75" ||
                    parameterValue(scenario, "modification", "none") != "none" ||
                    parameterValue(scenario, "background", "off") != "off" ||
                    parameterValue(scenario, "outputStf", "off") != "off")) ||
        (nbrgb &&
         (parameterValue(scenario, "channels", "RGB") != "RGB" ||
          parameterValue(scenario, "rgbBandwidthNm", "100") != "100" ||
          parameterValue(scenario, "narrowbandBandwidthNm", "7,8.5,8.5") != "7,8.5,8.5" ||
          parameterValue(scenario, "scale", "1.2") != "1.2" || parameterValue(scenario, "fit", "0,0.92") != "0,0.92" ||
          parameterValue(scenario, "rescale", "false") != "false")))
    {
        skipReason = "scenario '" + scenario.id + "' declares a narrowband operation this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    const auto makePlanarLayout = [&](const an::ImageDescriptor& descriptor) {
        const std::optional<an::ImageLayoutRequest> request =
            an::layoutRequestFor(descriptor, an::StorageArrangement::Planar, 1u);
        return request.has_value() ? an::ImageLayout::create(*request, engine->limits())
                                   : std::optional<an::ImageLayout>{};
    };
    std::string failure;

    if (extraction)
    {
        const an::Result<an::ImageDescriptor> measured = an::measuredBayerChannelDescriptor(source->descriptor);
        if (!measured.hasValue())
        {
            skipReason = describeEngineFailure("the measured-channel descriptor could not be built", measured.status());
            return false;
        }
        const std::optional<an::ImageLayout> measuredLayout = makePlanarLayout(*measured);
        std::array<an::NarrowbandExtractionPlan, 3u> plans{};
        std::array<std::optional<an::ImageLayout>, 3u> hydrogenLayouts{};
        std::array<std::optional<an::ImageLayout>, 3u> oxygenLayouts{};
        const std::array<an::NarrowbandExtractionScaling, 3u> scalings{
            an::NarrowbandExtractionScaling::Native, an::NarrowbandExtractionScaling::UpsampleHydrogenAlpha,
            an::NarrowbandExtractionScaling::DownsampleOxygenIII};
        for (std::size_t index = 0u; index < scalings.size(); ++index)
        {
            const an::Result<an::NarrowbandExtractionPlan> plan =
                an::planNarrowbandExtraction(source->descriptor, scalings[index]);
            if (!plan.hasValue())
            {
                skipReason = describeEngineFailure("the narrowband extraction could not be planned", plan.status());
                return false;
            }
            plans[index] = *plan;
            hydrogenLayouts[index] = makePlanarLayout(plan->hydrogenAlphaDescriptor());
            oxygenLayouts[index] = makePlanarLayout(plan->oxygenIIIDescriptor());
        }
        if (!measuredLayout.has_value() ||
            std::any_of(hydrogenLayouts.begin(), hydrogenLayouts.end(), [](const auto& value) { return !value; }) ||
            std::any_of(oxygenLayouts.begin(), oxygenLayouts.end(), [](const auto& value) { return !value; }))
        {
            skipReason = "the narrowband extraction output layouts could not be built";
            return false;
        }
        std::vector<float> hydrogen(measuredLayout->totalBytes() / sizeof(float));
        std::vector<float> green(measuredLayout->totalBytes() / sizeof(float));
        std::array<std::vector<float>, 3u> pairedHydrogen{};
        std::array<std::vector<float>, 3u> oxygen{};
        for (std::size_t index = 0u; index < scalings.size(); ++index)
        {
            pairedHydrogen[index].resize(hydrogenLayouts[index]->totalBytes() / sizeof(float));
            oxygen[index].resize(oxygenLayouts[index]->totalBytes() / sizeof(float));
        }
        const std::function<std::string(bool)> body = [&](bool produceDigest) {
            const an::Result<an::NarrowbandExtractionResult> hydrogenResult = an::extractBayerHydrogenAlpha(
                *execution, source->descriptor, fixture.bytes.data(), source->layout, hydrogen.data(), *measuredLayout);
            const an::Result<an::NarrowbandExtractionResult> greenResult = an::extractBayerGreen(
                *execution, source->descriptor, fixture.bytes.data(), source->layout, green.data(), *measuredLayout);
            if (!hydrogenResult.hasValue() || !greenResult.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to extract measured narrowband channels",
                                                    !hydrogenResult.hasValue() ? hydrogenResult.status()
                                                                               : greenResult.status());
                }
                return std::string{};
            }
            for (std::size_t index = 0u; index < scalings.size(); ++index)
            {
                const an::Result<an::NarrowbandExtractionResult> pair = an::extractBayerHydrogenAlphaAndOxygenIII(
                    *execution, plans[index], fixture.bytes.data(), source->layout, pairedHydrogen[index].data(),
                    *hydrogenLayouts[index], oxygen[index].data(), *oxygenLayouts[index]);
                if (!pair.hasValue())
                {
                    if (failure.empty())
                    {
                        failure = describeEngineFailure("the engine failed to extract the Ha/OIII pair", pair.status());
                    }
                    return std::string{};
                }
            }
            if (!produceDigest)
            {
                return std::string{};
            }
            Sha256 digest;
            digest.update(hydrogen.data(), hydrogen.size() * sizeof(float));
            digest.update(green.data(), green.size() * sizeof(float));
            for (std::size_t index = 0u; index < scalings.size(); ++index)
            {
                digest.update(pairedHydrogen[index].data(), pairedHydrogen[index].size() * sizeof(float));
                digest.update(oxygen[index].data(), oxygen[index].size() * sizeof(float));
            }
            out.counters["products"] = 5.0;
            return digest.hex();
        };
        return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    }

    const std::optional<an::ImageExtent> monoExtent =
        an::ImageExtent::create(scenario.input.width, scenario.input.height, 1u, engine->limits());
    const std::optional<an::ImageLayout> monoLayout = monoExtent.has_value()
                                                          ? an::ImageLayout::create({.extent = *monoExtent,
                                                                                     .elementSizeBytes = sizeof(float),
                                                                                     .samplesPerPixel = 1u,
                                                                                     .planeCount = 1u,
                                                                                     .rowAlignmentBytes = 1u},
                                                                                    engine->limits())
                                                          : std::nullopt;
    if (!monoLayout.has_value())
    {
        skipReason = "the narrowband source-plane layout could not be built";
        return false;
    }
    const float* const samples = reinterpret_cast<const float*>(fixture.bytes.data());
    const std::size_t planeStride = static_cast<std::size_t>(source->layout.planeStrideBytes() / sizeof(float));
    const std::array<const float*, 3u> channels{samples, samples + planeStride, samples + 2u * planeStride};

    if (palettes)
    {
        const std::span<const an::NarrowbandPaletteDescriptor> catalogue = an::narrowbandPalettes();
        if (catalogue.size() != 12u)
        {
            skipReason = "the narrowband palette catalogue is incomplete";
            return false;
        }
        std::array<std::vector<float>, 12u> outputs{};
        std::array<an::NarrowbandPaletteDestination, 12u> destinations{};
        std::array<an::NarrowbandPaletteResult, 12u> results{};
        for (std::size_t index = 0u; index < outputs.size(); ++index)
        {
            outputs[index].resize(source->layout.totalBytes() / sizeof(float));
            destinations[index] = {
                .palette = catalogue[index].palette, .samples = outputs[index].data(), .layout = &source->layout};
        }
        const an::NarrowbandPaletteChannels inputs{.hydrogenAlpha = {.samples = channels[0], .layout = &*monoLayout},
                                                   .oxygenIII = {.samples = channels[1], .layout = &*monoLayout},
                                                   .sulfurII = {.samples = channels[2], .layout = &*monoLayout}};
        const std::function<std::string(bool)> body = [&](bool produceDigest) {
            const an::Status ran = an::composeNarrowbandPaletteGrid(
                *execution, an::SampleType::Float32, inputs, destinations, an::NarrowbandPaletteIntensities{}, results);
            if (ran.failed())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to build the palette grid", ran);
                }
                return std::string{};
            }
            if (!produceDigest)
            {
                return std::string{};
            }
            Sha256 digest;
            for (const std::vector<float>& output : outputs)
            {
                digest.update(output.data(), output.size() * sizeof(float));
            }
            out.counters["palettes"] = static_cast<double>(outputs.size());
            return digest.hex();
        };
        return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    }

    if (mapper)
    {
        std::array<an::NarrowbandColorLayer, 3u> layers{};
        const std::array<double, 3u> hues{0.0, 120.0, 240.0};
        for (std::size_t index = 0u; index < layers.size(); ++index)
        {
            layers[index].samples = channels[index];
            layers[index].layout = &*monoLayout;
            layers[index].hueDegrees = hues[index];
            layers[index].saturation = 0.75;
        }
        std::vector<float> destination(source->layout.totalBytes() / sizeof(float));
        const std::function<std::string(bool)> body = [&](bool produceDigest) {
            const an::Result<an::NarrowbandColorMapResult> ran =
                an::mapNarrowbandColor(*execution, an::SampleType::Float32, layers, destination.data(), source->layout,
                                       an::NarrowbandColorMapRequest{});
            if (!ran.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to map the narrowband layers", ran.status());
                }
                return std::string{};
            }
            if (!produceDigest)
            {
                return std::string{};
            }
            out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
            out.counters["layers"] = static_cast<double>(ran->includedLayers);
            return Sha256::of(destination.data(), destination.size() * sizeof(float));
        };
        return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    }

    an::NarrowbandRgbRequest request;
    for (std::size_t index = 0u; index < request.channels.size(); ++index)
    {
        request.channels[index].samples = channels[index];
        request.channels[index].layout = &*monoLayout;
        request.channels[index].enabled = true;
        request.channels[index].bandwidthNanometres = index == 0u ? 7.0 : 8.5;
        request.channels[index].scale = 1.2;
    }
    std::vector<float> destination(source->layout.totalBytes() / sizeof(float));
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::NarrowbandRgbResult> ran =
            an::combineNarrowbandRgb(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                     destination.data(), source->layout, request);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to combine NBRGB", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        out.counters["fitChannels"] =
            static_cast<double>(std::count(ran->fitApplied.begin(), ran->fitApplied.end(), true));
        return Sha256::of(destination.data(), destination.size() * sizeof(float));
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runStarReduction(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "fixture", "target-starless-v1") != "target-starless-v1" ||
        parameterValue(scenario, "method", "Star") != "Star" ||
        parameterValue(scenario, "strength", "Strong") != "Strong" ||
        parameterValue(scenario, "iterations", "2") != "2")
    {
        skipReason = "scenario '" + scenario.id + "' declares a star reduction this runner does not implement";
        return false;
    }

    Fixture sourceFixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, sourceFixture, error))
    {
        skipReason = error;
        return false;
    }
    std::vector<float> entropy(static_cast<std::size_t>(scenario.elementsProcessed), 0.0f);
    if (sourceFixture.bytes.size() != entropy.size() * sizeof(float))
    {
        skipReason = "the star-reduction entropy fixture does not contain its declared Float32 plane";
        return false;
    }
    std::memcpy(entropy.data(), sourceFixture.bytes.data(), sourceFixture.bytes.size());
    const StarReductionFixture fixture = makeStarReductionFixture(entropy);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(scenario.input);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(fixture.target.size()) * sizeof(float);
    const std::optional<BridgedFixture> frame = bridgeFixture(scenario.input, semantics, engine->limits(), frameBytes);
    if (!frame.has_value())
    {
        skipReason = describeFixtureBridgeFailure(checkFixture(scenario.input, semantics, engine->limits(), frameBytes),
                                                  scenario.input);
        return false;
    }
    an::StarReductionRequest request;
    request.method = an::StarReductionMethod::Star;
    request.strength = an::StarReductionStrength::Strong;
    request.iterations = 2u;
    const an::Result<an::StarReductionPlan> plan = an::planStarReduction(frame->descriptor, frame->descriptor, request);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the star-reduction request", plan.status());
        return false;
    }
    std::vector<float> destination(fixture.target.size(), 0.0f);
    Sha256 fixtureHash;
    fixtureHash.update(fixture.target.data(), fixture.target.size() * sizeof(float));
    fixtureHash.update(fixture.starless.data(), fixture.starless.size() * sizeof(float));
    out.fixtureDigest = fixtureHash.hex();
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;

    an::StarReductionResult report;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::StarReductionResult> reduced =
            an::reduceStars(*execution, fixture.target.data(), frame->layout, fixture.starless.data(), frame->layout,
                            destination.data(), frame->layout, *plan);
        if (!reduced.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", reduced.status());
            }
            return std::string{};
        }
        report = *reduced;
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = starReductionMaximumError(destination, fixture, request.iterations);
        out.counters["pixels"] = static_cast<double>(report.pixelsProcessed);
        out.counters["samplesWritten"] = static_cast<double>(report.samplesWritten);
        out.counters["clampedOutputs"] = static_cast<double>(report.clampedOutputs);
        out.counters["imagePasses"] = static_cast<double>(report.imagePasses);
        out.counters["workingBytes"] = static_cast<double>(report.workingBytes);
        return Sha256::of(destination.data(), destination.size() * sizeof(float));
    };
    const bool finished = finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    if (an::MemoryResource* const memory = engine->memoryResource())
    {
        const an::MemoryCounters counters = memory->counters();
        out.memory.allocationCount = counters.totalAllocations;
        out.memory.allocatedBytes = counters.totalBytes;
    }
    return finished;
}

bool AfterNightEngineAdapter::runPlateSolveScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                    ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != 42u || scenario.input.height != 1u ||
        parameterValue(scenario, "fixture", "linear-hinted-v1") != "linear-hinted-v1" ||
        parameterValue(scenario, "stars", "42") != "42" ||
        parameterValue(scenario, "catalog", "prebuilt") != "prebuilt" ||
        parameterValue(scenario, "search", "exact-hint") != "exact-hint" ||
        parameterValue(scenario, "fixtureMapping", "plate-solve-v1") != "plate-solve-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a plate solve this runner does not implement";
        return false;
    }

    const PlateSolveFixture fixture = makePlateSolveFixture();
    std::vector<an::PlateSolveStar> imageStars;
    std::vector<an::CatalogEntry> entries;
    imageStars.reserve(fixture.stars.size());
    entries.reserve(fixture.stars.size());
    for (const PlateSolveFixtureStar& star : fixture.stars)
    {
        imageStars.push_back({.position = {.x = star.imageX, .y = star.imageY}});
        an::CatalogEntry entry;
        entry.identifier = std::to_string(star.identifier);
        entry.coordinate = {.rightAscensionDegrees = star.rightAscensionDegrees,
                            .declinationDegrees = star.declinationDegrees};
        entry.magnitude = star.magnitude;
        entries.push_back(std::move(entry));
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the plate-solve operation could not be started: ") + execution.status().message();
        return false;
    }
    an::Result<an::SkyCatalog> catalog = an::buildSkyCatalog(
        *execution, std::move(entries), an::SkyCatalogRequest{.photometricBand = "G", .buildNameIndex = false});
    if (!catalog.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not build the plate-solve catalog", catalog.status());
        return false;
    }

    an::PlateSolveRequest request;
    request.imageWidth = fixture.width;
    request.imageHeight = fixture.height;
    request.initialCenter = an::SkyCoordinate{.rightAscensionDegrees = fixture.centerRightAscensionDegrees,
                                              .declinationDegrees = fixture.centerDeclinationDegrees};
    request.approximateScaleArcsecPerPixel = fixture.scaleArcsecPerPixel;
    request.minimumMatchedStars = 12u;
    request.maximumRmsResidualPixels = 1.5;

    out.fixtureDigest = Sha256::of(plateSolveFixtureText(fixture));
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::optional<an::PlateSolveResult> result;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        an::Result<an::PlateSolveResult> solved = an::solvePlate(*execution, *catalog, imageStars, request);
        if (!solved.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", solved.status());
            }
            return std::string{};
        }
        result = std::move(*solved);
        if (!produceDigest)
        {
            return std::string{};
        }
        const an::Result<an::SkyCoordinate> center = result->solution.fieldCenter();
        if (!center.hasValue())
        {
            failure = describeEngineFailure("the solved WCS has no field centre", center.status());
            return std::string{};
        }
        const double scale = result->solution.meanPixelScaleArcsecPerPixel();
        const double rms = result->matches.empty() ? (std::numeric_limits<double>::max)() : std::sqrt([&] {
            double squares = 0.0;
            for (const an::PlateSolveMatch& match : result->matches)
            {
                squares += match.residualPixels * match.residualPixels;
            }
            return squares / static_cast<double>(result->matches.size());
        }());
        out.correctnessMetric =
            plateSolveCorrectness(center->rightAscensionDegrees, center->declinationDegrees, scale, rms, fixture);
        out.counters["matches"] = static_cast<double>(result->matches.size());
        out.counters["rmsResidual"] = rms;
        out.counters["catalogQueries"] = result->diagnostics.catalogQueries;
        out.counters["trialCenters"] = result->diagnostics.examinedTrialCenters;
        out.counters["workingBytes"] = static_cast<double>(result->diagnostics.peakWorkingBytes);
        return Sha256::of(plateSolveResultText(center->rightAscensionDegrees, center->declinationDegrees, scale,
                                               result->matches.size(), rms));
    };
    const bool finished = finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    if (an::MemoryResource* const memory = engine->memoryResource())
    {
        const an::MemoryCounters counters = memory->counters();
        out.memory.allocationCount = counters.totalAllocations;
        out.memory.allocatedBytes = counters.totalBytes;
    }
    return finished;
}

bool AfterNightEngineAdapter::runStarAlignment(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "fixture", "similarity-outliers-v1") != "similarity-outliers-v1" ||
        parameterValue(scenario, "points", "40") != "40" || parameterValue(scenario, "referenceOutliers", "3") != "3" ||
        parameterValue(scenario, "model", "Similarity") != "Similarity" ||
        parameterValue(scenario, "reference", "prepared") != "prepared")
    {
        skipReason = "scenario '" + scenario.id + "' declares a star alignment this runner does not implement";
        return false;
    }

    const StarAlignmentFixture fixture = makeStarAlignmentFixture(40u, 3u, scenario.seed);
    std::vector<an::StarAlignmentPoint> source;
    std::vector<an::StarAlignmentPoint> reference;
    source.reserve(fixture.source.size());
    reference.reserve(fixture.reference.size());
    for (const AlignmentFixturePoint& point : fixture.source)
    {
        source.push_back({.x = point.x, .y = point.y});
    }
    for (const AlignmentFixturePoint& point : fixture.reference)
    {
        reference.push_back({.x = point.x, .y = point.y});
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Similarity;
    request.minimumMatches = 12u;
    request.seed = scenario.seed;
    const an::Result<an::PreparedStarAlignmentReference> prepared =
        an::prepareStarAlignmentReference(*execution, reference, request);
    if (!prepared.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not prepare the alignment reference", prepared.status());
        return false;
    }

    out.fixtureDigest = Sha256::of(starAlignmentFixtureText(fixture));
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    an::StarAlignmentResult result;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        an::Result<an::StarAlignmentResult> aligned = an::alignStarFields(*execution, source, *prepared);
        if (!aligned.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", aligned.status());
            }
            return std::string{};
        }
        result = std::move(*aligned);
        if (!produceDigest)
        {
            return std::string{};
        }
        const double residual = starAlignmentResidual(result.transform.sourceToReference, fixture);
        out.correctnessMetric = residual;
        out.counters["matches"] = static_cast<double>(result.matches.size());
        out.counters["sourceTriangles"] = static_cast<double>(result.quality.sourceTriangles);
        out.counters["referenceTriangles"] = static_cast<double>(result.quality.referenceTriangles);
        out.counters["candidateMatches"] = static_cast<double>(result.quality.candidateMatches);
        out.counters["rejectedOutliers"] = static_cast<double>(result.quality.rejectedOutliers);
        out.counters["rootMeanSquareResidual"] = result.quality.rootMeanSquareResidual;
        out.counters["workingBytes"] = static_cast<double>(result.quality.workingBytes);
        return Sha256::of(starAlignmentResultText(result.transform.sourceToReference, result.matches.size(), residual));
    };
    const bool finished = finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    if (an::MemoryResource* const memory = engine->memoryResource())
    {
        const an::MemoryCounters counters = memory->counters();
        out.memory.allocationCount = counters.totalAllocations;
        out.memory.allocatedBytes = counters.totalBytes;
    }
    return finished;
}

bool AfterNightEngineAdapter::runStarDetection(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    const bool planted = scenario.id == "stars.detect.planted.f32";
    if (scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        scenario.input.sampleType != (planted ? SampleType::Float32 : SampleType::UInt16) ||
        parameterValue(scenario, "profile", "Gaussian") != "Gaussian" ||
        parameterValue(scenario, "radius", "5") != "5" || parameterValue(scenario, "sigma", "1") != "1" ||
        parameterValue(scenario, "roundness", "0.5") != "0.5" ||
        parameterValue(scenario, "smoothing", "engine-default") != "engine-default" ||
        (planted && (parameterValue(scenario, "field", "planted-star-v1") != "planted-star-v1" ||
                     parameterValue(scenario, "cell", "48") != "48")) ||
        (!planted && parameterValue(scenario, "conversion", "u16-to-f32") != "u16-to-f32"))
    {
        skipReason = "scenario '" + scenario.id + "' declares a detection this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::size_t sampleCount = static_cast<std::size_t>(scenario.elementsProcessed);
    SampleDescriptor floatDescriptor = fixture.descriptor;
    floatDescriptor.sampleType = SampleType::Float32;
    floatDescriptor.rowStrideBytes = 0u;
    floatDescriptor.planeStrideBytes = 0u;
    const FixtureSemantics semantics = defaultFixtureSemantics(floatDescriptor);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(sampleCount) * sizeof(float);
    const std::optional<BridgedFixture> frame = bridgeFixture(floatDescriptor, semantics, engine->limits(), frameBytes);
    if (!frame.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(floatDescriptor, semantics, engine->limits(), frameBytes), floatDescriptor);
        return false;
    }

    std::vector<float> samples(sampleCount, 0.0f);
    PlantedStarField field;
    if (planted)
    {
        field = makePlantedStarField(scenario.input.width, scenario.input.height, 48u, scenario.seed);
        std::copy(field.samples.begin(), field.samples.end(), samples.begin());
        // The planted samples are what the engine detects on, so they are what the fixture digest
        // has to cover.
        out.fixtureDigest = Sha256::of(field.samples.data(), field.samples.size() * sizeof(float));
    }
    else
    {
        const FixtureSemantics sourceSemantics = defaultFixtureSemantics(fixture.descriptor);
        const std::optional<BridgedFixture> integerSource =
            bridgeFixture(fixture.descriptor, sourceSemantics, engine->limits(), fixture.bytes.size());
        if (!integerSource.has_value())
        {
            skipReason = describeFixtureBridgeFailure(
                checkFixture(fixture.descriptor, sourceSemantics, engine->limits(), fixture.bytes.size()),
                fixture.descriptor);
            return false;
        }
        an::ConversionCounters counters;
        const an::Status converted = an::convertImageSamples(
            *execution, an::SampleType::UInt16, fixture.bytes.data(), integerSource->layout, an::SampleType::Float32,
            samples.data(), frame->layout, an::ConversionPolicy{}, counters);
        if (converted.failed())
        {
            skipReason = describeEngineFailure("the engine could not convert the real frame", converted);
            return false;
        }
        out.fixtureDigest = fixture.digest;
    }

    const an::Result<an::StarFrameView> view =
        an::StarFrameView::create(samples.data(), frameBytes, frame->layout, frame->descriptor);
    if (!view.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the detection frame", view.status());
        return false;
    }
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, frame->descriptor, frame->layout);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the engine refused the detection request", plan.status());
        return false;
    }

    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    an::StarDetectionResult detected;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Status ran = an::detectStars(*execution, *view, *plan, detected);
        if (ran.failed())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran);
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        std::vector<double> residuals;
        std::vector<DetectedStarSummary> found;
        residuals.reserve(detected.stars.size());
        found.reserve(detected.stars.size());
        for (const an::DetectedStar& star : detected.stars)
        {
            found.push_back(DetectedStarSummary{
                .x = star.positionX, .y = star.positionY, .meanFullWidth = star.metrics.meanFullWidth});
            if (star.psf.amplitude > 0.0 && std::isfinite(star.psf.residualRootMeanSquare))
            {
                residuals.push_back(star.psf.residualRootMeanSquare / star.psf.amplitude);
            }
        }
        out.counters["stars"] = static_cast<double>(detected.stars.size());
        out.counters["medianFullWidth"] = detected.summary.medianFullWidth.value_or(0.0);
        out.counters["medianRoundness"] = detected.summary.medianRoundness.value_or(0.0);
        out.counters["medianSignalToNoise"] = detected.summary.medianSignalToNoise.value_or(0.0);
        out.counters["background"] = detected.summary.channel.background;
        out.counters["backgroundNoise"] = detected.summary.channel.backgroundNoise;
        out.counters["medianRelativeResidual"] = medianRelativeResidual(residuals);
        out.counters["candidates"] = static_cast<double>(detected.report.candidates);
        out.counters["fittedCandidates"] = static_cast<double>(detected.report.fittedCandidates);
        out.counters["unconvergedFits"] = static_cast<double>(detected.report.unconvergedFits);
        out.counters["workingBytes"] = static_cast<double>(detected.report.peakWorkingBytes);
        out.counters["duplicateCandidates"] = static_cast<double>(detected.report.duplicateCandidates);
        out.counters["rawCandidates"] = static_cast<double>(detected.report.rawCandidates);
        for (std::size_t reason = 0u; reason < an::kStarRejectionReasonCount; ++reason)
        {
            out.counters[std::string("rejected.") +
                         an::starRejectionReasonName(static_cast<an::StarRejectionReason>(reason))] =
                static_cast<double>(detected.report.rejections[reason]);
        }
        if (planted)
        {
            const PlantedStarScore score = scorePlantedStarField(field, found);
            out.correctnessMetric = score.shortfall();
            out.counters["plantedStars"] = static_cast<double>(field.stars.size());
            out.counters["matchedStars"] = static_cast<double>(score.matched);
            out.counters["missedStars"] = static_cast<double>(score.missed);
            out.counters["spuriousStars"] = static_cast<double>(score.spurious);
            out.counters["recall"] = score.recall();
            out.counters["precision"] = score.precision();
            out.counters["positionRootMeanSquare"] = score.positionRootMeanSquare;
            out.counters["fullWidthRootMeanSquare"] = score.fullWidthRootMeanSquare;
        }
        else
        {
            out.correctnessMetric = medianRelativeResidual(residuals);
        }
        return Sha256::of(starDetectionText(
            static_cast<std::uint64_t>(detected.stars.size()), detected.summary.medianFullWidth.value_or(0.0),
            detected.summary.medianRoundness.value_or(0.0), detected.summary.quality.value_or(0.0),
            detected.summary.medianSignalToNoise.value_or(0.0), detected.summary.channel.background,
            detected.summary.channel.backgroundNoise));
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runCatalogScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    const bool parseScenario = scenario.id == "catalog.parse.star_csv";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != 16384u || scenario.input.height != 1u ||
        parameterValue(scenario, "schema", "bright-star-v1") != "bright-star-v1" ||
        parameterValue(scenario, "fixtureMapping", "catalog-text-v1") != "catalog-text-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a catalog this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> entropy{reinterpret_cast<const float*>(fixture.bytes.data()),
                                         fixture.bytes.size() / sizeof(float)};
    const std::string catalogText = makeCatalogFixtureText(entropy);
    Sha256 textHash;
    textHash.update(catalogText.data(), catalogText.size());
    out.fixtureDigest = textHash.hex();

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    an::CatalogTextRequest textRequest;
    textRequest.schema = an::CatalogTextSchema::Star;
    an::SkyCatalogRequest catalogRequest;
    catalogRequest.photometricBand = "V";

    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;

    // The whole table in one deterministic order: the digest is of the answer to a whole-sphere
    // lookup, and the fixture gives every entry a distinct magnitude precisely so that this order is
    // the magnitudes themselves rather than a tie-break.
    const auto digestWholeCatalog = [&execution](const an::SkyCatalog& catalog, std::string& failed) {
        an::CatalogRegionQuery everything;
        everything.center = an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
        everything.radiusDegrees = 180.0;
        everything.magnitudeLimit = 100.0;
        const an::Result<an::CatalogQueryResult> answered = queryCatalogRegion(*execution, catalog, everything);
        if (!answered.hasValue())
        {
            if (failed.empty())
            {
                failed = describeEngineFailure("the engine could not read back the catalog", answered.status());
            }
            return std::string{};
        }
        Sha256 digest;
        for (const std::uint32_t index : answered->entryIndices)
        {
            const an::CatalogEntry* const entry = catalog.entry(index);
            if (entry == nullptr)
            {
                continue;
            }
            const double magnitude = entry->magnitude.value_or(99.0);
            digest.update(entry->identifier.data(), entry->identifier.size());
            digest.update(&entry->coordinate.rightAscensionDegrees, sizeof(double));
            digest.update(&entry->coordinate.declinationDegrees, sizeof(double));
            digest.update(&magnitude, sizeof(magnitude));
        }
        return digest.hex();
    };

    if (parseScenario)
    {
        const std::function<std::string(bool)> body = [&](bool produceDigest) {
            an::Result<an::ParsedCatalogText> parsed = parseCatalogText(*execution, catalogText, textRequest);
            if (!parsed.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine could not parse the catalog text", parsed.status());
                }
                return std::string{};
            }
            const std::uint32_t unknownMagnitudes = parsed->report.unknownMagnitudeCount;
            an::Result<an::SkyCatalog> catalog =
                buildSkyCatalog(*execution, std::move(parsed->entries), catalogRequest);
            if (!catalog.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine could not build the catalog", catalog.status());
                }
                return std::string{};
            }
            if (!produceDigest)
            {
                return std::string{};
            }
            out.correctnessMetric = 0.0;
            out.counters["entries"] = static_cast<double>(catalog->entryCount());
            out.counters["unknownMagnitudes"] = static_cast<double>(unknownMagnitudes);
            return digestWholeCatalog(*catalog, failure);
        };
        return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    }

    an::Result<an::ParsedCatalogText> parsed = parseCatalogText(*execution, catalogText, textRequest);
    if (!parsed.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not parse the catalog text", parsed.status());
        return false;
    }
    an::Result<an::SkyCatalog> built = buildSkyCatalog(*execution, std::move(parsed->entries), catalogRequest);
    if (!built.hasValue())
    {
        skipReason = describeEngineFailure("the engine could not build the catalog", built.status());
        return false;
    }
    const an::SkyCatalog catalog = std::move(*built);
    const std::vector<CatalogFixtureCone> cones = makeCatalogFixtureCones();

    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        Sha256 digest;
        std::size_t returned = 0u;
        std::size_t examined = 0u;
        for (const CatalogFixtureCone& cone : cones)
        {
            an::CatalogRegionQuery query;
            query.center = an::SkyCoordinate{.rightAscensionDegrees = cone.rightAscensionDegrees,
                                             .declinationDegrees = cone.declinationDegrees};
            query.radiusDegrees = kCatalogFixtureQueryRadiusDegrees;
            query.magnitudeLimit = kCatalogFixtureMagnitudeLimit;
            const an::Result<an::CatalogQueryResult> answered = queryCatalogRegion(*execution, catalog, query);
            if (!answered.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine could not search the catalog", answered.status());
                }
                return std::string{};
            }
            returned += answered->entryIndices.size();
            examined += answered->examinedCount;
            if (!produceDigest)
            {
                continue;
            }
            for (const std::uint32_t index : answered->entryIndices)
            {
                const an::CatalogEntry* const entry = catalog.entry(index);
                if (entry != nullptr)
                {
                    digest.update(entry->identifier.data(), entry->identifier.size());
                }
            }
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = 0.0;
        out.counters["entries"] = static_cast<double>(catalog.entryCount());
        out.counters["cones"] = static_cast<double>(cones.size());
        out.counters["returnedEntries"] = static_cast<double>(returned);
        out.counters["examinedEntries"] = static_cast<double>(examined);
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runDrizzleExecutionScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                          ResultRecord& out, std::string& skipReason)
{
    const auto index = drizzleExecutionScenarioIndex(scenario);
    if (!index)
    {
        skipReason = "unsupported full drizzle fixture";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    out.fixtureDigest = fixture.digest;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto extent =
        *an::ImageExtent::create(kDrizzleAccumulationWidth, kDrizzleAccumulationHeight, 1u, execution->limits());
    const auto descriptor = *an::ImageDescriptor::create({.extent = extent,
                                                          .sampleType = an::SampleType::Float32,
                                                          .channelSchema = an::ChannelSchema::Mono,
                                                          .colorSpace = an::ColorSpace::Unspecified,
                                                          .numericDomain = an::NumericDomain::NormalizedLinear,
                                                          .cfa = {},
                                                          .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                          .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = descriptor, .frameCount = kDrizzleExecutionFrames};
    const bool peakSum = scenario.parameters.contains("combination");
    request.output.combination =
        peakSum ? an::DrizzleCombination::PeakNormalizedSum : an::DrizzleCombination::WeightedMean;
    request.parameters.kernel = static_cast<an::DrizzleKernel>(*index);
    request.parameters.scale = request.parameters.pixelFraction = 1.0;
    request.output.weightMap = true;
    const auto model = *an::DrizzleModel::create(request);
    const auto frames =
        drizzleExecutionFrames({reinterpret_cast<const float*>(fixture.bytes.data()), fixture.bytes.size() / 4u});
    const auto oracle = drizzleExecutionOracle(frames, *index, peakSum);
    const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, execution->limits());
    std::vector<an::DrizzleFrameData> data;
    std::vector<an::DrizzleFrameParameters> geometry;
    for (const auto& frame : frames)
    {
        data.push_back({{layout, std::as_bytes(std::span{frame}), an::SampleType::Float32}});
        geometry.push_back({*an::DrizzleMapDescriptor::create(model, {.postTranslation = {0.25, 0.25}})});
    }
    auto source = an::ResidentDrizzleFrameSource::create(data, an::DrizzleSourceAccess::Region);
    if (!source.hasValue())
    {
        skipReason = source.status().message();
        return false;
    }
    an::DrizzleExecutionRequest run;
    run.planning.maximumSourceRows = kDrizzleExecutionSourceRows;
    run.planning.preparedStateBytes = fixture.bytes.size() * kDrizzleExecutionFrames;
    std::string failure;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = BackendId::Scalar;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        auto result = an::executeDrizzle(*engine, *execution, model, *source, geometry, run);
        if (!result.hasValue())
        {
            failure = result.status().message();
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        const auto* means = static_cast<const float*>(result->image.data());
        const auto* weights = static_cast<const float*>(result->weightMap.data());
        out.correctnessMetric = drizzleAccumulationResidual(
            oracle, [&](std::size_t i) { return means[i]; }, [&](std::size_t i) { return weights[i]; });
        out.counters["readRequests"] = static_cast<double>(result->report.readRequests);
        out.counters["inputBytesRead"] = static_cast<double>(result->report.inputBytesRead);
        out.counters["plannedPeakBytes"] = static_cast<double>(result->report.plan.memory.peakBytes);
        out.counters["contextPeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
        out.counters["sourceRows"] = result->report.plan.sourceRows;
        out.counters["destinationRows"] = result->report.plan.destinationRows;
        Sha256 digest;
        digest.update(means, extent.sampleCount() * sizeof(float));
        digest.update(weights, extent.sampleCount() * sizeof(float));
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runDrizzleAccumulationScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                             ResultRecord& out, std::string& skipReason)
{
    const auto index = drizzleAccumulationScenarioIndex(scenario);
    if (!index)
    {
        skipReason = "unsupported drizzle accumulation fixture";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    out.fixtureDigest = fixture.digest;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto extent =
        *an::ImageExtent::create(kDrizzleAccumulationWidth, kDrizzleAccumulationHeight, 1u, execution->limits());
    const auto descriptor = *an::ImageDescriptor::create({.extent = extent,
                                                          .sampleType = an::SampleType::Float32,
                                                          .channelSchema = an::ChannelSchema::Mono,
                                                          .colorSpace = an::ColorSpace::Unspecified,
                                                          .numericDomain = an::NumericDomain::NormalizedLinear,
                                                          .cfa = {},
                                                          .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                          .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = descriptor};
    request.parameters.kernel = static_cast<an::DrizzleKernel>(*index);
    request.parameters.scale = request.parameters.pixelFraction = 1.0;
    const auto model = *an::DrizzleModel::create(request);
    const auto geometry = *an::DrizzleGeometryPlan::create(
        *execution, *an::DrizzleMapDescriptor::create(model, {.postTranslation = {0.25, 0.25}}));
    auto map = an::generateDrizzlePixmap(*execution, geometry, {.storage = an::DrizzleMapStorage::Dense});
    if (!map.hasValue())
    {
        skipReason = map.status().message();
        return false;
    }
    const auto view = *map->view();
    const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, execution->limits());
    const an::DrizzleInputBand input{{layout, std::as_bytes(std::span{fixture.bytes}), an::SampleType::Float32}};
    const std::span<const float> samples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                         fixture.bytes.size() / sizeof(float)};
    const auto oracle = drizzleAccumulationOracle(samples, *index);
    std::optional<an::DrizzleKernelTable> table;
    if (*index >= 4u)
        table = *an::DrizzleKernelTable::create(request.parameters.kernel);
    std::vector<double> sums(extent.sampleCount()), weights(sums.size());
    const an::DrizzleAccumulatorBand destination{0u, extent.height(), sums, weights};
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::fill(sums.begin(), sums.end(), 0.0);
        std::fill(weights.begin(), weights.end(), 0.0);
        auto result = an::accumulateDrizzleBand(*execution, model, input, view, destination, table ? &*table : nullptr);
        if (!result.hasValue())
        {
            failure = result.status().message();
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        out.correctnessMetric = drizzleAccumulationResidual(
            oracle, [&](std::size_t i) { return weights[i] > 0.0 ? sums[i] / weights[i] : 0.0; },
            [&](std::size_t i) { return weights[i]; });
        out.counters["contributions"] = static_cast<double>(result->contributions);
        out.counters["plannedWorkers"] = result->workers;
        Sha256 digest;
        digest.update(sums.data(), sums.size() * sizeof(double));
        digest.update(weights.data(), weights.size() * sizeof(double));
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runDrizzleGeometryScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                         ResultRecord& out, std::string& skipReason)
{
    const auto index = drizzleGeometryScenarioIndex(scenario);
    if (!index.has_value())
    {
        skipReason = "unsupported drizzle geometry fixture or storage contract";
        return false;
    }
    const bool curved = *index != 0u;
    const bool bounded = *index == 2u;
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    out.fixtureDigest = fixture.digest;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto extent = an::ImageExtent::create(kDrizzleGeometryWidth, kDrizzleGeometryHeight, 1u, execution->limits());
    const auto image = an::ImageDescriptor::create({.extent = *extent,
                                                    .sampleType = an::SampleType::Float32,
                                                    .channelSchema = an::ChannelSchema::Mono,
                                                    .colorSpace = an::ColorSpace::Unspecified,
                                                    .numericDomain = an::NumericDomain::NormalizedLinear,
                                                    .cfa = {},
                                                    .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                    .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = *image, .frameCount = 1u};
    request.parameters.scale = kDrizzleGeometryScale;
    auto model = an::DrizzleModel::create(request);
    an::RegistrationTransformRequest transform;
    transform.linearKind = an::RegistrationTransformKind::Projective;
    // Conjugate the entire matrix from integer-centred to half-pixel coordinates:
    // engine(s) = fixture(s - 0.5) + 0.5.
    auto h = kDrizzleGeometryMatrix;
    for (std::size_t row = 0u; row < 3u; ++row)
        h[row * 3u + 2u] -= 0.5 * (h[row * 3u] + h[row * 3u + 1u]);
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        h[column] += 0.5 * h[6u + column];
        h[3u + column] += 0.5 * h[6u + column];
    }
    transform.sourceToReference = h;
    an::DrizzleMappingRequest mapping;
    mapping.postTranslation = {-0.25, 0.75};
    if (curved)
    {
        transform.polynomial.order = 2u;
        transform.polynomial.normalizationOrigin = {256.5, 254.5};
        transform.polynomial.normalizationScale = {256.0, 256.0};
        std::copy(kDrizzleGeometryPolynomialX.begin(), kDrizzleGeometryPolynomialX.end(),
                  transform.polynomial.x.begin());
        std::copy(kDrizzleGeometryPolynomialY.begin(), kDrizzleGeometryPolynomialY.end(),
                  transform.polynomial.y.begin());
        an::SipDistortionRequest sip;
        sip.referencePixel = {256.5, 254.5};
        sip.forwardX.order = sip.forwardY.order = 2u;
        sip.forwardX.coefficients[2][0] = kDrizzleGeometrySipX[0];
        sip.forwardX.coefficients[1][1] = kDrizzleGeometrySipX[1];
        sip.forwardX.coefficients[0][2] = kDrizzleGeometrySipX[2];
        sip.forwardY.coefficients[2][0] = kDrizzleGeometrySipY[0];
        sip.forwardY.coefficients[1][1] = kDrizzleGeometrySipY[1];
        sip.forwardY.coefficients[0][2] = kDrizzleGeometrySipY[2];
        mapping.sourceDistortion = an::SipDistortion::create(sip).value();
    }
    mapping.transform = an::RegistrationTransform::create(transform).value();
    const auto descriptor = an::DrizzleMapDescriptor::create(*model, mapping);
    auto plan = an::DrizzleGeometryPlan::create(*execution, *descriptor);
    if (!plan.hasValue())
    {
        skipReason = plan.status().message();
        return false;
    }
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = BackendId::Scalar;
    std::string failure;
    if (*index == 3u)
    {
        auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
        if (!map.hasValue())
        {
            skipReason = map.status().message();
            return false;
        }
        const auto view = map->view().value();
        std::vector<an::DrizzleFootprint> output(static_cast<std::size_t>(kDrizzleGeometryWidth) *
                                                 kDrizzleGeometryHeight);
        const std::function<std::string(bool)> corners = [&](bool produceDigest) {
            for (std::uint32_t y = 0u; y < kDrizzleGeometryHeight; ++y)
            {
                const auto row = std::span<an::DrizzleFootprint>(output).subspan(
                    static_cast<std::size_t>(y) * kDrizzleGeometryWidth, kDrizzleGeometryWidth);
                const auto result = an::interpolateDrizzleCornerRow(*execution, view, y, 0u, 0.5, row);
                if (!result.hasValue() || result->unmapped != 0u)
                {
                    failure = result.hasValue() ? "invalid benchmark footprint" : result.status().message();
                    return std::string{};
                }
            }
            if (!produceDigest)
                return std::string{};
            Sha256 digest;
            digest.update(output.data(), output.size() * sizeof(output.front()));
            static_assert(sizeof(an::DrizzleFootprint) == 8u * sizeof(float));
            out.correctnessMetric =
                drizzleCornerResidual(output.size(), [&](std::size_t pixel, std::size_t corner, std::size_t axis) {
                    return axis == 0u ? output[pixel][corner].x : output[pixel][corner].y;
                });
            return digest.hex();
        };
        return finishScenario(scenario, options, out, skipReason, corners, failure);
    }
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        Sha256 digest;
        double residual = 0.0;
        for (std::uint32_t row = 0u; row < kDrizzleGeometryHeight; row += bounded ? 32u : kDrizzleGeometryHeight)
        {
            an::DrizzlePixmapRequest mapRequest;
            mapRequest.storage = bounded ? an::DrizzleMapStorage::BoundedRows : an::DrizzleMapStorage::Dense;
            mapRequest.firstRow = row;
            mapRequest.haloRows = bounded ? 1u : 0u;
            auto map = an::generateDrizzlePixmap(*execution, *plan, mapRequest);
            if (!map.hasValue())
            {
                failure = map.status().message();
                return std::string{};
            }
            if (produceDigest)
            {
                const auto view = map->view().value();
                const auto first = view.row(view.storedRowBegin()).value();
                const auto count =
                    static_cast<std::size_t>(view.storedRowEnd() - view.storedRowBegin()) * kDrizzleGeometryWidth;
                const std::span<const float> xs(first.x.data(), count), ys(first.y.data(), count);
                digest.update(xs.data(), xs.size_bytes());
                digest.update(ys.data(), ys.size_bytes());
                residual = (std::max)(residual, drizzleGeometryResidual(view.storedRowBegin(), xs, ys, curved));
                out.counters["peakCoordinateBytes"] =
                    (std::max)(out.counters["peakCoordinateBytes"], static_cast<double>(map->coordinateBytes()));
                out.counters["generatedPairs"] += static_cast<double>(map->report().mapped + map->report().unmapped);
            }
        }
        if (produceDigest)
        {
            out.correctnessMetric = residual;
            out.counters["peakContextBytes"] = static_cast<double>(execution->memoryResource()->counters().peakBytes);
        }
        return produceDigest ? digest.hex() : std::string{};
    };
    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runSipInverseMapScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                       ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != kSipFixtureWidth ||
        scenario.input.height != kSipFixtureHeight || parameterValue(scenario, "order", "") != "2" ||
        parameterValue(scenario, "inverseModel", "") != "absent" ||
        parameterValue(scenario, "fixtureMapping", "") != kSipFixtureMapping)
    {
        skipReason = "scenario '" + scenario.id + "' declares a distortion map this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    out.fixtureDigest = fixture.digest;

    an::SipDistortionRequest request;
    request.referencePixel = an::PixelCoordinate{.x = kSipFixtureReferenceX, .y = kSipFixtureReferenceY};
    request.forwardX.order = 2u;
    request.forwardX.coefficients[2][0] = kSipFixtureA20;
    request.forwardX.coefficients[1][1] = kSipFixtureA11;
    request.forwardX.coefficients[0][2] = kSipFixtureA02;
    request.forwardY.order = 2u;
    request.forwardY.coefficients[2][0] = kSipFixtureB20;
    request.forwardY.coefficients[1][1] = kSipFixtureB11;
    request.forwardY.coefficients[0][2] = kSipFixtureB02;
    const an::Result<an::SipDistortion> model = an::SipDistortion::create(request);
    if (!model.hasValue())
    {
        skipReason = std::string("the distortion model could not be built: ") + model.status().message();
        return false;
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::size_t pixels = static_cast<std::size_t>(kSipFixtureWidth) * kSipFixtureHeight;
    std::vector<float> xMap(pixels, 0.0f);
    std::vector<float> yMap(pixels, 0.0f);
    an::SipMapRequest mapRequest;
    mapRequest.width = kSipFixtureWidth;
    mapRequest.height = kSipFixtureHeight;
    mapRequest.xMap = std::span<float>(xMap);
    mapRequest.yMap = std::span<float>(yMap);

    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;

    const std::function<std::string(bool)> body = [&](const bool produceDigest) {
        const an::Result<an::SipMapReport> report = an::computeSipInverseMap(*execution, *model, mapRequest);
        if (!report.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not map the distortion", report.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        Sha256 digest;
        digest.update(xMap.data(), xMap.size() * sizeof(float));
        digest.update(yMap.data(), yMap.size() * sizeof(float));
        out.counters["pixels"] = static_cast<double>(pixels);
        out.counters["unmapped"] = static_cast<double>(report->unmapped);
        out.counters["newtonRoute"] = static_cast<double>(report->newtonRoute);
        out.counters["newtonIterations"] = static_cast<double>(report->newtonIterations);
        out.counters["maximumResidualPixels"] = report->maximumResidualPixels;
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runRegistrationMapScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                         ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != kRegistrationFixtureWidth ||
        scenario.input.height != kRegistrationFixtureHeight || parameterValue(scenario, "polynomialOrder", "") != "2" ||
        parameterValue(scenario, "model", "") != "affine" ||
        parameterValue(scenario, "fixtureMapping", "") != kRegistrationFixtureMapping)
    {
        skipReason = "scenario '" + scenario.id + "' declares a coordinate map this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    out.fixtureDigest = fixture.digest;

    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = {kRegistrationFixtureMatrix00,
                                 kRegistrationFixtureMatrix01,
                                 kRegistrationFixtureMatrix02,
                                 kRegistrationFixtureMatrix10,
                                 kRegistrationFixtureMatrix11,
                                 kRegistrationFixtureMatrix12,
                                 0.0,
                                 0.0,
                                 1.0};
    request.polynomial.order = 2u;
    request.polynomial.x[0] = kRegistrationFixtureX0;
    request.polynomial.x[1] = kRegistrationFixtureX1;
    request.polynomial.x[2] = kRegistrationFixtureX2;
    request.polynomial.x[3] = kRegistrationFixtureX3;
    request.polynomial.x[4] = kRegistrationFixtureX4;
    request.polynomial.x[5] = kRegistrationFixtureX5;
    request.polynomial.y[0] = kRegistrationFixtureY0;
    request.polynomial.y[1] = kRegistrationFixtureY1;
    request.polynomial.y[2] = kRegistrationFixtureY2;
    request.polynomial.y[3] = kRegistrationFixtureY3;
    request.polynomial.y[4] = kRegistrationFixtureY4;
    request.polynomial.y[5] = kRegistrationFixtureY5;
    request.polynomial.normalizationOrigin = {.x = kRegistrationFixtureNormalizationOriginX,
                                              .y = kRegistrationFixtureNormalizationOriginY};
    request.polynomial.normalizationScale = {.x = kRegistrationFixtureNormalizationScaleX,
                                             .y = kRegistrationFixtureNormalizationScaleY};
    const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(request);
    if (!transform.hasValue())
    {
        skipReason = std::string("the registration transform could not be built: ") + transform.status().message();
        return false;
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::size_t pixels = static_cast<std::size_t>(kRegistrationFixtureWidth) * kRegistrationFixtureHeight;
    std::vector<float> xMap(pixels, 0.0f);
    std::vector<float> yMap(pixels, 0.0f);
    an::RegistrationMapRequest mapRequest;
    mapRequest.width = kRegistrationFixtureWidth;
    mapRequest.height = kRegistrationFixtureHeight;
    mapRequest.xMap = std::span<float>(xMap);
    mapRequest.yMap = std::span<float>(yMap);
    mapRequest.seed = an::RegistrationInverseSeed::AuthoritativeSeed;

    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;

    const std::function<std::string(bool)> body = [&](const bool produceDigest) {
        const an::Result<an::RegistrationMapReport> report =
            an::computeRegistrationDestinationToSourceMap(*execution, *transform, nullptr, mapRequest);
        if (!report.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not map the transform", report.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        Sha256 digest;
        digest.update(xMap.data(), xMap.size() * sizeof(float));
        digest.update(yMap.data(), yMap.size() * sizeof(float));
        out.counters["pixels"] = static_cast<double>(pixels);
        out.counters["unmapped"] = static_cast<double>(report->unmapped);
        out.counters["newtonIterations"] = static_cast<double>(report->newtonIterations);
        out.counters["maximumResidualPixels"] = report->maximumResidualPixels;
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runCometDetectionScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                        ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || scenario.input.width != kCometDetectionFixtureWidth ||
        scenario.input.height != kCometDetectionFixtureHeight ||
        parameterValue(scenario, "frames", "") != std::to_string(kCometDetectionFixtureFrameCount) ||
        parameterValue(scenario, "fixtureMapping", "") != kCometDetectionFixtureMapping ||
        parameterValue(scenario, "background", "") != "temporal-median" ||
        parameterValue(scenario, "track", "") != "linear" ||
        parameterValue(scenario, "minimumPositiveResidual", "") != "0.06" ||
        parameterValue(scenario, "detectionSigma", "") != "4" ||
        parameterValue(scenario, "minimumIntegratedSignalToNoise", "") != "5" ||
        parameterValue(scenario, "componentArea", "") != "3-48" ||
        parameterValue(scenario, "minimumDetections", "") != "5" ||
        parameterValue(scenario, "maximumTrackResidualPixels", "") != "1.5" ||
        parameterValue(scenario, "minimumMotionPixels", "") != "15" ||
        parameterValue(scenario, "maximumCandidates", "") != "8")
    {
        skipReason = "scenario '" + scenario.id + "' declares a comet search this runner does not implement";
        return false;
    }

    const CometDetectionFixture fixture = makeCometDetectionFixture(scenario.seed);
    Sha256 fixtureDigest;
    for (const std::vector<float>& frame : fixture.frames)
    {
        fixtureDigest.update(frame.data(), frame.size() * sizeof(float));
    }
    out.fixtureDigest = fixtureDigest.hex();

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const FixtureSemantics semantics = defaultFixtureSemantics(scenario.input);
    const std::uint64_t frameBytes =
        std::uint64_t{kCometDetectionFixtureWidth} * kCometDetectionFixtureHeight * sizeof(float);
    const std::optional<BridgedFixture> bridged =
        bridgeFixture(scenario.input, semantics, engine->limits(), frameBytes);
    if (!bridged.has_value())
    {
        skipReason = describeFixtureBridgeFailure(checkFixture(scenario.input, semantics, engine->limits(), frameBytes),
                                                  scenario.input);
        return false;
    }

    std::vector<an::CometDetectionFrame> frames;
    frames.reserve(fixture.frames.size());
    for (std::size_t index = 0u; index < fixture.frames.size(); ++index)
    {
        const std::vector<float>& samples = fixture.frames[index];
        const an::Result<an::StarFrameView> view =
            an::StarFrameView::create(samples.data(), frameBytes, bridged->layout, bridged->descriptor);
        if (!view.hasValue())
        {
            skipReason = describeEngineFailure("the engine refused a comet fixture frame", view.status());
            return false;
        }
        frames.push_back(
            {.frameId = 1000u + index,
             .midpoint = an::UtcInstant{.nanosecondsSinceEpoch = static_cast<std::int64_t>(index) * 10000000000LL},
             .frame = *view,
             .required = true});
    }

    const an::CometMovingObjectDetectionOptions request{.minimumPositiveResidual = 0.06,
                                                        .detectionSigma = 4.0,
                                                        .minimumIntegratedSignalToNoise = 5.0,
                                                        .minimumComponentArea = 3u,
                                                        .maximumComponentArea = 48u,
                                                        .minimumDetections = 5u,
                                                        .maximumTrackResidualPixels = 1.5,
                                                        .minimumMotionPixels = 15.0,
                                                        .maximumCandidates = 8u};
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;

    an::CometMovingObjectDetectionResult detected;
    std::string failure;
    const std::function<std::string(bool)> body = [&](const bool produceDigest) {
        an::Result<an::CometMovingObjectDetectionResult> result =
            an::detectMovingCometCandidates(*execution, frames, request);
        if (!result.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            }
            return std::string{};
        }
        detected = std::move(*result);
        if (!produceDigest)
        {
            return std::string{};
        }

        std::vector<CometDetectionFixturePoint> reported;
        if (!detected.candidates.empty())
        {
            reported.reserve(detected.candidates.front().points.size());
            for (const an::CometMovingObjectPoint& point : detected.candidates.front().points)
            {
                reported.push_back({.frameId = point.frameId, .x = point.position.x, .y = point.position.y});
            }
        }
        out.correctnessMetric = cometDetectionRootMeanSquare(fixture, reported, 0.0);
        out.counters["candidates"] = static_cast<double>(detected.candidates.size());
        out.counters["framesExamined"] = static_cast<double>(detected.framesExamined);
        out.counters["componentsExamined"] = static_cast<double>(detected.componentsExamined);
        out.counters["workingBytes"] = static_cast<double>(detected.peakWorkingBytes);
        out.counters["trackPoints"] = static_cast<double>(reported.size());
        if (!detected.candidates.empty())
        {
            out.counters["trackMotionPixels"] = detected.candidates.front().motionPixels;
            out.counters["trackResidualPixels"] = detected.candidates.front().rootMeanSquareResidualPixels;
        }

        Sha256 digest;
        digest.update(&detected.framesExamined, sizeof(detected.framesExamined));
        digest.update(&detected.componentsExamined, sizeof(detected.componentsExamined));
        digest.update(&detected.peakWorkingBytes, sizeof(detected.peakWorkingBytes));
        const std::uint64_t frameDiagnosticCount = detected.frameDiagnostics.size();
        digest.update(&frameDiagnosticCount, sizeof(frameDiagnosticCount));
        for (const an::CometDetectionFrameDiagnostics& diagnostic : detected.frameDiagnostics)
        {
            digest.update(&diagnostic.frameId, sizeof(diagnostic.frameId));
            digest.update(&diagnostic.componentsRetained, sizeof(diagnostic.componentsRetained));
        }
        const std::uint64_t candidateCount = detected.candidates.size();
        digest.update(&candidateCount, sizeof(candidateCount));
        for (const an::CometMovingObjectCandidate& candidate : detected.candidates)
        {
            const std::uint64_t pointCount = candidate.points.size();
            digest.update(&pointCount, sizeof(pointCount));
            digest.update(&candidate.score, sizeof(candidate.score));
            digest.update(&candidate.meanIntegratedSignalToNoise, sizeof(candidate.meanIntegratedSignalToNoise));
            digest.update(&candidate.persistence, sizeof(candidate.persistence));
            digest.update(&candidate.rootMeanSquareResidualPixels, sizeof(candidate.rootMeanSquareResidualPixels));
            digest.update(&candidate.motionPixels, sizeof(candidate.motionPixels));
            for (const an::CometMovingObjectPoint& point : candidate.points)
            {
                digest.update(&point.frameId, sizeof(point.frameId));
                digest.update(&point.midpoint.nanosecondsSinceEpoch, sizeof(point.midpoint.nanosecondsSinceEpoch));
                digest.update(&point.position.x, sizeof(point.position.x));
                digest.update(&point.position.y, sizeof(point.position.y));
                digest.update(&point.integratedSignal, sizeof(point.integratedSignal));
                digest.update(&point.integratedSignalToNoise, sizeof(point.integratedSignalToNoise));
                digest.update(&point.area, sizeof(point.area));
            }
        }
        return digest.hex();
    };

    const bool finished = finishScenario(scenario, options, out, skipReason, body, failure, &probe);
    if (an::MemoryResource* const memory = engine->memoryResource())
    {
        const an::MemoryCounters counters = memory->counters();
        out.memory.allocationCount = counters.totalAllocations;
        out.memory.allocatedBytes = counters.totalBytes;
    }
    return finished;
}

bool AfterNightEngineAdapter::runHealpixQueryScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                      ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        scenario.input.width != static_cast<std::uint32_t>(kCatalogFixtureQueryCount) || scenario.input.height != 1u ||
        parameterValue(scenario, "order", "") != std::to_string(kHealpixFixtureOrder) ||
        parameterValue(scenario, "cones", "") != std::to_string(kCatalogFixtureQueryCount) ||
        parameterValue(scenario, "radiusDegrees", "") != std::to_string(kHealpixFixtureQueryRadiusDegrees) ||
        parameterValue(scenario, "coverage", "") != "conservative-overlap" ||
        parameterValue(scenario, "fixtureMapping", "") != "healpix-cones-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a HEALPix query this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    out.fixtureDigest = fixture.digest;

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::HealpixGrid grid{.order = kHealpixFixtureOrder, .ordering = an::HealpixOrdering::Ring};
    const std::vector<CatalogFixtureCone> cones = makeHealpixFixtureCones();
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;

    const std::function<std::string(bool)> body = [&](const bool produceDigest) {
        Sha256 digest;
        std::uint64_t returned = 0u;
        std::uint64_t rings = 0u;
        std::uint64_t candidateCenters = 0u;
        for (const CatalogFixtureCone& cone : cones)
        {
            an::HealpixDiscQuery query;
            query.center = an::SkyCoordinate{.rightAscensionDegrees = cone.rightAscensionDegrees,
                                             .declinationDegrees = cone.declinationDegrees};
            query.radiusDegrees = kHealpixFixtureQueryRadiusDegrees;
            query.coverage = an::HealpixDiscCoverage::ConservativeOverlap;
            const an::Result<an::HealpixDiscResult> selected = an::queryHealpixDisc(*execution, grid, query);
            if (!selected.hasValue())
            {
                if (failure.empty())
                {
                    failure =
                        describeEngineFailure("the engine could not resolve the HEALPix tiles", selected.status());
                }
                return std::string{};
            }
            returned += selected->pixels.size();
            rings += selected->ringsExamined;
            candidateCenters += selected->candidateCentersExamined;
            if (produceDigest)
            {
                for (const an::HealpixPixelIndex& pixel : selected->pixels)
                {
                    digest.update(&pixel.value, sizeof(pixel.value));
                }
            }
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.counters["cones"] = static_cast<double>(cones.size());
        out.counters["returnedPixels"] = static_cast<double>(returned);
        out.counters["ringsExamined"] = static_cast<double>(rings);
        out.counters["candidateCentersExamined"] = static_cast<double>(candidateCenters);
        out.counters["fullSkyPixelsPerCone"] = static_cast<double>(an::healpixPixelCount(grid));
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runDeepSkyCatalogScenario(const ScenarioSpec& scenario, const RunOptions& options,
                                                        ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        scenario.input.width != static_cast<std::uint32_t>(kDeepSkyFixtureEntryCount) || scenario.input.height != 1u ||
        parameterValue(scenario, "schema", "deep-sky-v1") != "deep-sky-v1" ||
        parameterValue(scenario, "fixtureMapping", "catalog-deep-sky-v1") != "catalog-deep-sky-v1")
    {
        skipReason = "scenario '" + scenario.id + "' declares a catalog this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> entropy{reinterpret_cast<const float*>(fixture.bytes.data()),
                                         fixture.bytes.size() / sizeof(float)};
    const std::string catalogText = makeDeepSkyFixtureText(entropy);
    Sha256 textHash;
    textHash.update(catalogText.data(), catalogText.size());
    out.fixtureDigest = textHash.hex();

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::stop_source probe;
    an::Result<an::ExecutionContext> execution =
        an::ExecutionContext::create(*engine, measuredExecutionOptions(options, probe));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    an::CatalogTextRequest textRequest;
    textRequest.schema = an::CatalogTextSchema::ExtendedObject;
    an::SkyCatalogRequest catalogRequest;
    catalogRequest.photometricBand = "V";

    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    std::string failure;

    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        an::Result<an::ParsedCatalogText> parsed = parseCatalogText(*execution, catalogText, textRequest);
        if (!parsed.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not parse the deep-sky text", parsed.status());
            }
            return std::string{};
        }
        an::Result<an::SkyCatalog> catalog = buildSkyCatalog(*execution, std::move(parsed->entries), catalogRequest);
        if (!catalog.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not build the catalog", catalog.status());
            }
            return std::string{};
        }
        an::Result<an::CatalogAliasIndex> aliases = buildCatalogAliasIndex(*execution, *catalog);
        if (!aliases.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not build the alias relation", aliases.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }

        // The whole table in one deterministic order, collapsed to one row per object. The fixture
        // gives every row a distinct measured magnitude precisely so that this order is the
        // magnitudes rather than a tie-break.
        an::CatalogRegionQuery everything;
        everything.center = an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
        everything.radiusDegrees = 180.0;
        everything.magnitudeLimit = 100.0;
        everything.admitNamedEntriesWithoutMagnitude = true;
        an::Result<an::CatalogQueryResult> answered = queryCatalogRegion(*execution, *catalog, everything);
        if (!answered.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not read back the catalog", answered.status());
            }
            return std::string{};
        }
        an::Result<std::vector<std::uint32_t>> collapsed =
            collapseCatalogAliases(*execution, *aliases, answered->entryIndices);
        if (!collapsed.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine could not collapse the alias rows", collapsed.status());
            }
            return std::string{};
        }

        Sha256 digest;
        for (const std::uint32_t index : *collapsed)
        {
            const an::CatalogEntry* const entry = catalog->entry(index);
            if (entry == nullptr)
            {
                continue;
            }
            const double magnitude = entry->magnitude.value_or(99.0);
            digest.update(entry->identifier.data(), entry->identifier.size());
            digest.update(&entry->coordinate.rightAscensionDegrees, sizeof(double));
            digest.update(&entry->coordinate.declinationDegrees, sizeof(double));
            digest.update(&magnitude, sizeof(magnitude));
        }
        out.correctnessMetric = 0.0;
        out.counters["entries"] = static_cast<double>(catalog->entryCount());
        out.counters["answeredEntries"] = static_cast<double>(collapsed->size());
        out.counters["aliasRows"] = static_cast<double>(aliases->aliasCount);
        out.counters["examinedPairs"] = static_cast<double>(aliases->examinedPairCount);
        return digest.hex();
    };
    return finishScenario(scenario, options, out, skipReason, body, failure, &probe);
}

bool AfterNightEngineAdapter::runPhaseCorrelation(const ScenarioSpec& scenario, const RunOptions& options,
                                                  ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "shiftX", "7") != std::to_string(kPhaseCorrelationShiftX) ||
        parameterValue(scenario, "shiftY", "-4") != std::to_string(kPhaseCorrelationShiftY) ||
        parameterValue(scenario, "window", "hann") != "hann" ||
        parameterValue(scenario, "normalization", "subtract-mean") != "subtract-mean")
    {
        skipReason = "scenario '" + scenario.id + "' declares a measurement this runner does not implement";
        return false;
    }
    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    const std::span<const float> reference{reinterpret_cast<const float*>(fixture.bytes.data()),
                                           static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<float> target = cyclicallyShiftedFrame(reference, scenario.input.width, scenario.input.height,
                                                             kPhaseCorrelationShiftX, kPhaseCorrelationShiftY);

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::PhaseCorrelationResult> measured = an::computeImagePhaseCorrelation(
            *execution, an::SampleType::Float32, fixture.bytes.data(), source->layout, target.data(), source->layout,
            an::PhaseCorrelationRequest{.subtractMean = true, .window = an::PhaseCorrelationWindow::Hann});
        if (!measured.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", measured.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = phaseCorrelationResidual(measured->translationX, measured->translationY,
                                                         kPhaseCorrelationShiftX, kPhaseCorrelationShiftY);
        out.counters["response"] = measured->response;
        out.counters["peakValue"] = measured->peakValue;
        out.counters["workingBytes"] = static_cast<double>(measured->workingBytes);
        out.counters["plannedWorkers"] = static_cast<double>(measured->plannedWorkers);
        return Sha256::of(phaseCorrelationText(measured->translationX, measured->translationY, measured->response));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runMultiscaleTransform(const ScenarioSpec& scenario, const RunOptions& options,
                                                     ResultRecord& out, std::string& skipReason)
{
    double layerValue = 5.0;
    const std::string algorithm = parameterValue(scenario, "algorithm", "starlet");
    const bool starlet = algorithm == "starlet";
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || (!starlet && algorithm != "multiscale-linear") ||
        !doubleParameter(scenario, "layers", 5.0, layerValue, skipReason) || layerValue < 2.0 || layerValue > 8.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a transform this runner does not implement";
        }
        return false;
    }
    const auto layerCount = static_cast<std::uint32_t>(layerValue);

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    const an::Result<an::MultiscaleTransformPlan> plan = an::planMultiscaleTransform(
        {.algorithm = starlet ? an::MultiscaleAlgorithm::Starlet : an::MultiscaleAlgorithm::MultiscaleLinear,
         .layerCount = layerCount});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the multiscale transform could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const bool reconstruct = parameterValue(scenario, "reconstruct", "") == "unit-weights";
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<PlaneProbe> identityProbes =
        reconstruct ? computeIdentityOracle(fixtureSamples, scenario.input.width, scenario.input.height,
                                            kConvolutionProbeStride)
                    : std::vector<PlaneProbe>{};
    const std::uint32_t deepestScale = reconstruct ? 0u : std::uint32_t{1} << (layerCount - 2u);
    const double residualSigma = static_cast<double>(deepestScale) / 3.0;
    const auto residualRadius =
        reconstruct ? 0u : static_cast<std::uint32_t>(std::ceil(an::kDefaultGaussianTruncation * residualSigma));
    const std::vector<double> residualTaps =
        reconstruct ? std::vector<double>{} : gaussianSeparableTaps(residualRadius, residualSigma);
    const std::vector<ConvolutionProbe> residualProbes =
        reconstruct ? std::vector<ConvolutionProbe>{}
                    : computeSeparableGaussianOracle(fixtureSamples, scenario.input.width, scenario.input.height,
                                                     residualTaps, kConvolutionProbeStride);
    if ((reconstruct && identityProbes.empty()) || (!reconstruct && residualProbes.empty()))
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::MultiscaleDecomposition> layers = an::decomposeImageMultiscale(
            *engine, *execution, an::SampleType::Float32, fixture.bytes.data(), source->layout, *plan);
        if (!layers.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", layers.status());
            }
            return std::string{};
        }
        if (reconstruct)
        {
            const an::Result<an::SpatialFilterResult> summed = an::reconstructImageMultiscale(
                *execution, *layers, an::SampleType::Float32, destination->data(), source->layout);
            if (!summed.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", summed.status());
                }
                return std::string{};
            }
        }
        else
        {
            // Without a reconstruction the residual is what a caller keeps, so it is what the
            // digest reports rather than nothing at all.
            const std::optional<an::PlaneView<const float>> residual = layers->layer(0u, layers->layerCount() - 1u);
            if (!residual.has_value())
            {
                return std::string{};
            }
            for (std::uint32_t y = 0u; y < scenario.input.height; ++y)
            {
                const std::span<const float> from = *residual->row(y);
                std::copy(from.begin(), from.end(),
                          destination->data() + static_cast<std::size_t>(y) * scenario.input.width);
            }
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.counters["layers"] = static_cast<double>(layers->layerCount());
        out.counters["layerBytes"] = static_cast<double>(layers->layerBytes());
        out.counters["workingBytes"] = static_cast<double>(layers->result().workingBytes);
        out.counters["imagePasses"] = static_cast<double>(layers->result().imagePasses);
        if (reconstruct)
        {
            out.correctnessMetric = planeProbeError(produced, scenario.input.width, identityProbes);
            return Sha256::of(planeProbeText(produced, scenario.input.width, identityProbes));
        }
        out.correctnessMetric = separableConvolutionError(produced, scenario.input.width, residualProbes);
        return Sha256::of(separableConvolutionText(produced, scenario.input.width, residualProbes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runBilateralFilter(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    double spatialSigma = 2.0;
    double rangeSigma = 0.05;
    double radiusValue = 3.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        parameterValue(scenario, "border", "reflect-excluding-edge") != "reflect-excluding-edge" ||
        !doubleParameter(scenario, "spatialSigma", 2.0, spatialSigma, skipReason) ||
        !doubleParameter(scenario, "rangeSigma", 0.05, rangeSigma, skipReason) ||
        !doubleParameter(scenario, "radius", 3.0, radiusValue, skipReason) || radiusValue < 1.0 ||
        radiusValue > static_cast<double>(an::kMaxBilateralRadius))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a filter shape this runner does not implement";
        }
        return false;
    }
    const auto radius = static_cast<std::uint32_t>(radiusValue);

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    const an::Result<an::BilateralPlan> plan =
        an::planBilateralFilter({.spatialSigma = spatialSigma, .rangeSigma = rangeSigma, .radius = radius});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the bilateral filter could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<PlaneProbe> probes =
        computeBilateralOracle(fixtureSamples, scenario.input.width, scenario.input.height, radius, spatialSigma,
                               rangeSigma, kConvolutionProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::NoiseReductionResult> ran =
            an::filterImageBilateral(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                     destination->data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = planeProbeError(produced, scenario.input.width, probes);
        out.counters["windowSamples"] = static_cast<double>(plan->windowSamples());
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        out.counters["samplesChanged"] = static_cast<double>(ran->samplesChanged);
        return Sha256::of(planeProbeText(produced, scenario.input.width, probes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runTotalVariation(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    const std::string method = parameterValue(scenario, "method", "chambolle");
    double weight = 0.1;
    double iterationsValue = 30.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || (method != "chambolle" && method != "split-bregman") ||
        !doubleParameter(scenario, "weight", 0.1, weight, skipReason) || weight <= 0.0 ||
        !doubleParameter(scenario, "iterations", 30.0, iterationsValue, skipReason) || iterationsValue < 1.0 ||
        iterationsValue > static_cast<double>(an::kMaxTotalVariationIterations))
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a denoise this runner does not implement";
        }
        return false;
    }
    const auto iterations = static_cast<std::uint32_t>(iterationsValue);
    const bool isChambolle = method == "chambolle";

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::TotalVariationRequest request;
    request.method = isChambolle ? an::TotalVariationMethod::Chambolle : an::TotalVariationMethod::SplitBregman;
    request.weight = weight;
    request.maximumIterations = iterations;
    // No stopping rule, so the row times the iterations it says it times.
    request.tolerance = 0.0;
    const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the total-variation denoise could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    // Chambolle is held to the definition, position by position. The split reaches the same minimum
    // by a route whose intermediate states are not meant to match, so it is held to the functional
    // that the definition's own answer achieves.
    const std::vector<PlaneProbe> probes =
        isChambolle ? computeChambolleOracle(fixtureSamples, scenario.input.width, scenario.input.height, weight,
                                             iterations, kConvolutionProbeStride)
                    : std::vector<PlaneProbe>{};
    std::vector<float> reference;
    double referenceEnergy = 0.0;
    if (!isChambolle)
    {
        const std::vector<PlaneProbe> dense = computeChambolleOracle(
            fixtureSamples, scenario.input.width, scenario.input.height, 1.0 / weight, iterations * 8u, 1u);
        reference.reserve(dense.size());
        for (const PlaneProbe& probe : dense)
        {
            reference.push_back(static_cast<float>(probe.value));
        }
        referenceEnergy =
            totalVariationEnergy(reference, fixtureSamples, scenario.input.width, scenario.input.height, weight);
    }
    if (isChambolle && probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::TotalVariationResult> ran =
            an::denoiseImageTotalVariation(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                           destination->data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.counters["iterationsRun"] = static_cast<double>(ran->iterationsRun);
        out.counters["workingBytes"] = static_cast<double>(ran->noise.filter.workingBytes);
        out.counters["samplesChanged"] = static_cast<double>(ran->noise.samplesChanged);
        if (isChambolle)
        {
            out.correctnessMetric = planeProbeError(produced, scenario.input.width, probes);
            return Sha256::of(planeProbeText(produced, scenario.input.width, probes));
        }
        const double energy =
            totalVariationEnergy(produced, fixtureSamples, scenario.input.width, scenario.input.height, weight);
        out.counters["rofEnergy"] = energy;
        out.correctnessMetric =
            referenceEnergy > 0.0 ? std::abs(energy - referenceEnergy) / referenceEnergy : std::abs(energy);
        return Sha256::of(planeProbeText(
            produced, scenario.input.width,
            computeIdentityOracle(produced, scenario.input.width, scenario.input.height, kConvolutionProbeStride)));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runNoiseGeneration(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    double variance = 0.0025;
    double seedValue = 0.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "kind", "gaussian") != "gaussian" ||
        !doubleParameter(scenario, "variance", 0.0025, variance, skipReason) || variance <= 0.0 ||
        !doubleParameter(scenario, "seed", 0.0, seedValue, skipReason) || seedValue < 0.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a generator this runner does not implement";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::NoiseGenerationRequest request;
    request.kind = an::NoiseKind::Gaussian;
    request.variance = variance;
    request.seed = static_cast<std::uint64_t>(seedValue);
    // The clip would bias the variance this row measures, and what the row is about is the draw.
    request.clip = false;
    const an::Result<an::NoiseGenerationPlan> plan = an::planNoiseGeneration(request);
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the noise generator could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::NoiseGenerationResult> ran =
            an::generateImageNoise(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                   destination->data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        const double measured = addedNoiseVariance(produced, fixtureSamples);
        out.counters["measuredVariance"] = measured;
        out.counters["samplesChanged"] = static_cast<double>(ran->samplesChanged);
        out.counters["samplesClipped"] = static_cast<double>(ran->samplesClipped);
        // A distribution is what was asked for, so the distance from it is what is measured.
        out.correctnessMetric = std::abs(measured - variance) / variance;
        return Sha256::of(planeProbeText(
            produced, scenario.input.width,
            computeIdentityOracle(produced, scenario.input.width, scenario.input.height, kConvolutionProbeStride)));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runCurveInterpolation(const ScenarioSpec& scenario, const RunOptions& options,
                                                    ResultRecord& out, std::string& skipReason)
{
    const std::string ruleName = parameterValue(scenario, "interpolation", "");
    const std::string boundary = parameterValue(scenario, kCurveBoundaryParameter, "");
    const std::string samplesSpec = parameterValue(scenario, kCurveSamplesParameter, "");
    CurveRule rule = CurveRule::Akima;
    an::CurveInterpolation interpolation = an::CurveInterpolation::Akima;
    if (!curveRuleNamed(ruleName, rule) || rule == CurveRule::Linear)
    {
        skipReason = "scenario '" + scenario.id + "' names an interpolation this runner does not implement";
        return false;
    }
    interpolation = rule == CurveRule::NaturalCubic ? an::CurveInterpolation::NaturalCubic
                    : rule == CurveRule::Steffen    ? an::CurveInterpolation::Steffen
                                                    : an::CurveInterpolation::Akima;
    const bool codes = samplesSpec == kCurveCodes;
    const bool binned = samplesSpec == kCurveBinnedColour;
    const bool held = samplesSpec == kCurveHeldToUnit;
    const bool asGiven = samplesSpec == kCurveAsGiven;
    const bool ownCodes = samplesSpec == kCurveCodesAsGiven;
    const bool floating = held || asGiven;
    if ((boundary != kCurvePrepared && boundary != kCurveComplete && boundary != kCurvePreview) ||
        (!codes && !binned && !held && !asGiven && !ownCodes) || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u ||
        scenario.input.sampleType != (floating ? SampleType::Float32 : SampleType::UInt16) ||
        (ownCodes && boundary == kCurvePrepared) ||
        parameterValue(scenario, "tableEntries", "") !=
            std::to_string(ownCodes ? kCurveCodeTableEntries : kCurveTableEntries))
    {
        skipReason = "scenario '" + scenario.id + "' declares a curve this runner does not implement";
        return false;
    }

    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
    {
        return false;
    }
    // The samples the curve reads are made before the clock, alike in every run: fractions of full scale, or a
    // sixteen-bit image's own codes.
    std::vector<float> samples;
    std::vector<std::uint16_t> codeSamples;
    std::uint32_t width = scenario.input.width;
    std::uint32_t height = scenario.input.height;
    std::uint32_t channels = 1u;
    if (ownCodes)
    {
        codeSamples.resize(fixture.bytes.size() / sizeof(std::uint16_t));
        std::memcpy(codeSamples.data(), fixture.bytes.data(), codeSamples.size() * sizeof(std::uint16_t));
    }
    else if (codes)
    {
        samples = makeUnitIntervalCapture(fixture.bytes);
    }
    else if (binned)
    {
        samples = makeBinnedColourCapture(fixture.bytes, width, height);
        width /= 2u;
        height /= 2u;
        channels = 3u;
    }
    else
    {
        samples.resize(fixture.bytes.size() / sizeof(float));
        std::memcpy(samples.data(), fixture.bytes.data(), samples.size() * sizeof(float));
        if (held)
        {
            for (float& sample : samples)
            {
                sample = (std::min)((std::max)(sample, 0.0f), 1.0f);
            }
        }
    }
    const std::size_t sampleCount = static_cast<std::size_t>(width) * height * channels;
    if ((ownCodes ? codeSamples.size() : samples.size()) != sampleCount)
    {
        skipReason = "scenario '" + scenario.id + "' could not make its samples";
        return false;
    }
    std::vector<CurvePoint> oraclePoints(kCurveRowPoints.begin(), kCurveRowPoints.end());
    const CurveOracle oracle(oraclePoints, rule);
    std::vector<an::CurveControlPoint> points;
    for (const CurvePoint& point : kCurveRowPoints)
    {
        points.push_back(an::CurveControlPoint{.input = point.input, .output = point.output});
    }

    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    SampleDescriptor descriptor = fixture.descriptor;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.channels = channels;
    descriptor.sampleType = ownCodes ? SampleType::UInt16 : SampleType::Float32;
    descriptor.layout = Layout::Planar;
    descriptor.rowStrideBytes = 0u;
    descriptor.planeStrideBytes = 0u;
    descriptor.cfaPattern.clear();
    const std::size_t sampleBytes = ownCodes ? sizeof(std::uint16_t) : sizeof(float);
    const std::optional<BridgedFixture> source =
        bridgeFixture(descriptor, defaultFixtureSemantics(descriptor), engine->limits(), sampleCount * sampleBytes);
    if (!source.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' cannot describe its samples to the engine";
        return false;
    }
    std::optional<an::ImageStorage<float>> destination;
    std::optional<an::ImageStorage<std::uint16_t>> codeDestination;
    if (ownCodes)
    {
        an::Result<an::ImageStorage<std::uint16_t>> made = an::ImageStorage<std::uint16_t>::create(
            *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
        if (!made.hasValue())
        {
            skipReason = std::string("the destination could not be allocated: ") + made.status().message();
            return false;
        }
        codeDestination.emplace(std::move(*made));
    }
    else
    {
        an::Result<an::ImageStorage<float>> made = an::ImageStorage<float>::create(
            *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
        if (!made.hasValue())
        {
            skipReason = std::string("the destination could not be allocated: ") + made.status().message();
            return false;
        }
        destination.emplace(std::move(*made));
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    // The product's curve: its points under the dialog's rule, sampled into the largest table over a floating image
    // and into a table of every code over a sixteen-bit one, clamped outside them, normalized in and out, built on
    // the run's own workers (CurvesTransformationProcess.cpp's curvesProgramFor).
    an::TonePlanShape shape;
    shape.sampleType = ownCodes ? an::SampleType::UInt16 : an::SampleType::Float32;
    shape.channelCount = channels;
    shape.inputDomain = an::NumericDomain::NormalizedLinear;
    shape.outputDomain = an::NumericDomain::NormalizedLinear;
    an::CurveProgramRequest request;
    request.points = points;
    request.interpolation = interpolation;
    request.approximation =
        ownCodes ? an::CurveApproximation::ExactAtResidentValues : an::CurveApproximation::SampledLinear;
    request.tableEntries = kCurveTableEntries;
    request.domainPolicy = an::CurveDomainPolicy::Clamp;
    // A prepared row makes its curve once, before the clock.
    std::optional<an::CurveProgramPlan> prepared;
    if (boundary == kCurvePrepared)
    {
        an::Result<an::CurveProgramPlan> plan = an::buildCurveProgram(*execution, shape, request);
        if (!plan.hasValue())
        {
            skipReason = describeEngineFailure("the curve program could not be built", plan.status());
            return false;
        }
        prepared.emplace(std::move(*plan));
    }

    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    const std::array<an::PointwiseImageView, 1u> sourceStorage{an::PointwiseImageView{
        .samples = ownCodes ? static_cast<const void*>(codeSamples.data()) : static_cast<const void*>(samples.data()),
        .layout = &source->layout}};
    void* const destinationSamples =
        ownCodes ? static_cast<void*>(codeDestination->data()) : static_cast<void*>(destination->data());
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::optional<an::CurveProgramPlan> made;
        if (!prepared.has_value())
        {
            an::Result<an::CurveProgramPlan> plan = an::buildCurveProgram(*execution, shape, request);
            if (!plan.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the curve program could not be built", plan.status());
                }
                return std::string{};
            }
            made.emplace(std::move(*plan));
        }
        const an::CurveProgramPlan& plan = prepared.has_value() ? *prepared : *made;
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            *execution, plan.program(), sourceStorage,
            an::PointwiseMutableImageView{.samples = destinationSamples, .layout = &source->layout},
            an::KernelValidityView{});
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        CurveOutputCheck check;
        std::string digest;
        bool compared = false;
        if (ownCodes)
        {
            const std::span<const std::uint16_t> produced{codeDestination->data(), sampleCount};
            compared = checkCurveOutputCodes(oracle, codeSamples, produced, check);
            digest = Sha256::of(produced.data(), produced.size_bytes());
        }
        else
        {
            const std::span<const float> produced{destination->data(), sampleCount};
            compared = checkCurveOutput(oracle, samples, produced, check);
            digest = Sha256::of(produced.data(), produced.size_bytes());
        }
        if (!compared)
        {
            if (failure.empty())
            {
                failure = "scenario '" + scenario.id + "' could not compare its output with the oracle";
            }
            return std::string{};
        }
        reportCurveOutputCheck(check, out.counters);
        out.counters["tableEntries"] = static_cast<double>(plan.tableEntries());
        out.counters["maximumAbsoluteError"] = plan.maximumAbsoluteError();
        out.correctnessMetric = check.largest;
        return digest;
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runChromaticNoise(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    double amount = 1.0;
    double whiteLevel = 1.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u || parameterValue(scenario, "method", "average-neutral") != "average-neutral" ||
        parameterValue(scenario, "preserveLightness", "false") != "false" ||
        !doubleParameter(scenario, "amount", 1.0, amount, skipReason) || amount < 0.0 || amount > 1.0 ||
        !doubleParameter(scenario, "whiteLevel", 1.0, whiteLevel, skipReason) || whiteLevel <= 0.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a chromatic reduction this runner does not implement";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    const an::Result<an::ChromaticNoisePlan> plan = an::planChromaticNoiseReduction(
        {.method = an::ChromaticNoiseMethod::AverageNeutral, .amount = amount, .whiteLevel = whiteLevel});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the chromatic reduction could not be planned", plan.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<RgbProbe> probes =
        computeAverageNeutralOracle(fixtureSamples, scenario.input.width, scenario.input.height, kColorProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::NoiseReductionResult> ran =
            an::reduceImageChromaticNoise(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                          destination->data(), source->layout, *plan);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = rgbProbeError(produced, scenario.input.width, scenario.input.height, probes);
        out.counters["samplesChanged"] = static_cast<double>(ran->samplesChanged);
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        return Sha256::of(rgbProbeText(produced, scenario.input.width, scenario.input.height, probes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runBandingSuppression(const ScenarioSpec& scenario, const RunOptions& options,
                                                    ResultRecord& out, std::string& skipReason)
{
    double amount = 1.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "axis", "horizontal") != "horizontal" ||
        parameterValue(scenario, "protectHighlights", "false") != "false" ||
        !doubleParameter(scenario, "amount", 1.0, amount, skipReason) || amount < 0.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a banding reduction this runner does not implement";
        }
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<PlaneProbe> probes =
        computeHorizontalBandingOracle(fixtureSamples, scenario.input.width, scenario.input.height, kMedianProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::NoiseReductionResult> ran =
            an::suppressImageBanding(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                     destination->data(), source->layout, {.amount = amount});
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = planeProbeError(produced, scenario.input.width, probes);
        out.counters["samplesChanged"] = static_cast<double>(ran->samplesChanged);
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        return Sha256::of(planeProbeText(produced, scenario.input.width, probes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runImpulseRejection(const ScenarioSpec& scenario, const RunOptions& options,
                                                  ResultRecord& out, std::string& skipReason)
{
    double radiusValue = 1.0;
    double threshold = 0.2;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || parameterValue(scenario, "border", "truncated") != "truncated" ||
        !doubleParameter(scenario, "radius", 1.0, radiusValue, skipReason) || radiusValue < 1.0 || radiusValue > 4.0 ||
        !doubleParameter(scenario, "highThreshold", 0.2, threshold, skipReason) || threshold < 0.0 || threshold >= 1.0)
    {
        if (skipReason.empty())
        {
            skipReason = "scenario '" + scenario.id + "' declares a rejection this runner does not implement";
        }
        return false;
    }
    const auto radius = static_cast<std::uint32_t>(radiusValue);

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()),
                                                static_cast<std::size_t>(scenario.elementsProcessed)};
    const std::vector<PlaneProbe> probes = computeImpulseOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, radius, threshold, kMedianProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no oracle probes";
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const an::ImpulseNoiseRequest request{
        .radius = radius, .highThreshold = threshold, .borders = {.border = an::KernelBorderMode::NotApplicable}};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::NoiseReductionResult> ran =
            an::rejectImageImpulseNoise(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                        destination->data(), source->layout, request);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(),
                                              static_cast<std::size_t>(scenario.elementsProcessed)};
        out.correctnessMetric = planeProbeError(produced, scenario.input.width, probes);
        out.counters["samplesChanged"] = static_cast<double>(ran->samplesChanged);
        out.counters["workingBytes"] = static_cast<double>(ran->filter.workingBytes);
        return Sha256::of(planeProbeText(produced, scenario.input.width, probes));
    };

    return finishScenario(scenario, options, out, skipReason, body, failure);
}

bool AfterNightEngineAdapter::runResize(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                        std::string& skipReason)
{
    std::uint32_t outputWidth = 0u;
    std::uint32_t outputHeight = 0u;
    const std::string interpolation = parameterValue(scenario, "interpolation", "bilinear");
    const auto unsignedParameter = [&](const char* name, std::uint32_t fallback, std::uint32_t& value) {
        const auto at = scenario.parameters.find(name);
        if (at == scenario.parameters.end())
        {
            value = fallback;
            return true;
        }
        const char* const first = at->second.data();
        const char* const last = first + at->second.size();
        const auto parsed = std::from_chars(first, last, value);
        return parsed.ec == std::errc{} && parsed.ptr == last;
    };
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 1u || !unsignedParameter("outputWidth", 997u, outputWidth) || outputWidth == 0u ||
        !unsignedParameter("outputHeight", 773u, outputHeight) || outputHeight == 0u ||
        parameterValue(scenario, "border", "clamp") != "clamp")
    {
        skipReason = "scenario '" + scenario.id + "' declares a resize shape this runner does not implement";
        return false;
    }
    an::KernelInterpolation engineInterpolation = an::KernelInterpolation::Linear;
    std::uint32_t tapsPerOutput = 4u;
    if (interpolation == "nearest_neighbor")
    {
        engineInterpolation = an::KernelInterpolation::Nearest;
        tapsPerOutput = 1u;
    }
    else if (interpolation == "bicubic")
    {
        engineInterpolation = an::KernelInterpolation::Cubic;
        tapsPerOutput = 16u;
    }
    else if (interpolation == "area")
    {
        engineInterpolation = an::KernelInterpolation::Area;
        tapsPerOutput = 0u;
    }
    else if (interpolation == "lanczos4")
    {
        engineInterpolation = an::KernelInterpolation::Lanczos4;
        tapsPerOutput = 64u;
    }
    else if (interpolation == "bit_exact_bilinear")
    {
        engineInterpolation = an::KernelInterpolation::LinearExact;
    }
    else if (interpolation == "bit_exact_nearest_neighbor")
    {
        engineInterpolation = an::KernelInterpolation::NearestExact;
        tapsPerOutput = 1u;
    }
    else if (interpolation != "bilinear")
    {
        skipReason = "scenario '" + scenario.id + "' names an interpolation this runner does not implement";
        return false;
    }
    const bool bilinearOracle = interpolation == "bilinear" || interpolation == "bit_exact_bilinear";

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> sourceShape =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!sourceShape.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    SampleDescriptor destinationDescriptor = fixture.descriptor;
    destinationDescriptor.width = outputWidth;
    destinationDescriptor.height = outputHeight;
    destinationDescriptor.rowStrideBytes = 0u;
    destinationDescriptor.planeStrideBytes = 0u;
    const std::optional<BridgedFixture> destinationShape =
        bridgeFixture(destinationDescriptor, defaultFixtureSemantics(destinationDescriptor), engine->limits(),
                      destinationDescriptor.totalBytes());
    if (!destinationShape.has_value())
    {
        skipReason = describeFixtureBridgeFailure(checkFixture(destinationDescriptor,
                                                               defaultFixtureSemantics(destinationDescriptor),
                                                               engine->limits(), destinationDescriptor.totalBytes()),
                                                  destinationDescriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, destinationShape->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::size_t sourceCount = static_cast<std::size_t>(scenario.input.width) * scenario.input.height;
    const std::size_t outputCount = static_cast<std::size_t>(outputWidth) * outputHeight;
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(fixture.bytes.data()), sourceCount};
    const an::Result<an::GeometryPlan> plan =
        an::planResize(sourceShape->descriptor,
                       {.width = outputWidth,
                        .height = outputHeight,
                        .sampling = {.interpolation = engineInterpolation, .border = an::KernelBorderMode::Clamp}});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the resize could not be planned", plan.status());
        return false;
    }
    an::Result<an::ValidityMask> destinationValidity =
        an::ValidityMask::create(*engine, destinationShape->descriptor.extent());
    if (!destinationValidity.hasValue())
    {
        skipReason =
            describeEngineFailure("the resize validity mask could not be allocated", destinationValidity.status());
        return false;
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> destinationValidityView = destinationValidity->view();
    if (!destinationValidityView.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not borrow its resize validity mask";
        return false;
    }
    const std::vector<BilinearResampleProbe> probes = computeBilinearResizeOracle(
        fixtureSamples, scenario.input.width, scenario.input.height, outputWidth, outputHeight, kResampleProbeStride);
    if (bilinearOracle && probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no resize oracle probes";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::Result<an::GeometryResult> ran = an::transformImageGeometry(
            *execution, an::SampleType::Float32, fixture.bytes.data(), sourceShape->layout, destination->data(),
            destinationShape->layout, *plan, {}, *destinationValidityView);
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(), outputCount};
        out.correctnessMetric = bilinearOracle ? bilinearResizeError(produced, outputWidth, probes)
                                               : unitRangeResizeError(produced, kResampleProbeStride);
        out.counters["tapsPerOutput"] = static_cast<double>(tapsPerOutput);
        out.counters["plannedWorkers"] = static_cast<double>(ran->plannedWorkers);
        out.counters["workingBytes"] = static_cast<double>(ran->workingBytes);
        out.counters["validOutputs"] = static_cast<double>(ran->run.outputsWritten);
        return Sha256::of(produced.data(), produced.size_bytes());
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runTileRoundTrip(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    constexpr std::uint32_t kGrid = 3u;
    const std::string scalarAuthority = parameterValue(scenario, "scalarAuthority", "");
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u || scenario.input.width % kGrid == 0u || scenario.input.height % kGrid == 0u ||
        parameterValue(scenario, "grid", "3") != "3" || scalarAuthority != "facade")
    {
        skipReason = "scenario '" + scenario.id + "' declares a tile round trip this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = describeEngineFailure("the engine context could not be built", engine.status());
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = describeEngineFailure("the joined image could not be allocated", destination.status());
        return false;
    }
    const std::optional<an::ImageView<float>> destinationView = destination->view();
    if (!destinationView.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' could not borrow its joined image";
        return false;
    }

    std::vector<an::GeometryPlan> splitPlans;
    std::vector<an::GeometryPlan> joinPlans;
    std::vector<an::ImageStorage<float>> tiles;
    std::vector<an::ValidityMask> masks;
    std::vector<std::array<std::uint32_t, 4u>> regions;
    splitPlans.reserve(kGrid * kGrid);
    joinPlans.reserve(kGrid * kGrid);
    tiles.reserve(kGrid * kGrid);
    masks.reserve(kGrid * kGrid);
    regions.reserve(kGrid * kGrid);
    for (std::uint32_t tileRow = 0u; tileRow < kGrid; ++tileRow)
    {
        for (std::uint32_t tileColumn = 0u; tileColumn < kGrid; ++tileColumn)
        {
            const std::uint32_t x = tileColumn * (scenario.input.width / kGrid);
            const std::uint32_t y = tileRow * (scenario.input.height / kGrid);
            const std::uint32_t width =
                tileColumn + 1u == kGrid ? scenario.input.width - x : scenario.input.width / kGrid;
            const std::uint32_t height =
                tileRow + 1u == kGrid ? scenario.input.height - y : scenario.input.height / kGrid;
            an::Result<an::GeometryPlan> split =
                an::planCrop(source->descriptor, {.x = x, .y = y, .width = width, .height = height});
            if (!split.hasValue())
            {
                skipReason = describeEngineFailure("a split crop could not be planned", split.status());
                return false;
            }
            const std::optional<an::ImageLayout> tileLayout =
                an::ImageLayout::create({.extent = split->destinationDescriptor().extent(),
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = scenario.input.channels,
                                         .rowAlignmentBytes = 1u},
                                        engine->limits());
            if (!tileLayout.has_value())
            {
                skipReason = "scenario '" + scenario.id + "' could not describe a tile allocation";
                return false;
            }
            an::Result<an::ImageStorage<float>> tile = an::ImageStorage<float>::create(
                *engine, *tileLayout, an::StorageInitialization::Uninitialized, an::kDefaultBufferAlignmentBytes);
            an::Result<an::ValidityMask> mask = an::ValidityMask::create(*engine, tileLayout->extent());
            an::Result<an::GeometryPlan> join =
                an::planCrop(split->destinationDescriptor(), {.x = 0u, .y = 0u, .width = width, .height = height});
            if (!tile.hasValue() || !mask.hasValue() || !join.hasValue())
            {
                skipReason = "scenario '" + scenario.id + "' could not prepare its caller-owned tiles";
                return false;
            }
            splitPlans.push_back(std::move(*split));
            joinPlans.push_back(std::move(*join));
            tiles.push_back(std::move(*tile));
            masks.push_back(std::move(*mask));
            regions.push_back({x, y, width, height});
        }
    }

    an::ExecutionOptions executionOptions = executionOptionsForRun(options);
    executionOptions.backend = an::ComputeBackend::Reference;
    executionOptions.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptions);
    if (!execution.hasValue())
    {
        skipReason = describeEngineFailure("the operation could not be started", execution.status());
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Scalar;
    out.actualBackend = toBenchBackend(selection);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        for (std::size_t index = 0u; index < tiles.size(); ++index)
        {
            const std::optional<an::ValidityMaskView<std::uint8_t>> mask = masks[index].view();
            const std::optional<an::ImageLayout> tileLayout = tiles[index].layout();
            if (!mask.has_value() || !tileLayout.has_value())
            {
                failure = "the tile or validity map could not be borrowed";
                return std::string{};
            }
            const an::Result<an::GeometryResult> split =
                an::transformImageGeometry(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                           tiles[index].data(), *tileLayout, splitPlans[index], {}, *mask);
            if (!split.hasValue())
            {
                failure = describeEngineFailure("the explicit split failed", split.status());
                return std::string{};
            }
        }
        for (std::size_t index = 0u; index < tiles.size(); ++index)
        {
            const auto [x, y, width, height] = regions[index];
            const std::optional<an::ImageView<float>> joined = destinationView->subview(x, y, width, height);
            const std::optional<an::ValidityMaskView<std::uint8_t>> mask = masks[index].view();
            const std::optional<an::ImageLayout> tileLayout = tiles[index].layout();
            if (!joined.has_value() || !mask.has_value() || !tileLayout.has_value())
            {
                failure = "the joined tile or validity map could not be borrowed";
                return std::string{};
            }
            const an::Result<an::GeometryResult> joinedTile =
                an::transformImageGeometry(*execution, an::SampleType::Float32, tiles[index].data(), *tileLayout,
                                           joined->data(), joined->layout(), joinPlans[index], {}, *mask);
            if (!joinedTile.hasValue())
            {
                failure = describeEngineFailure("the explicit join failed", joinedTile.status());
                return std::string{};
            }
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric =
            std::memcmp(destination->data(), fixture.bytes.data(), fixture.bytes.size()) == 0 ? 0.0 : 1.0;
        out.counters["tiles"] = static_cast<double>(tiles.size());
        out.counters["callerOwnedTileBytes"] = static_cast<double>(fixture.bytes.size());
        return Sha256::of(destination->data(), fixture.bytes.size());
    };

    const bool finished = finishScenario(scenario, options, out, skipReason, body, failure);
    if (an::MemoryResource* const memory = engine->memoryResource())
    {
        const an::MemoryCounters counters = memory->counters();
        out.memory.allocationCount = counters.totalAllocations;
        out.memory.allocatedBytes = counters.totalBytes;
    }
    return finished;
}

bool AfterNightEngineAdapter::runGammaTransfer(const ScenarioSpec& scenario, const RunOptions& options,
                                               ResultRecord& out, std::string& skipReason)
{
    const bool fusedCurve = scenario.id == "pointwise.gamma_curve_fused.f32";
    const bool preview = scenario.id.rfind("pointwise.gamma_transfer.preview.", 0u) == 0u;
    // A capture's codes are carried into the unit interval before the clock, as the statistics rows do.
    const bool captured = scenario.inputSource == InputSource::Corpus;
    const std::string boundary = parameterValue(scenario, kPreviewGammaBoundaryParameter, kPreviewGammaApplication);
    const bool tableOnly = boundary == kPreviewGammaTable;
    double gamma = 0.0;
    double curveMiddleInput = 0.5;
    double curveMiddleOutput = 0.25;
    if (scenario.input.sampleType != (captured ? SampleType::UInt16 : SampleType::Float32) ||
        scenario.input.layout != Layout::Planar || scenario.input.channels != 1u ||
        (captured && (!preview || tableOnly || parameterValue(scenario, "samples", "") != "codes-over-65535")) ||
        (!preview && scenario.parameters.contains(kPreviewGammaBoundaryParameter)) ||
        (boundary != kPreviewGammaApplication && boundary != kPreviewGammaTableAndApplication && !tableOnly))
    {
        skipReason = "scenario '" + scenario.id + "' declares an input this runner does not implement";
        return false;
    }
    if (!doubleParameter(scenario, "gamma", 2.2, gamma, skipReason))
    {
        return false;
    }
    if (!(gamma > 0.0))
    {
        skipReason = "scenario '" + scenario.id + "' states a gamma that is not positive";
        return false;
    }
    if (fusedCurve && (!doubleParameter(scenario, "curveMiddleInput", 0.5, curveMiddleInput, skipReason) ||
                       !doubleParameter(scenario, "curveMiddleOutput", 0.25, curveMiddleOutput, skipReason) ||
                       parameterValue(scenario, "tableEntries", "4097") != "4097"))
    {
        return false;
    }
    if (preview && (parameterValue(scenario, "purpose", "realtime-preview") != "realtime-preview" ||
                    parameterValue(scenario, "tableEntries", "1024") != "1024"))
    {
        skipReason = "scenario '" + scenario.id + "' does not name the retained preview lookup";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    std::vector<float> capturedSamples;
    SampleDescriptor descriptor = fixture.descriptor;
    if (captured)
    {
        capturedSamples = makeUnitIntervalCapture(fixture.bytes);
        descriptor.sampleType = SampleType::Float32;
        descriptor.rowStrideBytes = 0u;
        descriptor.planeStrideBytes = 0u;
    }
    const std::uint8_t* const sourceBytes =
        captured ? reinterpret_cast<const std::uint8_t*>(capturedSamples.data()) : fixture.bytes.data();
    const std::size_t sourceByteCount = captured ? capturedSamples.size() * sizeof(float) : fixture.bytes.size();
    const FixtureSemantics semantics = defaultFixtureSemantics(descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(descriptor, semantics, engine->limits(), sourceByteCount);
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(descriptor, semantics, engine->limits(), sourceByteCount), descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::size_t sampleCount = static_cast<std::size_t>(scenario.input.width) * scenario.input.height;
    const std::span<const float> fixtureSamples{reinterpret_cast<const float*>(sourceBytes), sampleCount};
    // A table scored alone is applied to a ramp of its own width, so every entry is probed.
    const std::uint32_t probeStride = tableOnly ? 1u : kGammaProbeStride;
    const std::vector<GammaTransferProbe> probes =
        fusedCurve
            ? computeGammaLinearCurveOracle(fixtureSamples, gamma, curveMiddleInput, curveMiddleOutput, probeStride)
            : computeGammaTransferOracle(fixtureSamples, gamma, probeStride);
    if (probes.empty())
    {
        skipReason = "scenario '" + scenario.id + "' produced no gamma oracle probes";
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    an::TonePlanShape gammaShape;
    std::optional<an::PowerProgramPlan> productionGammaPlan;
    std::optional<an::RealtimePreviewGammaProgramPlan> previewGammaPlan;
    std::optional<an::PointwiseProgram> program;
    if (preview)
    {
        an::Result<an::RealtimePreviewGammaProgramPlan> madePreview =
            an::buildRealtimePreviewGammaProgram(gammaShape, gamma, an::GammaDirection::Encode);
        if (!madePreview.hasValue())
        {
            skipReason = describeEngineFailure("the preview gamma program could not be built", madePreview.status());
            return false;
        }
        previewGammaPlan.emplace(std::move(*madePreview));
    }
    else
    {
        an::Result<an::PowerProgramPlan> madeProduction =
            an::buildGammaProgram(gammaShape, gamma, an::GammaDirection::Encode);
        if (!madeProduction.hasValue())
        {
            skipReason = describeEngineFailure("the gamma program could not be built", madeProduction.status());
            return false;
        }
        productionGammaPlan.emplace(std::move(*madeProduction));
        program.emplace(productionGammaPlan->program());
    }
    std::optional<an::CurveProgramPlan> curvePlan;
    if (fusedCurve)
    {
        const std::array<an::CurveControlPoint, 3u> points{
            an::CurveControlPoint{.input = 0.0, .output = 0.0},
            an::CurveControlPoint{.input = curveMiddleInput, .output = curveMiddleOutput},
            an::CurveControlPoint{.input = 1.0, .output = 1.0}};
        an::TonePlanShape curveShape = gammaShape;
        curveShape.inputDomain = an::NumericDomain::EncodedNonlinear;
        curveShape.outputDomain = an::NumericDomain::EncodedNonlinear;
        an::CurveProgramRequest curveRequest;
        curveRequest.points = points;
        curveRequest.interpolation = an::CurveInterpolation::Linear;
        curveRequest.approximation = an::CurveApproximation::SampledLinear;
        curveRequest.tableEntries = 4097u;
        an::Result<an::CurveProgramPlan> madeCurve = an::buildCurveProgram(curveShape, curveRequest);
        if (!madeCurve.hasValue())
        {
            skipReason = describeEngineFailure("the curve program could not be built", madeCurve.status());
            return false;
        }
        curvePlan.emplace(std::move(*madeCurve));
        const std::array<an::PointwiseProgram, 2u> stages{productionGammaPlan->program(), curvePlan->program()};
        an::Result<an::PointwiseProgram> fused = an::fusePointwisePrograms(stages);
        if (!fused.hasValue())
        {
            skipReason = describeEngineFailure("the gamma and curve programs could not be fused", fused.status());
            return false;
        }
        program.emplace(std::move(*fused));
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = sourceBytes, .layout = &source->layout}};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        // A preview whose gamma moved plans its table again; the plan it replaces is released here.
        if (preview && boundary != kPreviewGammaApplication)
        {
            an::Result<an::RealtimePreviewGammaProgramPlan> rebuilt =
                an::buildRealtimePreviewGammaProgram(gammaShape, gamma, an::GammaDirection::Encode);
            if (!rebuilt.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the preview gamma program could not be built", rebuilt.status());
                }
                return std::string{};
            }
            previewGammaPlan = std::move(*rebuilt);
            if (tableOnly && !produceDigest)
            {
                return std::string{};
            }
        }
        const an::PointwiseMutableImageView output{.samples = destination->data(), .layout = &source->layout};
        const an::Result<an::PointwiseExecutionResult> ran =
            preview ? an::executeRealtimePreviewGamma(*execution, *previewGammaPlan, sources.front(), output,
                                                      an::KernelValidityView{})
                    : an::executePointwiseProgram(*execution, *program, sources, output, an::KernelValidityView{});
        if (!ran.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", ran.status());
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        const std::span<const float> produced{destination->data(), sampleCount};
        out.correctnessMetric = gammaTransferError(produced, probes);
        // Which of the three providers evaluated the transfer is recorded beside the timing: a
        // number produced by the platform math library and one produced by a compiled polynomial
        // are not the same measurement, and the report must not have to guess which it holds.
        if (productionGammaPlan.has_value())
        {
            out.counters["mathProvider"] =
                static_cast<double>(static_cast<std::uint8_t>(productionGammaPlan->provider()));
        }
        if (previewGammaPlan.has_value())
        {
            out.counters["previewTableEntries"] = static_cast<double>(previewGammaPlan->tableEntries());
            out.counters["previewMaximumAbsoluteError"] = previewGammaPlan->maximumAbsoluteError();
        }
        out.counters["fusedSteps"] = preview ? 1.0 : static_cast<double>(program->steps().size());
        out.counters["samplesConverted"] = static_cast<double>(ran->conversion.samplesConverted);
        if (curvePlan.has_value())
        {
            out.counters["curveTableEntries"] = static_cast<double>(curvePlan->tableEntries());
            out.counters["curveMaximumAbsoluteError"] = curvePlan->maximumAbsoluteError();
        }
        return Sha256::of(produced.data(), produced.size_bytes());
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runStretch(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                         std::string& skipReason)
{
    const auto stretch = parseStretchSettings(scenario, skipReason);
    if (!stretch.has_value())
        return false;
    if (options.forcedBackend.has_value() && *options.forcedBackend != BackendId::Scalar)
    {
        skipReason = "stretch benchmarks implement only the scalar backend";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
        return false;
    auto executionOptions = executionOptionsForRun(options);
    executionOptions.reductionDeterminism = an::ReductionDeterminism::DecisionFeeding;
    auto execution = an::ExecutionContext::create(*engine, executionOptions);
    if (!execution.hasValue())
        return false;
    const auto source = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                      fixture.bytes.size());
    if (!source.has_value())
        return false;
    const auto function = stretch->function;
    std::vector<float> output(static_cast<std::size_t>(scenario.input.width) * scenario.input.height);
    an::StretchImageRequest request;
    request.stretch.function = static_cast<an::StretchFunction>(function);
    request.stretch.amount = stretch->amount;
    request.stretch.bias = stretch->bias;
    request.stretch.symmetryPoint = stretch->symmetry;
    request.stretch.blackPoint = stretch->blackPoint;
    request.stretch.whitePoint = stretch->whitePoint;
    request.stretch.lowProtection = stretch->lowProtection;
    request.stretch.highProtection = stretch->highProtection;
    for (auto& channel : request.stretch.screenTransfer)
    {
        channel.midtones = stretch->midtones;
        channel.shadows = stretch->shadows;
        channel.highlights = stretch->highlights;
    }
    an::StatisticalStretchParameters statistical;
    statistical.targetMedian = stretch->targetMedian;
    statistical.blackpointSigma = stretch->blackpointSigma;
    statistical.noBlackClip = stretch->noBlackClip;
    statistical.normalize = stretch->normalize;
    statistical.applyCurves = stretch->applyCurves;
    statistical.curvesBoost = stretch->curvesBoost;
    // A prepared row measures the statistics once, outside timing, and hands them to every call,
    // as a preview trying several targets over one image does.
    std::optional<an::StatisticalStretchAnalysis> prepared;
    if (function == 9u && stretch->preparedAnalysis)
    {
        auto analysis = an::analyzeImageStatisticalStretch(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                           source->layout, statistical.linked);
        if (!analysis.hasValue())
        {
            skipReason = analysis.status().message();
            return false;
        }
        prepared = *analysis;
    }
    std::uint64_t stretchCalls = 0u;
    const std::function<std::string(bool)> body = [&](bool digest) {
        an::Status status;
        if (function == 9u)
        {
            const auto result = an::stretchImageStatistically(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                              source->layout, output.data(), source->layout,
                                                              statistical, prepared ? &*prepared : nullptr);
            status = result.status();
            if (digest && result.hasValue())
            {
                out.counters["analysisMedian"] = result->analysis.channels[0].median;
                out.counters["analysisMinimum"] = result->analysis.channels[0].minimum;
                out.counters["analysisBlackPoint"] = result->clipping.blackPoints[0];
            }
        }
        else
        {
            const auto result = an::stretchImage(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                 source->layout, output.data(), source->layout, request);
            status = result.status();
        }
        if (status.failed())
        {
            skipReason = status.message();
            return std::string{};
        }
        if (!digest)
        {
            ++stretchCalls;
            return std::string{};
        }
        // Bounded diagnostics: the range check and digest still visit every output, and small
        // numerical fixtures retain every sample counter.
        const std::size_t probeStride = 1u + (output.size() - 1u) / 4096u;
        out.counters["outputProbeStride"] = static_cast<double>(probeStride);
        out.counters["outputSamples"] = static_cast<double>(output.size());
        out.correctnessMetric = 0.0;
        for (std::size_t i = 0u; i < output.size(); ++i)
        {
            if (std::isfinite(output[i]) && (i % probeStride == 0u || i + 1u == output.size()))
                out.counters["sample" + std::to_string(i)] = output[i];
            if (!std::isfinite(output[i]) || output[i] < 0.0f || output[i] > 1.0f)
                out.correctnessMetric = 1.0;
        }
        // The complete digest is useful now; a range invariant alone does not qualify
        // unequal capture outputs against another engine.
        out.counters["rangeViolation"] = out.correctnessMetric.value_or(1.0);
        if (scenario.inputSource == InputSource::Corpus)
            out.correctnessMetric.reset();
        if (requiresHistogramStretchOracle(scenario))
            checkHistogramStretchOutput(*stretch, {reinterpret_cast<const float*>(fixture.bytes.data()), output.size()},
                                        output, out);
        if (requiresAsinhStretchOracle(scenario))
            checkAsinhStretchOutput(*stretch, {reinterpret_cast<const float*>(fixture.bytes.data()), output.size()},
                                    output, out);
        if (requiresGeneralizedHyperbolicStretchOracle(scenario))
            checkGeneralizedHyperbolicStretchOutput(
                *stretch, {reinterpret_cast<const float*>(fixture.bytes.data()), output.size()}, output, out);
        if (requiresMidtonesTransferStretchOracle(scenario))
            checkMidtonesTransferStretchOutput(
                *stretch, {reinterpret_cast<const float*>(fixture.bytes.data()), output.size()}, output, out);
        if (requiresStatisticalStretchOracle(scenario))
            checkStatisticalStretchOutput(
                *stretch, {reinterpret_cast<const float*>(fixture.bytes.data()), output.size()}, output, out);
        return Sha256::of(output.data(), output.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = BackendId::Scalar;
    out.counters["reductionDeterminism"] = static_cast<double>(execution->reductionDeterminism());
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
    {
        body(false);
        out.counters["stretchWarmupCalls"] += 1.0;
    }
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.counters.try_emplace("stretchWarmupCalls", 0.0);
    out.counters["stretchTimedCalls"] = static_cast<double>(stretchCalls) - out.counters["stretchWarmupCalls"];
    out.counters["stretchTimedBatch"] = timedBatchIterations(scenario).value();
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return skipReason.empty();
}

bool AfterNightEngineAdapter::runPresentation(const ScenarioSpec& scenario, const RunOptions& options,
                                              ResultRecord& out, std::string& skipReason)
{
    const bool subsampled = scenario.id == kPresentationSubsampled8bit;
    if (!subsampled && scenario.id != kPresentationAutostretchRgba)
    {
        skipReason = "AfterNightEngineBench does not implement presentation scenario '" + scenario.id + "'";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto source = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                      fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = "the presentation fixture cannot be described to the engine";
        return false;
    }
    // The viewer's defaults: an unlinked automatic stretch, or a linear subsampled presentation below full zoom.
    an::DisplayPresentationRequest request;
    request.stretch = subsampled ? an::PresentationStretch::Linear : an::PresentationStretch::Automatic;
    request.output = subsampled ? an::PresentationOutput::Interleaved8 : an::PresentationOutput::Rgba8;
    const auto production = an::planDisplayPresentation(request);
    const auto preview = an::planRealtimePreviewDisplayPresentation(request, kPresentationSubsampleFactor);
    if (!production.hasValue() || !preview.hasValue())
    {
        skipReason = "the presentation plan was refused";
        return false;
    }
    auto layoutRequest = subsampled ? an::realtimePreviewDisplayPresentationLayout(*preview, source->descriptor)
                                    : an::displayPresentationLayout(*production, source->descriptor);
    if (!layoutRequest.hasValue())
    {
        skipReason = layoutRequest.status().message();
        return false;
    }
    layoutRequest->rowAlignmentBytes = 1u;
    const auto layout = an::ImageLayout::create(*layoutRequest, engine->limits());
    if (!layout.has_value())
    {
        skipReason = "the presentation output cannot be laid out";
        return false;
    }
    std::vector<std::uint8_t> output(static_cast<std::size_t>(layout->totalBytes()), 0u);
    const an::DisplayPresentationSource view{
        .descriptor = &source->descriptor, .samples = fixture.bytes.data(), .layout = &source->layout};
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result = subsampled
                                ? an::presentImageRealtimePreview(*execution, *preview, view, output.data(), *layout)
                                : an::presentImage(*execution, *production, view, output.data(), *layout);
        if (!result.hasValue())
        {
            skipReason = result.status().message();
            return std::string{};
        }
        if (!digest)
            return std::string{};
        if (subsampled)
        {
            checkPresentationSubsampledOutput(
                {reinterpret_cast<const std::uint16_t*>(fixture.bytes.data()), fixture.bytes.size() / 2u},
                scenario.input.width, scenario.input.height, kPresentationSubsampleFactor, output, out);
        }
        else
        {
            checkPresentationAutostretchOutput(
                {reinterpret_cast<const float*>(fixture.bytes.data()), fixture.bytes.size() / sizeof(float)},
                scenario.input.width, scenario.input.height, output, out);
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                out.counters["engineMidtones" + std::to_string(channel)] = result->transfer[channel].midtones;
                out.counters["engineShadows" + std::to_string(channel)] = result->transfer[channel].shadows;
            }
        }
        return Sha256::of(output.data(), output.size());
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return skipReason.empty();
}

bool AfterNightEngineAdapter::runAutostretch(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    const std::string boundary = parameterValue(scenario, kAutostretchBoundaryParameter, "");
    const std::string samplesSpec = parameterValue(scenario, kAutostretchSamplesParameter, "");
    const bool codes = samplesSpec == kAutostretchCodes;
    const bool binned = samplesSpec == kAutostretchBinnedColour;
    const bool asGiven = samplesSpec == kAutostretchAsGiven;
    const bool linked = parameterValue(scenario, "linked", "") == "true";
    if ((boundary != kAutostretchComplete && boundary != kAutostretchStatistics && boundary != kAutostretchPrepared) ||
        (!codes && !binned && !asGiven) || scenario.input.layout != Layout::Planar ||
        scenario.input.sampleType != (asGiven ? SampleType::Float32 : SampleType::UInt16) ||
        (!asGiven && scenario.input.channels != 1u) ||
        (asGiven && scenario.input.channels != 1u && scenario.input.channels != 3u) ||
        parameterValue(scenario, "shadowsClipping", "") != "-2.8" ||
        parameterValue(scenario, "targetBackground", "") != "0.25")
    {
        skipReason = "scenario '" + scenario.id + "' declares an autostretch this runner does not implement";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    // The samples the composition reads are made before the clock, alike in every run.
    std::vector<float> samples;
    std::uint32_t width = scenario.input.width;
    std::uint32_t height = scenario.input.height;
    std::uint32_t channels = scenario.input.channels;
    if (codes)
    {
        samples = makeUnitIntervalCapture(fixture.bytes);
    }
    else if (binned)
    {
        samples = makeBinnedColourCapture(fixture.bytes, width, height);
        width /= 2u;
        height /= 2u;
        channels = 3u;
    }
    else
    {
        samples.resize(fixture.bytes.size() / sizeof(float));
        std::memcpy(samples.data(), fixture.bytes.data(), samples.size() * sizeof(float));
    }
    if (samples.size() != static_cast<std::size_t>(width) * height * channels)
    {
        skipReason = "scenario '" + scenario.id + "' could not make its samples";
        return false;
    }
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    SampleDescriptor descriptor = fixture.descriptor;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.channels = channels;
    descriptor.sampleType = SampleType::Float32;
    descriptor.layout = Layout::Planar;
    descriptor.rowStrideBytes = 0u;
    descriptor.planeStrideBytes = 0u;
    // A colour frame binned from a Bayer capture is colour, not a mosaic.
    descriptor.cfaPattern.clear();
    const auto source = bridgeFixture(descriptor, defaultFixtureSemantics(descriptor), engine->limits(),
                                      samples.size() * sizeof(float));
    if (!source.has_value())
    {
        skipReason = "the autostretch samples cannot be described to the engine";
        return false;
    }
    // Apply Stretch's defaults: the production plan, the automatic stretch, a Float32 image written back.
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Automatic;
    request.linked = linked;
    request.output = an::PresentationOutput::WorkingFloat32;
    const auto plan = an::planDisplayPresentation(request);
    if (!plan.hasValue())
    {
        skipReason = "the autostretch plan was refused";
        return false;
    }
    auto layoutRequest = an::displayPresentationLayout(*plan, source->descriptor);
    if (!layoutRequest.hasValue())
    {
        skipReason = layoutRequest.status().message();
        return false;
    }
    // The image Apply writes back is tightly packed, as the source is.
    layoutRequest->rowAlignmentBytes = 1u;
    const auto layout = an::ImageLayout::create(*layoutRequest, engine->limits());
    if (!layout.has_value() || layout->totalBytes() != samples.size() * sizeof(float))
    {
        skipReason = "the autostretch output cannot be laid out as tightly packed planes";
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, *layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the autostretch output could not be allocated: ") + destination.status().message();
        return false;
    }
    const an::DisplayPresentationSource view{
        .descriptor = &source->descriptor, .samples = samples.data(), .layout = &source->layout};
    // A prepared row measures its image once, before the clock, as a preview that applies it again does.
    an::DisplayPresentationAnalysis prepared{};
    if (boundary == kAutostretchPrepared)
    {
        const auto analysis = an::analyzeDisplayPresentation(*execution, *plan, view);
        if (!analysis.hasValue())
        {
            skipReason = analysis.status().message();
            return false;
        }
        prepared = *analysis;
    }
    const std::size_t total = samples.size();
    const std::function<std::string(bool)> body = [&](bool digest) {
        an::DisplayPresentationAnalysis analysis{};
        std::array<an::MidtonesTransferParameters, 3u> transfer{};
        if (boundary == kAutostretchStatistics)
        {
            const auto measured = an::analyzeDisplayPresentation(*execution, *plan, view);
            if (!measured.hasValue())
            {
                skipReason = measured.status().message();
                return std::string{};
            }
            analysis = *measured;
        }
        else
        {
            const auto result = an::presentImage(*execution, *plan, view, destination->data(), *layout,
                                                 boundary == kAutostretchPrepared ? &prepared : nullptr);
            if (!result.hasValue())
            {
                skipReason = result.status().message();
                return std::string{};
            }
            analysis = result->analysis;
            transfer = result->transfer;
        }
        if (!digest)
            return std::string{};
        AutostretchReport report;
        report.channels = analysis.colorChannels;
        for (std::uint32_t channel = 0u; channel < (std::min)(report.channels, 3u); ++channel)
        {
            report.medians[channel] = analysis.medians[channel];
            report.deviations[channel] = analysis.medianAbsoluteDeviations[channel];
            report.transfers[channel] = PresentationTransfer{.midtones = transfer[channel].midtones,
                                                             .shadows = transfer[channel].shadows,
                                                             .highlights = transfer[channel].highlights};
        }
        const std::span<const float> output = boundary == kAutostretchStatistics
                                                  ? std::span<const float>{}
                                                  : std::span<const float>(destination->data(), total);
        checkAutostretchOutput(
            scenario, samples, output, report,
            codes ? exactCapturedLocations(fixture.bytes) : exactAutostretchLocations(samples, channels), out);
        if (boundary == kAutostretchStatistics)
        {
            return Sha256::of(autostretchStatisticsText(report));
        }
        return Sha256::of(destination->data(), total * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return skipReason.empty();
}

namespace
{

/// The channel scope row's process: the engine's gamma program over the plane the scope hands it.
class GammaPlaneOperation final : public an::ChannelScopeOperation
{
public:
    /// Borrows the program for the operation's life.
    explicit GammaPlaneOperation(const an::PointwiseProgram& program) noexcept : m_program(program) {}

    an::Status run(const an::ExecutionContext& execution, const an::ChannelScopeInput& input,
                   const an::ChannelScopeOutput& output) override
    {
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = input.samples, .layout = input.layout}};
        const auto ran = an::executePointwiseProgram(
            execution, m_program, sources,
            an::PointwiseMutableImageView{.samples = output.samples, .layout = output.layout}, input.validity);
        return ran.hasValue() ? an::Status::success() : ran.status();
    }

private:
    const an::PointwiseProgram& m_program;
};

} // namespace

bool AfterNightEngineAdapter::runChannelScope(const ScenarioSpec& scenario, const RunOptions& options,
                                              ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto source = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                      fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = "the channel scope fixture cannot be described to the engine";
        return false;
    }
    // The product's channel processor reads a colour frame as sRGB-encoded and hands the process the lightness packed
    // into the unit interval; the process is the gamma correction at 2.2.
    const auto plan = an::planChannelScope(
        an::ChannelScopeRequest{.scope = an::ChannelScope::CieLightness,
                                .sourceEncoding = an::ColorEncoding{.schema = an::ChannelSchema::Rgb,
                                                                    .transfer = an::ColorTransferFunction::Srgb},
                                .units = an::ChannelScopeUnits::UnitInterval});
    const auto gamma = an::buildGammaProgram(an::TonePlanShape{}, kChannelScopeGamma, an::GammaDirection::Encode);
    if (!plan.hasValue() || !gamma.hasValue())
    {
        skipReason = describeEngineFailure("the channel scope could not be planned",
                                           plan.hasValue() ? gamma.status() : plan.status());
        return false;
    }
    GammaPlaneOperation operation(gamma->program());
    std::vector<std::uint8_t> output(static_cast<std::size_t>(source->layout.totalBytes()), 0u);
    const an::ChannelScopeSource view{
        .descriptor = &source->descriptor, .samples = fixture.bytes.data(), .layout = &source->layout};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result = an::applyChannelScope(*execution, *plan, view, operation, output.data(), source->layout);
        if (!result.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        checkChannelScopeOutput(
            {reinterpret_cast<const float*>(fixture.bytes.data()), fixture.bytes.size() / sizeof(float)},
            scenario.input.width, scenario.input.height,
            {reinterpret_cast<const float*>(output.data()), output.size() / sizeof(float)}, kChannelScopeGamma, out);
        return Sha256::of(output.data(), output.size());
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runMaskOverlay(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto source = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                      fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = "the mask overlay fixture cannot be described to the engine";
        return false;
    }
    // The viewer's red tint of the mask as it is: the layer it paints over the image.
    const auto plan = an::planMaskOverlay(an::MaskOverlayRequest{});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the mask overlay could not be planned", plan.status());
        return false;
    }
    auto layoutRequest = an::maskOverlayLayout(*plan, source->descriptor);
    if (!layoutRequest.hasValue())
    {
        skipReason = layoutRequest.status().message();
        return false;
    }
    const auto layout = an::ImageLayout::create(*layoutRequest, engine->limits());
    if (!layout.has_value())
    {
        skipReason = "the mask overlay layer cannot be laid out";
        return false;
    }
    std::vector<std::uint8_t> output(static_cast<std::size_t>(layout->totalBytes()), 0u);
    const an::MaskOverlaySource mask{
        .descriptor = &source->descriptor, .samples = fixture.bytes.data(), .layout = &source->layout};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result = an::renderMaskOverlay(*execution, *plan, mask, nullptr, output.data(), *layout);
        if (!result.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        checkMaskOverlayOutput(fixture.bytes, output, 255u, 0u, 0u, out);
        return Sha256::of(output.data(), output.size());
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runDabBlend(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                          std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const auto brush = an::planBrush(an::BrushRequest{.diameter = kDabBlendDiameter,
                                                      .hardness = kDabBlendHardness,
                                                      .shape = an::BrushTipShape::Round,
                                                      .spacing = kDabBlendSpacing,
                                                      .flow = kDabBlendFlow});
    const auto blend =
        an::planLayerBlend(an::LayerBlendRequest{.mode = an::LayerBlendMode::Normal, .opacity = kDabBlendOpacity});
    if (!brush.hasValue() || !blend.hasValue())
    {
        skipReason = describeEngineFailure("the stroke could not be planned",
                                           brush.hasValue() ? blend.status() : brush.status());
        return false;
    }
    const std::span<const float> paint{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * sizeof(float);
    const auto paintView = an::PlaneView<const float>::create(paint, width, height, 1u, rowBytes);
    std::vector<float> stroke(paint.size(), 0.0f);
    std::vector<float> painted(paint.size(), 0.0f);
    std::vector<float> tip(static_cast<std::size_t>(brush->tipSide()) * brush->tipSide(), 0.0f);
    const auto strokeView = an::PlaneView<float>::create(stroke, width, height, 1u, rowBytes);
    const auto paintedView = an::PlaneView<float>::create(painted, width, height, 1u, rowBytes);
    const auto tipView = an::PlaneView<float>::create(tip, brush->tipSide(), brush->tipSide(), 1u,
                                                      static_cast<std::uint64_t>(brush->tipSide()) * sizeof(float));
    if (!paintView.has_value() || !strokeView.has_value() || !paintedView.has_value() || !tipView.has_value())
    {
        skipReason = "the stroke's planes cannot be described to the engine";
        return false;
    }
    const std::vector<DabBlendPoint> points = dabBlendStrokePoints(width, height);
    std::vector<an::BrushDab> dabs;
    std::string failure;
    const auto fail = [&](const an::Status& status) {
        if (failure.empty())
            failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", status);
        return std::string{};
    };
    // The stroke plane cleared, the tip built, every point's segment dabbed in, and the stroke blended into the paint
    // once from the paint as it was.
    const std::function<std::string(bool)> body = [&](bool digest) {
        std::fill(stroke.begin(), stroke.end(), 0.0f);
        const an::Status tipWritten = an::writeBrushTip(*brush, *tipView);
        if (tipWritten.failed())
            return fail(tipWritten);
        for (std::size_t index = 0u; index < points.size(); ++index)
        {
            const an::BrushPoint end{.x = points[index].x, .y = points[index].y};
            an::BrushSegment segment = an::makeBrushSegment(end, end, end);
            if (index > 0u)
            {
                const DabBlendPoint& start = points[index - 1u];
                const DabBlendPoint& before = points[index >= 2u ? index - 2u : index - 1u];
                segment = an::makeBrushSegment({.x = before.x, .y = before.y}, {.x = start.x, .y = start.y}, end);
            }
            const auto count = an::countBrushSegmentDabs(*brush, segment);
            if (!count.hasValue())
                return fail(count.status());
            dabs.resize(*count);
            const auto placed = an::placeBrushSegmentDabs(*brush, segment, dabs);
            if (!placed.hasValue())
                return fail(placed.status());
            const auto stamped = an::stampBrushDabs(*execution, *brush, *tipView, dabs, 1.0f, *strokeView);
            if (!stamped.hasValue())
                return fail(stamped.status());
        }
        const auto blended = an::blendLayerPlane(*execution, *blend, *paintView, *strokeView, *paintedView);
        if (!blended.hasValue())
            return fail(blended.status());
        if (!digest)
            return std::string{};
        checkDabBlendOutput(paint, width, height, painted, out);
        return Sha256::of(painted.data(), painted.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runLrgbCombination(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> color{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    // The luminance frame is derived before the clock, by the shared definition.
    const std::vector<float> luminance = lrgbLuminanceFrame(color, width, height);
    SampleDescriptor plane = fixture.descriptor;
    plane.channels = 1u;
    const auto colorImage = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor),
                                          engine->limits(), fixture.bytes.size());
    const auto luminanceImage =
        bridgeFixture(plane, defaultFixtureSemantics(plane), engine->limits(), luminance.size() * sizeof(float));
    if (!colorImage.has_value() || !luminanceImage.has_value())
    {
        skipReason = "the LRGB frames cannot be described to the engine";
        return false;
    }
    const auto plan = an::planLrgbCombination(an::LrgbCombinationRequest{.lightnessWeight = kLrgbLightnessWeight,
                                                                         .lightnessMidtones = kLrgbLightnessMidtones,
                                                                         .chromaMidtones = kLrgbSaturationMidtones,
                                                                         .channelWeights = kLrgbChannelWeights});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the LRGB combination could not be planned", plan.status());
        return false;
    }
    auto layoutRequest = an::lrgbCombinationLayout(*plan, colorImage->descriptor);
    const auto layout = layoutRequest.hasValue() ? an::ImageLayout::create(*layoutRequest, engine->limits())
                                                 : std::optional<an::ImageLayout>{};
    if (!layout.has_value())
    {
        skipReason = "the LRGB output cannot be laid out";
        return false;
    }
    std::vector<float> output(static_cast<std::size_t>(layout->totalBytes() / sizeof(float)), 0.0f);
    const an::LrgbCombinationSource colorSource{
        .descriptor = &colorImage->descriptor, .samples = fixture.bytes.data(), .layout = &colorImage->layout};
    const an::LrgbCombinationSource luminanceSource{
        .descriptor = &luminanceImage->descriptor, .samples = luminance.data(), .layout = &luminanceImage->layout};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result = an::combineLrgb(*execution, *plan, colorSource, luminanceSource, output.data(), *layout);
        if (!result.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        checkLrgbOutput(color, luminance, width, height, output, out);
        return Sha256::of(output.data(), output.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runBlendModes(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                            std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> target{reinterpret_cast<const float*>(fixture.bytes.data()),
                                        fixture.bytes.size() / sizeof(float)};
    // The second frame is derived before the clock, by the shared definition.
    const std::vector<float> second = blendSecondFrame(target, width, height);
    const auto image = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                     fixture.bytes.size());
    if (!image.has_value())
    {
        skipReason = "the blend frames cannot be described to the engine";
        return false;
    }
    // The composition's modes in the product's menu order, which is the order of `kBlendModeLabels`.
    std::vector<an::BlendCompositionPlan> plans;
    for (std::uint8_t mode = 0u; mode < static_cast<std::uint8_t>(kBlendModeLabels.size()); ++mode)
    {
        const auto plan =
            an::planBlendComposition(an::BlendCompositionRequest{.mode = static_cast<an::BlendCompositionMode>(mode)});
        if (!plan.hasValue())
        {
            skipReason = describeEngineFailure("a blend mode could not be planned", plan.status());
            return false;
        }
        plans.push_back(*plan);
    }
    auto layoutRequest = an::blendCompositionLayout(plans.front(), image->descriptor, image->descriptor);
    const auto layout = layoutRequest.hasValue() ? an::ImageLayout::create(*layoutRequest, engine->limits())
                                                 : std::optional<an::ImageLayout>{};
    if (!layout.has_value())
    {
        skipReason = "the blend output cannot be laid out";
        return false;
    }
    std::vector<float> output(static_cast<std::size_t>(layout->totalBytes() / sizeof(float)), 0.0f);
    const an::BlendCompositionSource targetSource{
        .descriptor = &image->descriptor, .samples = fixture.bytes.data(), .layout = &image->layout};
    const an::BlendCompositionSource blendSource{
        .descriptor = &image->descriptor, .samples = second.data(), .layout = &image->layout};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        BlendModesCheck check;
        std::string digests;
        for (std::size_t mode = 0u; mode < plans.size(); ++mode)
        {
            const auto result =
                an::composeBlend(*execution, plans[mode], targetSource, blendSource, output.data(), *layout);
            if (!result.hasValue())
            {
                if (failure.empty())
                    failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
                return std::string{};
            }
            if (!digest)
                continue;
            checkBlendModeOutput(mode, target, second, width, height, output, check);
            digests += Sha256::of(output.data(), output.size() * sizeof(float));
        }
        if (!digest)
            return std::string{};
        finishBlendModesCheck(check, out);
        return Sha256::of(digests.data(), digests.size());
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runStarRecombination(const ScenarioSpec& scenario, const RunOptions& options,
                                                   ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> stars{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    // The starless frame and the mask are derived before the clock, by the shared definitions.
    const std::vector<float> starless = recombinationStarless(width, height);
    const std::vector<float> mask = recombinationMask(stars, width, height);
    SampleDescriptor plane = fixture.descriptor;
    plane.channels = 1u;
    const auto image = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                     fixture.bytes.size());
    const auto maskImage =
        bridgeFixture(plane, defaultFixtureSemantics(plane), engine->limits(), mask.size() * sizeof(float));
    if (!image.has_value() || !maskImage.has_value())
    {
        skipReason = "the recombination frames cannot be described to the engine";
        return false;
    }
    const auto plan =
        an::planStarMaskRecombination(an::StarMaskRecombinationRequest{.edgeTightening = kRecombinationTightening});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the recombination could not be planned", plan.status());
        return false;
    }
    auto layoutRequest = an::starMaskRecombinationLayout(*plan, image->descriptor, image->descriptor);
    const auto layout = layoutRequest.hasValue() ? an::ImageLayout::create(*layoutRequest, engine->limits())
                                                 : std::optional<an::ImageLayout>{};
    if (!layout.has_value())
    {
        skipReason = "the recombination output cannot be laid out";
        return false;
    }
    std::vector<float> output(static_cast<std::size_t>(layout->totalBytes() / sizeof(float)), 0.0f);
    const an::StarMaskRecombinationSource starlessSource{
        .descriptor = &image->descriptor, .samples = starless.data(), .layout = &image->layout};
    const an::StarMaskRecombinationSource starsSource{
        .descriptor = &image->descriptor, .samples = fixture.bytes.data(), .layout = &image->layout};
    const an::StarMaskRecombinationSource maskSource{
        .descriptor = &maskImage->descriptor, .samples = mask.data(), .layout = &maskImage->layout};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result =
            an::recombineStarMask(*execution, *plan, starlessSource, starsSource, &maskSource, output.data(), *layout);
        if (!result.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        checkRecombinationOutput(stars, starless, mask, width, height, output, out);
        return Sha256::of(output.data(), output.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runEdgeTighten(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::span<const float> stars{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    const auto image = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                     fixture.bytes.size());
    if (!image.has_value())
    {
        skipReason = "the edge tightening frame cannot be described to the engine";
        return false;
    }
    // The product's Tighten Edges request at the row's amount: nothing added, held to the unit interval, a neighbour
    // past the frame's edge read as zero.
    const auto plan = an::planEdgeTighten(
        an::EdgeTightenRequest{.amount = kEdgeTightenAmount, .offset = 0.0, .lowest = 0.0, .highest = 1.0});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the edge tightening could not be planned", plan.status());
        return false;
    }
    std::vector<float> output(stars.size(), 0.0f);
    std::string failure;
    std::uint64_t passes = 0u;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result = an::filterImageEdgeTighten(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                       image->layout, output.data(), image->layout, *plan);
        if (!result.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            return std::string{};
        }
        passes = result->imagePasses;
        if (!digest)
            return std::string{};
        checkEdgeTightenOutput(stars, scenario.input.width, scenario.input.height, output, out);
        return Sha256::of(output.data(), output.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.counters["imagePasses"] = static_cast<double>(passes);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runCometComposite(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> field{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    // Both registrations of every frame are derived before the clock, by the shared definitions.
    std::vector<std::vector<float>> starFrames;
    std::vector<std::vector<float>> cometFrames;
    for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
    {
        starFrames.push_back(cometStarAlignedFrame(field, width, height, frame));
        cometFrames.push_back(cometCometAlignedFrame(field, width, height, frame));
    }
    const auto image = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                     fixture.bytes.size());
    if (!image.has_value())
    {
        skipReason = "the comet frames cannot be described to the engine";
        return false;
    }
    std::vector<an::CometApertureSplitPlan> starPlans;
    for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
    {
        const auto plan = an::planCometApertureSplit(
            {.aperture = {.center = {.x = cometStarAlignedX(frame), .y = kCometY}, .radius = kCometRadius}});
        if (!plan.hasValue())
        {
            skipReason = describeEngineFailure("the split could not be planned", plan.status());
            return false;
        }
        starPlans.push_back(*plan);
    }
    const auto cometPlan =
        an::planCometApertureSplit({.aperture = {.center = {.x = kCometStartX, .y = kCometY}, .radius = kCometRadius}});
    const auto blendPlan = an::planCometCompositeBlend({});
    const std::optional<an::StackModel> model = an::StackModel::create(an::StackRequest{
        .input = an::StackInputDescription{.frameCount = kCometFrames, .descriptor = image->descriptor}});
    if (!cometPlan.hasValue() || !blendPlan.hasValue() || !model.has_value())
    {
        skipReason = "the comet composite could not be planned";
        return false;
    }
    const std::size_t samples = static_cast<std::size_t>(image->layout.totalBytes() / sizeof(float));
    std::vector<std::vector<float>> starLayers(kCometFrames, std::vector<float>(samples, 0.0f));
    std::vector<std::vector<float>> cometLayers(kCometFrames, std::vector<float>(samples, 0.0f));
    std::vector<float> mask(samples, 0.0f);
    std::vector<float> composite(samples, 0.0f);
    const an::CometCompositeSource maskSource{
        .descriptor = &image->descriptor, .samples = mask.data(), .layout = &image->layout};
    std::string failure;
    const auto note = [&](const char* what, const an::Status& status) {
        if (failure.empty())
            failure = describeEngineFailure(std::string("the engine failed to ") + what, status);
    };
    // The plain average of one layer's frames, through the stack executor over the layers where they lie.
    const auto stackOf = [&](const std::vector<std::vector<float>>& layers) -> std::optional<an::StackResult> {
        std::vector<an::StackSourceBand> bands;
        for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
        {
            bands.push_back(an::StackSourceBand{.sampleType = an::SampleType::Float32,
                                                .samples = layers[frame].data(),
                                                .layout = &image->layout,
                                                .frameIndex = frame,
                                                .firstRow = 0u});
        }
        auto source = an::ResidentStackFrameSource::create(bands);
        if (!source.hasValue())
        {
            note("hold the layers", source.status());
            return std::nullopt;
        }
        auto stacked = an::executeStack(*engine, *execution, *model, *source, {}, {}, {});
        if (!stacked.hasValue())
        {
            note("stack a layer", stacked.status());
            return std::nullopt;
        }
        return std::move(*stacked);
    };
    const std::function<std::string(bool)> body = [&](bool digest) {
        for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
        {
            const an::CometCompositeSource starFrame{
                .descriptor = &image->descriptor, .samples = starFrames[frame].data(), .layout = &image->layout};
            const an::CometCompositeSource cometFrame{
                .descriptor = &image->descriptor, .samples = cometFrames[frame].data(), .layout = &image->layout};
            const auto starSplit = an::splitCometAperture(*execution, starPlans[frame], starFrame,
                                                          starLayers[frame].data(), &image->layout, nullptr, nullptr);
            const auto cometSplit = an::splitCometAperture(*execution, *cometPlan, cometFrame, nullptr, nullptr,
                                                           cometLayers[frame].data(), &image->layout);
            if (!starSplit.hasValue() || !cometSplit.hasValue())
            {
                note("split a frame", starSplit.hasValue() ? cometSplit.status() : starSplit.status());
                return std::string{};
            }
        }
        const std::optional<an::StackResult> starStack = stackOf(starLayers);
        const std::optional<an::StackResult> cometStack = stackOf(cometLayers);
        if (!starStack.has_value() || !cometStack.has_value())
            return std::string{};
        std::vector<an::CometApertureCircle> circles;
        for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
        {
            const auto projection =
                an::projectCometAperture(*execution, {.sourceCenter = {.x = cometStarAlignedX(frame), .y = kCometY},
                                                      .sourceRadiusPixels = kCometRadius,
                                                      .sourceWidth = width,
                                                      .sourceHeight = height,
                                                      .outputWidth = width,
                                                      .outputHeight = height,
                                                      .boundarySamples = 48u});
            const auto circle = projection.hasValue() ? an::cometApertureCircleOf(*projection, kCometRadius)
                                                      : an::Result<an::CometApertureCircle>(projection.status());
            if (!circle.hasValue())
            {
                note("project an aperture", circle.status());
                return std::string{};
            }
            circles.push_back(*circle);
        }
        const auto drawn = an::renderCometMask(*execution, circles, {}, mask.data(), image->layout);
        const std::optional<an::ImageLayout> starLayout = starStack->image.layout();
        const std::optional<an::ImageLayout> cometLayout = cometStack->image.layout();
        if (!drawn.hasValue() || !starLayout.has_value() || !cometLayout.has_value())
        {
            note("draw the mask", drawn.hasValue() ? an::Status::failure(an::ErrorCode::Internal) : drawn.status());
            return std::string{};
        }
        const an::CometCompositeSource starSource{
            .descriptor = &starStack->descriptor, .samples = starStack->image.data(), .layout = &*starLayout};
        const an::CometCompositeSource cometSource{
            .descriptor = &cometStack->descriptor, .samples = cometStack->image.data(), .layout = &*cometLayout};
        const auto blended = an::blendCometComposite(*execution, *blendPlan, starSource, cometSource, maskSource,
                                                     composite.data(), image->layout);
        if (!blended.hasValue())
        {
            note("blend the comet stack", blended.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        checkCometCompositeOutput(field, width, height, composite, out);
        return Sha256::of(composite.data(), composite.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runRejectionMapStatistics(const ScenarioSpec& scenario, const RunOptions& options,
                                                        ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::uint32_t channels = scenario.input.channels;
    const std::span<const float> field{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    // The two maps are derived before the clock, by the shared definitions.
    const std::vector<std::uint32_t> low = rejectionLowCounts(field, width, height, channels);
    const std::vector<std::uint32_t> high = rejectionHighCounts(field, width, height, channels);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channels, engine->limits());
    if (!extent.has_value())
    {
        skipReason = "the rejection maps cannot be described to the engine";
        return false;
    }
    std::vector<an::RejectionClusterDetection> lowClusters(kRejectionMaximumClusters);
    std::vector<an::RejectionClusterDetection> highClusters(kRejectionMaximumClusters);
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto counted = an::countRejectionMaps(*execution, low, high, *extent);
        const auto lowReport = an::analyzeRejectionClusters(
            *execution, low, *extent,
            {.source = an::RejectionClusterSource::Low, .minimumClusterPixels = kRejectionMinimumClusterPixels},
            lowClusters);
        const auto highReport = an::analyzeRejectionClusters(
            *execution, high, *extent,
            {.source = an::RejectionClusterSource::High, .minimumClusterPixels = kRejectionMinimumClusterPixels},
            highClusters);
        if (!counted.hasValue() || !lowReport.hasValue() || !highReport.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'",
                                                !counted.hasValue()     ? counted.status()
                                                : !lowReport.hasValue() ? lowReport.status()
                                                                        : highReport.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        const RejectionMapStatistics produced{
            .eitherElements = static_cast<double>(counted->eitherElements),
            .eitherFraction = counted->eitherFraction,
            .clusters = static_cast<double>(lowReport->clustersReported + highReport->clustersReported),
            .largestClusterPixels =
                static_cast<double>((std::max)(lowReport->largestClusterPixels, highReport->largestClusterPixels)),
            .largestClusterFraction =
                (std::max)(lowReport->largestClusterFraction, highReport->largestClusterFraction)};
        checkRejectionMapStatistics(low, high, width, height, channels, produced, out);
        const std::array<double, 5u> figures{produced.eitherElements, produced.eitherFraction, produced.clusters,
                                             produced.largestClusterPixels, produced.largestClusterFraction};
        return Sha256::of(figures.data(), figures.size() * sizeof(double));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runExternalHandoff(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    const std::span<const float> source{reinterpret_cast<const float*>(fixture.bytes.data()),
                                        fixture.bytes.size() / sizeof(float)};
    const auto image = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                     fixture.bytes.size());
    if (!image.has_value())
    {
        skipReason = "the hand-off frame cannot be described to the engine";
        return false;
    }
    const auto plan = an::planExternalToolHandoff(an::ExternalToolHandoffRequest{
        .format = an::ExternalToolFormat::UInt16, .preStretch = an::ExternalToolPreStretch::Unlinked});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the hand-off could not be planned", plan.status());
        return false;
    }
    auto handedRequest = an::externalToolHandoffLayout(*plan, image->descriptor);
    const auto handedLayout = handedRequest.hasValue() ? an::ImageLayout::create(*handedRequest, engine->limits())
                                                       : std::optional<an::ImageLayout>{};
    // The tool's answer is the codes it was handed, described as sixteen-bit fractions of full scale.
    an::ImageDescriptorRequest answerRequest = image->descriptor.toRequest();
    answerRequest.sampleType = an::SampleType::UInt16;
    answerRequest.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> answerDescriptor = an::ImageDescriptor::create(answerRequest);
    if (!handedLayout.has_value() || !answerDescriptor.has_value())
    {
        skipReason = "the handed image cannot be described";
        return false;
    }
    std::vector<std::uint16_t> handed(static_cast<std::size_t>(handedLayout->totalBytes() / sizeof(std::uint16_t)), 0u);
    std::vector<float> restored(source.size(), 0.0f);
    const an::ExternalToolSource sourceView{
        .descriptor = &image->descriptor, .samples = fixture.bytes.data(), .layout = &image->layout};
    const an::ExternalToolSource answerView{
        .descriptor = &*answerDescriptor, .samples = handed.data(), .layout = &*handedLayout};
    std::string failure;
    const auto fail = [&](const an::Status& status) {
        if (failure.empty())
            failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", status);
        return std::string{};
    };
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto handoff =
            an::prepareExternalToolHandoff(*execution, *plan, sourceView, handed.data(), *handedLayout);
        if (!handoff.hasValue())
            return fail(handoff.status());
        const auto back = an::restoreExternalToolAnswer(*execution, *plan, *handoff, an::ExternalToolLayer::Image,
                                                        answerView, restored.data(), image->layout);
        if (!back.hasValue())
            return fail(back.status());
        if (!digest)
            return std::string{};
        checkHandoffOutput(source, width, height, restored, out);
        return Sha256::of(restored.data(), restored.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runImageDifference(const ScenarioSpec& scenario, const RunOptions& options,
                                                 ResultRecord& out, std::string& skipReason)
{
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = engine.status().message();
        return false;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = execution.status().message();
        return false;
    }
    const auto source = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor), engine->limits(),
                                      fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = "the difference fixture cannot be described to the engine";
        return false;
    }
    // The second frame is derived before the clock, by the shared definition.
    const std::span<const float> first{reinterpret_cast<const float*>(fixture.bytes.data()),
                                       fixture.bytes.size() / sizeof(float)};
    const std::vector<float> second = differenceSecondInput(first, scenario.input.width);
    const auto plan = an::planImageDifference(an::ImageDifferenceRequest{});
    if (!plan.hasValue())
    {
        skipReason = describeEngineFailure("the difference could not be planned", plan.status());
        return false;
    }
    auto layoutRequest = an::imageDifferenceLayout(*plan, source->descriptor);
    if (!layoutRequest.hasValue())
    {
        skipReason = layoutRequest.status().message();
        return false;
    }
    layoutRequest->rowAlignmentBytes = sizeof(float);
    const auto layout = an::ImageLayout::create(*layoutRequest, engine->limits());
    if (!layout.has_value())
    {
        skipReason = "the difference output cannot be laid out";
        return false;
    }
    std::vector<float> output(static_cast<std::size_t>(layout->totalBytes() / sizeof(float)), 0.0f);
    const an::ImageDifferenceSource left{
        .descriptor = &source->descriptor, .samples = fixture.bytes.data(), .layout = &source->layout};
    const an::ImageDifferenceSource right{
        .descriptor = &source->descriptor, .samples = second.data(), .layout = &source->layout};
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool digest) {
        const auto result = an::computeImageDifference(*execution, *plan, left, right, output.data(), *layout);
        if (!result.hasValue())
        {
            if (failure.empty())
                failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", result.status());
            return std::string{};
        }
        if (!digest)
            return std::string{};
        checkImageDifferenceOutput(first, second, output, out);
        return Sha256::of(output.data(), output.size() * sizeof(float));
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.actualBackend = toBenchBackend(an::selectBackend(*execution));
    for (std::uint32_t i = 0u; i < options.warmupIterations; ++i)
        body(false);
    ResidentMemorySampler sampler;
    std::uint64_t peak = 0u;
    for (std::uint32_t i = 0u; i < (std::max)(1u, options.repetitions); ++i)
        timeOnce(scenario, body, out.timings, sampler, peak);
    out.memory.peakResidentBytes = peak;
    out.outputDigest = body(true);
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    return true;
}

bool AfterNightEngineAdapter::runColorTransform(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    const bool preview = scenario.id.find(".preview.") != std::string::npos;
    // Absent, the conversion alone is timed; stated, the plan, a new destination, the conversion and the
    // destination's release, as one call that returns a new image does them.
    const std::string timed = parameterValue(scenario, "timed", "conversion");
    const bool newImage = timed == "plan-allocate-convert-release";
    if ((timed != "conversion" && !newImage) || (newImage && preview))
    {
        skipReason = "scenario " + scenario.id + " times a part of the colour conversion this runner does not separate";
        return false;
    }
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u || parameterValue(scenario, "source", "srgb") != "srgb" ||
        parameterValue(scenario, "destination", "lab") != "lab" ||
        parameterValue(scenario, "packing", "unit") != "unit")
    {
        skipReason = "scenario " + scenario.id + " declares a colour transform this runner does not implement";
        return false;
    }
    if (preview && (parameterValue(scenario, "purpose", "realtime-preview") != "realtime-preview" ||
                    parameterValue(scenario, "tableEntries", "1024") != "1024"))
    {
        skipReason = "scenario '" + scenario.id + "' does not name the retained preview lookup";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(scenario.input.width) * scenario.input.height;
    const std::size_t planeStride = static_cast<std::size_t>(source->layout.planeStrideBytes() / sizeof(float));
    const float* const fixturePlanes = reinterpret_cast<const float*>(fixture.bytes.data());
    const std::vector<SrgbToLabProbe> probes =
        computeSrgbToLabOracle(std::span<const float>{fixturePlanes, pixelCount},
                               std::span<const float>{fixturePlanes + planeStride, pixelCount},
                               std::span<const float>{fixturePlanes + 2u * planeStride, pixelCount}, kColorProbeStride);
    if (probes.empty())
    {
        skipReason = "scenario " + scenario.id + " produced no colour oracle probes";
        return false;
    }

    an::ColorEncoding encodedRgb;
    encodedRgb.transfer = an::ColorTransferFunction::Srgb;
    an::ColorEncoding packedLab;
    packedLab.schema = an::ChannelSchema::Lab;
    packedLab.perceptual = an::PerceptualEncoding::UnitPacked;
    std::optional<an::ColorConversionPlan> productionPlan;
    std::optional<an::RealtimePreviewColorConversionPlan> previewPlan;
    if (preview)
    {
        an::Result<an::RealtimePreviewColorConversionPlan> madePreview =
            an::planRealtimePreviewColorConversion(encodedRgb, packedLab);
        if (!madePreview.hasValue())
        {
            skipReason =
                describeEngineFailure("the preview colour conversion could not be planned", madePreview.status());
            return false;
        }
        previewPlan.emplace(std::move(*madePreview));
    }
    else
    {
        an::Result<an::ColorConversionPlan> madeProduction = an::planColorConversion(encodedRgb, packedLab);
        if (!madeProduction.hasValue())
        {
            skipReason = describeEngineFailure("the colour conversion could not be planned", madeProduction.status());
            return false;
        }
        productionPlan.emplace(std::move(*madeProduction));
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        // A row that times the whole call plans and allocates here, and its destination is released
        // when the call returns, digest or not.
        std::optional<an::ColorConversionPlan> plannedHere;
        std::optional<an::ImageStorage<float>> allocatedHere;
        if (newImage)
        {
            an::Result<an::ColorConversionPlan> planned = an::planColorConversion(encodedRgb, packedLab);
            if (!planned.hasValue())
            {
                failure = describeEngineFailure("the colour conversion could not be planned", planned.status());
                return std::string{};
            }
            plannedHere.emplace(std::move(*planned));
            an::Result<an::ImageStorage<float>> allocated = an::ImageStorage<float>::create(
                *engine, source->layout, an::StorageInitialization::Uninitialized, an::kDefaultBufferAlignmentBytes);
            if (!allocated.hasValue())
            {
                failure = describeEngineFailure("the destination could not be allocated", allocated.status());
                return std::string{};
            }
            allocatedHere.emplace(std::move(*allocated));
        }
        float* const produced = newImage ? allocatedHere->data() : destination->data();
        const an::Status ran =
            preview
                ? an::convertRealtimePreviewImageColor(*execution, *previewPlan, an::SampleType::Float32,
                                                       fixture.bytes.data(), source->layout, produced, source->layout)
                : an::convertImageColor(*execution, newImage ? *plannedHere : *productionPlan, an::SampleType::Float32,
                                        fixture.bytes.data(), source->layout, produced, source->layout);
        if (ran.failed())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the engine failed to run " + scenario.id, ran);
            }
            return std::string{};
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        out.correctnessMetric = srgbToLabError(std::span<const float>{produced, pixelCount},
                                               std::span<const float>{produced + planeStride, pixelCount},
                                               std::span<const float>{produced + 2u * planeStride, pixelCount}, probes);
        const an::ColorConversionPlan& executedPlan = preview ? previewPlan->productionPlan() : *productionPlan;
        out.counters["colorStages"] = static_cast<double>(executedPlan.stages().size());
        if (preview)
        {
            out.counters["previewTableEntries"] = static_cast<double>(an::kRealtimePreviewColorTableEntries);
            out.counters["previewMaximumAbsolutePowerError"] = previewPlan->maximumAbsolutePowerError();
        }
        return Sha256::of(produced, static_cast<std::size_t>(source->layout.totalBytes()));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runMaskOperations(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    const bool packedMask = scenario.id == "masks.packed_combine_count.f32";
    double lowerLimit = 0.0;
    double upperLimit = 0.0;
    double fuzziness = 0.0;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u)
    {
        skipReason = "scenario " + scenario.id + " declares a mask shape this runner does not implement";
        return false;
    }
    if (!doubleParameter(scenario, "lowerLimit", 0.25, lowerLimit, skipReason) ||
        !doubleParameter(scenario, "upperLimit", 0.75, upperLimit, skipReason) ||
        !doubleParameter(scenario, "fuzziness", 0.0, fuzziness, skipReason))
    {
        return false;
    }
    const std::string screeningValue = parameterValue(scenario, "screening", "false");
    if (!(upperLimit >= lowerLimit) || !(fuzziness >= 0.0) || fuzziness > 1.0 ||
        (screeningValue != "true" && screeningValue != "false"))
    {
        skipReason = "scenario " + scenario.id + " states an interval that ends before it starts";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    an::SampleThreshold threshold;
    threshold.predicate = an::MaskPredicate::InsideClosedRange;
    threshold.lowerBound = lowerLimit;
    threshold.upperBound = upperLimit;

    std::string failure;
    std::function<std::string(bool)> body;
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    an::Result<an::ValidityMask> inside = an::ValidityMask::create(*engine, source->layout.extent());
    an::Result<an::ValidityMask> outside = an::ValidityMask::create(*engine, source->layout.extent());
    if (packedMask)
    {
        if (!inside.hasValue() || !outside.hasValue())
        {
            skipReason = std::string("the mask could not be allocated: ") +
                         (inside.hasValue() ? outside.status().message() : inside.status().message());
            return false;
        }
        body = [&](bool produceDigest) {
            const std::optional<an::ValidityMaskView<std::uint8_t>> insideView = inside->view();
            const std::optional<an::ValidityMaskView<std::uint8_t>> outsideView = outside->view();
            if (!insideView.has_value() || !outsideView.has_value())
            {
                failure = "the mask holds no bits";
                return std::string{};
            }
            // Build the interval and its complement from the samples independently, invert the
            // first in place, and intersect the two. The answer is the complement both routes
            // produced, so the count is the number of samples outside the interval rather than a
            // number the chain could reach without reading a pixel.
            an::MaskThresholdRequest inRange;
            inRange.threshold = threshold;
            an::MaskThresholdRequest outOfRange;
            outOfRange.threshold = threshold;
            outOfRange.threshold.predicate = an::MaskPredicate::OutsideClosedRange;
            an::Status stage = an::buildImageMask(*execution, an::SampleType::Float32, fixture.bytes.data(),
                                                  source->layout, inRange, *insideView);
            if (stage.ok())
            {
                stage = an::buildImageMask(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                           outOfRange, *outsideView);
            }
            if (stage.ok())
            {
                stage = an::invertImageMask(*execution, *insideView, insideView->constView());
            }
            if (stage.ok())
            {
                stage = an::combineImageMasks(*execution, *insideView, insideView->constView(),
                                              outsideView->constView(), an::ValidityCombination::Intersection);
            }
            if (stage.failed())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run " + scenario.id, stage);
                }
                return std::string{};
            }
            const an::Result<an::ValidityCount> counted = an::countImageMask(*execution, insideView->constView());
            if (!counted.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run " + scenario.id, counted.status());
                }
                return std::string{};
            }
            if (!produceDigest)
            {
                return std::string{};
            }
            out.counters["validPixels"] = static_cast<double>(counted->valid);
            out.counters["totalPixels"] = static_cast<double>(counted->total);
            return Sha256::of(insideView->data(), static_cast<std::size_t>(insideView->spannedByteCount()));
        };
    }
    else
    {
        if (!destination.hasValue())
        {
            skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
            return false;
        }
        body = [&](bool produceDigest) {
            const an::RangeSelectionRequest request{.lowerLimit = lowerLimit,
                                                    .upperLimit = upperLimit,
                                                    .fuzziness = fuzziness,
                                                    .screening = screeningValue == "true"};
            const an::Result<an::RangeSelectionResult> ran =
                an::rangeSelectImage(*execution, an::SampleType::Float32, fixture.bytes.data(), source->layout,
                                     destination->data(), source->layout, request);
            if (!ran.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run " + scenario.id, ran.status());
                }
                return std::string{};
            }
            if (!produceDigest)
            {
                return std::string{};
            }
            out.counters["selectedSamples"] = static_cast<double>(ran->run.selection.valid);
            out.counters["totalSamples"] = static_cast<double>(ran->run.selection.total);
            return Sha256::of(destination->data(), static_cast<std::size_t>(source->layout.totalBytes()));
        };
    }

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runExpression(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                            std::string& skipReason)
{
    std::string script;
    std::string path;
    if (scenario.input.sampleType != SampleType::Float32 || scenario.input.layout != Layout::Planar ||
        scenario.input.channels != 3u)
    {
        skipReason = "scenario " + scenario.id + " declares an input this runner does not implement";
        return false;
    }
    script = parameterValue(scenario, "expression", "$T * 0.5 + 0.25");
    if (script.empty())
    {
        skipReason = "scenario " + scenario.id + " states no expression";
        return false;
    }
    path = parameterValue(scenario, "path", "evaluator");
    if (path != "automatic" && path != "evaluator" && path != "fused")
    {
        skipReason = "scenario " + scenario.id + " names an expression path this runner does not implement";
        return false;
    }
    const bool requiresFused = path == "fused";
    const bool permitsFused = path != "evaluator";
    double seed = 0.0;
    if (!doubleParameter(scenario, "seed", 0.0, seed, skipReason))
    {
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }
    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }
    an::Result<an::ImageStorage<float>> destination = an::ImageStorage<float>::create(
        *engine, source->layout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }
    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    an::PixelMathBindings bindings;
    const an::PixelMathImageDeclaration declaration{source->layout.extent(), an::SampleType::Float32,
                                                    an::NumericDomain::NormalizedLinear};
    an::Status bound = bindings.bindImage("T", declaration);
    if (bound.ok())
    {
        bound = bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T");
    }
    if (bound.failed())
    {
        skipReason = describeEngineFailure("the expression images could not be bound", bound);
        return false;
    }

    std::string failure;
    const std::uint64_t randomSeed = static_cast<std::uint64_t>(seed);
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        const an::PixelMathParse parsed = an::parsePixelMath(script);
        if (!parsed.succeeded())
        {
            if (failure.empty())
            {
                failure = "the expression was refused: " + an::describePixelMathDiagnostic(parsed.diagnostics()[0]);
            }
            return std::string{};
        }
        const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings);
        if (!resolved.succeeded())
        {
            if (failure.empty())
            {
                failure = "the expression was refused: " + an::describePixelMathDiagnostic(resolved.diagnostics()[0]);
            }
            return std::string{};
        }
        const an::Result<an::PixelMathProgram> program = an::compilePixelMath(resolved);
        if (!program.hasValue())
        {
            if (failure.empty())
            {
                failure = describeEngineFailure("the expression could not be compiled", program.status());
            }
            return std::string{};
        }

        bool ranFused = false;
        if (permitsFused)
        {
            const an::Result<an::PixelMathLowering> lowered = an::lowerPixelMathToPointwise(*program);
            if (lowered.hasValue() && lowered->lowered())
            {
                std::vector<an::PointwiseImageView> sources;
                sources.reserve(lowered->sourceImages().size());
                for (const std::uint32_t image : lowered->sourceImages())
                {
                    // Every source of this scenario is the one bound frame; the index is what an
                    // expression naming several images would resolve through.
                    static_cast<void>(image);
                    sources.push_back(an::PointwiseImageView{fixture.bytes.data(), &source->layout});
                }
                const an::PointwiseMutableImageView written{destination->data(), &source->layout};
                const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                    *execution, *lowered->program(), std::span<const an::PointwiseImageView>(sources), written,
                    an::KernelValidityView{});
                if (!ran.hasValue())
                {
                    if (failure.empty())
                    {
                        failure = describeEngineFailure("the engine failed to run " + scenario.id, ran.status());
                    }
                    return std::string{};
                }
                ranFused = true;
                out.counters["pointwiseLowered"] = 1.0;
            }
            else if (requiresFused)
            {
                if (failure.empty())
                {
                    failure = lowered.hasValue()
                                  ? std::string("the expression is not a fusable chain: ") +
                                        an::pixelMathLoweringBlockerName(lowered->blocker())
                                  : describeEngineFailure("the expression could not be lowered", lowered.status());
                }
                return std::string{};
            }
        }
        if (!ranFused)
        {
            out.counters["pointwiseLowered"] = 0.0;
            const std::array<an::PixelMathImageView, 1u> images{
                an::PixelMathImageView{an::SampleType::Float32, fixture.bytes.data(), &source->layout}};
            an::PixelMathExecutionRequest request;
            request.images = images;
            request.destination =
                an::PixelMathMutableImageView{an::SampleType::Float32, destination->data(), &source->layout};
            request.randomSeed = randomSeed;
            const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(*execution, *program, request);
            if (!ran.hasValue())
            {
                if (failure.empty())
                {
                    failure = describeEngineFailure("the engine failed to run " + scenario.id, ran.status());
                }
                return std::string{};
            }
        }

        if (!produceDigest)
        {
            return std::string{};
        }
        // Gathered into a tightly packed plane-major array rather than digested where it lies, so
        // that padding is not part of the answer and a result of another shape - a double-precision
        // image, say - would produce the identical bytes.
        const an::ImageExtent extent = source->layout.extent();
        std::vector<float> gathered(static_cast<std::size_t>(extent.sampleCount()), 0.0f);
        std::size_t at = 0u;
        for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < extent.height(); ++y)
            {
                const std::uint8_t* const row = reinterpret_cast<const std::uint8_t*>(destination->data()) +
                                                *source->layout.rowByteOffset(channel, y);
                const float* const samples = reinterpret_cast<const float*>(row);
                for (std::uint32_t x = 0u; x < extent.width(); ++x)
                {
                    gathered[at++] = samples[x];
                }
            }
        }
        return Sha256::of(gathered.data(), gathered.size() * sizeof(float));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0u; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0u;
    for (std::uint32_t index = 0u; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);
    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }
    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runTransfer(const TransferBandwidthScenario& transfer, const ScenarioSpec& scenario,
                                          const RunOptions& options, ResultRecord& out, std::string& skipReason)
{
    if (scenario.input.sampleType != transfer.sampleType || scenario.input.layout != transfer.inputLayout ||
        scenario.input.channels != transfer.channels)
    {
        skipReason = "scenario '" + scenario.id + "' declares an input this runner does not implement";
        return false;
    }

    Fixture fixture;
    std::string error;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error))
    {
        skipReason = error;
        return false;
    }

    an::EngineContextOptions engineOptions = benchmarkEngineOptions(options);
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    if (!engine.hasValue())
    {
        skipReason = std::string("the engine context could not be built: ") + engine.status().message();
        return false;
    }

    const FixtureSemantics semantics = defaultFixtureSemantics(fixture.descriptor);
    const std::optional<BridgedFixture> source =
        bridgeFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size());
    if (!source.has_value())
    {
        skipReason = describeFixtureBridgeFailure(
            checkFixture(fixture.descriptor, semantics, engine->limits(), fixture.bytes.size()), fixture.descriptor);
        return false;
    }

    const SampleDescriptor destinationDescriptor =
        transferDestinationDescriptor(fixture.descriptor, transfer.operation);
    const std::optional<BridgedFixture> destinationShape =
        bridgeFixture(destinationDescriptor, defaultFixtureSemantics(destinationDescriptor), engine->limits(),
                      destinationDescriptor.totalBytes());
    if (!destinationShape.has_value())
    {
        skipReason = describeFixtureBridgeFailure(checkFixture(destinationDescriptor,
                                                               defaultFixtureSemantics(destinationDescriptor),
                                                               engine->limits(), destinationDescriptor.totalBytes()),
                                                  destinationDescriptor);
        return false;
    }

    const an::ImageLayout& sourceLayout = source->layout;
    const an::ImageLayout& destinationLayout = destinationShape->layout;
    const std::optional<an::SampleType> sampleTypeOrNothing = toEngineSampleType(fixture.descriptor.sampleType);
    if (!sampleTypeOrNothing.has_value())
    {
        skipReason = "scenario '" + scenario.id + "' names a representation this engine does not have";
        return false;
    }
    const an::SampleType sampleType = *sampleTypeOrNothing;

    // Untyped storage on purpose. These operations are described by an element width rather than by
    // a sample type - which is the whole reason the family has four bodies and not eight - so the
    // engine memory they write into needs an alignment and a byte count and no C++ type at all.
    an::Result<an::ImageStorageBytes> destination = an::ImageStorageBytes::create(
        *engine, destinationLayout, an::StorageInitialization::Zeroed, an::kDefaultBufferAlignmentBytes);
    if (!destination.hasValue())
    {
        skipReason = std::string("the destination could not be allocated: ") + destination.status().message();
        return false;
    }

    // A byte swap is the one member of the family that rewrites what it read, so it works on a copy
    // of the fixture rather than on the fixture itself. The copy is made once, outside the timed
    // region, and refreshed only for the digest pass, so an involution repeated an even number of
    // times cannot make the recorded output depend on how many repetitions were asked for.
    const bool inPlace = transfer.operation == TransferOperation::ByteSwap;
    if (inPlace)
    {
        std::memcpy(destination->data(), fixture.bytes.data(), static_cast<std::size_t>(sourceLayout.totalBytes()));
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
    if (!execution.hasValue())
    {
        skipReason = std::string("the operation could not be started: ") + execution.status().message();
        return false;
    }

    const an::Result<an::BackendSelection> selection = an::selectBackend(*execution);
    out.fixtureDigest = fixture.digest;
    out.threadCount = execution->admittedConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = toBenchBackend(selection);

    // Eight bytes so the widest representation this family moves can be filled from it, and a
    // pattern rather than a value: what a fill repeats is bytes, and a benchmark that filled
    // with zero would measure a memory system doing something it is allowed to special-case.
    const std::array<std::uint8_t, 8> fillPattern{0x2Au, 0x7Fu, 0x13u, 0x3Eu, 0x2Au, 0x7Fu, 0x13u, 0x3Eu};

    // A composition reads one single-channel picture per channel, and the three planes of the planar
    // fixture are exactly that: three monochrome frames that happen to be adjacent. Describing them
    // is geometry rather than work, so it happens once, here, outside everything that is timed.
    std::vector<an::ImageLayout> planeLayouts;
    std::vector<an::ChannelSource> planeSources;
    if (transfer.operation == TransferOperation::ComposeChannels)
    {
        const std::optional<an::ImageExtent> planeExtent = an::ImageExtent::create(
            sourceLayout.extent().width(), sourceLayout.extent().height(), 1u, engine->limits());
        if (!planeExtent.has_value())
        {
            skipReason = "scenario '" + scenario.id + "' has an extent this engine cannot describe one plane of";
            return false;
        }
        planeLayouts.reserve(transfer.channels);
        planeSources.reserve(transfer.channels);
        for (std::uint32_t channel = 0u; channel < transfer.channels; ++channel)
        {
            const std::optional<an::ImageLayout> planeLayout = an::ImageLayout::createWithStrides(
                an::ImageLayoutRequest{.extent = *planeExtent,
                                       .elementSizeBytes = sourceLayout.elementSizeBytes(),
                                       .samplesPerPixel = 1u,
                                       .planeCount = 1u,
                                       .rowAlignmentBytes = 1u},
                sourceLayout.rowStrideBytes(), sourceLayout.planeStrideBytes(), engine->limits());
            if (!planeLayout.has_value())
            {
                skipReason = "scenario '" + scenario.id + "' has strides this engine cannot describe one plane of";
                return false;
            }
            planeLayouts.push_back(*planeLayout);
        }
        for (std::uint32_t channel = 0u; channel < transfer.channels; ++channel)
        {
            planeSources.push_back(
                an::ChannelSource{.samples = fixture.bytes.data() + channel * sourceLayout.planeStrideBytes(),
                                  .layout = &planeLayouts[channel]});
        }
    }
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        if (inPlace && produceDigest)
        {
            std::memcpy(destination->data(), fixture.bytes.data(), static_cast<std::size_t>(sourceLayout.totalBytes()));
        }
        an::Status moved = an::Status::success();
        switch (transfer.operation)
        {
        case TransferOperation::Copy:
            moved = an::copyImageSamples(*execution, sampleType, fixture.bytes.data(), sourceLayout,
                                         destination->data(), destinationLayout);
            break;
        case TransferOperation::Fill:
            moved = an::fillImageSamples(*execution, sampleType, fillPattern.data(), destination->data(),
                                         destinationLayout);
            break;
        case TransferOperation::Deinterleave:
        case TransferOperation::Interleave:
            moved = an::repackImageSamples(*execution, sampleType, fixture.bytes.data(), sourceLayout,
                                           destination->data(), destinationLayout);
            break;
        case TransferOperation::ExtractChannel:
            moved = an::extractImageChannel(*execution, sampleType, fixture.bytes.data(), sourceLayout,
                                            transfer.extractedChannel(), destination->data(), destinationLayout);
            break;
        case TransferOperation::ComposeChannels:
            moved =
                an::composeImageChannels(*execution, sampleType, planeSources, destination->data(), destinationLayout);
            break;
        case TransferOperation::ByteSwap:
            moved = an::swapImageByteOrder(*execution, sampleType, destination->data(), destinationLayout);
            break;
        }
        if (moved.failed() && failure.empty())
        {
            failure = describeEngineFailure("the engine failed to run '" + scenario.id + "'", moved);
        }
        if (!produceDigest)
        {
            return std::string{};
        }
        return Sha256::of(destination->data(), static_cast<std::size_t>(destinationLayout.totalBytes()));
    };

    const std::uint32_t repetitions = (std::max)(options.repetitions, std::uint32_t{1});
    for (std::uint32_t index = 0; index < options.warmupIterations; ++index)
    {
        body(false);
    }
    ResidentMemorySampler sampler;
    std::uint64_t peakBytes = 0;
    for (std::uint32_t index = 0; index < repetitions; ++index)
    {
        timeOnce(scenario, body, out.timings, sampler, peakBytes);
    }
    out.memory.peakResidentBytes = peakBytes;
    out.outputDigest = body(true);

    if (!failure.empty())
    {
        skipReason = failure;
        return false;
    }

    out.bytesProcessed = scenario.bytesProcessed;
    out.elementsProcessed = scenario.elementsProcessed;
    return true;
}

bool AfterNightEngineAdapter::runSequence(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                          std::string& skipReason)
{
    if (!matchesSequenceScenario(scenario))
    {
        skipReason = "unsupported sequence scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    const auto mode = scenario.parameters.at("mode");
    const std::array codecs{*an::createFitsCodec()};
    auto engineOptions = benchmarkEngineOptions(options);
    auto registry = an::CodecRegistry::create(codecs);
    if (!registry)
    {
        skipReason = "sequence registry creation failed";
        return false;
    }
    engineOptions.codecRegistry = *registry;
    auto engine = an::EngineContext::create(engineOptions);
    if (!engine)
    {
        skipReason = "sequence context creation failed";
        return false;
    }
    auto extent = *an::ImageExtent::create(2048u, 2048u, 1u, engine->limits());
    auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                       {4u, an::RawSampleClass::FloatingPoint},
                                                       an::ByteOrder::Native,
                                                       an::ChannelSchema::Mono,
                                                       an::ColorSpace::Unspecified,
                                                       an::NumericDomain::PhysicalScaled,
                                                       {},
                                                       an::CoordinateOrigin::TopLeft,
                                                       1.0});
    auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, engine->limits());
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-bench-list-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
    {
        skipReason = "sequence temporary directory already exists";
        return false;
    }
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto text = (directory / "frame.fit").u8string();
    const std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    if (mode != "dummy")
    {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        if (!execution || !raw ||
            !an::writeImage(*engine, *execution,
                            {.destinationPath = path,
                             .format = an::CodecFormat::Fits,
                             .durability = an::OutputDurability::CloseOnly},
                            *raw, {}))
        {
            skipReason = "sequence fixture preparation failed";
            return false;
        }
    }
    std::array<an::CodecReadRequest, 4u> entries;
    entries.fill({path, an::CodecFormat::Fits});
    const an::ImageListSourceOptions policy =
        mode == "eager"    ? an::ImageListSourceOptions{an::ImageListLoadMode::Eager, layout.totalBytes() * 4u}
        : mode == "cached" ? an::ImageListSourceOptions{an::ImageListLoadMode::Cached, layout.totalBytes() * 2u}
                           : an::ImageListSourceOptions{};
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution)
        {
            failure = "sequence execution creation failed";
            return std::string{};
        }
        auto source = mode == "dummy" ? an::createDummySource(*engine, *execution, {descriptor, layout, 4u})
                                      : an::createImageListSource(*engine, *execution, entries, policy);
        if (!source)
        {
            failure = describeEngineFailure("create sequence", source.status());
            return std::string{};
        }
        std::array<std::optional<an::SequenceFrame>, 4u> frames;
        for (std::uint64_t index = 0u; index < frames.size(); ++index)
        {
            // The bounded workload revisits one frame before forcing an eviction.
            if (mode == "cached")
            {
                const std::array<std::uint64_t, 4u> order{0u, 1u, 0u, 2u};
                auto sought = source->seek(*execution, {.index = order[index]});
                if (!sought)
                {
                    failure = "sequence seek failed";
                    return std::string{};
                }
            }
            auto read = source->read(*engine, *execution);
            if (!read || !read->frame)
            {
                failure = read ? "sequence ended early" : describeEngineFailure("read sequence", read.status());
                return std::string{};
            }
            frames[index] = std::move(read->frame);
        }
        if (const auto closed = source->close(); closed.failed())
        {
            failure = "sequence close failed";
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        std::string digests;
        out.correctnessMetric = 0.0;
        for (const auto& frame : frames)
        {
            const auto bytes = frame->frame.bytes();
            if (bytes.size() != fixture.bytes.size() ||
                (mode == "dummy" ? !std::ranges::all_of(bytes, [](std::byte byte) { return byte == std::byte{}; })
                                 : std::memcmp(bytes.data(), fixture.bytes.data(), bytes.size()) != 0))
                out.correctnessMetric = 1.0;
            digests += Sha256::of(bytes.data(), bytes.size());
        }
        return Sha256::of(digests.data(), digests.size());
    };
    const auto completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    out.counters["outputBytes"] = static_cast<double>(scenario.bytesProcessed);
    return completed;
}

bool AfterNightEngineAdapter::runImageListWrite(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    if (!matchesImageListWriteScenario(scenario))
    {
        skipReason = "unsupported image-list write scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    const std::array codecs{*an::createFitsCodec()};
    auto engineOptions = benchmarkEngineOptions(options);
    engineOptions.codecRegistry = *an::CodecRegistry::create(codecs);
    auto engine = an::EngineContext::create(engineOptions);
    if (!engine)
    {
        skipReason = "image-list write context creation failed";
        return false;
    }
    const auto extent = *an::ImageExtent::create(scenario.input.width, scenario.input.height, 1u, engine->limits());
    const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                             {4u, an::RawSampleClass::FloatingPoint},
                                                             an::ByteOrder::Native,
                                                             an::ChannelSchema::Mono,
                                                             an::ColorSpace::Unspecified,
                                                             an::NumericDomain::PhysicalScaled,
                                                             {},
                                                             an::CoordinateOrigin::TopLeft,
                                                             1.0});
    const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, engine->limits());
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-bench-list-write-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
    {
        skipReason = "image-list write temporary directory already exists";
        return false;
    }
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto nativePath = (directory / "frames.anseq").u8string();
    const std::string path(reinterpret_cast<const char*>(nativePath.data()), nativePath.size());
    const an::SequenceDescriptor sequence{
        .format = an::SequenceFormat::ImageList, .fixedFrame = descriptor, .frameCount = 8u};
    std::string failure;
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        if (const auto removed = an::removeImageListFromDisk(path); removed.failed())
        {
            failure = describeEngineFailure("remove previous image list", removed);
            return std::string{};
        }
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        auto output =
            execution
                ? an::SequenceOutput::begin(*engine, *execution,
                                            {.destinationPath = path, .durability = an::OutputDurability::CloseOnly})
                : an::Result<an::SequenceOutput>(execution.status());
        auto sink = output ? an::createImageListSink(
                                 *engine, *execution, *output, sequence,
                                 {.frameDurability = an::OutputDurability::CloseOnly, .timestampSeconds = 1714595462u})
                           : an::Result<an::FrameSink>(output.status());
        auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
        auto raw = buffer ? an::RawFrame::create(descriptor, layout, std::move(*buffer))
                          : an::Result<an::RawFrame>(buffer.status());
        if (!sink || !raw)
        {
            failure = describeEngineFailure("create image-list sink", sink ? raw.status() : sink.status());
            return std::string{};
        }
        an::SequenceFrame frame{std::move(*raw), {}, {}};
        for (std::uint64_t index = 0u; index < 8u; ++index)
        {
            frame.position.index = index;
            if (!sink->append(*engine, *execution, frame))
            {
                failure = "append image-list frame failed";
                return std::string{};
            }
        }
        auto finished = sink->finish(*execution);
        auto committed =
            finished ? output->commit(*execution, *finished) : an::Result<an::SequenceOutputReport>(finished.status());
        if (!committed || sink->close().failed())
        {
            failure = "finish image-list publication failed";
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        auto manifest = an::readImageListManifest(*engine, *execution, path);
        if (!manifest)
        {
            failure = describeEngineFailure("read published image list", manifest.status());
            return std::string{};
        }
        std::vector<std::string> resolvedPaths;
        resolvedPaths.reserve(manifest->framePaths.size());
        for (const std::string& framePath : manifest->framePaths)
        {
            auto resolved = an::resolveImageListFramePath(path, framePath);
            if (!resolved)
            {
                failure = describeEngineFailure("resolve published image-list frame", resolved.status());
                return std::string{};
            }
            resolvedPaths.push_back(std::move(*resolved));
        }
        // A read request borrows its path, so each resolved path must outlive the source's creation, which copies it.
        std::vector<an::CodecReadRequest> requests;
        requests.reserve(resolvedPaths.size());
        for (const std::string& resolvedPath : resolvedPaths)
            requests.push_back({resolvedPath, an::CodecFormat::Fits});
        auto verification = an::createImageListSource(*engine, *execution, requests, {});
        if (!verification)
        {
            failure = describeEngineFailure("open published image list", verification.status());
            return std::string{};
        }
        std::string digests;
        out.correctnessMetric = 0.0;
        for (unsigned index = 0u; index < 8u; ++index)
        {
            auto read = verification->read(*engine, *execution);
            if (!read || !read->frame)
            {
                failure = "published image list returned no frame";
                return std::string{};
            }
            const auto bytes = read->frame->frame.bytes();
            if (bytes.size() != fixture.bytes.size() ||
                std::memcmp(bytes.data(), fixture.bytes.data(), fixture.bytes.size()) != 0)
                out.correctnessMetric = 1.0;
            digests += Sha256::of(bytes.data(), bytes.size());
        }
        if (verification->close().failed())
        {
            failure = "published image-list close failed";
            return std::string{};
        }
        return Sha256::of(digests.data(), digests.size());
    };
    const bool completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    out.counters["outputBytes"] = static_cast<double>(scenario.bytesProcessed);
    return completed;
}

bool AfterNightEngineAdapter::runStreaming(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                           std::string& skipReason)
{
    if (!matchesStreamingScenario(scenario))
    {
        skipReason = "unsupported streaming scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    const auto codec = an::createFitsCodec();
    const std::array codecs{*codec};
    auto settings = benchmarkEngineOptions(options);
    settings.codecRegistry = *an::CodecRegistry::create(codecs);
    auto engine = an::EngineContext::create(settings);
    if (!engine)
    {
        skipReason = engine.status().message();
        return false;
    }
    const bool writing = scenario.parameters.at("operation") == "write";
    const bool queued = scenario.parameters.at("mode") == "queued";
    const auto extent = *an::ImageExtent::create(1024u, 1024u, 1u, engine->limits());
    const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                             {4u, an::RawSampleClass::FloatingPoint},
                                                             an::ByteOrder::Native,
                                                             an::ChannelSchema::Mono,
                                                             an::ColorSpace::Unspecified,
                                                             an::NumericDomain::PhysicalScaled,
                                                             {},
                                                             an::CoordinateOrigin::TopLeft,
                                                             1.0});
    const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, engine->limits());
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-bench-stream-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
    {
        skipReason = "streaming temporary directory already exists";
        return false;
    }
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    auto pathText = [](const std::filesystem::path& path) {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    };
    const auto inputPath = pathText(directory / "input.fit");
    std::array<std::string, 8u> outputPaths;
    for (std::size_t i = 0u; i < outputPaths.size(); ++i)
        outputPaths[i] = pathText(directory / (std::to_string(i) + ".fit"));
    if (!writing)
    {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
        auto frame = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        auto saved = an::writeImage(*engine, *execution,
                                    {.destinationPath = inputPath,
                                     .format = an::CodecFormat::Fits,
                                     .durability = an::OutputDurability::CloseOnly},
                                    *frame, {});
        if (!saved)
        {
            skipReason = saved.status().message();
            return false;
        }
    }
    class CloneSource final : public an::FrameSourceBackend
    {
    public:
        CloneSource(const an::RawFrameDescriptor& descriptor, const an::ImageLayout& layout,
                    std::span<const std::byte> bytes)
            : m_descriptor(descriptor), m_layout(layout), m_bytes(bytes)
        {
        }
        an::Result<an::SequenceReadResult> read(const an::EngineContext& engine, const an::ExecutionContext&,
                                                std::uint64_t index) override
        {
            auto buffer = an::SampleBuffer::createOwned(engine, m_layout, an::StorageInitialization::Uninitialized);
            if (!buffer)
                return buffer.status();
            std::memcpy(buffer->mutableBytes()->data(), m_bytes.data(), m_bytes.size());
            auto frame = an::RawFrame::create(m_descriptor, m_layout, std::move(*buffer));
            if (!frame)
                return frame.status();
            return an::SequenceReadResult{an::SequenceFrame{std::move(*frame), {}, {index}}};
        }
        an::Status close() override
        {
            return an::Status::success();
        }

    private:
        an::RawFrameDescriptor m_descriptor;
        an::ImageLayout m_layout;
        std::span<const std::byte> m_bytes;
    };
    class PrivateFiles final : public an::FrameSinkBackend
    {
    public:
        explicit PrivateFiles(const std::array<std::string, 8u>& paths) : m_paths(paths) {}
        an::Result<an::SequenceWriteResult> append(const an::EngineContext& engine,
                                                   const an::ExecutionContext& execution,
                                                   const an::SequenceFrame& frame) override
        {
            // These files remain inside the benchmark's private staging directory. Publication
            // overhead is included identically to a real image write, without exposing an artifact.
            auto written = an::writeImage(engine, execution,
                                          {.destinationPath = m_paths[frame.position.index],
                                           .format = an::CodecFormat::Fits,
                                           .durability = an::OutputDurability::CloseOnly},
                                          frame.frame, frame.metadata);
            if (!written)
                return written.status();
            ++m_progress.framesWritten;
            m_progress.bytesWritten += frame.frame.bytes().size();
            return m_progress;
        }
        an::Result<an::SequenceWriteResult> finish(const an::ExecutionContext&) override
        {
            return m_progress;
        }
        an::Status close() override
        {
            return an::Status::success();
        }

    private:
        const std::array<std::string, 8u>& m_paths;
        an::SequenceWriteResult m_progress{};
    };
    class Delay final : public an::FrameStreamProcessor
    {
    public:
        bool check = false;
        std::chrono::milliseconds delay{};
        std::span<const std::byte> expected;
        std::string digests;
        an::Result<an::SequenceFrame> process(const an::EngineContext&, const an::ExecutionContext&,
                                              an::SequenceFrame frame) override
        {
            if (delay.count() != 0)
                std::this_thread::sleep_for(delay);
            if (check)
            {
                const auto bytes = frame.frame.bytes();
                if (bytes.size() != expected.size() || std::memcmp(bytes.data(), expected.data(), bytes.size()) != 0)
                    return an::Status::failure(an::ErrorCode::InvalidData);
                digests += Sha256::of(bytes.data(), bytes.size());
            }
            return frame;
        }
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        std::array<an::CodecReadRequest, 8u> entries;
        if (!execution)
        {
            failure = execution.status().message();
            return std::string{};
        }
        entries.fill({inputPath, an::CodecFormat::Fits});
        const an::SequenceDescriptor sequence{
            .format = an::SequenceFormat::ImageList, .fixedFrame = descriptor, .frameCount = 8u};
        auto source =
            writing ? an::FrameSource::create(
                          *engine, sequence,
                          std::make_unique<CloneSource>(descriptor, layout, std::as_bytes(std::span(fixture.bytes))))
                    : an::createImageListSource(*engine, *execution, entries, {});
        auto sink = an::FrameSink::create(*engine, sequence, std::make_unique<PrivateFiles>(outputPaths));
        if (!execution || !source || !sink)
        {
            failure = "streaming session creation failed";
            return std::string{};
        }
        Delay processor;
        processor.check = produceDigest && !writing;
        processor.delay = streamingProcessingDelay(scenario);
        processor.expected = std::as_bytes(std::span(fixture.bytes));
        an::FrameStreamOptions request;
        const auto resident = streamingResidentQueueFrames(scenario);
        request.readAheadDepth = queued && !writing ? resident : 0u;
        request.writeBehindDepth = queued && writing ? resident : 0u;
        request.maxFrameBytes = layout.totalBytes();
        request.readAheadBytes = request.writeBehindBytes = resident * layout.totalBytes();
        an::FrameStreamCounters counters;
        const auto status =
            an::streamFrames(*engine, *execution, *source, writing ? &*sink : nullptr, &processor, request, counters);
        if (status.failed())
        {
            failure = describeEngineFailure("streaming", status);
            return std::string{};
        }
        out.counters["queuePeakFrames"] =
            static_cast<double>(writing ? counters.writeBehind.peakFrames : counters.readAhead.peakFrames);
        out.counters["queuePeakReservedBytes"] = static_cast<double>(writing ? counters.writeBehind.peakReservedBytes
                                                                             : counters.readAhead.peakReservedBytes);
        out.counters["queueWaitNanoseconds"] =
            static_cast<double>(writing ? counters.writeBehind.waitNanoseconds : counters.readAhead.waitNanoseconds);
        out.counters["frames"] = static_cast<double>(counters.framesProcessed);
        if (writing && produceDigest)
            for (const auto& path : outputPaths)
            {
                auto decoded = an::decodeImage(*engine, *execution, {path, an::CodecFormat::Fits});
                if (!decoded || decoded->frame.bytes().size() != fixture.bytes.size() ||
                    std::memcmp(decoded->frame.bytes().data(), fixture.bytes.data(), fixture.bytes.size()) != 0)
                {
                    failure = "streaming output differs from the complete fixture";
                    return std::string{};
                }
                processor.digests += Sha256::of(decoded->frame.bytes().data(), decoded->frame.bytes().size());
            }
        out.correctnessMetric = 0.0;
        return produceDigest ? Sha256::of(processor.digests.data(), processor.digests.size()) : std::string{};
    };
    const auto completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    return completed;
}

bool AfterNightEngineAdapter::runSerSequence(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    if (!matchesSerSequenceScenario(scenario))
    {
        skipReason = "unsupported SER sequence scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    const bool writing = scenario.parameters.at("mode") == "write";
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine)
    {
        skipReason = "SER context creation failed";
        return false;
    }
    const auto extent = *an::ImageExtent::create(2048u, 2048u, 1u, engine->limits());
    const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                             {2u, an::RawSampleClass::UnsignedInteger},
                                                             an::ByteOrder::Little,
                                                             an::ChannelSchema::Mono,
                                                             an::ColorSpace::Unspecified,
                                                             an::NumericDomain::RawSensorCode,
                                                             {},
                                                             an::CoordinateOrigin::TopLeft,
                                                             1.0});
    const auto layout = *an::ImageLayout::create({extent, 2u, 1u, 1u, 1u}, engine->limits());
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-bench-ser-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
    {
        skipReason = "SER temporary directory already exists";
        return false;
    }
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto text = (directory / "frames.ser").u8string();
    const std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    constexpr std::uint64_t kFrames = 8u;
    const std::uint64_t frameBytes = fixture.bytes.size();
    std::string failure;
    // Unknown dates at the Unix epoch and a trailer of unknown timestamps, so two runs write files that
    // compare bit for bit.
    const an::SerSinkRequest request{.dateTimeTicks = an::kSerUnixEpochTicks,
                                     .dateTimeUtcTicks = an::kSerUnixEpochTicks,
                                     .timestamps = an::SerTimestampSource::ExposureStart};
    const an::SequenceDescriptor sequence{
        .format = an::SequenceFormat::Ser, .fixedFrame = descriptor, .frameCount = kFrames};
    const auto writeContainer = [&](const an::ExecutionContext& execution) {
        auto output = an::SequenceOutput::begin(
            *engine, execution, {.destinationPath = path, .durability = an::OutputDurability::CloseOnly});
        if (!output)
        {
            failure = describeEngineFailure("begin SER output", output.status());
            return false;
        }
        auto sink = an::createSerSink(*engine, execution, *output, sequence, request);
        if (!sink)
        {
            failure = describeEngineFailure("create SER sink", sink.status());
            return false;
        }
        auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
        auto raw = buffer ? an::RawFrame::create(descriptor, layout, std::move(*buffer))
                          : an::Result<an::RawFrame>(buffer.status());
        if (!raw)
        {
            failure = describeEngineFailure("describe SER frame", raw.status());
            return false;
        }
        an::SequenceFrame frame{std::move(*raw), {}, {}};
        for (std::uint64_t index = 0u; index < kFrames; ++index)
        {
            frame.position.index = index;
            auto appended = sink->append(*engine, execution, frame);
            if (!appended)
            {
                failure = describeEngineFailure("append SER frame", appended.status());
                return false;
            }
        }
        auto finished = sink->finish(execution);
        if (!finished)
        {
            failure = describeEngineFailure("finish SER sink", finished.status());
            return false;
        }
        auto committed = output->commit(execution, *finished);
        if (!committed)
        {
            failure = describeEngineFailure("commit SER output", committed.status());
            return false;
        }
        return sink->close().ok();
    };
    if (!writing)
    {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution || !writeContainer(*execution))
        {
            skipReason = failure.empty() ? "SER fixture preparation failed" : failure;
            return false;
        }
    }
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution)
        {
            failure = "SER execution creation failed";
            return std::string{};
        }
        if (writing)
        {
            if (!writeContainer(*execution) || !produceDigest)
                return std::string{};
            std::ifstream stream(std::filesystem::path(text), std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            out.correctnessMetric = bytes.size() == an::kSerHeaderBytes + kFrames * (frameBytes + 8u) ? 0.0 : 1.0;
            for (std::uint64_t index = 0u; index < kFrames && out.correctnessMetric == 0.0; ++index)
                if (std::memcmp(bytes.data() + an::kSerHeaderBytes + index * frameBytes, fixture.bytes.data(),
                                frameBytes) != 0)
                    out.correctnessMetric = 1.0;
            return Sha256::of(bytes.data(), bytes.size());
        }
        auto source = an::createSerSource(*engine, *execution, path, {});
        if (!source)
        {
            failure = describeEngineFailure("open SER source", source.status());
            return std::string{};
        }
        std::array<std::optional<an::SequenceFrame>, kFrames> frames;
        for (auto& frame : frames)
        {
            auto read = source->read(*engine, *execution);
            if (!read || !read->frame)
            {
                failure = read ? "SER source ended early" : describeEngineFailure("read SER frame", read.status());
                return std::string{};
            }
            frame = std::move(read->frame);
        }
        if (const auto closed = source->close(); closed.failed())
        {
            failure = "SER source close failed";
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        std::string digests;
        out.correctnessMetric = 0.0;
        for (const auto& frame : frames)
        {
            const auto bytes = frame->frame.bytes();
            if (bytes.size() < frameBytes || std::memcmp(bytes.data(), fixture.bytes.data(), frameBytes) != 0)
                out.correctnessMetric = 1.0;
            digests += Sha256::of(bytes.data(), static_cast<std::size_t>(frameBytes));
        }
        return Sha256::of(digests.data(), digests.size());
    };
    const auto completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    out.counters["outputBytes"] = static_cast<double>(scenario.bytesProcessed);
    return completed;
}

bool AfterNightEngineAdapter::runFfmpegSequence(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    if (!matchesFfmpegSequenceScenario(scenario))
    {
        skipReason = "unsupported FFmpeg sequence scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine)
    {
        skipReason = "FFmpeg context creation failed";
        return false;
    }
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-bench-ffmpeg-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
    {
        skipReason = "FFmpeg temporary directory already exists";
        return false;
    }
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto file = directory / "frames.mkv";
    if (const auto prepared = writeFfv1Video(file, std::as_bytes(std::span(fixture.bytes)), 2048, 2048, 8, 16);
        !prepared.empty())
    {
        skipReason = "FFV1 fixture preparation failed: " + prepared;
        return false;
    }
    const auto text = file.u8string();
    const std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    constexpr std::size_t kFrames = 8u;
    const bool cached = scenario.parameters.at("mode") == "cache";
    constexpr std::array<std::uint64_t, kFrames> kCachedAccess{0u, 1u, 0u, 1u, 0u, 1u, 2u, 3u};
    const std::uint64_t frameBytes = fixture.bytes.size();
    std::string failure;
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution)
        {
            failure = "FFmpeg execution creation failed";
            return std::string{};
        }
        auto opened = an::createFfmpegSource(*engine, *execution, path, {});
        if (!opened)
        {
            failure = describeEngineFailure("open FFmpeg source", opened.status());
            return std::string{};
        }
        an::FrameSource source = std::move(*opened);
        an::FrameCacheControl cacheControl;
        if (cached)
        {
            auto wrapped = an::createCachedFrameSource(*engine, *execution, std::move(source), {.maxFrames = 2u});
            if (!wrapped)
            {
                failure = describeEngineFailure("create FFmpeg frame cache", wrapped.status());
                return std::string{};
            }
            source = std::move(wrapped->source);
            cacheControl = std::move(wrapped->control);
        }
        std::array<std::optional<an::SequenceFrame>, kFrames> frames;
        for (std::size_t index = 0u; index < frames.size(); ++index)
        {
            if (cached && !source.seek(*execution, {.index = kCachedAccess[index]}))
            {
                failure = "FFmpeg cache seek failed";
                return std::string{};
            }
            auto read = source.read(*engine, *execution);
            if (!read || !read->frame)
            {
                failure =
                    read ? "FFmpeg source ended early" : describeEngineFailure("read FFmpeg frame", read.status());
                return std::string{};
            }
            frames[index] = std::move(read->frame);
        }
        if (produceDigest && cached)
        {
            const auto counters = cacheControl.counters();
            out.counters["cacheHits"] = static_cast<double>(counters.hits);
            out.counters["cacheMisses"] = static_cast<double>(counters.misses);
        }
        if (const auto closed = source.close(); closed.failed())
        {
            failure = "FFmpeg source close failed";
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        std::string digests;
        out.correctnessMetric = 0.0;
        for (const auto& frame : frames)
        {
            const auto bytes = frame->frame.bytes();
            if (bytes.size() < frameBytes || std::memcmp(bytes.data(), fixture.bytes.data(), frameBytes) != 0)
                out.correctnessMetric = 1.0;
            digests += Sha256::of(bytes.data(), static_cast<std::size_t>(frameBytes));
        }
        return Sha256::of(digests.data(), digests.size());
    };
    const auto completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    out.counters["outputBytes"] = static_cast<double>(scenario.bytesProcessed);
    return completed;
}

bool AfterNightEngineAdapter::runVideoWrite(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                            std::string& skipReason)
{
    if (!matchesVideoWriteScenario(scenario))
    {
        skipReason = "unsupported video write scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    const std::uint32_t width = scenario.input.width;
    const std::uint32_t height = scenario.input.height;
    // The picture is mirrored the same way before timing in every run, so a writer that stores it upside down writes
    // the same rows as one that stores it upright.
    mirrorRows(std::as_writable_bytes(std::span(fixture.bytes)), width, height);
    const std::string format = scenario.parameters.at("format");
    const bool avi = format == "AVI";
    const bool vp9 = format == "VP9";
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine)
    {
        skipReason = "video write context creation failed";
        return false;
    }
    const auto encoderThreads =
        avi ? 1u : static_cast<std::uint32_t>(std::stoul(scenario.parameters.at("encoderThreads")));
    if (encoderThreads > 1u && engine->maxConcurrency() <= encoderThreads)
    {
        skipReason = "the scenario lends the encoder " + std::to_string(encoderThreads) +
                     " threads beside its appending operation, and the context has " +
                     std::to_string(engine->maxConcurrency());
        return false;
    }
    const auto extent = *an::ImageExtent::create(width, height, 1u, engine->limits());
    const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                             {1u, an::RawSampleClass::UnsignedInteger},
                                                             an::ByteOrder::Native,
                                                             an::ChannelSchema::Mono,
                                                             an::ColorSpace::Unspecified,
                                                             an::NumericDomain::RawSensorCode,
                                                             {},
                                                             an::CoordinateOrigin::TopLeft,
                                                             1.0});
    const auto layout = *an::ImageLayout::create({extent, 1u, 1u, 1u, 1u}, engine->limits());
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("an-bench-video-write-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
    {
        skipReason = "video write temporary directory already exists";
        return false;
    }
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto file = directory / (avi ? "frames.avi" : vp9 ? "frames.webm" : "frames.mp4");
    const auto text = file.u8string();
    const std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    constexpr std::uint64_t kFrames = 8u;
    std::string failure;
    const an::SequenceDescriptor sequence{.format = avi   ? an::SequenceFormat::Avi
                                                    : vp9 ? an::SequenceFormat::Video
                                                          : an::SequenceFormat::Mp4,
                                          .fixedFrame = descriptor,
                                          .frameCount = kFrames,
                                          .nominalFrameRate = an::SequenceRate{25u, 1u}};
    const an::FfmpegSinkRequest videoRequest{.codec = vp9 ? an::FfmpegVideoCodec::Vp9 : an::FfmpegVideoCodec::H264,
                                             .container =
                                                 vp9 ? an::FfmpegVideoContainer::WebM : an::FfmpegVideoContainer::Mp4,
                                             .quality = an::FfmpegEncodeQuality::Standard,
                                             .encoderThreads = encoderThreads};
    const auto writeContainer = [&](const an::ExecutionContext& execution) {
        auto output = an::SequenceOutput::begin(
            *engine, execution, {.destinationPath = path, .durability = an::OutputDurability::CloseOnly});
        if (!output)
        {
            failure = describeEngineFailure("begin video output", output.status());
            return false;
        }
        auto sink = avi ? an::createAviSink(*engine, execution, *output, sequence, {})
                        : an::createFfmpegSink(*engine, execution, *output, sequence, videoRequest);
        if (!sink)
        {
            failure = describeEngineFailure("create video sink", sink.status());
            return false;
        }
        auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
        auto raw = buffer ? an::RawFrame::create(descriptor, layout, std::move(*buffer))
                          : an::Result<an::RawFrame>(buffer.status());
        if (!raw)
        {
            failure = describeEngineFailure("describe video frame", raw.status());
            return false;
        }
        an::SequenceFrame frame{std::move(*raw), {}, {}};
        for (std::uint64_t index = 0u; index < kFrames; ++index)
        {
            frame.position.index = index;
            auto appended = sink->append(*engine, execution, frame);
            if (!appended)
            {
                failure = describeEngineFailure("append video frame", appended.status());
                return false;
            }
        }
        auto finished = sink->finish(execution);
        if (!finished)
        {
            failure = describeEngineFailure("finish video sink", finished.status());
            return false;
        }
        auto committed = output->commit(execution, *finished);
        if (!committed)
        {
            failure = describeEngineFailure("commit video output", committed.status());
            return false;
        }
        return sink->close().ok();
    };
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        // An encoder's threads are leased beside the appending operation, which therefore asks for one.
        an::ExecutionOptions executionOptions = executionOptionsForRun(options);
        executionOptions.maxConcurrency = avi ? an::kUseContextConcurrency : 1u;
        auto execution = an::ExecutionContext::create(*engine, executionOptions);
        if (!execution)
        {
            failure = "video write execution creation failed";
            return std::string{};
        }
        if (!writeContainer(*execution) || !produceDigest)
            return std::string{};
        const auto samples = std::as_bytes(std::span(fixture.bytes));
        if (avi)
        {
            AviFrames frames;
            if (auto read = readAviFrames(file, samples, frames); !read.empty())
            {
                failure = "read the written AVI: " + read;
                return std::string{};
            }
            out.correctnessMetric = frames.frames == kFrames && frames.everyFrameIs ? 0.0 : 1.0;
            return frames.digest;
        }
        VideoPackets packets;
        if (auto read = readVideoPackets(file, samples, static_cast<int>(width), static_cast<int>(height), packets);
            !read.empty())
        {
            failure = "read the written video: " + read;
            return std::string{};
        }
        out.correctnessMetric = packets.frames == kFrames ? packets.lumaRms : 1.0e9;
        return packets.digest;
    };
    const auto completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    out.counters["outputBytes"] = static_cast<double>(scenario.bytesProcessed);
    return completed;
}

bool AfterNightEngineAdapter::runCodecRoundTrip(const ScenarioSpec& scenario, const RunOptions& options,
                                                ResultRecord& out, std::string& skipReason)
{
    const auto format = parameterValue(scenario, "format", "FITS");
    const bool dump = format == "DUMP";
    if (dump && !matchesDumpCodecScenario(scenario))
    {
        skipReason = "unsupported dump codec scenario";
        return false;
    }
    const auto access = parameterValue(scenario, "access", "roundtrip");
    const bool regionRead = dump && access != "roundtrip";
    const an::CodecRegion area =
        access == "strip" ? an::CodecRegion{0u, 512u, 2048u, 1024u} : an::CodecRegion{512u, 512u, 256u, 1024u};
    const bool xisf = format == "XISF";
    const bool raw = format == "RAW";
    const bool fits = format == "FITS";
    const bool png = format == "PNG";
    const bool tiff = format == "TIFF";
    const bool jpeg = format == "JPEG";
    const bool raster = png || tiff || jpeg;
    const auto expectedSampleType = jpeg                 ? SampleType::UInt8
                                    : raw || png || tiff ? SampleType::UInt16
                                                         : SampleType::Float32;
    if (scenario.input.sampleType != expectedSampleType || scenario.input.channels != 1u ||
        scenario.input.layout != Layout::Planar || !(fits || xisf || raw || raster || dump))
    {
        skipReason = "unsupported codec round-trip fixture";
        return false;
    }
    if (raw && !matchesRawCodecScenario(scenario))
    {
        skipReason = "unsupported RAW compatibility scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    if (raw)
        prepareRawCodecFixture(fixture);
    auto adapter = dump   ? an::createDumpCodec()
                   : raw  ? an::createRawCodec()
                   : xisf ? an::createXisfCodec()
                   : png  ? an::createPngCodec()
                   : jpeg ? an::createJpegCodec()
                   : tiff ? an::createTiffCodec()
                          : an::createFitsCodec();
    if (!adapter.hasValue())
    {
        skipReason = describeEngineFailure("create codec", adapter.status());
        return false;
    }
    const std::array adapters{*adapter};
    auto registry = an::CodecRegistry::create(adapters);
    if (!registry.hasValue())
    {
        skipReason = describeEngineFailure("register codec", registry.status());
        return false;
    }
    auto engineOptions = benchmarkEngineOptions(options);
    engineOptions.codecRegistry = *registry;
    auto engine = an::EngineContext::create(engineOptions);
    if (!engine.hasValue())
    {
        skipReason = describeEngineFailure("create codec context", engine.status());
        return false;
    }
    const auto bridged = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor),
                                       engine->limits(), fixture.bytes.size());
    if (!bridged)
    {
        skipReason = "invalid codec fixture layout";
        return false;
    }
    const auto descriptor = *an::RawFrameDescriptor::create(
        {bridged->layout.extent(),
         {static_cast<std::uint8_t>(jpeg                 ? 1u
                                    : raw || png || tiff ? 2u
                                                         : 4u),
          raw || raster ? an::RawSampleClass::UnsignedInteger : an::RawSampleClass::FloatingPoint},
         an::ByteOrder::Native,
         an::ChannelSchema::Mono,
         an::ColorSpace::Unspecified,
         raw      ? an::NumericDomain::RawSensorCode
         : raster ? an::NumericDomain::NormalizedLinear
                  : an::NumericDomain::PhysicalScaled,
         {},
         an::CoordinateOrigin::TopLeft,
         1.0});
    auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
    if (!buffer.hasValue())
    {
        skipReason = "invalid codec fixture buffer";
        return false;
    }
    auto frame = an::RawFrame::create(descriptor, bridged->layout, std::move(*buffer));
    if (!frame.hasValue())
    {
        skipReason = describeEngineFailure("create codec frame", frame.status());
        return false;
    }
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("afternight-bench-codec-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
        }
    } cleanup{directory};
    const auto nativePath = (directory / (dump   ? "roundtrip.dump"
                                          : raw  ? "mosaic.dng"
                                          : xisf ? "roundtrip.xisf"
                                          : png  ? "roundtrip.png"
                                          : tiff ? "roundtrip.tif"
                                          : jpeg ? "roundtrip.jpg"
                                                 : "roundtrip.fit"))
                                .u8string();
    const std::string path(reinterpret_cast<const char*>(nativePath.data()), nativePath.size());
    if (raw)
        writeRawCodecFixture(directory / "mosaic.dng", scenario.input.width, scenario.input.height, fixture.bytes,
                             scenario.parameters.at("operation") == "orientation" ? 6u : 1u);
    if (regionRead)
    {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution.hasValue())
        {
            skipReason = "dump preparation execution refused";
            return false;
        }
        auto saved = an::writeImage(
            *engine, *execution,
            {.destinationPath = path, .format = an::CodecFormat::Dump, .durability = an::OutputDurability::CloseOnly},
            *frame, {});
        if (!saved.hasValue())
        {
            skipReason = "dump preparation failed";
            return false;
        }
    }
    std::vector<std::uint8_t> expected;
    if (regionRead)
    {
        expected.resize(std::size_t{area.width} * area.height * 4u);
        for (std::uint32_t y = 0u; y < area.height; ++y)
            std::memcpy(expected.data() + std::size_t{y} * area.width * 4u,
                        fixture.bytes.data() + (std::size_t{area.y + y} * scenario.input.width + area.x) * 4u,
                        std::size_t{area.width} * 4u);
    }
    const auto& expectedBytes = regionRead ? expected : fixture.bytes;
    const bool rawMosaicBalance = raw && scenario.parameters.at("operation") == "mosaic-balance";
    std::vector<std::uint16_t> rawMosaicOutput;
    if (rawMosaicBalance)
        rawMosaicOutput.resize(static_cast<std::size_t>(scenario.input.width) * scenario.input.height);
    auto intent = an::OutputIntent::create(
        jpeg ? an::OutputIntentRequest{.sampleType = an::SampleType::UInt8,
                                       .compression = an::OutputCompression::Jpeg,
                                       .compressionLevel = 95u}
             : an::OutputIntentRequest{.sampleType = png || tiff ? an::SampleType::UInt16 : an::SampleType::Float32});
    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution.hasValue())
        {
            failure = describeEngineFailure("create codec execution", execution.status());
            return std::string{};
        }
        if (!raw && !regionRead)
        {
            // Close-before-rename durability, as an ordinary save asks for.
            auto saved = an::writeImage(*engine, *execution,
                                        {.destinationPath = path,
                                         .format = dump ? an::CodecFormat::Dump : an::CodecFormat::Unknown,
                                         .intent = &*intent,
                                         .durability = an::OutputDurability::CloseOnly},
                                        *frame, {});
            if (!saved.hasValue())
            {
                failure = describeEngineFailure("save frame", saved.status());
                return std::string{};
            }
        }
        auto decoded = regionRead ? an::decodeImageRegion(*engine, *execution, {path, an::CodecFormat::Dump}, area)
                                  : an::decodeImage(*engine, *execution,
                                                    {path, dump ? an::CodecFormat::Dump : an::CodecFormat::Unknown});
        if (!decoded.hasValue())
        {
            failure = describeEngineFailure("decode frame", decoded.status());
            return std::string{};
        }
        if (rawMosaicBalance)
        {
            const auto rawDescriptor = decoded->frame.descriptor();
            const auto layout = decoded->frame.layout();
            const auto camera = decoded->metadata.acquisition().rawCamera();
            if (!rawDescriptor || !layout || !camera)
            {
                failure = "RAW mosaic balance lacks its decoded descriptor or camera record";
                return std::string{};
            }
            const auto balance = an::resolveCfaWhiteBalance(
                *camera, {.mode = an::CfaWhiteBalanceMode::Custom, .custom = {2.0, 1.0, 1.5}});
            if (!balance)
            {
                failure = describeEngineFailure("resolve RAW mosaic balance", balance.status());
                return std::string{};
            }
            const an::ConversionPolicy writeBack{.rounding = an::RoundingMode::TowardZero,
                                                 .domain = an::DomainMapping::CodeValue};
            const auto applied = an::applyCfaChannelGains(
                *execution, an::SampleType::UInt16, decoded->frame.bytes().data(), *layout, rawDescriptor->cfa(),
                rawMosaicOutput.data(), *layout, balance->multipliers(), writeBack);
            if (!applied)
            {
                failure = describeEngineFailure("apply RAW mosaic balance", applied.status());
                return std::string{};
            }
            if (produceDigest)
            {
                double worst = 0.0;
                for (std::uint32_t y = 0u; y < scenario.input.height; ++y)
                {
                    for (std::uint32_t x = 0u; x < scenario.input.width; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(y) * scenario.input.width + x;
                        std::uint16_t source;
                        std::memcpy(&source, fixture.bytes.data() + index * sizeof(source), sizeof(source));
                        const double gain = (y % 2u == 0u && x % 2u == 0u)   ? 2.0
                                            : (y % 2u == 1u && x % 2u == 1u) ? 1.5
                                                                             : 1.0;
                        const double expected = (std::min)(std::trunc(static_cast<double>(source) * gain), 65535.0);
                        worst = (std::max)(worst, std::abs(static_cast<double>(rawMosaicOutput[index]) - expected));
                    }
                }
                out.correctnessMetric = worst;
            }
            return produceDigest ? Sha256::of(rawMosaicOutput.data(), rawMosaicOutput.size() * sizeof(std::uint16_t))
                                 : std::string{};
        }
        if (raw && scenario.parameters.at("operation") == "orientation")
        {
            const auto rawDescriptor = decoded->frame.descriptor();
            const auto sourceLayout = decoded->frame.layout();
            const auto camera = decoded->metadata.acquisition().rawCamera();
            const auto orientation = camera ? an::imageOrientationFromRawFlip(camera->orientation) : std::nullopt;
            const auto imageDescriptor = rawDescriptor ? rawDescriptor->toImageDescriptor() : std::nullopt;
            if (!rawDescriptor || !sourceLayout || !orientation || !imageDescriptor)
            {
                failure = "RAW orientation lacks its decoded descriptor, layout, or orientation record";
                return std::string{};
            }
            const auto plan = an::planOrientation(*imageDescriptor, *orientation);
            if (!plan)
            {
                failure = describeEngineFailure("plan RAW orientation", plan.status());
                return std::string{};
            }
            const auto destinationLayout = an::ImageLayout::create({.extent = plan->destinationDescriptor().extent(),
                                                                    .elementSizeBytes = 2u,
                                                                    .samplesPerPixel = 1u,
                                                                    .planeCount = 1u,
                                                                    .rowAlignmentBytes = 1u},
                                                                   engine->limits());
            auto mask = an::ValidityMask::create(*engine, plan->destinationDescriptor().extent());
            if (!destinationLayout || !mask || !mask->view())
            {
                failure = "RAW orientation destination allocation failed";
                return std::string{};
            }
            std::vector<std::uint16_t> oriented(static_cast<std::size_t>(scenario.input.width) * scenario.input.height);
            const auto transformed = an::transformImageGeometry(
                *execution, an::SampleType::UInt16, decoded->frame.bytes().data(), *sourceLayout, oriented.data(),
                *destinationLayout, *plan, {}, *mask->view());
            if (!transformed)
            {
                failure = describeEngineFailure("apply RAW orientation", transformed.status());
                return std::string{};
            }
            return produceDigest ? Sha256::of(oriented.data(), oriented.size() * sizeof(std::uint16_t)) : std::string{};
        }
        if (!produceDigest)
            return std::string{};
        out.correctnessMetric =
            !jpeg && decoded->frame.bytes().size() == expectedBytes.size() &&
                    std::memcmp(decoded->frame.bytes().data(), expectedBytes.data(), expectedBytes.size()) == 0
                ? 0.0
                : 1.0;
        if (jpeg)
        {
            // The transform is lossy, so what is measured is how far the round trip moved a
            // sample.
            const auto* const produced = reinterpret_cast<const unsigned char*>(decoded->frame.bytes().data());
            const auto* const started = reinterpret_cast<const unsigned char*>(fixture.bytes.data());
            double worst = 0.0;
            for (std::size_t index = 0u; index < fixture.bytes.size(); ++index)
            {
                worst = (std::max)(worst, std::abs(static_cast<double>(produced[index]) -
                                                   static_cast<double>(started[index])));
            }
            out.correctnessMetric = decoded->frame.bytes().size() == fixture.bytes.size() ? worst / 255.0 : 1.0;
        }
        return Sha256::of(decoded->frame.bytes().data(), decoded->frame.bytes().size());
    };
    const bool completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["outputBytes"] = static_cast<double>(expectedBytes.size());
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    return completed;
}

bool AfterNightEngineAdapter::runSnapshotRoundTrip(const ScenarioSpec& scenario, const RunOptions& options,
                                                   ResultRecord& out, std::string& skipReason)
{
    if (!matchesSnapshotScenario(scenario))
    {
        skipReason = "unsupported snapshot scenario";
        return false;
    }
    Fixture fixture;
    if (!m_fixtures.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, skipReason))
        return false;
    auto engine = an::EngineContext::create(benchmarkEngineOptions(options));
    if (!engine.hasValue())
    {
        skipReason = describeEngineFailure("create snapshot context", engine.status());
        return false;
    }
    const auto bridged = bridgeFixture(fixture.descriptor, defaultFixtureSemantics(fixture.descriptor),
                                       engine->limits(), fixture.bytes.size());
    if (!bridged)
    {
        skipReason = "invalid snapshot fixture layout";
        return false;
    }
    const auto descriptor = an::RawFrameDescriptor::create({bridged->layout.extent(),
                                                            {4u, an::RawSampleClass::FloatingPoint},
                                                            an::ByteOrder::Native,
                                                            an::ChannelSchema::Mono,
                                                            an::ColorSpace::Unspecified,
                                                            an::NumericDomain::NormalizedLinear,
                                                            {},
                                                            an::CoordinateOrigin::TopLeft,
                                                            1.0});
    auto buffer = an::SampleBuffer::borrowConst(std::as_bytes(std::span(fixture.bytes)));
    if (!descriptor || !buffer.hasValue())
    {
        skipReason = "invalid snapshot fixture";
        return false;
    }
    auto frame = an::RawFrame::create(*descriptor, bridged->layout, std::move(*buffer));
    if (!frame.hasValue())
    {
        skipReason = describeEngineFailure("create snapshot frame", frame.status());
        return false;
    }

    // What a history step holds beside the samples: the header's cards, a solution and a lineage.
    std::vector<an::CodecCard> cards;
    for (std::uint32_t card = 0u; card < 40u; ++card)
        cards.push_back(
            *an::CodecCard::create(an::CodecNamespace::Fits, snapshotCardKeyword(card), snapshotCardValue(card), ""));
    for (const auto& [keyword, value] :
         std::initializer_list<std::pair<const char*, const char*>>{{"CTYPE1", "'RA---TAN'"},
                                                                    {"CTYPE2", "'DEC--TAN'"},
                                                                    {"CRVAL1", "83.822"},
                                                                    {"CRVAL2", "-5.391"},
                                                                    {"CRPIX1", "1024.5"},
                                                                    {"CRPIX2", "1024.5"},
                                                                    {"CD1_1", "-6.1E-04"},
                                                                    {"CD2_2", "6.1E-04"}})
        cards.push_back(*an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, ""));
    auto codec = an::CodecMetadata::create({}, std::move(cards));
    auto acquisition = an::AcquisitionMetadata::create(
        {.instrumentName = "bench camera", .objectName = "M 42", .filterName = "L", .exposureSeconds = 300.0});
    auto provenance = an::Provenance::create(an::ProvenanceSourceKind::File, "light-0001.fits");
    auto solution = an::AstrometricSolution::create({.referencePoint = {83.822, -5.391},
                                                     .referencePixelX = 1023.5,
                                                     .referencePixelY = 1023.5,
                                                     .pixelScaleArcsecPerPixel = 2.2,
                                                     .orientationDegrees = 0.0,
                                                     .mirrored = false,
                                                     .hasDistortion = false});
    if (!codec || !acquisition || !provenance || !solution)
    {
        skipReason = "invalid snapshot metadata";
        return false;
    }
    for (const char* step : {"decodeImage", "calibrateImage", "transformImageGeometry"})
        provenance = *provenance->appended(*an::ProvenanceEntry::create(step, ""));
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(*acquisition, {}, *solution, *provenance, *codec, std::nullopt);
    const auto identity = an::ArtifactIdentity::create("history-step-0001", "bench");
    if (!identity)
    {
        skipReason = "invalid snapshot identity";
        return false;
    }

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("afternight-bench-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
        }
    } cleanup{directory};
    const auto nativePath = (directory / "step.ansnap").u8string();
    const std::string path(reinterpret_cast<const char*>(nativePath.data()), nativePath.size());

    out.fixtureDigest = fixture.digest;
    out.threadCount = engine->maxConcurrency();
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::CompilerVectorized;
    std::string failure;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        {
            auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
            if (!execution.hasValue())
            {
                failure = describeEngineFailure("create snapshot execution", execution.status());
                return std::string{};
            }
            // A history's swap file is written without a flush.
            auto written = an::writeImageSnapshot(
                *engine, *execution,
                {.destinationPath = path, .identity = &*identity, .durability = an::OutputDurability::CloseOnly},
                *frame, metadata);
            if (!written.hasValue())
            {
                failure = describeEngineFailure("write snapshot", written.status());
                return std::string{};
            }
        }
        auto execution = an::ExecutionContext::create(*engine, executionOptionsForRun(options));
        if (!execution.hasValue())
        {
            failure = describeEngineFailure("create snapshot execution", execution.status());
            return std::string{};
        }
        auto restored = an::readImageSnapshot(*engine, *execution, path);
        if (!restored.hasValue())
        {
            failure = describeEngineFailure("read snapshot", restored.status());
            return std::string{};
        }
        if (!produceDigest)
            return std::string{};
        const auto bytes = restored->frame.bytes();
        out.correctnessMetric = bytes.size() >= fixture.bytes.size() &&
                                        std::memcmp(bytes.data(), fixture.bytes.data(), fixture.bytes.size()) == 0 &&
                                        restored->metadata == metadata && restored->identity == *identity
                                    ? 0.0
                                    : 1.0;
        return Sha256::of(bytes.data(), fixture.bytes.size());
    };
    const bool completed = finishScenario(scenario, options, out, skipReason, body, failure);
    out.counters["outputBytes"] = static_cast<double>(scenario.input.totalBytes());
    out.counters["enginePeakBytes"] = static_cast<double>(engine->memoryResource()->counters().peakBytes);
    return completed;
}

bool AfterNightEngineAdapter::runCodecLookup(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                             std::string& skipReason)
{
    if (!matchesCodecLookupScenario(scenario))
    {
        skipReason = "unsupported codec path fixture";
        return false;
    }
    out.fixtureDigest = Sha256::of(codecLookupFixtureText());
    out.threadCount = 1u;
    out.requestedBackend = BackendId::Unspecified;
    out.actualBackend = BackendId::Scalar;
    const std::function<std::string(bool)> body = [&](bool produceDigest) {
        std::array<std::uint8_t, kCodecLookupCases.size()> codes{};
        for (std::size_t i = 0u; i < codes.size(); ++i)
        {
            switch (an::codecFormatFromPath(kCodecLookupCases[i].path))
            {
            case an::CodecFormat::Fits:
                codes[i] = 1u;
                break;
            case an::CodecFormat::Xisf:
                codes[i] = 2u;
                break;
            case an::CodecFormat::Raw:
                codes[i] = 3u;
                break;
            case an::CodecFormat::Png:
                codes[i] = 4u;
                break;
            case an::CodecFormat::Jpeg:
                codes[i] = 5u;
                break;
            case an::CodecFormat::Tiff:
                codes[i] = 6u;
                break;
            case an::CodecFormat::Unknown:
            case an::CodecFormat::Dump:
                codes[i] = 0u;
                break;
            }
        }
        if (!produceDigest)
            return std::string{};
        out.correctnessMetric = 0.0;
        for (std::size_t i = 0u; i < codes.size(); ++i)
            if (codes[i] != kCodecLookupCases[i].expected)
                *out.correctnessMetric += 1.0;
        return Sha256::of(codes.data(), codes.size());
    };
    std::string failure;
    return finishScenario(scenario, options, out, skipReason, body, failure);
}

} // namespace an::bench::engine
