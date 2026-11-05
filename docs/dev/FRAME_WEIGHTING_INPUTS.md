# Frame weighting inputs

The lights stacking stage turns per-frame registration metrics into stacking
weights. `weightsFor` (`workflow/stages/LightsStackingStage.cpp`) states each
light's persisted record as an `an::StackFrameMeasurement`, and the engine's
`computeStackFrameWeights` derives the weights from those figures. Every mode
reads a different metric, and a mode whose metric was never persisted
**degrades to an unweighted stack** - which the stage says in a workflow warning
rather than leaving the settings line to claim a weighting that did not happen
(see "Failing loudly"). This note records what each mode needs and where it comes
from.

| Mode (stored key) | Engine source | Read through `StackFrameMeasurement` | Persisted in |
| --- | --- | --- | --- |
| `star-count` | `StarCount` | `starCount` (`stars`); `medianRoundness` (`1 - eccentricity`) for the roundness-weighted scaling | `registration_metrics.stars`, `.eccentricity` |
| `weighted-fwhm` | `Fwhm` | `fullWidth` (`fwhm`) and `starCount` | `registration_metrics.fwhm`, `.stars` |
| `noise` | `Noise` | `backgroundNoise` and `dispersion` (`global_scale`, else `global_mad`) | `registration_metrics.background_noise`, `.global_scale`, `.global_mad` |
| `photometric` | `Photometric` | `photometric` (zero point, `zero_point_scatter` as its uncertainty, `astrometric_rms_pixels`) through `toPhotometricFrameMeasurement`; needs plate solving | the matching `registration_metrics` columns |
| `image-count` | `ImageCount` | nothing: every light counts as one (`integratedFrameCount = 1`) | - |

## Why noise weighting matters

FWHM and star count are *sharpness* metrics. Neither responds to transparency, and FWHM actively
inverts under it: a hazy frame's stars are faint, so the fitted profile is **narrower** and the frame
scores as one of the sharpest in the session. A weighted FWHM compensates with a star-count penalty
(`fwhm + 2 * fwhm * (ref_stars - stars) / stars`), but that term vanishes when every frame saturates
the detector's star cap - which is what happens in a rich field.

Background noise is the metric that does respond to transparency, so `Noise` is the mode that can
separate a hazy sub from a good one. `w = 1 / (scale^2 * noise^2)`, where
`scale = scale_reference / scale_frame` is the normalization scale ratio. Note what this means: the
weight reflects the noise a frame contributes *after* normalization, so a frame that is merely
darker (lower sky, proportionally lower noise) ends up weighted the same, while a frame that is
genuinely noisier for its signal is down-weighted. The engine's rule is the same: one over the square of the
noise,
multiplied by the reference frame's dispersion over the frame's own where both are measured, and by
one otherwise.

## Where the values live

`FrameNoiseStatistics` (`model/FrameQuality.h`) carries the three values. It is a **separate struct
from `FrameQualityMetrics`** because the two have different audiences - these values are read only
by the persistence and stacking-weight paths, while `FrameQualityMetrics` is what the registration
UI displays, sorts and scores. That is the only reason for the split.

Adding fields to either struct is an ordinary change. `FrameQuality.h` pins `FrameQualityMetrics`'s
size and alignment with a `static_assert` and a `#pragma detect_mismatch` tag, so an object compiled
against an older layout fails the MSVC link (`LNK2038`) instead of reading fields at the wrong
offsets; [INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md](INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md)
describes the stale-object failure that once made growing it look like a memory bug.

The values are attached to `RegistrationMetricRecord::noise` and `RegistrationStarCacheEntry::noise`
(`processes/core/RegistrationMetricsPersistenceTypes.h`), and the stacking stage reads the record
whole: its metrics and its noise statistics together.

## Where the values come from

Star detection measures all three: the engine's star-field summary reports the background noise, the
median absolute deviation and the iterative k-sigma scale of the channel it detected in, and
`detectRegistrationStars` (`processes/core/RegistrationRun.cpp`) copies them into the frame's
`FrameNoiseStatistics`. `LightsRegistrationStage` persists them. Two details are easy to get wrong:

