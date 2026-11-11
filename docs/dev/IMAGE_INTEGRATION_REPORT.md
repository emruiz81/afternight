# The Image Integration report

How a run of `ImageIntegrationProcess` reports itself: when a report is written, what it is built from, the model
a report is stated in, how its values are printed, how its charts are drawn, how its document is written, and the
runs its suites are held to. The code is under
`src/AfterNightProcesses/processes/ImageIntegration/reporting/`, the archive that calls it in
`workflow/WorkflowArchiver.cpp`, and the suites in `src/AfterNightTests/Test_ImageIntegration/reporting/`.

## When a report is written

A report belongs to a run's archive, `history/<executionId>/` in the run's output folder, and is written when the
executor commits that archive.

| Step | Where | What happens |
| --- | --- | --- |
| Commit | `WorkflowExecutor.cpp` (the `commitArchive` lambda) | The executor builds an `IWorkflowRunArchive` from its archive factory and calls `commit(manifest, result, capturePhase)`. A run is committed when it succeeded, or when it stopped with at least one successful group. A run that failed or was cancelled before any master was made puts its staged files back and writes no archive and no report. |
| Whether | `shouldGenerateWorkflowReport` (`WorkflowArchiver.cpp`) | A registration-only run or scope gets no report. |
| Previews | `WorkflowArchiver::commit` | `report_previews/<group>/master.jpg` and `detail.jpg` from the pictures the stacking stage prepared from the master it held in memory (`IntegrationGroupResult::reportPreviewJpeg`, `reportDetailJpeg`); when none was prepared and the archiver has a session, one read of the file gives both. The rejection and quality maps get `rejection_map_N.jpg` and `quality_map_N.jpg`. A master the run reused takes its two pictures from the archive of the run that made it, or from one read of its file; each calibration master gets `report_previews/calibration/<master>.jpg`. See "The pictures". |
| Build | `WorkflowReportBuilder::build()` | The sections are appended to one Markdown text. The builder opens the run's `registration_metrics.db`, probes each light's header, reads each master once to measure it, and reads the session notes and the previous run's archive. |
| Render | `MarkdownHtmlRenderer` | The Markdown becomes `workflow_report.html`, with a stylesheet made from the application's design tokens inlined. |
| Finish | `WorkflowArchiver::finalizeTimingArtifacts`, `finalizeLogArtifact` | The executor calls the first after the commit, once the final counters and the engine's memory figures exist, and the second with the final log. The report's own time is one of its figures, so the stored documents carry tokens until then. |
| Open | `WorkflowProgressDialogV2`, `WorkflowHistoryModel` | The progress dialog opens `Result::htmlReportPath`; the history list prefers `workflow_report.html` and falls back to `workflow_report.md`. |
| Rebuild | `SummaryTab::exportReportRequested`, handled in `ImageIntegrationProcess.cpp` | The history row's report action rebuilds both documents in the run's folder from its archive, on the thread pool, and opens the result. |

A run's archive holds `workflow_manifest.json` (the manifest, the stage, calibration, alignment and integration group
results, the groups the run reused, the timings, the counters, the warnings and errors), `workflow_report.md`,
`workflow_report.html`, `workflow_log.txt`, a snapshot of `registration_metrics.db` when the run's payloads are
retained, `groups/<group>/` (a summary, a thumbnail, a copy of the master and the one it replaced),
`report_previews/<group>/` and `report_previews/calibration/`. The history keeps 20 runs and 5 GiB
(`ImageIntegrationConstants.h`); a run whose masters do not fit is archived without them, and each master's result
says so (`OutputRetentionStatus`). The pictures are written before the run's size is measured, so that decision
counts them.

## What a report is built from

| Source | Holds |
| --- | --- |
| `WorkflowManifest` | The groups and their own settings: each bias, dark and flat group with its stacking settings; each light's calibration (the masters it takes and its `CalibrationSettings`); each registration group with its settings, its reference and its lights' exposures and capture times; each integration group with its lights, its stacking, normalization and drizzle settings and the registration group it takes frames from. Only the execution, the plate solve and the RGB combination are stated once for the run. The lights the user left out, which no group names, are listed apart (`leftOutLights`). |
| `WorkflowExecutor::Result` | The outcome, the warnings and errors as text, each stage's status and time, the calibration, alignment and integration group results, the integration groups the run reused (`reusedIntegrationGroups`), the log, the timings and, when collected, the counters, the cache diagnostics and the engine's memory. |
| `IntegrationGroupResult` | One per master the run attempted: the output, the lights combined, the lights the stack dropped with the reason of each (`droppedLights`), the solution's origin, the time and the finalization phases, the maps, and the stack's `StackQualityRecord`. A combined RGB output is one more result, keyed by `combinedRgbGroupKey`. |
| `registration_metrics.db` | Per light and settings version: the stars, the star width, the SNR, the eccentricity, the noise figures, the photometric and astrometric figures, whether registration succeeded, the registered output and the transform. Per registration group: the reference frame. |
| The files | Each light's header, each master's header and samples, the normalization coefficients, the session notes and the earlier runs under `history/`. Reading any of them takes the engine session the report is given; without one the report states no figure that needs a file. |

Facts worth knowing before changing what a report states:

- **A reused master has no result, and the run names it.** A group whose output is still valid returns from the
  stacking stage before any `IntegrationGroupResult` is made, and the plan of an incremental run leaves such a
  group out before any stage sees it. Either way the group is in `Result::reusedIntegrationGroups`, which is the
  only thing that makes a master reused: a manifest group with neither a result nor that record was not reached.
- **The stack's per-light figures are in the stack's order.** `StackQualityRecord::frameRejectionRates` runs
  parallel to `lightWeights`, the lights the stack combined in the order it read them. Neither is in the order of
  the group's `lightFiles`: a light the stack dropped is absent, and after a restack only the kept lights remain.
  Only `spatialRestackExcludedFrameIndices` are places in `lightFiles`.
