# Overlap-Based Normalization

## What is it?

Overlap-based normalization matches brightness and contrast between images by
analysing only the regions where images overlap, rather than entire images.
It builds a weighted system of linear equations from pairwise overlap statistics
and solves it globally to produce optimal per-image normalization coefficients.

The result is delivered as the same per-frame, per-channel matches
(`an::FrameNormalization`) a standard global normalization produces, so the
normalization stage stores them in the group's coefficient document and the
stack applies them unchanged, through `stackNormalizationInputsFor`.

## When to use it

- **Mosaic imaging**: Multi-panel mosaics where panels cover different sky regions.
- **Large dithering**: When dither patterns cause significant non-overlap with
  the reference frame.
- **Variable backgrounds**: When images have different gradients or vignetting
  patterns that bias whole-image statistics.

## When NOT to use it

- **Small dithering with full overlap**: Standard normalization works fine and is
  faster.
- **Unregistered images**: Overlap normalization requires the lights to lie on
  the registration's output plane, which is what the registration's transforms
  give them.
- **Single-panel stacks with uniform fields**: No benefit over standard
  normalization.

## How it works

1. **Coverage** — Each light is read on the plane the normalization measures on:
   the registered lights while they are current, otherwise the registration
   inputs warped onto the registration's output plane in memory. The smallest
   rectangle holding the non-zero samples of each light's first plane is taken
   (`coverageOf`); an exact zero marks a sample the frame does not reach (see
   [NO_DATA_SAMPLE_CONTRACT.md](NO_DATA_SAMPLE_CONTRACT.md)).

2. **Candidate pairs** — Every pair of lights whose rectangles intersect is a
   candidate. A pair whose shared rectangle is below the minimum (see
   Parameters) is dropped.

3. **Pair selection and connectivity check** — `selectOverlapPairs` keeps, under
   the workflow's graph policy (`kWorkflowOverlapGraphPolicy`, the reference-star
   spanning tree), every candidate that touches the reference plus a maximum
   spanning tree over overlap area, and reports whether the kept pairs connect
   every light to the reference. If they do not, the system is under-determined
   and the attempt is refused.

4. **Overlap statistics** — For each kept pair, `measureOverlapStatistics`
   measures the shared rectangle of both lights over the positions both admit
   under their own validity, so an uncovered border in either takes no part, and
   computes location and scale per channel with the group's scale estimator.

5. **Linear system construction & solving** — `solveOverlapNormalization`
   accumulates weighted equations for each pair, each pair weighted by its shared
   sample count and the reference fixed at the identity, and solves the system
   `A·x = b` per channel by a dense Float64 elimination with partial pivoting.
   Separate systems are solved for additive offsets, multiplicative scales, and
   dispersion scales depending on the chosen normalization method. The plain
   offset and factor systems are required; the dispersion system and the
   dispersion-scaled offset system are subsidiary.

6. **Retry over every pair** — The first attempt refuses a degenerate subsidiary
   system. When it fails for any reason but a stop, the stage repeats steps 3 to
   5 over every candidate pair (`OverlapGraphPolicy::AllPairs`), where a
   subsidiary system that cannot be solved is replaced by unity dispersion and
   the plain additive offsets (`OverlapDegeneracyPolicy::SubstituteIdentity`),
   and each substitution is counted in the solve's report.

7. **Coefficient output** — The solver writes one `an::FrameNormalization` per
   light. The stage keeps them as the global matches of the group's coefficient
   document, and the stack applies them exactly like standard matches.

## Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| Minimum overlap pixels | 1000 | Pairs whose shared rectangle holds fewer pixels are skipped. |
| Minimum overlap fraction | 0.01 (1%) | Relative threshold: fraction of the smaller light's covered rectangle. The effective minimum is `max(min_overlap_pixels, fraction × min_area)`. |
| Scale estimator | IKSS | The group's scale estimator, shared with standard normalization: IKSS (the iterative k-sigma estimator, recommended) or MAD. |

## Interaction with other normalization modes

Overlap normalization is a **variant** of global normalization, not a new mode.
It is only active when:

- The normalization mode is set to **Global**. Local mode never uses it: it fits
  its background surfaces and then measures its own global match over the locally
  normalized light.
- The **overlap normalization toggle** is enabled in stacking settings (it is
  off by default).
