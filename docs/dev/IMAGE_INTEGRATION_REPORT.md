# The Image Integration report

How a run of `ImageIntegrationProcess` reports itself: when a report is written, what it is built from, how its values
are printed, and the runs its suites are held to. The code is under
`src/AfterNightProcesses/processes/ImageIntegration/reporting/`, the archive that calls it in
`workflow/WorkflowArchiver.cpp`, and the suites in `src/AfterNightTests/Test_ImageIntegration/reporting/`.

## When a report is written

A report belongs to a run's archive, `history/<executionId>/` in the run's output folder, and is written when the
executor commits that archive.

| Step | Where | What happens |
| --- | --- | --- |
| Commit | `WorkflowExecutor.cpp` (the `commitArchive` lambda) | The executor builds an `IWorkflowRunArchive` from its archive factory and calls `commit(manifest, result, capturePhase)`. A run is committed when it succeeded, or when it stopped with at least one successful group. A run that failed or was cancelled before any master was made puts its staged files back and writes no archive and no report. |
| Whether | `shouldGenerateWorkflowReport` (`WorkflowArchiver.cpp`) | A registration-only run or scope gets no report. |
| Previews | `WorkflowArchiver::commit` | `report_previews/<group>/master.jpg` from the preview the stacking stage prepared from the master it held in memory (`IntegrationGroupResult::reportPreviewJpeg`); when none was prepared and the archiver has a session, the file is read. The rejection and quality maps get `rejection_map_N.jpg` and `quality_map_N.jpg`. |
| Build | `WorkflowReportBuilder::build()` | The sections are appended to one Markdown text. The builder opens the run's `registration_metrics.db`, probes each light's header, reads each master once to measure it, and reads the session notes and the previous run's archive. |
| Render | `MarkdownHtmlRenderer` | The Markdown becomes `workflow_report.html`, with a stylesheet made from the application's design tokens inlined. |
| Finish | `WorkflowArchiver::finalizeTimingArtifacts`, `finalizeLogArtifact` | The executor calls the first after the commit, once the final counters and the engine's memory figures exist, and the second with the final log. The report's own time is one of its figures, so the stored documents carry tokens until then. |
| Open | `WorkflowProgressDialogV2`, `WorkflowHistoryModel` | The progress dialog opens `Result::htmlReportPath`; the history list prefers `workflow_report.html` and falls back to `workflow_report.md`. |
| Rebuild | `SummaryTab::exportReportRequested`, handled in `ImageIntegrationProcess.cpp` | The history row's report action rebuilds both documents in the run's folder from its archive, on the thread pool, and opens the result. |

A run's archive holds `workflow_manifest.json` (the manifest, the stage, calibration, alignment and integration group
results, the timings, the counters, the warnings and errors), `workflow_report.md`, `workflow_report.html`,
`workflow_log.txt`, a snapshot of `registration_metrics.db` when the run's payloads are retained, `groups/<group>/`
(a summary, a thumbnail, a copy of the master and the one it replaced) and `report_previews/<group>/`. The history
keeps 20 runs and 5 GiB (`ImageIntegrationConstants.h`); a run whose masters do not fit is archived without them,
and each master's result says so (`OutputRetentionStatus`).

## What a report is built from

| Source | Holds |
| --- | --- |
| `WorkflowManifest` | The groups and their own settings: each bias, dark and flat group with its stacking settings; each light's calibration (the masters it takes and its `CalibrationSettings`); each registration group with its settings, its reference and its lights' exposures and capture times; each integration group with its lights, its stacking, normalization and drizzle settings and the registration group it takes frames from. Only the execution, the plate solve and the RGB combination are stated once for the run. |
| `WorkflowExecutor::Result` | The outcome, the warnings and errors as text, each stage's status and time, the calibration, alignment and integration group results, the log, the timings and, when collected, the counters, the cache diagnostics and the engine's memory. |
| `IntegrationGroupResult` | One per master the run attempted: the output, the lights combined, the solution's origin, the time and the finalization phases, the maps, and the stack's `StackQualityRecord`. A combined RGB output is one more result, keyed by `combinedRgbGroupKey`. |
| `registration_metrics.db` | Per light and settings version: the stars, the star width, the SNR, the eccentricity, the noise figures, the photometric and astrometric figures, whether registration succeeded, the registered output and the transform. Per registration group: the reference frame. |
| The files | Each light's header, each master's header and samples, the normalization coefficients, the session notes and the earlier runs under `history/`. Reading any of them takes the engine session the report is given; without one the report states no figure that needs a file. |

