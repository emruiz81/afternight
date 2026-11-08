# The no-data sample contract (exact zero)

Registration renders every light onto one shared canvas, so a stacked pixel is normally covered by
only a subset of the frames. AfterNight marks the uncovered part of a frame with an **exact `0.0`**
sample, and what every operation does with that marker is stated, never inferred: each frame carries
a `ValidityDescriptor` (`Image/ValidityDescriptor.h`), and the integration workflow states
`ValidityRule::ZeroIsNoData` for a frame on the registration's plane - a registered light, a light
warped onto that plane as the stack reads it, a comet-aligned light - and `ValidityRule::FiniteOnly`
for a light as it was read, where an exact zero is an ordinary sample (a genuinely black pixel is
data) and only a non-finite one, which calibration writes, means nothing. The engine reads that
statement in every operation that touches the samples:

| Consumer | Where |
| --- | --- |
| Rejection estimators leave the sample out before clipping; a missing sample is not a rejection event | `combineRejectedStackSampleHistory` reads each frame's own rule |
| Coverage / variance / weight-sum maps and the quality report describe what contributed | a streaming stack counts each admitted sample (`accumulateStackFrame` reports those it kept out as `samplesExcludedByValidity`); a sample-history stack's maps take the length of the admitted column |
| Weighted and unweighted mean, sum and extrema skip it; the unweighted mean divides by the covered count, not the frame count | `accumulateStackFrame`, `finalizeStackAccumulators` |
| Median and Auto-Adaptive combine only the admitted samples | `combineStackSampleHistory` |
| Warping onto the registration's plane fills out-of-footprint canvas with it | `stage_utils::warpRegistrationFrame` |
| Global and local normalization exclude it from their statistics and models and copy it unchanged through the apply | `measureGlobalNormalization`, `applyGlobalNormalization`, `fitLocalBackgroundGrid`, `applyLocalNormalization` |

There is no single "is this sample missing" function to keep every consumer in step: the question is
the frame's `ValidityDescriptor` - and its mask, where the rule names one - and each operation takes
it as an argument. `sampleIsValidByValue` is the per-sample statement of its three value rules.

## The rule

**Never turn a no-data zero into a measurement, and never count one as sky.**

Concretely, any code that transforms or measures frame samples has to decide what it does with an
exact zero:

- **Transforms** (the global match, the local surface) must leave it at `0.0`. An additive
  coefficient applied to the border turns it into an ordinary - and very low - sample. It then
  survives rejection (a coherent block of low values does not look like an outlier to linear-fit
  clipping) and drags down every canvas pixel that is not covered by all frames, printing the frame
  footprints into the master as straight-edged brightness steps. `applyGlobalNormalization` and
  `applyLocalNormalization` transform the samples the frame's rule admits and copy the rest
  unchanged, and the stack's source path (`convertStackSourceBand`) applies the same matches to the
  bands it reads the same way. A transformed sample that lands exactly on zero under `ZeroIsNoData`
  would become no-data downstream, so it is moved to the smallest normal magnitude with the sign of
  the scaled sample and counted (`noDataCollisions`).
- **Statistics that feed those transforms** must exclude it. The uncovered fraction differs from
  frame to frame, so counting the border as sky biases each frame's location and scale by a
  different amount and the derived coefficients stop agreeing with each other.
  `measureGlobalNormalization` ranks a frame under the validity its view states, and the workflow
  builds every view with `validityRuleFor(plane)` (`LightsNormalizationStage.cpp`): `ZeroIsNoData` on
  a registered or warped plane, `FiniteOnly` for lights measured as read. The normalization stage and a
  stack that measures its own matches call the same function, so the two routes cannot measure the
  same frames differently.
- **Overlap normalization follows the same rule.** `measureOverlapStatistics` takes, per
  channel, only the positions both frames admit under their own validity, so there is no separate
  statistics spelling for it. What the stage hands it is the rectangle each light covers - the
  smallest holding the non-zero samples of its first plane (`coverageOf`) - which only finds the
  overlaps.
