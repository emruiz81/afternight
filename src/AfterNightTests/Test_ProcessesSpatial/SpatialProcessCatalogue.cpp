// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The nineteen spatial and noise processes as the cases see them, and the direct run of a window's body.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "processes/BandingReductionProcess.h"
#include "processes/BilateralFilterProcess.h"
#include "processes/CLAHEProcess.h"
#include "processes/CosmeticCorrectionProcess.h"
#include "processes/GRAYCstorationProcess.h"
#include "processes/GaussianBlurProcess.h"
#include "processes/GmicProcessSupport.h"
#include "processes/HighPassFilterProcess.h"
#include "processes/LocalContrastEnhancementProcess.h"
#include "processes/LocalHistogramEqualizationProcess.h"
#include "processes/MedianFilterProcess.h"
#include "processes/MorphologicalTransformationProcess.h"
#include "processes/NLBayesProcess.h"
#include "processes/NoiseGeneratorProcess.h"
#include "processes/PatchPCADenoiseProcess.h"
#include "processes/SharpenAlphaProcess.h"
#include "processes/TightenEdgesProcess.h"
#include "processes/TotalVariationDenoiseProcess.h"
#include "processes/UnsharpMaskProcess.h"
#include "processes/WaveletDenoiseProcess.h"

#include "session/ExecutionScope.h"

