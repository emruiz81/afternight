# The Image Integration report

How a run of `ImageIntegrationProcess` reports itself: when a report is written, what it is built from, the model
a report is stated in, how its values are printed, and the runs its suites are held to. The code is under
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

## The report model

`reporting/ReportModel.h` holds everything a report states as plain structures, and
`buildReportModel(inputs, sources)` (`ReportModelBuilder.h`) fills one from a run. No run writes anything from it yet:
the Markdown report above is still what a run writes, and the model is built beside it by the suites. It exists
first because of what the Markdown cannot give:

- **One structure, filled once.** A report used to exist only as text. Its suites could assert nothing but
  substrings, its timings had to be substituted into finished text, and a second output would have meant a second
  pass over that text. The model is filled once from the run's records; a document, the stored model and the suites
  all read it.
- **The model is a document of its own.** `ReportModelJson` writes it as the JSON a run's archive is to keep as
  `workflow_report.json`, under the format number `kReportSchema`. A later run can then compare itself with an
  earlier one without reading an image again, a newer application can write an older run's report again, and a
  suite asserts on structure instead of bytes.
- **Settings belong to groups.** The manifest holds a `StackingSettings` per calibration group and per integration
  group, a `RegistrationSettings` per registration group and a `CalibrationSettings` per light; only the
  execution, the plate solve and the colour combination are stated once. The model holds them the same way - on
  each `ReportCalibrationMaster`, `ReportRegistrationGroup` and `ReportMaster`, with `ReportRun::settings` for the
  rest - so "the same for every group" is something a writer finds, never something the model assumes.
- **No figure comes from the log.** The Markdown reads three figures out of the run's log by regular expression,
  and a change to a log line silently changed the report: the line two of them need is no longer logged. The model
  carries the log as text (`ReportRun::log`) and reads it for nothing. `ReportModelBuilder.NoFigureIsTakenFromTheLog`
  builds every scenario with its log, with another and with none, and holds the three models equal but for the
  log; the three parsers stay in `WorkflowReportBuilderHelpers.h`, which only the Markdown units include.

### What the model holds

| Structure | Holds |
| --- | --- |
| `ReportModel` | `reportSchema`, `run`, `issues`, `targets`, `masters`, `calibrationMasters`, `lightGroups`, `registrationGroups`, `stages`, `decisions`, `comparison`, `history`, `notStated` |
| `ReportRun` | How the run ended and that in one sentence; the title, process and profile; the targets; mode, scope and the groups an incremental run found changed; the masters by what became of them; lights and exposure; warnings and errors counted; the executor's interval with the preparation and executor phases when timed; the counters when collected; the session's notes; nights, lights by filter, equipment, what the lights' headers state; the application's version, the execution, when and where it was archived, the host; the archive's and the manifest's formats; the three fingerprints; the output folder; the settings stated once; the log |
| `ReportMaster` | One per master the run planned, a combined colour image among them: key, anchor, preview folder, name, target, filter, kind, state; file, size and whether the archive kept a copy; lights and exposures; the calibration masters its lights take; registration group; the solution's origin; stacking and drizzle in a line each; time and finalization phases; what one read measured, per channel, and the headline figures taken from it; the stack's record; its picture and maps; its own settings; how its lights were weighed; its lights |
| `ReportFrame` | One per light of a master's group, in the order of the group's lights: file, exposure, whether its registration succeeded and what it recorded, the weight the stack gave it, its quality score and its share of the stack |
| `ReportCalibrationMaster` | Kind, key, anchor, file, what became of it (`Made`, `Reused`, `Supplied`, `Failed`), the frames it is stacked from, the masters a flat is calibrated with, its stacking settings |
| `ReportLightGroup` | A group of lights the user marked as calibrated already |
| `ReportRegistrationGroup` | Key, anchor, lights and how many registered, the reference and who chose it, the transform and the kinds the registration recorded, the alignment's correction counts, the comet fits, the catalog sources kept for the colour calibration, its settings, the masters it serves |
| `ReportStage` | A stage as the run timed it: name, state, the run's own status word, time, the groups it works on |
| `ReportDecision` | A setting the application decided: what, for which group, what was asked, what it resolved to, why |
| `ReportTarget` | A target's lights, exposure, filters, masters and the mean of what its masters measured |
| `ReportComparison` | The run's totals beside those of the most recent earlier run of the folder that completed |

The builder states today what the Markdown report states. The model also defines what the Markdown has no word
for, which nothing fills yet: a light's state and the reason for it, its capture time, shift and normalization
coefficients; the issues; a master's sample distribution, full-scale picture and baseline; a calibration master's
level, noise and picture; the lights grouped by the calibration they share; the engine's memory, the cache's figures
and what was written to disk; the runs of the folder; and what the report could not state.

### What the builder reads through

`ReportSources.h` holds one interface per thing a report reads outside the manifest and the result, so a suite can
stand in for any of them:

| Source | Answers |
| --- | --- |
| `IReportMetrics` | A registration group's latest settings version, its lights' metrics and noise at a version, the comet fits and the matched catalog sources |
| `IReportHeaders` | A file's header, read without its pixels |
| `IReportImages` | One read of an image, measured per channel |
| `IReportFiles` | Whether a file is there, its size, when it changed, a text file's contents |
| `IReportHost` | The time, the time zone and the platform |

`ProductionReportSources` holds the product's implementations: the metrics database opened once and closed with the
object, headers probed and images measured in the engine session it is given, the host's files and clock. **The
caller names the metrics database**, not the builder: a model of the run being committed reads the output folder's,
and a model of a run read later must read that run's own snapshot, since the output folder's holds today's metrics.
The builder asks for each image and each header once however many facts need it (`ReportBuild`); a case counts the
reads.