- **A light whose registration failed is still in its group's lists.** `registration_metrics.succeeded` is the
  only record of the failure.
- **A light the stack dropped is on its group's result.** `IntegrationGroupResult::droppedLights` names each with
  one of four reasons: no registered output, no registration transform, no star base product, no comet-aligned
  image. The stage's warning, "Skipping N light frame(s): ...", still says the same in words.
- **A light the user left out is in no group, and in the manifest's list.** An excluded light is dropped when the
  lights are grouped, and a stack-excluded light stays in its registration group's lights and leaves its
  integration group's. A group whose every light is left out is no group at all. `WorkflowManifest::leftOutLights`
  names each such light, which exclusion it was and the integration group it would have joined.
- **A calibration master the run did not make has no result either**, unless the user supplied it: a supplied
  master is its group's `existingMaster` and has a result naming the user's file.

## What a run records for its report

Five things a report states have no other source, so a run records them. None of them changes what a run decides.

- **The lights the user left out are a list of the manifest's own** (`WorkflowManifest::leftOutLights`), each with
  its exclusion and the key of the integration group it would have joined. A list on the manifest and not on a
  group, because a group whose every light is left out does not exist. `ManifestBuilder` fills it: for a light
  excluded in the lights' list it asks the grouping for the key the light would take
  (`IntegrationGroupEngine::groupKeyFor`, which `computeGroups` itself uses), and it names a stack-excluded light
  under the group whose stack leaves it out. A run of one group lists that group's lights alone. The groups' own
  `lightFiles` already decide every hash; the list is written under one key of its own, and `ManifestHashing` and
  `ManifestDiff` enumerate the fields they read and do not read it. Cases hold that for every scenario: the group,
  calibration and registration hashes, the run's fingerprints and the comparison of two manifests are the same with
  the list and without, and the manifest's JSON differs by that key alone. Without this a user who excluded a light
  would see every master stacked again.
- **The lights a stack dropped are on the group's result** (`IntegrationGroupResult::droppedLights`), filled where
  the stacking stage plans a group's frames, and written with the group's result in the archive and in its
  `group_summary.json`.
- **The groups a run reused are on the run's result** (`WorkflowExecutor::Result::reusedIntegrationGroups`), each
  with its key and the master it left as it is: the groups an incremental run's plan leaves out, which the executor
  names before any stage runs, then the ones the stacking stage finds valid (`StageResult::reusedGroups`). The
  archive writes the list under `reusedIntegrationGroups`. No count of a run's successful groups reads it: a run
  that stopped is committed only when it made a master.
- **The detail** and **the calibration masters' pictures**, below.

### The pictures

A master has two pictures in its run's archive, `report_previews/<group>/master.jpg` and `detail.jpg`, and a
calibration master one, `report_previews/calibration/<master>.jpg`. A picture is not a workflow image: it carries no
identity, is never reused by a run and is never published through `publishWorkflowImage`.

- **The whole frame** is the master reduced so its longer side is at most 2,048 pixels, presented as the viewer
  presents an image.
- **The detail** is the part of the master `reportDetailRegion` names - at most 960 by 540 of its pixels, at its
  centre - at full scale, one picture pixel for each of the master's. A master smaller than that is its own
  detail. It is presented through the analysis the whole frame was presented through - the presentation returns
  the balance and the statistics it used and accepts them back - so the two pictures of one master share one
  stretch, and the detail shows what the whole frame's stretch looks like close up rather than a stretch of its
  own. The stacking stage makes both from the master it holds in memory (`generateResidentStackPreviewAssets`); the
  archiver, when the run prepared none and it has a session, makes both from one read of the file
  (`generateReportMasterPreviews`). A detail is kept only beside its whole frame.
- **A comparison needs both runs' pictures, and each run keeps its own.** A later report shows a master beside the
  one an earlier run made by the pictures each archive holds, the whole frame and the detail, rather than by
  reading and stretching the earlier master again: the history may not have kept that master's payload, and a
  picture of it is what the earlier run showed. So every archive holds the pictures of every master its report
  states - a reused master's included. For a master the run reused, the archiver copies the two pictures from the
  archive of the latest earlier run that made it, provided the master on disk is still the size that run recorded
  and that run kept the whole frame; otherwise it reads the file, when it has a session. The copies keep the
  report whole after the history drops the earlier run.
- **A calibration master's picture** is written for each bias, dark and flat group of the manifest whose file is
  there, when the archiver has a session, within 1,024 pixels a side: a report shows it small, and a frame of
  noise compresses badly. `archivedCalibrationPreviews` names them, by the master's own file name
  with a hash of its kind and group where two masters share a name, and the report model asks the same function
  for the name. `archiveGroupFolderNames` does the same for the groups' folders: the run's results first, then the
  groups it reused.