- **Accumulators** must skip it rather than normalize it. Every combination reads only the samples
  the frame's rule admits - the mean, the sum and the extrema through `accumulateStackFrame`, the
  median and Auto-Adaptive through `combineStackSampleHistory`, every rejection family through
  `combineRejectedStackSampleHistory` - so the no-rejection median strips it exactly as the rejection
  algorithms do: the frame's rule applies to every method, with and without rejection.
- **A pixel no frame measured** has nothing to combine and reads the marker itself: the engine
  writes it as an exact `0.0` with its validity bit cleared and every map entry zero, instead of
  asking for the median or the weighted mean of an empty sample set. The integration builds the
  master from the stack's samples and description (`stackedMaster`) and does not write
  `StackResult::mask` beside it, so on disk that pixel reads exact `0.0`.

## Non-finite samples

Exact zero is not the only marker. Calibration writes a non-finite value wherever the flat frame was
zero and states `ValidityRule::FiniteOnly` for the result
(`ImageCalibrationTest.AZeroFlatProducesNanAndTheTransitionStatesFiniteValidity`), and a non-finite
sample is **more** destructive than a zero, because it does not merely bias a pixel, it destroys it:
`dst += NaN` stays NaN and `NaN / count` stays NaN, so one dead flat pixel would turn the
corresponding master pixel into NaN for the whole stack.

**A non-finite sample is left out only where the frame says so**, through `FiniteOnly` or a mask.
Under `AllValid` or `ZeroIsNoData` an admitted sample that is not finite is refused with
`ErrorCode::InvalidData` - by the source path, by the accumulators, by the rejection estimators and
by the overlap measurement - and the stack fails with no partial result, because a frame that
claimed something its bytes did not keep would otherwise print the claim into every later pixel. The stack's
own source path states
`FiniteOnly` for a band it calibrates (`StackSourceTest.RefusesAnAdmittedNonFiniteSample`,
`StackAccumulatorsTest.AppliesEveryValidityRuleAndRefusesAnUndeclaredNonFiniteSample`).

The workflow says so on every route whose frames can hold one - a light calibrated in memory, a
calibrated light read from disk, and a registered or comet-aligned light warped from either. A light
as it was read is stated `FiniteOnly`. A frame on the registration's plane needs both claims at once -
an exact zero and a non-finite sample mean nothing - and a frame states one rule, so the workflow's
stack source restates every non-finite sample as the exact-zero marker before the band reaches the
stack under `ZeroIsNoData` (`zeroNonFiniteSamples`, `StackFrameSources.cpp`). A packed mask beside
each band would say the same at the cost of a mask per frame that every kernel then reads; the
marker the rule already reads costs one pass over the band. The normalization stage reads its frames
the same way (`FrameReader`, `LightsNormalizationStage.cpp`), so the two still measure the same
samples, and a drizzle excludes non-finite samples by policy (`DrizzleNonFinitePolicy::Exclude`,
`LightsStackingStage.cpp`), so every route leaves a non-finite sample out. The
stars-plus-comet composite reads each registered and comet-aligned image through the explicit mask
its split takes, and leaves a pixel holding a non-finite sample in any plane out of that mask with
the pixels the light does not reach (`readCovered`, `StackCometProducts.cpp`): the split admits or
refuses a whole pixel, so such a sample restated as a zero beside planes that hold numbers would be
split into both layers as data.

The registration reaches the same lights before any of them, and its star detection reads every
light as `FiniteOnly` (`starFrameOf`, `RegistrationRun.cpp`): the detector measures the sky without
a non-finite sample and searches the sky in its place. Read as all valid, a light calibrated by a
flat holding a zero would be refused while its sky is measured and left unregistered. The
alignment's warp still reads its source as all valid, so the sample spreads over the
interpolation's footprint in a registered light; every reader of the plane above leaves
that footprint out with the exact-zero marker, which is what stating `FiniteOnly` to the warp would
leave unwritten there.

Standalone Image Stacking reads every frame under `ZeroIsNoData` with each non-finite floating sample
restated as zero (`FileFrameSource`, `StackingBatch.cpp`), so both an exact zero and a non-finite
sample are left out of every frame's column, rejection and combination alike: the zero
border an alignment leaves where a frame does not reach is neither averaged nor counted as rejected.