### How it is built

`buildReportModel` calls one function per area, in an order that matters: the lights and the settings are stated on
the masters and the groups, and the run's rollups read what the masters measured.

| Unit (`reporting/facts/`) | States |
| --- | --- |
| `MasterFacts.cpp` | The masters: state, file, measurement, stack, maps, pictures |
| `FrameFacts.cpp` | Each master's lights and how they were weighed |
| `CalibrationFacts.cpp` | The calibration masters and the pre-calibrated groups |
| `RegistrationFacts.cpp` | The registration groups |
| `SettingsFacts.cpp` | Every group's settings, the run's own, and the decisions |
| `RunFacts.cpp` | The run: outcome, totals, stages, timings, counters, nights, equipment, headers, provenance, targets |
| `ComparisonFacts.cpp` | The totals of this run and of the earlier one |

`facts/ReportFacts.h` is private to the reporting units. It declares those functions and every function that reads
a fact out of a run's records - an exposure breakdown, a target's name, a transform's name, a light's weight, the
fingerprint. The Markdown builder calls the same ones: a function that reads a fact has one home, and
`WorkflowReportBuilderHelpers.h` keeps only what prints. The fingerprint is `computeReproducibilityFingerprint` in
`RunFacts.cpp`, which both reports state.

Rules the builder keeps:

- **A value that is not stated is absent, never a zero.** A master that was not measured has no statistics; a run
  that timed no phase has no phases, while a phase that took no time is a zero; a group the alignment did not
  report has no correction count, while one it reported with none has a zero; when no light's header could be read,
  what the headers state is absent rather than "none of them states it".
- **A master's state comes from the result, and from the file where there is none.** A result that succeeded is
  `Made`, one that did not is `Failed`. A planned group with no result is `Reused` when the run completed and its
  file is there, and otherwise `NotReached`, or `Cancelled` in a run the user stopped: a run that stopped cannot be
  said to have reused a master it may never have reached.
- **A result that is no group of the manifest is a master of its own.** The combined colour image is keyed by
  `combinedRgbGroupKey` of its registration group; it has a file, a measurement and no lights of its own.
- **The outcome is one of five.** A run the user stopped is `Cancelled`. A run that failed is `Partial` when it
  made a master, since those are kept, and `Failed` otherwise. A run that completed is `Partial` when a group
  failed inside it, `CompletedWithWarnings` when it warned, and `Completed` otherwise.
- **The lights are in the order of the group's lights.** The stack's records are in the stack's own order, so a
  weight is matched to its light by the light's identity, never by its place. A recorded weight is shown relative
  to the heaviest light, and a photometric one at its mean of one. A stack that recorded none is weighed as its
  settings imply, in the session the sources carry.
- **Anchors and folders are not derived from each other.** A master's `slug` comes from `ReportSlugs`; a
  registration group's and a calibration master's are made from the key with their kind before it, since their keys
  may be spelled as a group of lights is. A master's `previewFolder` is the name the archiver gives the folder it
  writes, hash and all.
- **The words are the report's own.** Names of settings, of counters and of outcomes go through
  `QCoreApplication::translate` under the context `ImageIntegrationReport`. No translation ships: a report reads in
  English.

Where the model and the Markdown differ, the model is the one that follows the records. The Markdown counts every
group with no result as reused, a run that stopped included, and counts a combined colour image among the masters
made but not among the groups; and it prints "n/a" for the figures it reads from the log.

### The stored document

`ReportModelJson::toJson` and `fromJson` write and read the model field for field: each structure's fields are
named once, in one function a writer and a reader both walk, so the two cannot disagree about a field.

- A value that is not stated is left out, and reads back as not stated; a stated zero is written.
- A key the reader does not know is ignored, so a field added later does not break a reader of the same format.
- A document of a newer format is refused by name: `fromJson` returns no model and a `ReportNotStated` saying which
  format it met and which it reads. A document that states no format is refused the same way.
- A number that is not finite is written as text, since JSON has no form for it; a time is written with its offset
  from UTC, to the millisecond; an enumerator is written by name.

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
- `ReportModelBuilderTest.cpp` - what the model states of the run, the masters, the calibration and the
  registration.
- `ReportSettingsFactsTest.cpp` - each group's settings, the ones of a mode, and the decisions.
- `ReportFrameFactsTest.cpp` - a master's lights and their weights.
- `ReportComparisonTest.cpp` - a run beside the earlier runs of its folder.
- `ReportModelJsonTest.cpp` - the stored document: every scenario's model and one with every kind of value survive
  it, and what a reader refuses and ignores.
- `ReportModelParityTest.cpp` - the model beside the Markdown of the same run, figure by figure, for every
  scenario. It goes when the Markdown report does.
- `ReportScenariosTest.cpp` - the scenarios themselves: each holds what it says it holds, is archived by the
  product's archiver, and builds a report.

`ReportScenarios.h` is where a suite gets a run from. `buildReportScenario(id)` writes one whole run under a
temporary directory the scenario owns - the masters and the lights through the neutral FITS writer, the metrics
database through the product's `RegistrationMetricsWriter`, the earlier runs of the folder through the product's
`WorkflowArchiver` - and returns its manifest, its result, the archive's context, the session to read it in and the
figures it was built to hold (`ReportScenarioFacts`). `archiveScenarioRun` commits the scenario's own run as the
executor does. The same header holds the file helpers the report's suites share: the session, the master and light
writers, the bare metrics table and an archived run's manifest. `ReportModelTestSupport.h` builds a
scenario's model through the product's sources and looks a master, a block or a row up by name.

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
