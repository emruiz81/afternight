// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineBench - the adapter that maps a scenario onto AfterNightEngine.
//
// This is the only place in the runner where an engine type appears. Everything else the runner
// does - arguments, manifests, the run loop, the result file - comes from the harness, which the
// benchmark layer's suite tests without an engine.
//
// Scenarios arrive here with the operations they measure. A scenario the engine cannot run is
// reported as skipped with that reason - a runner that cannot measure an operation must say so,
// because quietly measuring something else would publish a number for work nobody asked about.

#pragma once

#include "CorpusFixtures.h"
#include "RunnerHarness.h"

namespace an::bench::engine
{

/// Maps benchmark scenarios onto AfterNightEngine.
class AfterNightEngineAdapter final : public EngineAdapter
{
public:
    [[nodiscard]] std::string runnerVersion() const override;

    /// Records the compiler and configuration of the loaded AfterNightEngine binary, which the
    /// engine reports about itself. Asking the engine rather than the runner is deliberate: the
    /// runner's own build flags say nothing about the library whose behavior is being measured.
    void describeEngine(BuildManifest& manifest) const override;

    /// Attaches the corpus when the run supplies one; synthetic fixtures need nothing.
    bool prepare(const RunOptions& options, std::string& error) override;

    /// The scenarios this runner covers.
    [[nodiscard]] std::vector<ScenarioSpec> scenarios() const override;

    bool run(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
             std::string& skipReason) override;

private:
    /// Runs the reference integration pipeline over the corpus frame set the scenario names.
    ///
    /// Separate from `run` because it shares nothing with the rest: it measures one whole pipeline
    /// once rather than one operation repeatedly, its input is a list of paths rather than a
    /// fixture, and its result carries per-stage clocks and frame counts no other row has.
    bool runFrameSetScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                             std::string& skipReason);