**Negative samples are data, not missing samples.** Siril-style unclamped calibration and
normalization legitimately produce sub-zero background noise, and dropping it biases the background
upward. No frame rule asks about the sign, and the accumulators keep every extremum as it is rather
than clamping it to the unit interval.

## A warped plane: both routes write the same samples

A registered light and a light warped as the stack reads it come out of one function,
`stage_utils::warpRegistrationFrame` (`RegistrationFrames.cpp`): the destination starts zeroed and
a tap outside the source reads zero (`KernelBorderMode::Constant`, `constantBorder` 0), so what the
frame does not reach is exact zero on both routes. The alignment stage publishes the warped image as
a Float32 file (`publishWorkflowImage`) and the stack reads it back under `ZeroIsNoData`; the
transform-on-read route hands the same image to the stack in memory under the same rule.

Neither route clamps the result - nothing in `RegistrationFrames.cpp`, `LightsAlignmentStage.cpp` or
`StackFrameSources.cpp` does, and the Float32 file keeps what it was given - so the two cannot
disagree about an out-of-range sample: a sub-zero sample is a measurement on both, and an exact
zero the resampling produced is no-data on both.

## Local normalization: the rule is the frame's validity

An exact zero is only a marker where registration put it, and local normalization is the one
subsystem that works in both places, so it reads the frame's stated validity exactly as the global
match does:

- **`ZeroIsNoData`** - registered frames and anything warped onto the plane. This is where the
  coefficients are defined whenever frames do not fully cover the canvas: dither borders in standard
  mode, large regions in union/mosaic mode. Both halves of the rule apply. The **analysis**
  (`sampleLocalBackgroundNode`, `fitLocalBackgroundGrid`, `estimateLocalScale`, `fitLocalScaleGrid`)
  excludes the marker for the same reason global statistics do: the uncovered fraction differs from
  frame to frame, so counting it as sky biases each frame's model by a different amount. The **apply**
  step (`applyLocalNormalization`, and the stack's source path) writes it back untouched.
- **`FiniteOnly`** - lights measured as read. Nothing resampled those pixels onto a canvas, so there is
  no marker: an exact zero is an ordinary measurement and is transformed like any other sample, and
  only the non-finite sample a calibration left is excluded.

Non-finite is excluded from a node's samples in both cases, because a non-finite value never lies in
the clip window. The apply does not clamp - a downward overshoot stays a negative measurement, which
every combination keeps as data - and only an exact zero is moved off the marker, as above.

The fitted document records the rule its grids were fitted under (`LocalNormalizationDocument::validity`,
written as `validity=`), and the stack's source path refuses a frame whose rule is not the document's.
A drizzled group's coefficients are measured on its canvas's analysis plane
(`NormalizationMeasurementPlane::DrizzleAnalysisPlane`, a warped plane where an exact zero is where no
light reaches), while its drizzled lights mark where no drop reached with a quiet NaN under the
finite-only rule. A Local group's document is applied to them through a plane mapping
(`StackPlaneMapping`, `Algorithms/Stacking/StackSource.h`): the document keeps the zero rule of the
plane it was measured on, the mapping states the frames' finite-only rule, and each band is checked
against the mapping's.

### Changing the rule invalidates coefficients

What counts as a valid sample is part of what a coefficient means, and it is not derivable from any
user setting, so the workflow carries it in three records. The normalization document records the plane
its coefficients were measured on (`Frames`, `RegisteredCanvas` or `DrizzleAnalysisPlane`), and
`stackNormalizationInputsFor` refuses coefficients measured on another plane. The local document states
its rule, as above. And every derived family's fingerprint carries the engine generation and a revision
of the family's own meaning (`ArtifactFamily`, `artifactFamilySemanticRevision`,
`workflow/ArtifactSchema.h`): a stage that changes what a family computes moves its revision, and the
planner and the normalization stage ask `statesCurrentArtifactFamily` before they reuse a sidecar, so an
artifact recorded under the previous rule is measured again rather than reused.