Without a session the archive holds the pictures the run prepared and no other: no detail of a master the stage
did not picture, no picture of a reused master whose maker is gone from the history, no calibration picture.

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
| `ReportModel` | `reportSchema`, `run`, `issues`, `targets`, `masters`, `groupsNotStacked`, `calibrationMasters`, `lightGroups`, `registrationGroups`, `stages`, `decisions`, `comparison`, `history`, `notStated` |
| `ReportRun` | How the run ended and that in one sentence; the title, process and profile; the targets; mode, scope and the groups an incremental run found changed; the masters by what became of them; lights and exposure; warnings and errors counted; the executor's interval with the preparation and executor phases when timed; the counters when collected; the session's notes; nights, lights by filter, equipment, what the lights' headers state; the application's version, the execution, when and where it was archived, the host; the archive's and the manifest's formats; the three fingerprints; the output folder; the settings stated once; the log |
| `ReportMaster` | One per master the run planned, a combined colour image among them: key, anchor, preview folder, name, target, filter, kind, state; file, size and whether the archive kept a copy; lights and exposures; the calibration masters its lights take; registration group; the solution's origin; stacking and drizzle in a line each; time and finalization phases; what one read measured, per channel, and the headline figures taken from it; the stack's record; its two pictures and its maps; its own settings; how its lights were weighed; its lights |
| `ReportGroupNotStacked` | A group the run planned no master of because the user left every one of its lights out: its key, its name and those lights |
| `ReportFrame` | One per light taken: the lights of a master's group in the group's order, then the lights the user left out of that group. File, exposure, when it was taken and on which night, what became of it and why, whether its registration succeeded and what it recorded, its shift against the reference, the normalization's scale and offset, the weight the stack gave it, its quality score, its share of the stack, the share of its samples the stack rejected, and the gain, offset and temperature its header states |
| `ReportCalibrationMaster` | Kind, key, anchor, file, its size and when it changed, what became of it (`Made`, `Reused`, `Supplied`, `Failed`), the frames it is stacked from, its exposure, the masters a flat is calibrated with and their anchors, its stacking settings, its level and noise per channel with the share of the frame they were measured over, its picture, and the masters whose lights it calibrates |
| `ReportLightGroup` | The lights of one master that share a calibration: the master they are stacked into, how many they are, the masters they take with their anchors, their calibration settings, or that they were marked as calibrated already |
| `ReportRegistrationGroup` | Key, anchor, lights and how many registered, the reference and who chose it, the transform and the kinds the registration recorded, the alignment's correction counts, the comet fits, the catalog sources kept for the colour calibration, its settings, the masters it serves |
| `ReportStage` | A stage as the run timed it: name, state, the run's own status word, time, the groups it works on |
| `ReportDecision` | A setting the application decided: what, for which group, what was asked, what it resolved to, why |
| `ReportTarget` | A target's lights, exposure, filters, masters and the mean of what its masters measured |
| `ReportIssue` | Kind, severity, title, detail, what to do where there is something true to say, the group it concerns and the anchor of the place that shows it |
| `ReportNotStated` | Something the report could not state, and why |
| `ReportComparison` | The run's totals beside those of the most recent earlier run of the folder that completed |

The model states more than the Markdown report does: each light's fate, the lights the user left out, the issues,
the spread of a master's samples, the calibration masters' levels and pictures, a master's detail, the lineage and
what it could not state. It defines two things nothing fills yet, each waiting for the comparison with earlier
runs: a master's baseline in an earlier run, and the runs of the folder.

### What the builder reads through

`ReportSources.h` holds one interface per thing a report reads outside the manifest and the result, so a suite can
stand in for any of them:

| Source | Answers |
| --- | --- |
| `IReportMetrics` | A registration group's latest settings version, its lights' metrics and noise at a version, the reference it brought them to, the comet fits and the matched catalog sources |
| `IReportHeaders` | A file's header, read without its pixels |
| `IReportImages` | One read of an image, measured per channel with the spread of its samples; or measured for its level and noise alone |
| `IReportFiles` | Whether a file is there, its size, when it changed, a text file's contents, the folders inside a folder |
| `IReportHost` | The time, the time zone and the platform |

`ProductionReportSources` holds the product's implementations: the metrics database opened once and closed with the
object, headers probed and images measured in the engine session it is given, the host's files and clock. **The
caller names the metrics database**, not the builder: `committedRunMetricsDatabase(manifest)`, the output folder's,
as the run being committed left it.

The builder asks for each image and each header once however many facts need it (`ReportBuild`); a case counts the
reads and finds one per master and one per calibration master. A file that is not there is not probed.

### How it is built

`buildReportModel` calls one function per area, in an order that matters: the lights and the settings are stated on
the masters and the groups, the lineage joins what three areas state, the run's rollups read what the masters
measured, and the issues read everything.

| Unit (`reporting/facts/`) | States |
| --- | --- |
| `MasterFacts.cpp` | The masters: state, file, measurement and spread, the solution's facts, stack, maps, pictures; a reused master from the run that made it |
| `FrameFacts.cpp` | Each master's lights: when they were taken, what their headers and their registration state, how they were weighed, what became of them, their photometry |
| `CalibrationFacts.cpp` | The calibration masters with their levels; the groups of lights and the lineage (`fillLineage`) |
| `RegistrationFacts.cpp` | The registration groups |
| `SettingsFacts.cpp` | Every group's settings, the run's own, and the decisions |
| `RunFacts.cpp` | The run: outcome, totals, stages, timings, counters, memory, nights, acquisition, equipment, headers, provenance, targets; what was written to disk (`fillDisk`) |
| `ComparisonFacts.cpp` | The totals of this run and of the earlier one |
| `IssueFacts.cpp` | The issues, and the outcome they settle |

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
- **A master's state comes from the run's records, never from the file.** A result that succeeded is `Made`, one
  that did not is `Failed`. A planned group with no result is `Reused` when the run's record of the groups it
  reused names it, however the run ended, and otherwise `NotReached`, or `Cancelled` in a run the user stopped. A
  group with neither in a run that completed is `NotReached` too, and named among what is not stated: a master
  file being there says nothing of what the run did.
- **A result that is no group of the manifest is a master of its own.** The combined colour image is keyed by
  `combinedRgbGroupKey` of its registration group; it has a file, a measurement and no lights of its own.
- **The outcome is one of five, and a run that completed is held to its issues.** A run the user stopped is
  `Cancelled`. A run that failed is `Partial` when it made a master, since those are kept, and `Failed` otherwise.
  A run that completed is `Partial` when a group failed inside it or one of its issues is critical,
  `CompletedWithWarnings` when one is a warning, and `Completed` otherwise: notes alone leave a run clean.
- **The lights are in the order of the group's lights, and the lights left out follow them.** The stack's records
  are in the stack's own order, so a weight is matched to its light by the light's identity, never by its place;
  the places a restack names are among the group's lights, so a light the user left out is appended after them. A
  recorded weight is shown relative to the heaviest light, and a photometric one at its mean of one. A stack that
  recorded none states none.