- **The star cache must carry them.** `star_detection_cache.stars_blob` holds per-star geometry only,
  so a cache hit that skips detection has to restore the noise statistics from the cache row
  (`starCacheEntryOf` writes them, `registeredFrameFromCache` reads them back). Otherwise
  re-registering with changed *registration* settings (detection hash unchanged) rewrites the metric
  rows with zeros.
- **The stacking stage must map them back.** `weightsFor` loads the group's `RegistrationMetricRecord`s
  and copies each figure into the light's `StackFrameMeasurement`; anything it does not copy is
  invisible to the engine.

The IKSS scale is legitimately zero on a frame with no background dispersion (a synthetic flat
field, for example), so `weightsFor` takes the scale and falls back to the MAD when it is zero, and
leaves the dispersion unstated when both are. The background noise is the figure a light must carry
for the noise mode to weigh the group: `weightsFor` stacks the group unweighted when any light lacks
it. (`FrameNoiseStatistics::isUsableForWeighting()` states the older, stricter requirement - noise and
a scale - and nothing calls it.)

## Failing loudly

`weightsFor` falls back to uniform weights with a reason when it cannot weight - the metrics database
is missing or unreadable, a frame has no record, or a light lacks the figure the mode reads - and
`LightsStackingStage` logs it as a workflow warning ("... weighting is unavailable for <group>
(<reason>); stacking this group unweighted."). The engine adds its own statement of what it did: a
weighting that gave every frame the same weight is warned about with the engine's disposition
(`StackFrameWeightingDisposition`). Keep that contract: an unweighted master that advertises a
weighting mode is indistinguishable from a weighted one in the report.

The weight preview of `FrameQualityDashboard` asks the same engine source for the two modes whose
figures it reads from the registration records - `Noise` and `Photometric`, through
`previewStackWeights` - and shows uniform weights with a banner where the stack would stack the set
unweighted. The star-count and weighted-FWHM previews still compute their own version of the rule in
the dashboard (`FrameQualityDashboardMetrics.cpp`), which is not the engine's: its roundness-weighted
scaling, for one, is the quadratic scaling without the roundness.

## Schema

The three columns live in both `registration_metrics` and `star_detection_cache`.

## Regression coverage

- `StackFrameWeightingTest.NoiseWeightsAreTheInverseSquaredNoiseScaledToTheReferenceDispersion` and
  `APartiallyMeasuredSetIsRefusedByFrameAndAnUnmeasuredSetIsUniformWithAWarning`
  (`Test_EngineStacking`) - the engine formula, the scale ratio and the refusal of a partly measured set.
- `TempDirFixture.RegistrationMetricRoundTripPreservesFrameNoiseStatistics`,
  `RegistrationStarCacheRoundTripPreservesFrameNoiseStatistics`,
  `RegistrationMetricsSettingsVersionCopyCarriesFrameNoiseStatistics`,
  `RestoredRegisteredDataCarriesFrameNoiseStatisticsFromBothSources` and
  `RegistrationMetricsFormatTest.ADatabaseWhoseTablesAreNotItsFormatsIsRefusedAndLeftByteForByte`
  (`Test_ImageIntegration`) - persistence, and a database whose tables lack the format's columns refused.
- `LightsRegistrationTest.AWideRunRecordsWhatOneLightAtATimeRecords` (`Test_ImageIntegration`) - the
  registration stage records the noise statistics it measured, the same at any width.
- `LightsStackingTest.WeightsAreReadFromTheRegistrationsMetrics` and
  `AWeightingWithoutItsMetricsStacksUnweightedAndSaysSo` (`Test_ImageIntegration`) - the consumer and
  the diagnostic, over the star-count mode.
- `FrameQualityDashboardTest.WeightingPreview_NoiseWeighsByTheBackgroundNoiseTheStackReads`
  (`Test_ImageIntegration`) - the noise preview.

No case in `Test_ImageIntegration` stacks a group with noise weights applied, or checks that a
star-cache hit restores the noise statistics;
`LightsRegistrationTest.AWideRunRecordsWhatOneLightAtATimeRecords` checks that the registration stage
persists them.
