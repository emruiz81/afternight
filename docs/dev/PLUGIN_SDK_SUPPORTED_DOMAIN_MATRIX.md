# AfterNight Extension SDK Supported Domain Matrix

Last updated: 2026-10-17

This matrix states which SDK domains are public in each backend. It reflects the current public `runtime` SDK surface, the `protocol` lite-host boundary, and the source baseline for deferred `rpc` parity work.

Global notes:

- The `runtime` backend runs on AfterNightEngine: one engine session in the backend's module seat, `to_numpy()` writable and zero-copy over the session's image assets.
- The runtime backend opens its engine session without starting an interpreter, and the engine embeds no Python, so nothing in the bootstrap blocks public `runtime` engine-domain exposure: a domain waits only on the design of its binding.
- `_afternight_runtime.available_pixel_types()` names eight pixel types, one per engine sample type: `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `float32` and `float64`. `convert_pixel_type()` and the deconvolutions' output type also accept `sint8`, `sint16`, `sint32`, `float` and `double`, without regard to case. The shared-buffer protocol carries four of the eight in every backend - `uint8`, `uint16`, `float32` and `float64` - so an image of another type is not lent as a buffer, and the full host describes such an image to an extension with an empty pixel-type name.
- Domains marked `Public (runtime-only)` are shipped through the public SDK today, but only through the `runtime` backend. The deferred RPC backend must either add matching support through `AfterNightSdkHost` or keep the domain explicitly runtime-only with `UnsupportedBackendError` in `rpc` sessions.
- Domains marked `Public (protocol service)` are served through host protocol services. They are the only public surface for `protocol` lite-host sessions and are not engine-domain work for `AfterNightSdkHost`.
- `protocol` sessions must raise `UnsupportedBackendError` for every Engine-domain module, even when that module is available through `runtime`.
- Remaining candidates stay private until their API shape and backend support are ready.

## Current Status

| Domain / service | Public status | `runtime` backend | `protocol` backend target | `rpc` backend target | RPC parity baseline |
|---|---|---|---|---|---|
| `core` | Public (runtime-only) | Available through `_afternight_runtime` pybind11 bindings. Includes runtime image construction from NumPy, pixel-type conversion, linear transforms, channel extraction/composition, star FWHM detection, fitted-star discovery, Richardson-Lucy deconvolution, star-derived Richardson-Lucy, and Wiener deconvolution. | Unavailable; raise `UnsupportedBackendError`. | Planned sidecar parity through `AfterNightSdkHost`. | `ExtensionRuntimeBackendModuleTest.Core*`, `RuntimeContractTest.TheRuntimeBindingsKeepTheirRecordedContract`. |
| `io` | Public (runtime-only) | Available through `_afternight_runtime` pybind11 bindings for codec-backed `load()`, `save()`, and `is_supported_path()`. | Unavailable for engine-backed I/O; lite extensions use host view snapshots/buffers or their own libraries. | Planned sidecar parity for engine-backed image I/O. | Runtime import/roundtrip checks in `ExtensionRuntimeBackendModuleTest.LoadingModuleBeforeInterpreterRegistersRuntimePythonBindings`, `PackageSurfaceTest.TheCoreAndIoFacadesReachTheRuntime`, `WritableExportTest.*`. |
| `sequences` | Public (protocol-safe workflow helper) | Available as Python workflow sequence handles with frame metadata, registration metadata, inclusion flags, and in-memory/path-backed frame refs. | Available as local Python workflow sequence helpers; host-owned/open sequence integration is limited to explicitly exposed protocol services. | Planned protocol representation for host-owned/open sequences. | Workflow domain checks in `ExtensionRuntimeBackendModuleTest.LoadingModuleBeforeInterpreterRegistersRuntimePythonBindings` and bundled workflow execution tests. |
| `calibration` | Public (runtime-only) | Available through `_afternight_runtime` pybind11 bindings. `create_masters()` stacks bias/dark/flat masters through the engine's `executeStack`; `calibrate_lights()` calibrates each light through `prepareImageCalibration` and `calibrateImage`. | Unavailable; raise `UnsupportedBackendError`. | Planned sidecar parity. | `ExtensionRuntimeBackendModuleTest.CalibrationCalibrateLightsUsesNativeBatchPipeline`: calibrated pixel ~0.35, native master artifacts present. |
| `registration` | Public (runtime-only) | Available through `_afternight_runtime` pybind11 bindings. Includes `find_stars()`, native sequence `register()`, and stored-transform `apply()` with interpolation/alignment mode options; a registration states pixel centres at integer coordinates. | Unavailable; raise `UnsupportedBackendError`. | Planned sidecar parity. | `RegistrationFindStarsUsesEngineStarProfiler`, `RegistrationRegisterSequenceUsesNativeStarAlignment`, `RegistrationApplyUsesNativeAlignmentFromStoredTransforms`, `RegistrationConventionTest.*`. |
| `stacking` | Public (runtime-only) | Available through `_afternight_runtime` pybind11 bindings. Includes native non-drizzle stacking, quality reports/maps, rejection-capable methods, weighting options, and registered drizzle via `stacking.drizzle()`. | Unavailable; raise `UnsupportedBackendError`. | Planned sidecar parity. | `StackingStackSequenceUsesNativeEngineMedian`, `StackingStackSequenceWritesNativeQualityMaps`, `StackingDrizzleSequenceUsesNativeStoredTransforms`. |
| `ui` | Public (`ui_protocol` protocol service plus runtime native helpers) | `afternight.ui_protocol` host-brokered helpers plus `afternight.ui` native-control helpers for full hosted process windows. | `afternight.ui_protocol` host-brokered dialogs/notifications/result helpers only; extension-owned UI. | Same host-brokered service; not executed in `AfterNightSdkHost`. | Existing `ExtensionRuntimeBackendModuleTest.UiModuleSupportsNativeProcessWindowSurface` and native-window tests. |
| `settings`, logging, progress, diagnostics | Public (protocol service) | Host-brokered protocol services. | Same host-brokered services. | Same host-brokered services. | `ExtensionSdkServiceTest.*`, workflow progress/session tests. |
| `views` | Public internal service subset | Host-brokered view snapshots and view-result presentation used by extension execution. Public author-facing write access remains limited. | Same host-brokered service; primary lite-host image exchange surface. | Same host-brokered service. | `ExtensionSdkServiceTest.DispatchProtocolMessageHandlesViews*` and extension host view-sync tests. |
| `filters` | Candidate | Private. Not bound in `_afternight_runtime`. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `noise_reduction` | Candidate | Private. Not bound in `_afternight_runtime`. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `geometry` | Candidate | Private as a standalone SDK domain. Some geometry is exercised internally by registration/apply. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `normalization` | Candidate | Private as a standalone SDK domain. Stacking normalization options are public through `afternight.stacking`. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `drizzle` | Candidate | No standalone `afternight.drizzle` module yet. Registered drizzle is public through `afternight.stacking.drizzle()`. | Unavailable. | Not exposed until the standalone runtime API shape ships. | Covered indirectly by `StackingDrizzleSequenceUsesNativeStoredTransforms`. |
| `histogram` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `processing` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `operations` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `background` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `masks` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `multiscale` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `statistics` | Candidate | Private as a standalone SDK domain. Some statistics are exposed through `core.find_stars()` and stacking quality reports. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |
| `astrometry` | Candidate | Private. Pure-C++ engine path. | Unavailable. | Not exposed until the runtime API shape ships. | None yet. |

## RPC Runtime Baselines

The timings were captured from Linux Clang release/debug tests on April 29, 2026, with an earlier runtime backend whose cases then ran in `Test_ProcessFramework`. The invariants are held by the `ExtensionRuntimeBackendModuleTest` cases in `Test_ExtensionRuntimeBackend` and, for the bundled workflow, by `Test_ProcessFramework`. They are not final performance budgets; the RPC implementation should rerun them on the chosen representative machine, then compare `rpc` behavior against the same output invariants.

| Baseline area | Runtime invariant to preserve in `rpc` | Latest local timing signal |
|---|---|---|
| Runtime module import/domain availability | `_afternight_runtime` loads before interpreter startup; `core`, `io`, `calibration`, `registration`, `stacking`, `ui`, and workflow helpers are importable/callable. | Focused release/debug import smoke: ~26 ms each. |
| Calibration | Synthetic light calibrated through native masters lands near 0.35; native master paths exist and report `native=True`. | Full release `Test_ProcessFramework`: calibration runtime test reported ~1 ms after interpreter warm-up. |
| Registration | Native star registration writes aligned files with matched-star/RMS metadata; apply-registration moves the shifted peak back to the reference without re-solving stars. | Full release `Test_ProcessFramework`: register ~12 ms, apply ~1 ms after interpreter warm-up. |
| Stacking | Native median of `[0.10, 0.20, 0.90]` is ~0.20; quality-map requests return produced FITS maps and report method-specific omissions; drizzle from stored transforms outputs a 48x48 image for 24x24 input at 2x scale. | Focused release/debug quality+drizzle smoke: ~98 ms release, ~102 ms debug for import + two stacking cases. |
| Workflow extension | Bundled sequence workflow runs native calibrate -> register/align -> stack and emits the expected report/artifact contract. | Full release `Test_ProcessFramework`: bundled workflow test passed in the release suite. |

## Interpretation

- `Candidate` means the engine provides the domain's operations, but no `_afternight_runtime` binding has been designed for it yet.
- `Public (runtime-only)` means the domain is available in the public SDK today, but only through the `runtime` backend; `protocol` sessions reject it and `rpc` parity is not implied yet.
- `Public (protocol service)` means the domain/service is brokered through the host protocol in all sessions and is not public engine-domain work for `AfterNightSdkHost`.

## Related documents

- [Extension Host Modes and Licensing Boundary](EXTENSION_HOST_MODES_AND_LICENSING.md)