- **The lights taken are the groups' lights and the ones the user left out.** `ReportRun::frames` and the captured
  time count both; a light left out of a group that does not exist is listed under that group's key in
  `groupsNotStacked`. The share of the lights the masters combine is over the lights offered to a stack, so a
  light the user left out is no part of it.
- **No record of an earlier version is read, and no run is reported twice.** A model is built of a run once, as it
  is committed, from what this version of the application records; a run read later is read from the report its
  archive keeps. Nothing stands in for a record a run did not write: a stack that recorded no weights states no
  light's weight or fate, a result that names no map states no map, and a stored report of any format but this
  version's is refused by name. An archive written before the report was stored has no report the application can
  build.
- **Anchors and folders are not derived from each other.** A master's `slug` comes from `ReportSlugs`; a
  registration group's and a calibration master's are made from the key with their kind before it, since their keys
  may be spelled as a group of lights is. A master's `previewFolder` is the name the archiver gives the folder it
  writes, hash and all.
- **The words are the report's own.** Names of settings, of counters and of outcomes go through
  `QCoreApplication::translate` under the context `ImageIntegrationReport`. No translation ships: a report reads in
  English.

Where the model and the Markdown differ, the model is the one that follows the records. The Markdown counts every
group with no result as reused, a run that stopped included, and counts a combined colour image among the masters
made but not among the groups; it counts the lights of the groups alone, without the ones the user left out; and it
prints "n/a" for the figures it reads from the log.

### The stored document

`ReportModelJson::toJson` and `fromJson` write and read the model field for field: each structure's fields are
named once, in one function a writer and a reader both walk, so the two cannot disagree about a field.

- A value that is not stated is left out, and reads back as not stated; a stated zero is written.
- A key the reader does not know is ignored, so a field added later does not break a reader of the same format.
- A document of any format but the one this version writes is refused by name: `fromJson` returns no model and a
  `ReportNotStated` saying which format it met and which it reads. A document that states no format is refused the
  same way. No earlier format is read.
- A number that is not finite is written as text, since JSON has no form for it; a time is written with its offset
  from UTC, to the millisecond; an enumerator is written by name.

## Where each fact comes from

| Fact | Source | When the source is absent |
| --- | --- | --- |
| Lights taken, per group | The integration group's `lightFiles`, then the manifest's `leftOutLights` under the key they would have joined; a light left out of a group that does not exist is listed under that key in `groupsNotStacked` | - |
| A master's state | `Made`: a result that succeeded. `Failed`: one that did not. `Reused`: named in `Result::reusedIntegrationGroups`. `NotReached`, `Cancelled`: any other planned group with no result | A group with neither a result nor that record in a run that completed: `NotReached`, and named |
| A light's state | "A light's states", below | No metrics database: a light whose registration failed has no state, and the model says so |
| A master's pictures | `report_previews/<folder>/master.jpg` and `detail.jpg` in the run's folder, the folder named by `archiveGroupFolderNames` | A picture the archive does not hold is not stated |
| A calibration master's picture | `report_previews/calibration/<name>` in the run's folder, named by `archivedCalibrationPreviews` | Not stated |
| The metrics | The database the caller names | No per-light metric; the model says so once |
| Capture time and night | The registration group's `lightCaptureTimestampsUtc`; else the start the light's header states. A night is the local date twelve hours before the capture, in the zone the archive states its times in | The light is on no night, and the model says how many are not |
| Stars, FWHM, SNR, eccentricity, background noise, zero point, RMS, limiting magnitude, matches | `registration_metrics`, at the group's latest settings version | Not stated |
| Shift and transform kind | `transform_json`: the kind, and the translation of its matrix | A polynomial kind states the kind only: its matrix is not the whole transform |
| Weight | `StackQualityRecord::lightWeights`, by the light's identity | Not stated: nothing is derived from the settings in its place |
| Rejected fraction | `frameRejectionRates[i]` belongs to the light `lightWeights[i]` names, when the two lists have one length. A light excluded on a restack takes its rate from `spatialRestackExcludedFrameRates`, whose places are among the group's lights | The lengths differ: no rate is given to any light, and the model says so |
| Normalization scale and offset | The group's `coefficients.json`, when it holds one match per light | A local normalization, which gives a light a surface: not stated |
| Gain, offset, temperature | The light's header, in the one probe each light gets; the temperature falls back to the one the file's name states | Not stated, and counted as absent in what the headers state |
| A master's statistics and the spread of its samples | One read of the master in one admitted operation | No session, or the file is gone: not measured, and named |
| A reused master's figures, stack and lights | The stored report of the latest earlier run that made it, marked by `madeByRun`; its pictures are this run's own copies | That run's report is not kept: the statistics and the spread from one read of the file, the rest named as not stated |
| A master's solution facts | The solution in the master's header, through `an::ui::readImageWcsSolution` | No header read or no solution that reads: the outcome and its detail only |
| A master's photometry | The medians of what the registration recorded of the lights it combines | Not stated |
| A calibration master's level and noise | The stored report of the latest earlier run that states them for the same file, size and modification time; else one read of the file, measured over the centre of a large frame | Not measured, and named |
| A registration group's reference | The manifest's; else the registration's own group state | Not stated |
| A registration group's overlap | The smallest share of the capture's area its masters keep, since they share its output plane | Not stated |
| Stage states and times | `Result::stageResults`, in the order of the manifest's `stageSequence`. A stage with no result was not reached in a run that stopped; in a run that completed it was reused when the run is incremental and the stage has groups, and skipped otherwise | - |
| Engine memory, cache figures | `Result::engineMemory`, `Result::workflowCacheDiagnostics` | Not stated |
| Written to disk | Masters: their sizes. Maps and calibration masters the run made: their files. Registered lights: the size the registration recorded of each whose file is there | A kind with nothing is not listed |