    /// Runs a shared container save/decode scenario through the real transactional codec.
    bool runCodecRoundTrip(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);
    /// Writes an image's history snapshot and reads it back through the snapshot container.
    bool runSnapshotRoundTrip(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                              std::string& skipReason);
    /// Compares every generalized stretch family and the statistical stretch on a dense ramp.
    bool runStretch(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                    std::string& skipReason);
    /// Times the display presentation's complete composition per paint, against its definition.
    bool runPresentation(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                         std::string& skipReason);
    /// Times the complete autostretch Apply and its two stages, against the composition's definition.
    bool runAutostretch(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);
    /// Times a process run on the CIE lightness through the channel scope, against its definition.
    bool runChannelScope(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                         std::string& skipReason);
    /// Times the comparison view's difference of two colour frames, against its definition.
    bool runImageDifference(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);
    /// Times the viewer's tinted overlay of an eight-bit mask, against its definition.
    bool runMaskOverlay(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);
    /// Times a brush stroke stamped and blended into a paint plane, against its definition.
    bool runDabBlend(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                     std::string& skipReason);
    /// Times the LRGB combination of a colour frame with a luminance frame, against its definition.
    bool runLrgbCombination(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);
    /// Times all twenty-seven blend modes of one colour frame over another, against their definitions.
    bool runBlendModes(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                       std::string& skipReason);
    /// Times a star-mask recombination with tightened edges and a mask, against its definition.
    bool runStarRecombination(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                              std::string& skipReason);
    /// Times the published edge tightening of a colour frame, against its definition.
    bool runEdgeTighten(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);
    /// Times an external tool's hand-off and the restore of its answer, against their definition.
    bool runExternalHandoff(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);
    /// Times a comet stacked twice and put back together - every frame split, both layers stacked, the mask drawn
    /// and the comet stack blended in - against its definition.
    bool runCometComposite(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);
    /// Times a stack's two rejection maps read whole - the elements either reached and each map's clusters - against
    /// their definition.
    bool runRejectionMapStatistics(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                   std::string& skipReason);
    /// Measures format recognition over the complete shared filename vocabulary.
    bool runCodecLookup(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);
    /// Executes a complete ordered drizzle sequence, including mapping and output ownership.
    bool runDrizzleExecutionScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                     std::string& skipReason);
    /// Generates dense or bounded drizzle coordinates from engine-neutral geometry.
    bool runDrizzleAccumulationScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                        std::string& skipReason);
    bool runDrizzleGeometryScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                    std::string& skipReason);
    /// Runs the two-pass patch denoiser, measured by how much of the harness's own noise estimate
    /// survives it.
    bool runPatchDenoise(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                         std::string& skipReason);

    /// Runs one of the two deconvolutions over a frame the harness blurred by a kernel it built, so
    /// the unblurred frame is the exact answer the row is scored against.
    bool runDeconvolution(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs one of the two background removals over a frame the harness added a stated gradient to,
    /// so the row is scored on how much of that gradient survived rather than on the engine's
    /// own idea of what the background was.
    bool runBackground(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                       std::string& skipReason);

    /// Runs the dark-structure enhancement over a frame the harness cut a soft-edged well into, so
    /// the row is scored on how much more prominent that well became.
    bool runDarkStructure(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs the three performance anchors - first-touch allocation of fresh storage, generic
    /// per-pixel read and write, and a zero-copy view handed to OpenCV - which measure the costs of
    /// the storage design rather than any one algorithm.
    bool runPerformanceAnchor(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                              std::string& skipReason);

    /// Runs the shared three-stage scale/clamp chain through one fused pointwise program.
    bool runPointwise(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                      std::string& skipReason);

    /// Runs one chain twice over, once fused into a single image pass and once as one image pass per
    /// step, so the fused executor can be weighed against straightforward per-operation kernels.
    ///
    /// The two paths execute the *same* programs through the *same* compiled backend. What differs
    /// is only how many times the image is walked and whether an intermediate frame is
    /// materialised, which is exactly the question the pointwise intermediate form has to answer.
    ///
    /// @param scenario the scenario, whose `path` parameter selects fused or staged.
    /// @param options the run options.
    /// @param out where the timings, digests and counts go.
    /// @param skipReason why this scenario produced no result, when it did not.
    /// @return true when a result was produced.
    bool runFusionComparison(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                             std::string& skipReason);

    /// Runs full-frame or masked-region count, extrema and population moments through the public API.
    bool runReduction(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                      std::string& skipReason);

    /// Runs the bounded-scratch exact median and median absolute deviation.
    bool runOrderStatistics(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);

    /// Runs the complete statistics of a captured frame: the basic and advanced statistics, together.
    bool runCompleteStatistics(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                               std::string& skipReason);

    /// Runs the robust estimators: masked-region median and deviation under either reading
    /// strategy, and the clipped location and scale.
    bool runRobustStatistics(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                             std::string& skipReason);

    /// Runs the histogram rows: the full-scale integer count the engine builds, and the masked
    /// region whose cumulative counts a screen stretch reads percentiles out of.
    bool runHistogram(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                      std::string& skipReason);

    /// Runs the tiled separable Gaussian convolution.
    bool runSeparableConvolution(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                 std::string& skipReason);

    /// Runs the published Gaussian blur, exact or approximated, over a whole image.
    ///
    /// The operation rather than the kernel: it plans the blur, decides whether the image can be
    /// reached where it lies, and runs whichever of the two methods the scenario names. The
    /// approximation row measures what the fast Gaussian costs in accuracy.
    bool runGaussianBlur(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                         std::string& skipReason);

    /// Runs one demosaic estimate over the mosaic the scenario names.
    ///
    /// The correctness this reports is the re-mosaic residual, which is the property a demosaic can
    /// be held to without another implementation being the reference.
    bool runDemosaic(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                     std::string& skipReason);

    /// Runs the block split or the super-pixel, neither of which estimates anything.
    bool runCfaChannels(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);

    /// Runs a published uniform box average through its reusable plan.
    bool runBoxFilter(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                      std::string& skipReason);

    /// Runs the dense two-dimensional convolution over a whole image.
    bool runDenseConvolution(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                             std::string& skipReason);

    /// Runs the median filter through whichever of its two implementations the scenario names.
    ///
    /// The generated comparator network and the general bounded-scratch selection produce the same
    /// picture, so the pair of rows measures nothing but what each of them costs.
    bool runMedianFilter(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                         std::string& skipReason);

    /// Runs the median of a disc-shaped structuring element: the morphological selection's middle rank.
    bool runFootprintRank(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs a multiscale transform, and the reconstruction after it when the scenario asks for one.
    ///
    /// The round-trip form is the one worth timing: a decomposition on its
    /// own can be made arbitrarily cheap by producing the wrong layers, and adding them back is what
    /// says whether they were the right ones.
    bool runMultiscaleTransform(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                std::string& skipReason);

    /// Runs the defective-pixel detection over a frame the harness planted known defects in.
    bool runCosmeticDetection(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                              std::string& skipReason);

    /// Runs the defective-pixel repair over harness-stated maps, which is the per-light-frame half
    /// of the product workflow and the half whose speed a session pays hundreds of times.
    bool runCosmeticCorrection(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                               std::string& skipReason);

    /// Runs fixed-scale bias, dark and flat calibration through the reusable fused plan.
    bool runCalibration(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);

    /// Measures one frame, matches it to a measured reference and writes it in place after a copy.
    bool runGlobalNormalizationFrame(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                     std::string& skipReason);

    /// Measures one captured light, matches it to another measured before the clock and writes it in place
    /// after a copy.
    bool runGlobalNormalizationPair(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                    std::string& skipReason);

    /// Writes fixed normalization coefficients through one frame in place after a copy.
    bool runGlobalNormalizationApply(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                     std::string& skipReason);

    /// Fits one frame's background grid, estimates its scale against a fitted reference and writes
    /// it in place after a copy.
    bool runLocalNormalizationFrame(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                    std::string& skipReason);

    /// Writes fixed plane grids through one frame in place after a copy.
    bool runLocalNormalizationApply(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                    std::string& skipReason);

    /// Computes frame-level photometric weights from the shared compact measurement fixture.
    bool runPhotometricWeighting(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                 std::string& skipReason);

    /// Averages several resident frames through the streaming stack executor, an in-memory stack.
    bool runStackScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Measures every planted star of the shared three-plane field with fixed radii, as one batch.
    bool runAperturePhotometry(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                               std::string& skipReason);

    /// Fits red and blue channel gains from the shared colour samples.
    bool runColorFit(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                     std::string& skipReason);

    /// Integrates the shared table of linear spectra through the constant fixture response.
    bool runSyntheticPhotometry(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                std::string& skipReason);

    /// Fits the shared spectrophotometric colour samples with the repeated-median line.
    bool runSpectrophotometricFit(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                  std::string& skipReason);

    /// Neutralizes the sky of the planted field and writes the fixture gains.
    bool runSpectrophotometricApply(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                    std::string& skipReason);

    /// Runs one CFA extraction, palette-grid, layered-map or NBRGB composition scenario.
    bool runNarrowbandComposition(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                  std::string& skipReason);

    /// Reads the shared catalog text into an owned catalog, or searches one already built.
    bool runCatalogScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);

    /// Fills a full destination grid with the inverse of a forward-only distortion polynomial.
    bool runSipInverseMapScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                  std::string& skipReason);

    /// Solves the shared hinted synthetic star field against an already-built in-memory catalog.
    bool runPlateSolveScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                               std::string& skipReason);

    /// Fills a full destination grid with the source position of every canvas pixel, through a
    /// polynomial registration transform whose inverse is a Newton solve per pixel.
    bool runRegistrationMapScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                    std::string& skipReason);

    /// Detects the planted moving source in the shared star-aligned frame sequence.
    bool runCometDetectionScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                   std::string& skipReason);

    /// Resolves shared sky cones to the provider's standard HEALPix tile addresses.
    bool runHealpixQueryScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                 std::string& skipReason);

    /// Reads the shared deep-sky text into an owned catalog and the alias relation beside it.
    bool runDeepSkyCatalogScenario(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                                   std::string& skipReason);

    /// Detects and measures the stars of one frame, over a planted field or a real capture.
    bool runStarDetection(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs direct strong star reduction over the shared compatible target/starless fixture.
    bool runStarReduction(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Matches one source catalogue against a reusable prepared reference and fits its transform.
    bool runStarAlignment(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs the translation measurement between a frame and its cyclically shifted copy.
    bool runPhaseCorrelation(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                             std::string& skipReason);

    /// Runs the native bilateral filter, which is what replaces an imaging-library call.
    bool runBilateralFilter(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);

    /// Runs a curve program built with one of the three rules taken from the numerical library.
    bool runCurveInterpolation(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                               std::string& skipReason);

    /// Runs a total-variation denoise, which is what replaces an interpreter call.
    bool runTotalVariation(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);

    /// Runs the noise generator, which is what replaces the other interpreter call.
    bool runNoiseGeneration(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                            std::string& skipReason);

    /// Runs average-neutral chromatic-noise suppression over planar RGB.
    bool runChromaticNoise(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);

    /// Runs horizontal banding suppression with no highlight rejection.
    bool runBandingSuppression(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                               std::string& skipReason);

    /// Runs the impulse rejection, which replaces only the samples a threshold names.
    bool runImpulseRejection(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                             std::string& skipReason);

    /// Runs one shared odd-sized clamp-border interpolation scenario.
    bool runResize(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out, std::string& skipReason);

    /// Splits an odd-sized image into caller-owned crop results and joins them through checked
    /// destination subviews, measuring the explicit composition that replaces the old facade.
    bool runTileRoundTrip(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs the shared gamma transfer, which is where the vector transcendentals are measured.
    bool runGammaTransfer(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                          std::string& skipReason);

    /// Runs the shared colour transform, which is where the perceptual space and its compatibility
    /// packing are measured against the standard's own definitions.
    bool runColorTransform(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);

    /// Runs the mask family: an in-range selection into samples, and a packed mask built, combined
    /// and counted one bit per pixel.
    bool runMaskOperations(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);

    /// Runs one expression scenario: parses, resolves and compiles the script, then either lowers
    /// it into the fused pointwise program or runs the specialized evaluator, over a whole frame.
    ///
    /// The parse and the compile are inside the timed body on purpose: what a caller pays to run an
    /// expression once is the whole of it.
    bool runExpression(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                       std::string& skipReason);

    /// Runs one transfer/layout scenario: copy, fill, repack or byte swap over a whole frame.
    ///
    /// Separate from `run` because the family shares every step but the one call in the middle,
    /// and a single function that switched five ways inside its own fixture setup would make the
    /// one line that differs the hardest one to find.
    ///
    /// @param transfer which operation, from the shared table.
    /// @param scenario the scenario, whose declared input must match that table row.
    /// @param options the run options.
    /// @param out where the timings, digests and counts go.
    /// @param skipReason why this scenario produced no result, when it did not.
    /// @return true when a result was produced.
    bool runTransfer(const TransferBandwidthScenario& transfer, const ScenarioSpec& scenario, const RunOptions& options,
                     ResultRecord& out, std::string& skipReason);

    /// Measures complete source creation, four reads and teardown with explicit loading policy.
    bool runSequence(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                     std::string& skipReason);

    /// Measures eight FITS frames and their manifest published as one image-list sequence.
    bool runImageListWrite(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);

    /// Measures bounded FITS streaming with an explicitly simulated processing delay.
    bool runStreaming(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                      std::string& skipReason);

    /// Measures SER source creation, eight retained reads and teardown, or a sink writing the same eight
    /// frames through a published output.
    bool runSerSequence(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                        std::string& skipReason);

    /// Measures FFmpeg source creation - indexing included - eight retained FFV1 decodes and teardown.
    bool runFfmpegSequence(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                           std::string& skipReason);

    /// Measures an uncompressed AVI sink or an FFmpeg video sink writing eight mirrored grey frames through a
    /// published output.
    bool runVideoWrite(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                       std::string& skipReason);

    /// Synthetic generators plus, when the run supplies a manifest, the real-data corpus.
    CompositeFixtureSource m_fixtures;
};

} // namespace an::bench::engine