- The group's lights lie on a registration plane. A group with no registration
  is normalized as its lights are read, where nothing overlaps; the stage logs
  "Overlap normalization needs registered lights; standard global normalization
  is used." and measures the standard match.

When overlap normalization is enabled but fails (insufficient overlaps,
disconnected graph, singular matrix), the workflow automatically falls back to
standard global normalization and logs a warning.

Overlap normalization does not affect local normalization.

## Performance considerations

- **Pair count**: N images produce up to N(N−1)/2 candidate pairs. Candidates are
  found by intersecting the coverage rectangles, and the spanning-tree policy
  then measures only the pairs touching the reference plus N−1 more; every
  candidate is measured only on the retry over every pair.
- **Parallelism**: The stage walks the pairs in order; each measurement and the
  solve run on the threads the stage's admitted operation was granted, which the
  parallel-cores setting narrows.
- **Memory**: The coverage pass reads each light once and keeps only its
  rectangle. The pair pass holds at most two lights at a time, reading a light
  again when a later pair needs it, and each measurement takes two packed
  validity masks the size of the pair's frames from the engine context for the
  call. The lights are released before the solver runs.
- **Registered outputs**: When the registered lights are current, the
  normalization stage measures them as they are; otherwise it warps the
  registration inputs onto the same plane in memory.

## Error handling and fallback

Overlap normalization errors are never fatal — they trigger an automatic
fallback to standard global normalization, with a warning beginning "Overlap
normalization fell back to standard global normalization".

| Scenario | Behaviour |
|----------|-----------|
| No registration data available (lights measured as read) | Warning logged; standard normalization used |
| No two lights overlap enough ("no two lights overlap enough") | Warning logged; standard normalization used |
| Disconnected overlap graph ("the overlapping lights do not connect") | Warning logged; standard normalization used |
| No selected pair shares a sample | Warning logged; standard normalization used |
| Singular required system (offsets or factors) | Warning logged; standard normalization used |
| Degenerate subsidiary system (dispersion) | Refused on the first attempt; substituted with the identity on the retry over every pair |
| Some pairs below overlap threshold | Those pairs skipped; remaining pairs used |
| Cancellation | The stop is returned as a cancellation and the stage ends cancelled; it never falls back |

## Source files

| File | Purpose |
|------|---------|
| `src/AfterNightEngine/include/AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h` | Public API: `measureOverlapStatistics`, `selectOverlapPairs`, `solveOverlapNormalization`, `OverlapGraphPolicy`, `OverlapDegeneracyPolicy` |
| `src/AfterNightEngine/src/algorithms/normalization/GlobalNormalization.cpp` | Implementation: shared-validity measurement, pair selection, linear system solver |
| `src/AfterNightProcesses/processes/core/WorkflowSettingsRecords.h` | `OverlapNormalizationSettings` in `StackingSettings` |
| `src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/LightsNormalizationStage.cpp` | Workflow integration: coverage rectangles, candidate pairs, `measureOverlap`, fallback |
| `src/AfterNightProcesses/processes/ImageIntegration/workflow/ManifestHashing.cpp` | Incremental invalidation fingerprinting (`addOverlapNormalizationSettings`, which includes the graph policy) |
| `src/AfterNightProcesses/processes/ImageIntegration/workflow/ManifestDiff.cpp` | Change detection for the overlap setting |
| `src/AfterNightTests/Test_EngineCalibration/GlobalNormalizationTest.cpp` | Engine-level unit tests |
| `src/AfterNightTests/Test_ImageIntegration/workflow/NormalizationStageTest.cpp` | Workflow-stage tests (`AnOverlapNormalizationReconcilesTheRegisteredLights`) |
| `src/AfterNightTests/Test_ImageIntegration/workflow/test_manifest.cpp` | Fingerprint tests |

## Cross-references

- Standard normalization: `measureGlobalNormalization`,
  `combineNormalizationReferences` and `estimateGlobalNormalization` in
  `GlobalNormalization.h`
- Local normalization: `fitLocalBackgroundGrid`, `estimateLocalScale` and
  `applyLocalNormalization` in `LocalNormalization.h`
- Coefficient persistence: the `normalization-coefficients` document and its
  schema 6 sidecar (`workflow/stages/NormalizationArtifactSupport.h`)
- Registration transforms: `Algorithms/Registration/RegistrationTransform.h`
  and `RegistrationPlan.h`
- SIP distortion: `an::SipDistortion` in `Algorithms/Astrometry/SipDistortion.h`