No fact comes from `Result::capturedLog`. The log is printed, never parsed: a case builds each scenario's model with
its log and with another and finds the two equal.

### A light's states

A light has one state. The first rule that applies decides (`FrameFacts.cpp`):

1. `ExcludedByUser`, `StackExcludedByUser`: the manifest's `leftOutLights` names it. A light left out of the stack
   alone was still registered, and states what its registration recorded; one excluded in the lights' list states
   what its header does.
2. `RegistrationFailed`: its registration record says it did not succeed. Such a light is still in its group's
   lights; its detail states the stars the detection found. No reason is stored, and storing one would change the
   metrics database's format, which makes a folder's database unreadable to the version before.
3. `LeftOutByStack`: the group's result names it among the lights the stack dropped; the detail gives the reason.
4. `ExcludedOnRestack`: its place among the group's lights is one a second stack was made without.
5. `NotStacked`: its master failed, was not reached or was cancelled.
6. `Used`: the stack's weights name it, and nothing else makes a light used. Of a stack that recorded no weights
   only the failed registrations and the dropped lights are stated, and the model says so.

A light of a master the run made that none of these names has no state: nothing is inferred of it, and the model
says how many such lights the master has. The lights the model counts as used are checked against the count the run
recorded for the stack; a difference is named among what is not stated. A light that is not used has no weight and
no share. The lights of a reused master take their fates from the stored report of the run that made it, and have
none when that report is not kept, except a failed registration, which the metrics still state, and a light the
user left out, which the manifest does.

### The spread of a master's samples

One call of `computeImageHistogram` per channel counts the samples the image's validity admits into 65,536 linear
bins over the display range: a bin per code of a sixteen-bit image, and narrower than the narrowest bin of the
result. The counts are folded by each bin's centre into 96 bins of equal width on a logarithmic axis, from one
thousandth of the range to its top. The samples under a thousandth, those under the range among them, are counted
in the first bin; the ones above the range, and those that are not a number, are counted beside the bins
(`outside`), so the bins and that count add up to the samples admitted. The count is in the same admitted operation
as the statistics, over the image as it is already in memory.

### The issues

Each kind is computed from what the model states (`IssueFacts.cpp`); no message of the run is matched to a kind, so
a fact may appear once as a kind and once in the run's words. The computed kinds come first, the most severe first
and each severity in the order below; then every string of `Result::errors` and `Result::warnings`, as written.

| Kind | Raised when | Severity |
| --- | --- | --- |
| `RunFailed`, `RunCancelled` | The run did not succeed, or was cancelled. The action says what was kept and what a new run makes | Critical |
| `MasterFailed` | A group result that did not succeed; the detail is the finalization phase it failed in | Critical |
| `MasterNotReached` | A planned group the run has neither a result for nor a record of reusing | Critical |
| `OutputNotRetained` | The archive could not write its copy of a master | Warning |
| `LightsWithoutFlat`, `LightsWithoutDark` | A group of lights, not pre-calibrated, that takes no flat, or neither a dark nor a bias, while another group of the run takes one | Warning |
| `NoFlats`, `NoDarks` | No group of lights takes one: a choice, not an oversight | Note |
| `FlatMasterUncalibrated` | A flat the run stacked or reused whose frames take neither a bias nor a dark | Warning |
| `CosmeticCorrectionWithoutDark` | Cosmetic correction is on for lights that take no dark, so nothing is corrected | Warning |
| `RegistrationFailures` | A registration group with lights whose registration failed | Warning |
| `MasterWithoutAstrometry` | A master the run made and saved without a solution | Warning |
| `LowFrameUse` | The lights the masters combine over the lights offered to the stacks of the masters the run made - the ones the user left out are not - under 80 %; worded more strongly under 60 % | Warning |
| `RegistrationSuccessLow` | The lights registered over the lights of the registration groups with a record, under 90 % (75 %) | Warning |
| `LightsLeftOutByStack` | A master with lights in the state `LeftOutByStack`; the detail gives each reason with its lights | Warning |
| `ExposureMetadataIncomplete` | The lights that state an exposure, under 95 % (80 %) | Warning |
| `StackedTwice` | A master whose restack was applied | Note |
| `MasterReused`, `CalibrationMasterReused` | A master the run's record of reused groups names, or a calibration master that was still valid | Note |
| `PreCalibratedLights` | A group of lights marked as calibrated already | Note |
| `OutputNotRetainedByPolicy` | The history had no room for a copy of a master | Note |
| `RunMessage` | Every warning and error of the run | Warning, Critical |

The three ratios are the thresholds the report always held a run to, on figures the records define: the Markdown
report read them out of a log line the stage no longer writes.

### What a model costs

Reading a master is quick; measuring it is not. On a run of 714 lights, four masters of 8,288 by 5,644 pixels and
twelve calibration masters of 187 MB each, reading an image took about 45 ms and its full statistics 0.7 to 1.3
seconds. So a calibration master is measured for its level and its noise alone, and a frame of more than four
million pixels over a rectangle at its centre of the frame's own shape (`measureReportImageLevels`); the model
states the share of the frame measured beside the figures. On that run the model builds in 5.1 seconds, 3.2 of them
the four masters' statistics and 1.6 the twelve calibration masters, where measuring the calibration masters whole
took 11.6 seconds on their own. A calibration master an earlier run of the folder measured is not read again while
its file is the same, once that run's archive keeps its report.

`ReportRealRun.WhatBuildingTheModelOfARealRunCosts` prints these figures for the run whose folder under `history/`
the variable `AFTERNIGHT_REPORT_REAL_RUN` names, and is skipped without it.

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

## The charts

`ReportCharts.h` draws the report's charts: a function per chart, from plain series to one inline `svg` element.
The functions read no run and no report model - a caller hands each the values, the texts and the tips it is to
draw - so a chart can be drawn, tested and looked at on its own. The document's summary draws six of them ("The
document"); the report a run writes is still the Markdown one, which has none.