## Regression coverage

- `StackExecutorTest.HonoursEachFrameValidityRule`,
  `StackAccumulatorsTest.AppliesEveryValidityRuleAndRefusesAnUndeclaredNonFiniteSample`,
  `StackMethodsTest.AppliesEveryValidityRuleToTheColumns` and
  `StackRejectionTest.ValidityAndDegenerateColumnsAreExplicitForEveryFamily` (`Test_EngineStacking`) -
  every combination reads the frame's rule and refuses an undeclared non-finite sample.
- `StackSourceTest.CarriesEveryValidityRuleIntoTheTile`, `RefusesAnAdmittedNonFiniteSample` and
  `AppliesTheGlobalMatchExactlyAsApplyGlobalNormalization` (`Test_EngineStacking`) - the source path
  carries the rule into the tile and applies the match as the apply does.
- `GlobalNormalizationTest.MeasurementExcludesTheStatedNoDataAndReportsBothEstimators`,
  `ApplyTransformsAdmittedSamplesAndCopiesTheRest` (the marker survives, a collision is moved and
  counted) and `OverlapMeasurementUsesOnlySamplesValidInBothFrames` (`Test_EngineCalibration`) - the
  statistics and the transform leave the marker alone.
- `LocalNormalizationTest.SamplingVisitsOnlyAdmittedSamplesInAStableOrder`,
  `FitRecoversUncoveredNodesAndCountsEveryFallback` and
  `ApplyPreservesExcludedSamplesAndRefusesWhatItCannotWrite` (`Test_EngineCalibration`) - local
  normalization, analysis and apply.
- `ImageCalibrationTest.AZeroFlatProducesNanAndTheTransitionStatesFiniteValidity`
  (`Test_EngineCalibration`) - the non-finite marker calibration leaves.
- `LightsNormalizationTest.CurrentRegisteredLightsAreMeasuredOnTheRegistrationsPlane` and
  `WithoutCurrentRegisteredLightsTheInputsAreWarpedOntoThePlaneInMemory` (`Test_ImageIntegration`) -
  the plane a group's coefficients are measured on.
- `LightsStackingTest.ALightCalibratedInMemoryIsLeftOutWhereItsFlatIsZero`,
  `ALightCalibratedInMemoryAndWarpedAsItIsReadIsLeftOutWhereItsFlatIsZero` and
  `ARegisteredLightCalibratedInMemoryIsLeftOutWhereItsFlatIsZero`, and
  `LightsNormalizationTest.ALightCalibratedInMemoryIsMeasuredWithoutTheSampleItsFlatLeftNotANumber` and
  `ALightCalibratedInMemoryIsMeasuredOnTheRegistrationsPlaneWithoutItsNotANumber` (`Test_ImageIntegration`) -
  the non-finite sample a zero flat leaves is left out of the stack and the measurement on every route.
- `CometStackingTest.ALightCalibratedInMemoryIsLeftOutOfTheCompositeWhereItsFlatIsZero`
  (`Test_ImageIntegration`) - the stars-plus-comet composite's split leaves that sample's pixel out.
- `LightsRegistrationTest.ALightCalibratedInMemoryIsRegisteredWhereItsFlatIsZero` and
  `LightsCalibratedInMemoryByAFlatHoldingAZeroAreSearchedAsCalibrated` (`Test_ImageIntegration`) - the
  registration's star detection leaves that sample out, and every light is registered as calibrated.
- `StackingTest.AFramesZeroBorderIsLeftOutOfTheAverage`, `AFramesZeroBorderIsNotJudgedByTheRejection` and
  `ASampleThatIsNotANumberIsLeftOutOfTheAverage` (`Test_ProcessesWorkflowTools`) - Image Stacking leaves
  an exact zero and a non-finite sample out of every frame.

No case in `Test_ImageIntegration` stacks frames of unequal coverage and reads a band only some of them
reach; `Test_ProcessesWorkflowTools` does, for Image Stacking. The engine's validity rules themselves
are described in `docs/dev/guides/NUMERIC_VALIDITY_METADATA.md`.
