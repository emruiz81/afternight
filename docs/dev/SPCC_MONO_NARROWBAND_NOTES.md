# SPCC Mono And Narrowband Developer Notes

Developer notes for the current Spectrophotometric Color Calibration support boundary and future mono/narrowband work.

The current SPCC implementation is a broadband RGB/OSC calibration path. It fits `R/G` and `B/G` gains from Gaia XP
sampled spectra, response-profile RGB throughput curves, measured RGB image-star fluxes, and a white-reference
normalization. That contract does not apply to monochrome or narrowband data without a different scientific model.

## Current V1 Behavior

- Engine execution requires RGB source and destination images.
- The readiness panel refuses unknown or nonlinear state, an incompatible channel basis, an incomplete attestation and
  an already calibrated image through the engine's `spectrophotometricInputProblem` (`IO/ProcessingStateCards.h`); the
  engine's apply takes only three-channel linear floating RGB and reads no metadata.
- The SPCC process readiness panel reports narrowband target metadata as outside the current support envelope, even when
  the user manually selects a response profile.
- The user-facing image-state assignment action can mark imported data as linear RGB, but it does not convert mono or
  narrowband data into supported SPCC input.

The expected fallback is to use PCC or a dedicated workflow that matches the data type. Do not silently reinterpret mono
or narrowband frames as broadband RGB SPCC inputs.

## Where the refusals are made

The narrowband refusal is made twice. The product's target checks in
`src/AfterNightProcesses/processes/PhotometricCalibrationSupport.h` refuse a filter the engine names narrowband, an
image that is not three RGB channels, one with no plate solution and one declared nonlinear, before the run reads
any catalog; the readiness panel says the same. The engine's `planSpectrophotometricColorCalibration` refuses a
narrowband filter name - `isNarrowbandFilterName` - with `ErrorCode::Unsupported` unless the request sets
`allowNarrowbandInput`, which the product never sets. The engine's contract is documented in
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h` and the product's in
"Photometric and spectrophotometric colour calibration" in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`.

## Future Monochrome Support Requirements

Monochrome support should be treated as a separate mode, not a hidden variant of RGB SPCC. At minimum it needs:

- A clear product goal: luminance normalization, per-filter response calibration, or multi-filter color reconstruction.
- Input contracts for filter identity, exposure grouping, and whether channels come from separate mono-filter frames.
- Response profiles for each mono filter, including versioned throughput curves and white-reference handling.
- A source-to-channel association model that can explain which measured flux belongs to which filter.
- Output provenance that records mono-filter calibration state separately from RGB PCC/SPCC state.
- Validation against mono broadband filter sets before any production-selectable mode is exposed.

## Future Narrowband Support Requirements

Narrowband support is scientifically different from broadband SPCC because stellar continuum color is not enough to
predict emission-line image ratios. A future mode needs:

- Explicit filter center wavelength, bandwidth, transmission curve, and line/continuum semantics.
- Separate handling for H-alpha, OIII, SII, dual-band, tri-band, and other multi-line filters.
- A validation plan for targets where nebular emission dominates the measured signal.
- A response-profile schema that marks narrowband profiles as incompatible with normal broadband SPCC unless the future
  mode is active.
- UI language that does not imply broadband color calibration quality from narrowband data.
- Reference data and external-tool comparisons tied to the SPCC validation-data manifest.

Until that exists, narrowband metadata should remain a hard SPCC readiness and engine-execution rejection.

## Regression Anchors

Keep coverage around these behaviors:

- `SpectrophotometricColorCalibrationTest.PlanCompilesTheResponseAnchorsTheFitAndFixesTheIdentity`
- `SpectrophotometricColorCalibrationTest.RefusalsAreNamedAndNothingIsWrittenBeforeTheApply`
- `SpccProcessReadinessTest.NarrowbandImageStateBlocksExecution`