| Function | Draws |
| --- | --- |
| `conditionsChart` | Each measure of every light against the time it was taken: a row of panels per measure, a column per night on one clock axis, a band of time per group. Past four nights, or when a light states no time, one column in capture order (`conditionsLayout` says which). |
| `segmentedBar` | The lights of a run as one bar, a segment per state that has lights. |
| `rangeChart` | The range, the middle half and the median of one measure, a row per group (`chartRange` gives the five figures). |
| `scatterChart` | One measure against another, a mark per light. |
| `distributionChart` | How many lights lie in each range of a measure (`chartBins` counts them). |
| `frameMeasureChart` | Several measures over one master's lights in capture order, in one SVG: a layer per measure and one set of hover bands for all of them. |
| `ditherChart` | Where each light sat against its registration reference, on a square plot, a shape per night. |
| `sampleDistributionChart` | The spread of a master's samples on a logarithmic axis, a panel per channel, from the model's `ReportSampleDistribution`. |
| `coverageBar` | The least, the median and the most lights per pixel, on one bar from zero. |
| `cellBar` | A figure's share of its best or of its ideal, small enough for a table cell. |
| `timelineChart` | The stages of a run on one time axis, each with its state's icon. |
| `barsChart` | Plain bars from one baseline, a value at each bar's end. |
| `runsChart` | One figure over the runs of an output folder, a column per run. |
| `chartLegend` | What a chart's marks mean. The one function that returns HTML: a legend must wrap as text does. |

The rules every chart keeps, and why:

- **A chart is drawn when the report is written.** The document then shows it with no script and from no network,
  and a report sent to someone else is the same report.
- **A chart says no word of its own.** Titles, labels and tips arrive in its input, so the report's words are made
  in one place, in one translation context. The only texts a chart makes are the numbers on its axes.
- **A chart names no colour.** Its marks carry classes and the report's stylesheet colours them, so one document
  reads in the light theme, the dark one and in print. The markup holds no colour literal and no `style`
  attribute; a case fails on either.
- **A state is never told by colour alone.** A light the stacks left out is a cross whose class names its state
  (`s-user`, `s-registration`, `s-stack`, `s-restack`, `s-notstacked`), a night of a dither chart is one of four
  shapes as well as one of two colours, and a stage's state is an icon of its own shape.
- **Ticks are round numbers that span the data** (`chartTicks`): multiples of 1, 2, 2.5 or 5 times a power of ten,
  from the last at or under the lowest value to the first at or over the highest. A count's axis is whole numbers,
  a clock's whole hours, the timeline's the round times a clock is read in.
- **A value that is not a number is left out, never drawn at zero.** It breaks a line, has no mark, and takes no
  part in a scale. A light left out that states no value of a measure still carries its cross, on the axis, so it
  is seen at its time.
- **Nothing to draw is said.** A chart with no value is a short SVG of class `is-empty` holding the sentence its
  description gives for that case; a measure no light states is a layer that says the same.
- **A tip is one attribute.** An element a reader can point at carries `data-tip`: the heading, then the lines,
  separated by line feeds. A chart of lights has one transparent band or circle per light for it; a bar, a row, a
  bin and a column can also be reached with the keyboard (`tabindex`).
- **A dense chart keeps its markup small.** With more than 300 lights (`kChartDenseAbove`) a chart draws its lines
  and its crosses and neither a mark nor a hover band per light; a scatter and a dither chart draw all the marks of
  a series as one path. A cross then carries its light's tip itself. The values are in the report's tables either
  way.
- **The markup is well-formed XML that is valid inside an HTML document**, which is how the suite reads it back:
  attributes quoted, text escaped, a character XML forbids replaced by U+FFFD. An SVG element with no content is
  closed in its own tag, which HTML allows inside `svg` and nowhere else.
- **The same input is the same markup.** No counter and no clock is read. The one id a chart defines, the gradient
  under a master's lines, is named after the id its caller gives it (the master's slug), so a report rendered again
  from its model does not change and two charts of a document never share an id.
- **A width is the size the chart is meant to be read at.** Lengths and font sizes are in the units of the
  `viewBox`, so a chart shown narrower than its width shrinks its text with it. A wide chart belongs in a container
  that scrolls sideways on a phone instead.

The frame measure chart is one SVG so that a master's lights are written once: layer `n` is a group of class
`c-layer c-layer-n` with its own axis, and the hover bands, whose tips state every measure of a light, are shared.
The stylesheet shows one layer at a time; a radio button per layer switches between them with no script, and print
shows the first.

### Looking at the charts

`ReportChartSamples.h` holds series for every chart - the lights of an invented session of five filters over two
nights, with every state a light can end in - and a page that shows each chart on them, with the cases a report
meets: one night, six nights, 2,000 lights, a colour master, a run that stopped, nothing to draw. The page is
written by an opt-in case:

```text
set AFTERNIGHT_WRITE_REPORT_SAMPLES=<folder>
Test_ImageIntegration.exe --gtest_filter=ReportChartSamples.*
```

It writes `report_charts.html`, which opens from disk, follows the reader's theme and takes `?theme=light` or
`?theme=dark`. The page carries the report's own stylesheet and script, so a chart looks and answers there as it
does in a report. The cases that hold every class a chart emits to a rule of that stylesheet, and its colours to
their contrast, are the document's ("The style").

## The document

`ReportHtmlWriter.h` writes a report as one HTML file from its model: `writeReportDocument(model, options)` returns
the document, and `writeWorkflowReport` (`WorkflowReport.h`) writes it to a file. No run calls either yet: the report
a run writes is still the Markdown one, and the document is written by the suites, which is how it can be looked at
before it replaces anything. It has the header and the first of its six parts; the others are not written yet, so
nothing links to them.