namespace an::tests::processes::spatial
{

namespace
{

template <typename ProcessT>
[[nodiscard]] SpatialProcessSpec spec(std::string action, std::string className,
                                      std::function<void(QWidget&)> configure, bool gmic = false,
                                      std::string nonlinearOperation = {})
{
    return SpatialProcessSpec{.action = std::move(action),
                              .className = std::move(className),
                              .name = ProcessT::getProcessNameStatic(),
                              .exclusive = ProcessT::requiresExclusiveAccessStatic(),
                              .open = [](ImageViewControl& target) { return openProbed<ProcessT>(target); },
                              .configure = std::move(configure),
                              .gmic = gmic,
                              .nonlinearOperation = std::move(nonlinearOperation),
                              .nonFinite = gmic ? NonFiniteReading::Refused : NonFiniteReading::Computed};
}

/// A spec of a process that ranks its neighbours, reading a non-finite sample as one that means nothing.
[[nodiscard]] SpatialProcessSpec ranking(SpatialProcessSpec spec)
{
    spec.nonFinite = NonFiniteReading::Excluded;
    return spec;
}

} // namespace

const std::vector<SpatialProcessSpec>& spatialProcesses()
{
    static const std::vector<SpatialProcessSpec> specs{
        spec<GaussianBlurProcess>("actionGaussian_Blur", "GaussianBlurProcess",
                                  [](QWidget& window) { setReal(window, "sigmaValue", 2.0); }),
        ranking(
            spec<MedianFilterProcess>("actionMedian_Filter", "MedianFilterProcess",
                                      [](QWidget& window) { choose(window, "kernelSize", QStringLiteral("5x5")); })),
        spec<BilateralFilterProcess>("actionBilateral_Filter", "BilateralFilterProcess",
                                     [](QWidget& window) {
                                         setReal(window, "diameterValue", 7.0);
                                         setReal(window, "sigmaColorValue", 0.1);
                                     }),
        ranking(spec<BandingReductionProcess>("actionBanding_Reduction_Filter", "BandingReductionProcess",
                                              [](QWidget& window) { setReal(window, "amountValue", 2.0); })),
        spec<NLBayesProcess>("actionNon_Local_Bayes_Filter", "NLBayesProcess",
                             [](QWidget& window) {
                                 setChecked(window, "autoSigma", false);
                                 setReal(window, "sigmaValue", 12.0);
                             }),
        spec<PatchPCADenoiseProcess>(
            "actionPatch_PCA_Filter", "PatchPCADenoiseProcess",
            [](QWidget& window) { setReal(window, "strenghtValue", 8.0); }, true),
        spec<WaveletDenoiseProcess>(
            "actionWavelet_Denoise_Filter", "WaveletDenoiseProcess",
            [](QWidget& window) { setReal(window, "thresholdValue", 3.0); }, true),
        spec<TotalVariationDenoiseProcess>("actionTotal_Variation_Denoise_Filter", "TotalVariationDenoiseProcess",
                                           [](QWidget& window) { setReal(window, "weightValue", 0.3); }),
        spec<UnsharpMaskProcess>("actionUnsharp_Mask", "UnsharpMaskProcess",
                                 [](QWidget& window) { setReal(window, "amountValue", 2.0); }),
        spec<NoiseGeneratorProcess>(
            "actionNoise_Generator", "NoiseGeneratorProcess",
            [](QWidget& window) { choose(window, "noiseTypeSelector", QStringLiteral("Salt")); }),
        spec<GRAYCstorationProcess>(
            "actionGRAYCstoration_Filter", "GRAYCstorationProcess",
            [](QWidget& window) { setWhole(window, "amplitudeValue", 90); }, true),
        spec<HighPassFilterProcess>("actionHigh_Pass", "HighPassFilterProcess",
                                    [](QWidget& window) { setReal(window, "radiusValue", 3.0); }),
        spec<SharpenAlphaProcess>(
            "actionMultiscale_Alpha_Sharpen", "SharpenAlphaProcess",
            [](QWidget& window) { setReal(window, "amplitudeValue", 2.5); }, true),
        spec<TightenEdgesProcess>("actionTighten_Edges", "TightenEdgesProcess",
                                  [](QWidget& window) { setReal(window, "valueValue", 1.0); }),
        spec<LocalContrastEnhancementProcess>(
            "actionLocal_Contrast_Enhancement", "LocalContrastEnhancementProcess",
            [](QWidget& window) { setReal(window, "weightValue", 1.5); }, true),
        spec<LocalHistogramEqualizationProcess>(
            "actionLocal_Histogram_Equalization", "LocalHistogramEqualizationProcess",
            [](QWidget& window) { setReal(window, "strengthValue", 40.0); }, true, "local-histogram-equalization"),
        spec<CLAHEProcess>(
            "actionCLA_Histogram_Equalization", "CLAHEProcess",
            [](QWidget& window) { setReal(window, "contrastLimitValue", 4.0); }, false, "clahe"),
        ranking(spec<MorphologicalTransformationProcess>(
            "actionMorphological_Transformation", "MorphologicalTransformationProcess",
            [](QWidget& window) { choose(window, "operatorSelector", QStringLiteral("Dilation (Maximum)")); })),
        ranking(
            spec<CosmeticCorrectionProcess>("actionCosmetic_Correction_Filter", "CosmeticCorrectionProcess",
                                            [](QWidget& window) { setReal(window, "hotPixelsThresholdValue", 0.05); })),
    };
    return specs;
}

const SpatialProcessSpec* spatialProcess(std::string_view action)
{
    for (const SpatialProcessSpec& candidate : spatialProcesses())
    {
        if (candidate.action == action)
        {
            return &candidate;
        }
    }
    ADD_FAILURE() << "no spatial process for " << action;
    return nullptr;
}

bool runsHere(const SpatialProcessSpec& spec)
{
    return !spec.gmic || process_utils::gmicAvailable();
}

an::Result<an::consumer::ImageAsset> runProbeBody(ProbeAccess& probe, const an::consumer::EngineSession& session,
                                                  const an::ui::ImageDocument& document, an::ui::ProcessRun run,
                                                  bool stopFirst)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(session, scope->engine(), scope->execution(), source);
    if (!destination.hasValue())
    {
        scope->finish(destination.status());
        return destination.status();
    }
    if (stopFirst)
    {
        scope->requestStop();
    }
    const an::Status ran = probe.runBody(source, *destination, run, *scope);
    scope->finish(ran);
    if (ran.failed())
    {
        return ran;
    }
    return destination->release();
}

} // namespace an::tests::processes::spatial