Facts worth knowing before changing what a report states:

- **A reused master has no result.** A group whose output is still valid returns from the stacking stage before any
  `IntegrationGroupResult` is made. A manifest group with no result in a run that completed was reused; in a run
  that stopped it may equally have never been reached.
- **The stack's per-light figures are in the stack's order.** `StackQualityRecord::frameRejectionRates` runs
  parallel to `lightWeights`, the lights the stack combined in the order it read them. Neither is in the order of
  the group's `lightFiles`: a light the stack dropped is absent, and after a restack only the kept lights remain.
  Only `spatialRestackExcludedFrameIndices` are places in `lightFiles`.
- **A light whose registration failed is still in its group's lists.** `registration_metrics.succeeded` is the
  only record of the failure.
- **A light the stack dropped is named in a warning alone**: "Skipping N light frame(s): ...", with the reason in
  parentheses.
- **A light the user left out is in no list.** An excluded light is dropped when the lights are grouped, and a
  stack-excluded light stays in its registration group's lights and leaves its integration group's. A group whose
  every light is left out is no group at all.
- **A calibration master the run did not make has no result either**, unless the user supplied it: a supplied
  master is its group's `existingMaster` and has a result naming the user's file.

## How a value is printed

`ReportFormat.h` is the one unit that turns a value into text for a report. A figure, a count, a duration, a size, a
time and a group's name each have one form, so two sections cannot print the same value two ways, and a measurement
of `0.01742656553` is read as `0.01743`.

| Value | Function | Form |
| --- | --- | --- |
| A measured figure | `formatFigure` | Four significant digits, the zeros they end in dropped: `0.01743`, `2.5`, `123.5`. From a thousand up, a whole number in groups of three: `12 346`. Below one thousandth and from a trillion up, a power of ten: `6.749 × 10⁻⁵`. |
| A count | `formatCount` | Groups of three digits set apart by thin spaces: `1 284`. |
| A duration | `formatDurationMs` | Its two largest units: `297 ms`, `1.2 s`, `59 s`, `1 m 01 s`, `9 h 50 m`. Hours are not turned into days: an integration time is said in hours. |
| A size | `formatByteSize` | The largest unit it fills, in powers of 1,024 under the names `KB`, `MB`, `GB` and `TB`: `99.6 MB`, `234 MB`. |
| An instant | `formatDateTime` | To the minute, in the zone the run's archive states: `10 Nov 2026, 11:15`. |
| A date, an offset | `formatDate`, `formatUtcOffset` | `24 Mar 2026`; `UTC−03:00`. |
| A group key | `friendlyGroupKey` | Its stated parts joined by em dashes, the resolution last: `Blue — M 83 (3008 × 3004)`. |
| A value that is not stated | `noValueText` | An en dash. |

The rules behind the forms:

- **The form is decided after the rounding.** A value that rounds up to one thousandth prints as `0.001`, a duration
  of 59.6 seconds as `1 m 00 s`, and 1,023.99 KB as `1.0 MB`.
- **A missing value is never a zero.** A figure that is not a number prints as an absent one does; `formatFigure`
  takes an optional for that reason.
- **A negative value carries the minus sign** (U+2212), not a hyphen, and zero prints as `0` whatever its sign.
- **The text is the same on every host.** Nothing reads the system's locale: the decimal mark is a point and the
  months are named in English, because a report is read away from the machine that wrote it.
- **A time is printed in the archive's zone, not the reader's.** `ReportTimeZone` holds the zone's name and the
  offset the archive was written at. Where the host knows the named zone, each instant gets the offset it had - a
  light taken in winter and archived in summer - and otherwise the stated offset is used, fixed.

`ReportSlugs` gives each group of a run an anchor name: lower-case letters, digits and hyphens made from the group's
key. Two keys never share a slug - the second gets a number - and a key keeps the slug it was first given. A slug
names anchors only: a group's folder in the archive keeps the name the archiver gave it (`sanitizeFilename`, with a
hash appended where two keys collide), and the two are never derived from each other.