| Unit (`reporting/`) | Holds |
| --- | --- |
| `ReportHtmlWriter.h/.cpp` | `ReportMarkup`, the one way markup is written; the document's shell, its contents and its footer; the fragments the parts share; the two assets as the document carries them |
| `ReportHtmlParts.h` | Private to the reporting units: what a part is, what every part is written from, the fragments' declarations, and `documentText`, the one way a word of the document is made |
| `ReportHtmlSummary.cpp` | The header, and part 1: `outcome`, `attention`, `made`, `frames`, `nights`, `quality` |
| `ReportAssets.h/.cpp` | What a document of each form writes for a picture, a path and a message of the run |
| `WorkflowReport.h/.cpp` | The document as a file, in either form |
| `assets/report.css`, `report.js`, `Report.qrc` | The style and the script, compiled into the library as a Qt resource |

### The markup is XML and HTML at once

The document is written so that an XML parser reads it and a browser shows it. That is what lets the suite read a
report back with `QXmlStreamReader` and assert on its structure - an id that exists once, a link that lands, a
table's cells against the model - instead of matching substrings of its bytes, which is all the Markdown report's
suite could do. The XML parse proves half of it; the rules below and the browsers of the render tool prove the
other.

`ReportMarkup` keeps the rules, so no part can break them by hand: a part opens and closes elements by name and
hands over text, and what the rules forbid is refused and named in the document's `faults` rather than written.

- **Every element has an end tag, except the void ones** (`br`, `hr`, `img`, `input`, `meta`, `link`, `col`), which
  are closed in their own tag. No other element is: HTML reads `<div/>` as a `div` left open. The one exception is
  inside an `svg`, where HTML follows XML, and where the charts and the icons close an element with no content in
  its own tag: an end tag for every mark would cost about a twentieth of a report.
- **Attributes are quoted and escaped, and so is text.** The five characters XML reserves are written as
  references, and no other named entity is written: an XML parser knows none. In an attribute a tab and a line
  break are references too, since a parser would read each as a space and a tip's lines are separated by line
  feeds.
- **A character XML forbids is replaced by U+FFFD**: a control character other than a tab and a line break, and
  half of a surrogate pair. A file's name, a header's value and a line of a log can hold any of them.
- **Nothing a run wrote goes inside `style`, `script` or a CDATA section.** The style and the script are the
  report's own two assets, each wrapped as CDATA inside comments so both parsers take it whole; a case holds the two
  free of `]]>`, `</script`, `</style` and `<!--`. A run's words are text and attribute values, nowhere else.
- **An element's name and an attribute's are plain**: lower-case letters and digits, with hyphens inside an
  attribute's. A name that is not is refused, so a name can never be made of a run's words.
- **An attribute with a null value is left out; one with an empty value is written.** That is how a part states an
  attribute only sometimes (`href` on a card that has somewhere to lead), and still writes `alt=""`.

A chart and a legend are taken as `ReportCharts` returned them (`ReportMarkup::chart`), since those functions keep
the same rules; nothing else is handed to that function.

### Parts, blocks and anchors

A part is one unit's work: it states the anchors it will write, then writes its `section` and names its entries of
the contents. The anchors are stated first, for the whole document, because a link is made only to a place the
document has: an issue of the summary points at a master's file only when that file is written, and until then the
master's name stands there as text. So a document never holds a link that leads nowhere, whichever parts exist.

- **A block that has nothing to say is absent, and so is its entry in the contents.** `outcome` is there for a run
  that stopped and for no other; `made` for a run of more than one master; `nights` when a light states a measure
  to draw; `quality` when one states its sharpness or its weight. The contents are made from the parts and blocks
  that are present, so every entry lands.
- **A run of one master has no gallery and no table comparing masters.** Its picture stands in the header beside
  the figures, and the header's third figure is that master's noise, where a run of several counts its masters.
- **Several targets are grouped by target**: the title says how many, and the gallery and the comparison table
  give each target its own line of lights and integration with its masters under it.
- **A run that stopped leads with what happened**: what was kept, what was not made and why, and what to do, which
  is the action of the issue that says how it stopped. Only a run that made a master is archived, so there is
  always something kept.
- **The issues are in the model's order**, the computed kinds first and the most severe first, each with its
  action when it has one. The run's own warnings and errors follow under their own heading, word for word, and are
  folded away when there are more than five. A run with nothing to attend to says so.
- **A reason with no light is listed nowhere.** The table of why lights were left out has a row per state that has
  a light, and the bar's legend an entry per segment.
- **A status is never told by colour alone.** A pill is an icon and a word; a light's state in a table is a swatch
  and its name; a chart marks a light left out with a cross.
- **The same model is the same document.** Nothing reads a clock or a counter, so a report written again from its
  stored model does not change.

The ids a suite and the contents name: `top`; `summary` with `outcome`, `attention`, `made`, `frames`, `nights`
and `quality`. An issue's anchor may also be `file-<slug>`, `cal` or `registration`, which the later parts write.

### The words

Every word the document says is made by `documentText` or `counted`, under the translation context
`ImageIntegrationReport`, the one the model's words are made under. No translation ships: a report reads in
English. A sentence handed to `translate` is kept to ASCII, with a sign such as the multiplication sign put in as
an argument, so it does not depend on how a compiler reads a narrow literal. The charts say no word of their own:
the writer gives each its title, its labels, its tips and the sentence it holds when it has nothing to draw.

### The two forms

One writer writes two forms, which differ only in what `ReportAssets` answers:

- **Linked**, the one a run keeps: `workflow_report.html` in the run's folder, naming each picture by its path
  under that folder. The pictures are used as the archive holds them.
- **Embedded**, the one a person sends: one file with every picture inside it as a `data:` address, each read once,
  reduced so its longer side is at most 1,280 pixels and written again as a JPEG of quality 82
  (`boundedJpeg`). A path is printed as a file's name without its folders, and in a message of the run the output
  folder's path is replaced by the folder's name and the folder of every file the model names is dropped, in
  either spelling of the separator. A folder is noise to a stranger and says more than its owner may want. A
  picture the embedded form cannot read is left out: it never names a file outside itself.

Two forms rather than one because a run's own report should stay small and keep its full-size pictures one click
away, and a report meant to be sent must open from an empty folder. The embedded form is an action on a run, not a
setting: it works for a run archived earlier, and it is never written into `history/`, whose size is bounded.

A case holds the linked form to pictures that are in the run's folder, the embedded one to no `src` but a `data:`
address and no `href` but one inside the document, and both to their sizes: the linked form within 400 KB and
2.5 KB a light, the embedded one within that and 1 MB a master.

### The style

`assets/report.css` is carried inside every report.

- **The neutrals, the accent and the status colours are the user guide's**
  (`docs/user-guide/theme/assets/guide/css/tokens.css`), which takes them from the application's design tokens, so
  one copy serves both and a report looks like the application and the website. A case reads the guide's file and
  fails when a token the two share differs. A report does not follow an accent the user picked in the application:
  it is read away from it.
- **Dark is the default; a reader who prefers light gets light; the toggle wins over both; print is always
  light.** The light values are therefore stated three times - for the preference, for the choice, for print - and
  the same case holds the three equal.
- **The fonts are the system's.** Embedding the guide's three families would add about 300 KB to every report.
- **The chart colours are the report's own.** The two series are told apart by a reader with a colour vision
  deficiency. A case holds what a mark is drawn in (the two series, the status colours, the grey of a light the
  user left out) to 3:1 or more on a card and on the page in both themes, and what text is written in to 4.5:1 or
  more on those and on a tile. Another holds every class the document and the charts emit to a rule.
- **The layout gives way in steps.** The contents stand beside the page from 1,181 pixels; the grids fall to one
  column under 900; every grid child has `min-width: 0`, without which a table widens a phone's layout. A chart or
  a table wider than its card scrolls sideways inside it instead of shrinking; the container can be reached with
  the keyboard and says what it holds.
- **It loads nothing**: no `url(`, no `@import`, no font face. A case holds that.

### The script

`assets/report.js` shows the tip of what the reader points at or reaches with the keyboard, switches and
remembers the theme, sorts a table of class `sortable` by a column's `data-v`, opens a folded section a link leads
into or that is about to be printed, and moves a comparison's divider. It computes no figure of the report: every
number is written when the report is, so the same report is read with scripts off, where only those conveniences
are lost, and so no product logic runs in a browser. A case holds the file free of `fetch`, `XMLHttpRequest`,
`import`, `eval` and any address.

### Looking at a report

An opt-in case writes every scenario's report in both forms, with its stored model:

```text
set AFTERNIGHT_WRITE_REPORT_SAMPLES=<folder>
Test_ImageIntegration.exe --gtest_filter=ReportSamples.*
```

Each scenario gets a folder there, laid out as a run's archive is: `workflow_report.html` beside the
`report_previews/` it names, `workflow_report.json`, and `workflow_report.single.html`, the embedded form. The
linked report opens from that folder; it takes `?theme=light` or `?theme=dark`.

`build/scripts/render_integration_report.cjs` opens those samples in real browsers, as
`render_user_guide.cjs` opens the guide, with the same Node packages and browsers outside the repository (that
script's header says how they are set up):

```text
set NODE_PATH=%TEMP%\afternight-guide-render\node_modules
node build/scripts/render_integration_report.cjs --samples <folder> [--shots <folder>]
```

It opens each scenario's linked report where it lies and its embedded one copied alone into an empty folder, from
`file://` with the network blocked, in Chrome and Firefox, at 1,440, 1,024 and 390 pixels, in both themes, and
fails a load on: a request that leaves the file, a console or page error, a picture that did not load, a page
wider than its window, content that spills out of its card, a link that leads nowhere, a contents entry that does
not land, a theme toggle, a measure switch, a sort, a divider or a link into a folded section that does nothing, a
tip that does not show, a serious or critical axe-core finding, and a print render that is not light, shows the
contents or leaves a section folded. `--shots` writes a picture of every load and, from Chromium, a PDF of the
print render. The suite proves the document's structure; this proves what a reader sees.

To add a block to a part: write it through `ReportMarkup` in the part's unit, state its anchor among the part's
anchors under the same condition it is written under, name it in the part's entries, give every class it uses a
rule in `report.css`, and hold it in `ReportHtmlWriterTest.cpp`, for every scenario where that is cheap.

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
- `ReportChartsTest.cpp` - each chart, read back from the SVG it returns: a mark per value, a cross per light left
  out, a tip per mark, round ticks that span the data, and what a chart does with nothing, one value, equal values
  and a value that is not a number. Its series are in `ReportChartSamples.h` ("Looking at the charts").
- `ReportHtmlWriterTest.cpp` - the document, read back as XML (`ReportDocumentTestSupport.h`): well-formed for
  every scenario in both forms, each block there exactly when the run calls for it, every link landing, the
  header's figures and the issues against the model, what a run wrote coming out as text whatever it holds, the
  two forms' pictures and paths, and the style and the script held to their rules.
- `ReportSamplesTest.cpp` - writes every scenario's report where a person or the render tool can open it; skipped
  unless asked ("Looking at a report").

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

A scenario states only what a run records: the lights the user left out are in its manifest's `leftOutLights`, a
dropped light on its group's result, a reused group in the result's `reusedIntegrationGroups`. The lights left out
are also listed beside the scenario (`ReportScenario::lightsLeftOutByUser`), so a case holds the manifest and the
report to them.

To add a scenario, add its enumerator and its name, describe the run in `buildReportScenario` with the builder's
`addBias`, `addDark`, `addFlat`, `addRegistrationGroup` and `addLightGroup`, and give it a case in
`ReportScenariosTest.cpp` that states what it is for. A group that a later run of the same folder must find
unchanged takes a slot of the night of its own (`LightGroupPlan::nightStartMinutes`), so its lights have the same
names and times whatever the other groups take.