`WorkflowReportBuilder` still prints durations and sizes through its own `formatDuration` and `formatFileSize`, whose
output its suite holds to the character.

## The suites, and the runs they build

`Test_ImageIntegration` holds the report in `reporting/`:

- `test_workflow_report_builder.cpp` - the report's sections, figures and previews, asserted on the Markdown.
- `ReportFormatTest.cpp` - every form of the table above, with its edges.
- `ReportScenariosTest.cpp` - the scenarios themselves: each holds what it says it holds, is archived by the
  product's archiver, and builds a report.

`ReportScenarios.h` is where a suite gets a run from. `buildReportScenario(id)` writes one whole run under a
temporary directory the scenario owns - the masters and the lights through the neutral FITS writer, the metrics
database through the product's `RegistrationMetricsWriter`, the earlier runs of the folder through the product's
`WorkflowArchiver` - and returns its manifest, its result, the archive's context, the session to read it in and the
figures it was built to hold (`ReportScenarioFacts`). `archiveScenarioRun` commits the scenario's own run as the
executor does. The same header holds the file helpers the report's suites share: the session, the master and light
writers, the bare metrics table and an archived run's manifest.

| Scenario | The run | What it is for |
| --- | --- | --- |
| `OneMonoMaster` | One mono master, one night, no calibration frames | A run with one master; lights with no masters |
| `OneColourMaster` | One colour master from a mosaic camera, two nights, bias, dark and flat | Per-channel figures; a clean run |
| `FiveFilters` | Luminance, red, green, blue and hydrogen-alpha of one target (60, 30, 30, 30 and 24 lights), two registration groups, a combined RGB, a reused bias, no flat for hydrogen-alpha, rejection and coverage maps for luminance | Shared groups, settings that differ between groups, warnings |
| `TwoTargets` | Two targets, two filters each | Grouping by target |
| `DrizzledGroup` | A drizzled group beside a plain one | Drizzle settings and the drizzled output |
| `CometRun` | Lights registered and stacked on a comet | Comet settings |
| `HdrExposureMerge` | One group of three exposures | Per-light exposures |
| `FailedInStacking` | Three masters made, one failed, one never reached | A run that failed and is archived |
| `CancelledAfterTwoMasters` | Two of five masters made | A cancelled run that is archived |
| `CancelledBeforeAnyMaster`, `FailedBeforeAnyMaster` | No master made | Runs the executor archives nothing for |
| `IncrementalRun` | After a full run: two groups regenerated, three reused | Reused masters; an earlier run in the history |
| `EveryGroupReused` | After a full run: nothing regenerated | A run with no result at all |
| `PreCalibratedAndSuppliedDark` | A pre-calibrated group beside one calibrated with a supplied master dark | The two calibration states |
| `FiveFiltersWithoutSession` | The five filters, reported with no session | Nothing read from a file |
| `FiveFiltersWithoutMetrics` | The five filters with no metrics database | No per-light metric |
| `FiveFiltersAfterEarlierRun` | The five filters after a run of the first night alone | Comparison with an earlier run |
| `LiteFrontEnd` | One mono master from the Lite front end | The process label |
| `FiveFiltersWithoutPayloads` | The five filters in a history too small for its masters | An archive of metadata alone |
| `TwoThousandLights` | One group of 2,000 lights, named and not written | Size and time |
| `EveryLightState` | Lights excluded and stack-excluded by the user, a group left out whole, a restack, registration failures, a light with no registered output | Every way a light is left out |
| `PhotometricAndLocalWeighting` | Photometric weighting in one group, local weighting in another | The weighting figures |
| `DistortionCorrectionsAndManualPoints` | Distortion and chromatic aberration corrections in one registration group, manual control points in another | The registration's optional figures |

A scenario states only what a run records. Where a run records nothing - the lights the user left out - the
scenario names them itself (`ReportScenario::lightsLeftOutByUser`), so a case can hold a report to them once a run
does.

To add a scenario, add its enumerator and its name, describe the run in `buildReportScenario` with the builder's
`addBias`, `addDark`, `addFlat`, `addRegistrationGroup` and `addLightGroup`, and give it a case in
`ReportScenariosTest.cpp` that states what it is for. A group that a later run of the same folder must find
unchanged takes a slot of the night of its own (`LightGroupPlan::nightStartMinutes`), so its lights have the same
names and times whatever the other groups take.
