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
| Whether | `runHasWorkflowReport` (`WorkflowArchiver.h`) | A registration-only run or scope gets no report. |
| Previews | `WorkflowArchiver::commit` | `report_previews/<group>/master.jpg` and `detail.jpg` from the pictures the stacking stage prepared from the master it held in memory (`IntegrationGroupResult::reportPreviewJpeg`, `reportDetailJpeg`); when the stage did not try to prepare one and the archiver has a session, one read of the file gives both. The rejection and quality maps get `rejection_map_N.jpg` and `quality_map_N.jpg`. A master the run reused takes its two pictures from the archive of the run that made it, or from one read of its file; each calibration master gets `report_previews/calibration/<master>.jpg`. See "The pictures". |
| Build | `buildReportModel` (`ReportModelBuilder.h`), called by `WorkflowArchiver::commit` | The report model is built once, here, through `ProductionReportSources`: the output folder's `registration_metrics.db`, each light's header, one read of each master, the session notes and the reports the earlier runs' archives keep. |
| Store | `storeWorkflowReport` (`WorkflowReport.h`) | The pictures of the earlier runs the masters are compared with are copied into the run's folder, then the model is written as `workflow_report.json` and the document from it as `workflow_report.html`. The archiver keeps the model. |
| Finish | `WorkflowArchiver::finalizeTimingArtifacts`, `finalizeLogArtifact` | The executor calls the first after the commit, once the run's final timings, its final counters and the engine's memory figures exist, and the second with the final log. Each states what it brings on the kept model and stores the report again ("The figures are final because the report is stored again"). |
| Open | `WorkflowProgressDialogV2`, `WorkflowHistoryModel` | The progress dialog opens `Result::htmlReportPath`, which is `CommitOutcome::reportPath`; the history list offers `workflow_report.html`. |
| Write again | `SummaryTab::exportReportRequested`, `ImageIntegrationProcess::showRunReport` | The history row's report action writes the document again from the model the archive keeps, on the thread pool, and opens it (`rerenderWorkflowReport`). |
| Save as one file | `SummaryTab::saveReportAsOneFileRequested`, `ImageIntegrationProcess::saveReportAsOneFile` | The history row's Save As action asks for a path and writes the embedded form there from the model the archive keeps, on the thread pool (`saveWorkflowReportAsOneFile`). |

A run's archive holds `workflow_manifest.json` (the manifest, the stage, calibration, alignment and integration group
results, the groups the run reused, the timings, the counters, the warnings and errors), `workflow_report.json`,
`workflow_report.html`, `workflow_log.txt`, a snapshot of `registration_metrics.db` when the run's payloads are
retained, `groups/<group>/` (a summary, a thumbnail, a copy of the master and the one it replaced),
`report_previews/<group>/` and `report_previews/calibration/`. No Markdown is written: the model's JSON is the
companion that states the report as data. The document is written as HTML directly, so no report is rendered from
Markdown and the product links no Markdown library: one library less to build, deploy and credit.

The history keeps 20 runs and 5 GiB (`ImageIntegrationConstants.h`), dropping the runs that started first; a run
whose masters do not fit is archived without them, and each master's result says so (`OutputRetentionStatus`).
That decision is taken before the
manifest, the log and the report are written, so that a payload is never copied into the history and then found
not to fit. The pictures are written before the run's size is measured, so the decision counts them as they are;
what is written afterwards it bounds. `estimatedArchiveMetadataBytes` counts:

- the manifest's, the results' and the log's own bytes, each three times, since the model and the document state
  the run's words once more each, with a fixed allowance for the documents' own structure, for each result and for
  each stage;
- for the report, `kReportArchiveBaseBytes`, `kReportArchiveBytesPerLight` for each light and
  `kReportArchiveBytesPerMaster` for each master, which are twice what the largest reports the suites write
  measure: 965 KiB for a run of 174 lights and six masters, 3.0 MiB for a run of 2,000 lights in one group.

The commit adds to that, for each master the run made, the largest pair of pictures an earlier run of the folder
keeps of it (`earlierPicturesBound`), since the report copies the pair of the run each master is compared with.

`WorkflowArchiverReport.TheBoundOnWhatAnArchiveWritesLastHoldsWhatEveryScenarioWrites` holds the estimate above
what the archive of every scenario the executor archives then holds of those files, and prints both.

### The figures are final because the report is stored again

A run knows three things only after its archive is committed: the executor's own phases and its total, which
include the time the archive and the report themselves took; the diagnostics as they stood at the end, with what the
engine counted of its memory; and the log to its last line. So the archiver keeps the model it built, and the two
final steps state those on it and store the report again - `restateRunCompletion` and `restateRunArchive` after the
commit, `restateRunLog` last (`ReportModelBuilder.h`). `restateRunArchive` adds what the run's own folder in the
history holds on disk, which is whole only then. Nothing else of the model changes: it is built of a run once.

A report is therefore never a text with figures put into it afterwards, and the stored model, the document and
the manifest state the same final figures. Storing again is cheap, since it reads no image: a picture already
copied is left as it is, and stands when the history has dropped the run it came from in between.

A report that cannot be written does not fail a commit: the run's outputs and its history are what a commit is
for. The reason is logged, the commit names no report, and the final steps write what they can.

### A report written again, and saved as one file

After the commit a report is only ever written from the model the archive keeps. `rerenderWorkflowReport(runDir)`
writes the linked document again, and `saveWorkflowReportAsOneFile(runDir, destination)` the embedded one, to a
path the person picks. Neither builds anything of a run. An archive that keeps no stored report, or one of another
format, is refused in the reader's words ("The stored document") and nothing is written; the window shows the
refusal in a message titled as the action, and for the report action opens the report file the folder already
holds, as it was written, if there is one. No archive and no record of an earlier version is read: an archive
written before the report was stored has a report file like any other file, and no report the application can
write.

A linked report links the report of each earlier run of its folder. The history drops its oldest runs, so a link
is made only to a report that is there when the document is written: `storeWorkflowReport` and
`rerenderWorkflowReport` take a link to a report that is gone out of the model they write from. The run stays
listed. A report opened from disk without being written again may still hold a link to a run dropped since.

The runs of a folder have one order, by name, which is when each started. The archiver trims the history and
looks for the run whose pictures a reused master takes in that order (`sortedHistoryRunDirectories`), and the
report model reads the earlier runs in it ("A run beside the earlier runs of its folder"). A folder's
modification time is no order of the runs: writing a run's report again replaces a file in its folder, and would
make the oldest run the newest. A case holds it:
`WorkflowArchiverReport.TheHistoryDropsTheRunThatStartedFirstWhicheverFolderWasWrittenLast`.

## What a report is built from

| Source | Holds |
| --- | --- |
| `WorkflowManifest` | The groups and their own settings: each bias, dark and flat group with its stacking settings; each light's calibration (the masters it takes and its `CalibrationSettings`); each registration group with its settings, its reference and its lights' exposures and capture times; each integration group with its lights, its stacking, normalization and drizzle settings and the registration group it takes frames from. Only the execution, the plate solve and the RGB combination are stated once for the run. The lights the user left out, which no group names, are listed apart (`leftOutLights`). |
| `WorkflowExecutor::Result` | The outcome, the warnings and errors as text, each stage's status and time, the calibration, alignment and integration group results, the integration groups the run reused (`reusedIntegrationGroups`), the log, the timings and, when collected, the counters, the cache diagnostics and the engine's memory. |
| `IntegrationGroupResult` | One per master the run attempted: the output, the lights combined, the lights the stack dropped with the reason of each (`droppedLights`), the solution's origin, the time and the finalization phases, the maps, and the stack's `StackQualityRecord`. A combined RGB output is one more result, keyed by `combinedRgbGroupKey`. |
| `registration_metrics.db` | Per light and settings version: the stars, the star width, the SNR, the eccentricity, the noise figures, the photometric and astrometric figures, whether registration succeeded, the registered output and the transform. Per registration group: the reference frame. |
| The files | Each light's header, each master's header and samples, the normalization coefficients, the session notes and the earlier runs under `history/`. Reading a header or an image's samples takes the engine session the report is given; without one the report states no figure that needs either. The coefficients, the notes and the earlier runs' stored reports are read as plain files. |

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
- **A calibration group has a result when its stage ran, and the result says what the stage did with it.** The
  executor records one for every group of a bias, dark or flat stage that was not skipped, naming the master's
  file, whether it is there, and whether the stage found it still valid and left it as it was
  (`CalibrationGroupResult::reused`, from `StageResult::reusedCalibrationGroups`); a stage whose every master is
  supplied or still valid is skipped whole and leaves none. The model reads a master as supplied from its group's
  `existingMaster`, as reused from the absence of a result or from a result that says so, and as made otherwise.
  Without that record a master left as it was would read as made whenever another group of its stage was stacked.

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
  archiver, when the stage did not try to and it has a session, makes both from one read of the file
  (`generateReportMasterPreviews`). A master whose pictures the stage tried and failed to make has none. A detail
  is kept only beside its whole frame.
- **A comparison needs both runs' pictures, and each run keeps its own.** A later report shows a master beside the
  one an earlier run made by the pictures each archive holds, the whole frame and the detail, rather than by
  reading and stretching the earlier master again: the history may not have kept that master's payload, and a
  picture of it is what the earlier run showed. So every archive holds the pictures of every master its report
  states - a reused master's included. For a master the run reused, the archiver copies the two pictures from the
  archive of the latest earlier run that made it, provided the master on disk is still the size that run recorded
  and that run kept the whole frame; otherwise it reads the file, when it has a session. The copies keep the
  report whole after the history drops the earlier run.
- **The earlier run's two pictures of a master the report compares are copied beside the master's own**, as
  `report_previews/<group>/previous_master.jpg` and `previous_detail.jpg`, when the run's report is stored
  (`storeWorkflowReport`). See "A run beside the earlier runs of its folder".
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
`buildReportModel(inputs, sources)` (`ReportModelBuilder.h`) fills one from a run. Everything a run writes of its
report is written from it. It exists because of what a report held only as text cannot give:

- **One structure, filled once.** A report that exists only as text gives its suites nothing to assert but
  substrings, needs its timings substituted into finished text, and makes a second output a second pass over that
  text. The model is filled once from the run's records; the document, the stored model and the suites all read it.
- **The model is a document of its own.** `ReportModelJson` writes it as the JSON a run's archive keeps as
  `workflow_report.json`, under the format number `kReportSchema`. A later run compares itself with an earlier one
  without reading an image again, the report is written again and saved as one file without building anything, and
  a suite asserts on structure instead of bytes.
- **Settings belong to groups.** The manifest holds a `StackingSettings` per calibration group and per integration
  group, a `RegistrationSettings` per registration group and a `CalibrationSettings` per light; only the
  execution, the plate solve and the colour combination are stated once. The model holds them the same way - on
  each `ReportCalibrationMaster`, `ReportRegistrationGroup` and `ReportMaster`, with `ReportRun::settings` for the
  rest - so "the same for every group" is something a writer finds, never something the model assumes.
- **No figure comes from the log.** A figure read out of the run's log by a regular expression changes silently
  when a log line does. The model carries the log as text (`ReportRun::log`) and reads it for nothing.
  `ReportModelBuilder.NoFigureIsTakenFromTheLog` builds every scenario with its log, with another and with none,
  and holds the three models equal but for the log.

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
| `ReportBaseline` | On a master the run made: the latest earlier run that made it and that run's figures of it - lights used and taken, integration, noise, signal to noise, median star width, effective frames, samples rejected - whether the two frames are of different shapes, and the copies of that run's two pictures; or that no earlier run made it |
| `ReportComparison` | The run against the latest earlier run of the folder that completed: both runs' totals, the settings that differ in the groups both have and how many were compared, the groups only one has, the frames each shared group gained and lost, and the earlier run's version and fingerprints |
| `ReportHistoryEntry` | One earlier run of the folder, oldest first: when it was archived, how it ended, its lights, integration and masters, the noise it stated of this run's first master, and its report as a link; or, for an archive that keeps no report this version reads, its id and why |

Beyond a run's totals and each master's figures the model states each light's fate, the lights the user left out,
the issues, the spread of a master's samples, the calibration masters' levels and pictures, a master's detail, the
lineage, what changed since an earlier run, the runs of the folder and what it could not state.

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
| `ComparisonFacts.cpp` | Each master beside the latest earlier run that made it, the run beside the latest earlier run that completed, what changed between the two, and the runs of the folder |
| `IssueFacts.cpp` | The issues, and the outcome they settle |

`facts/ReportFacts.h` is private to the reporting units. It declares those functions and every function that reads
a fact out of a run's records - an exposure breakdown, a target's name, a transform's name, a light's weight, the
fingerprint. A function that reads a fact has one home. The fingerprint is `computeReproducibilityFingerprint` in
`RunFacts.cpp`.

Rules the builder keeps:

- **A value that is not stated is absent, never a zero.** A stack that kept no gain map, no weights and no count
  of the lights that reached each pixel records no effective frames, no measured gain and no coverage - the
  engine leaves them at zero, which no stack that combined a light can have - so the model states none of them
  (`stackOf`, `MasterFacts.cpp`) and names each among what it could not state. That is what a stack that rejects
  samples and was asked for no quality map hands over, which is what the defaults ask for. A master that was not
  measured has no statistics; a run
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
  light's weight, and no light's fate but a failed registration and a light the stack dropped; the builder looks
  for no map the result it is given does not name (the archiver names the maps it finds beside a master in the
  result it hands over); and a stored report of any format but this version's is refused by name. An archive
  written before the report was stored has no report the application can build.
- **Anchors and folders are not derived from each other.** A master's `slug` comes from `ReportSlugs`; a
  registration group's and a calibration master's are made from the key with their kind before it, since their keys
  may be spelled as a group of lights is. A master's `previewFolder` is the name the archiver gives the folder it
  writes, hash and all.
- **The words are the report's own.** Names of settings, of counters and of outcomes go through
  `QCoreApplication::translate` under the context `ImageIntegrationReport`. No translation ships: a report reads in
  English.

- **A master's file is where the run wrote it.** The model is built of the run as the archive holds it - each
  master's archived thumbnail and maps, and whether the history keeps a copy of it (`ReportMaster::retention`) - but
  each master is named by its path in the output folder, which is where the person finds it. The archive's own
  manifest names the copy under `groups/`.

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

The archive's copy is written without indentation (`storeWorkflowReport`): a history is bounded in size, every run
of it keeps one, and an indented document is nearly twice as long. A case holds it to 100 KB and 1 KB a light.

`ReportArchiveReader` (`ReportArchiveReader.h`) is how a run's archive is read back as a model: `read(runDirectory)`
returns the model `workflow_report.json` holds, or a refusal with its reason in a sentence a person can be shown.
There are three, and a caller can tell them apart: the archive keeps no stored report (`NoStoredReport`); the file
is there and is no document that reads - cut short, empty, not a JSON object (`NotReadable`); the document states no
format or another than this version's (`OtherFormat`). Nothing else is tried: no model is built from an archive's
manifest and results, and no image is read. `Fuzz_ConsumerDocuments` holds the reader to answering whatever its
bytes are, and holds a document it reads to writing back as one that reads and writes the same.

## Where each fact comes from

| Fact | Source | When the source is absent |
| --- | --- | --- |
| Lights taken, per group | The integration group's `lightFiles`, then the manifest's `leftOutLights` under the key they would have joined; a light left out of a group that does not exist is listed under that key in `groupsNotStacked` | - |
| A master's state | `Made`: a result that succeeded. `Failed`: one that did not. `Reused`: named in `Result::reusedIntegrationGroups`. `NotReached`, `Cancelled`: any other planned group with no result | A group with neither a result nor that record in a run that completed: `NotReached`, and named |
| A light's state | "A light's states", below | No metrics database: a light whose registration failed has no state, and the model says so |
| A master's pictures | `report_previews/<folder>/master.jpg` and `detail.jpg` in the run's folder, the folder named by `archiveGroupFolderNames` | A whole frame the archive does not hold is the picture the group's result names (`previewPath`), as the result names it, and a map's likewise; a detail the archive does not hold is not stated |
| A calibration master's picture | `report_previews/calibration/<name>` in the run's folder, named by `archivedCalibrationPreviews` | Not stated |
| The metrics | The database the caller names | No per-light metric; the model says so once |
| Capture time and night | The registration group's `lightCaptureTimestampsUtc`; else the start the light's header states. A night is the local date twelve hours before the capture, at the offset from UTC the archive was written at | The light is on no night, and the model says how many are not |
| Stars, FWHM, SNR, eccentricity, background noise, zero point, RMS, limiting magnitude, matches | `registration_metrics`, at the group's latest settings version | Not stated |
| Shift, turn and transform kind | `transform_json`: the kind and the matrix. The shift is how far the matrix moves the frame's centre, with the size the light's group states; the turn is the matrix's rotation. The matrix's own translation is where the frame's corner goes, which for a light taken after a meridian flip is about the frame's size whatever its dither | A polynomial kind states the kind only: its matrix is not the whole transform. A group that states no size states the translation of a light that is not turned, and no shift of one that is |
| Weight | `StackQualityRecord::lightWeights`, by the light's identity | Not stated: nothing is derived from the settings in its place |
| Rejected fraction | `frameRejectionRates[i]` belongs to the light `lightWeights[i]` names, when the two lists have one length. A light excluded on a restack takes its rate from `spatialRestackExcludedFrameRates`, in the order of `spatialRestackExcludedFrameIndices`, whose values are places among the group's lights | The lengths differ: no rate is given to a light the stack combined, and the model says so |
| Normalization scale and offset | The group's `coefficients.json`, when it holds one match per light | A local normalization, which gives a light a surface: not stated |
| Gain, offset, temperature | The light's header, in the one probe each light gets; the temperature falls back to the one the file's name states | Not stated, and counted as absent in what the headers state |
| A master's statistics and the spread of its samples | One read of the master in one admitted operation | No session, or the file is gone: not measured, and named |
| A reused master's figures, stack and lights | The stored report of the latest earlier run that made it, marked by `madeByRun`; its pictures are this run's own copies | That run's report is not kept: the statistics and the spread from one read of the file, the rest named as not stated |
| A master's solution facts | The solution in the master's header, through `an::ui::readImageWcsSolution` | No header read or no solution that reads: the outcome and its detail only |
| A master's baseline | The stored report of the latest earlier run of the folder in which the master of the same key is `Made` | No earlier run made it: the baseline says the master is new. No earlier run keeps a report, or the run reused the master: no baseline |
| The run's comparison | The stored report of the latest earlier run of the folder that completed | No such run: no comparison |
| The runs of the folder | The folders beside the run's own whose names sort before it, each read through `ReportArchiveReader` | An archive that gives no report is listed by its id with the reader's reason |
| A master's photometry | The medians of what the registration recorded of the lights it combines | Not stated |
| A calibration master's level and noise | The stored report of the latest earlier run that states them for the same file, size and modification time; else one read of the file, measured over the centre of a large frame | Not measured, and named |
| A registration group's reference | The manifest's; else the registration's own group state | Not stated |
| A registration group's overlap | The smallest share of the capture's area its masters keep, since they share its output plane | Not stated |
| Stage states and times | `Result::stageResults`, in the order of the manifest's `stageSequence`. A stage with no result was not reached in a run that stopped; in a run that completed it was reused when the run is incremental and the stage has groups, and skipped otherwise | - |
| Engine memory, cache figures | `Result::engineMemory`, `Result::workflowCacheDiagnostics` | Not stated |
| Written to disk | Masters: their sizes. Maps and calibration masters the run made: their files. Registered lights: the size the registration recorded of each whose file is there. The archive: what the run's own folder in the history holds, stated at the final step (`restateRunArchive`) | A kind with nothing is not listed |

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
3. `NotStacked`: its master failed, was not reached or was cancelled. The remaining rules are of a master the run
   stacked.
4. `LeftOutByStack`: the group's result names it among the lights the stack dropped; the detail gives the reason.
5. `Used`: the stack's weights name it, and nothing else makes a light used. Of a stack that recorded no weights
   only the failed registrations and the dropped lights are stated, and the model says so.
6. `ExcludedOnRestack`: its place among the group's lights is one a second stack was made without. A second
   stack's weights name only the lights it kept, so no light is both used and excluded.

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
| `RunFailed`, `RunCancelled` | The run did not succeed, or was cancelled. When the run made a master, the action says what was kept and what a new run makes | Critical |
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

The three ratios are thresholds on figures the records define, never on a line of the log.

### What a model costs

Reading a master is quick; measuring it is not. On a run of 714 lights, four masters of 8,288 by 5,644 pixels and
twelve calibration masters of 187 MB each, reading an image took about 45 ms and its full statistics 0.7 to 1.3
seconds. So a calibration master is measured for its level and its noise alone, and a frame of more than four
million pixels over a rectangle at its centre of the frame's own shape (`measureReportImageLevels`); the model
states the share of the frame measured beside the figures. On that run, at a narrower width than the table below
was measured at, the model builds in 5.1 seconds, 3.2 of them
the four masters' statistics and 1.6 the twelve calibration masters, where measuring the calibration masters whole
took 11.6 seconds on their own. A calibration master an earlier run of the folder measured is not read again while
its file is the same, once that run's archive keeps its report.

A run states what its own report cost: the executor times the report phase apart from the archive's, and the
report prints it among the run's phases (`ReportExecutorTiming::reportMs`), so the cost on a real run is read from
that run's report. Storing a report again costs no image read.

What real runs measured on one sixteen-thread host, the report phase as each run's own report states it and the
later writes timed on the archive each run left:

| Run | Lights | Masters | Report phase | Written again | Saved as one file |
| --- | --- | --- | --- | --- | --- |
| Three narrowband masters of 3,008 by 3,004 pixels, flats only | 64 | 3 | 229 ms; 203 ms for a second run, compared with the first | 23 ms | 0.27 to 0.35 s, 2.1 MB |
| Two colour masters of 5,544 by 3,684 pixels, bias, dark and flat | 35 | 2 | 787 ms; 634 ms for a second run | 14 ms | 0.26 to 0.31 s, 3.3 MB |
| Four masters of 8,288 by 5,644 pixels, twelve calibration masters | 714 | 4 | 7.0 s with nothing in the system's file cache, 1.3 s with the files in it | 169 ms | 0.39 to 0.40 s, 3.7 MB |

The first two are whole runs of the application's workflow; the third is the model built and stored from a copy of
a real run's archive, since stacking that run again writes a quarter of a terabyte. The report phase is the
masters' statistics before anything else, so it follows the masters' size and number far more than the lights';
writing a report again and saving it as one file read no master and stay under half a second.

## A run beside the earlier runs of its folder

A report compares a run with the runs archived before it in the same output folder (`facts/ComparisonFacts.cpp`),
from the reports their archives keep: no master of an earlier run is measured again.

- **Each master is compared with the latest earlier run that made it**, by its group's key, and not with "the
  previous run": an incremental run makes only some masters again, and a run that found a master still valid made
  none. A master the run itself reused has no baseline - it is the earlier run's own master, and there are not two
  of it. A master no earlier run made is new, and its baseline says so.
- **The run as a whole is compared with the latest earlier run that completed**, with or without warnings: its
  totals, what changed, the application's version and the three fingerprints. A run that stopped is no baseline for
  a run, and neither is one that ended `Partial` - a group failed inside it, or one of its issues is critical -
  though the masters such a run did make are baselines for those masters.
- **An earlier run is its stored report, and nothing else.** A run's figures are the ones it stated when it was
  committed, so no image, manifest, result or metrics database of another run is opened: the output folder's
  metrics database is this run's, and is never read for another. An archive that keeps no report this version
  reads is no baseline and is listed among the runs by its id, with the reader's reason. A case builds the model
  with no session and with no metrics database and finds the earlier runs' figures unchanged.
- **Only earlier runs are read.** The runs of a folder are named by when they started, so an earlier run is a
  folder beside this run's whose name sorts before it. The run being written - whose folder may already keep a
  report of itself, when the report is written again - and any run after it are never among them.
- **Both sides of a figure are read by one function.** `ReportFigures.h` holds `masterFigures(master)` and
  `runTotals(model)`. The builder reads an earlier run's stored model through `masterFigures` and the document
  reads this run's master through it; the builder takes both runs' totals through `runTotals` and keeps the pair
  in the comparison, which is what the document prints. So a figure is never counted one way for one run and
  another for the other. A case compares a run with a stored copy of itself and finds every change to be "No
  change".

What changed, between the run and the earlier run that completed:

| What | How it is found |
| --- | --- |
| Settings | The two models' settings blocks, group by group, block by block, by the setting's name. The groups are the calibration masters, the groups of lights, the registration groups, the masters, and the run for the settings stated once; each is matched by its key. A setting whose value differs is listed with both values; one only one run states is listed with the other side empty. A setting both state the same is counted (`settingsCompared`) and not listed. |
| Groups | A group only one of the two runs has is named, with which run has it, and its settings are no change. A calibration master is the same group in both runs when its kind and its key are: a bias group's key may be spelled as a dark's. |
| Frames | For a master both runs have, the lights by path; for a calibration master both have, its frames. The files this run has and the earlier one had not, and the reverse, by name. |
| Application | The earlier run's version and its three fingerprints, beside the run's own. |

**The pictures are each run's own.** The comparison shows a master by the two pictures each run archived - the
whole frame and the detail - and not by reading and stretching the earlier master again: the history may not have
kept that master's payload, and a restretch would not be what that run showed. Each picture therefore has its own
display stretch, and the document says so; the figures are measured on the linear masters. The model names the
earlier run's pictures only when that run's archive holds them, as copies in this run's folder
(`previous_master.jpg`, `previous_detail.jpg`), and building a model writes nothing: `storeWorkflowReport`
(`WorkflowReport.h`) copies them, from the place the earlier run's stored report names, before it writes the model
and the document. The copies keep the report whole once the history drops the earlier run. A picture that cannot
be copied is taken out of the model that is stored, so neither it nor the document names a file that is not there.
Two frames whose aspect ratios differ by more than one percent are flagged (`ReportBaseline::sideBySide`): a
divider over two frames of different shapes would compare nothing.

**The runs of the folder** are listed oldest first, each as its stored report states it: when it was archived,
how it ended, the lights it used and took, its integration, its masters, and the noise it stated of the first
master this run made or reused, so that figure is of one master over the runs. A run's report is linked when its
archive holds the file, whatever wrote it.

## How a value is printed

`ReportFormat.h` is the one unit that turns a value into text for a report. A figure, a count, a duration, a size, a
time and a group's name each have one form, so two sections cannot print the same value two ways, and a measurement
of `0.01742656553` is read as `0.01743`. The document has two shorter forms of an instant of its own, for a place
too narrow for the whole one - `23 Mar 21:40` in a table of lights (`shortInstant`) and `21:40` where the day is
already said (`clockText`) - in the same zone.

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

## The charts

`ReportCharts.h` draws the report's charts: a function per chart, from plain series to one inline `svg` element.
The functions read no run and no report model - a caller hands each the values, the texts and the tips it is to
draw - so a chart can be drawn, tested and looked at on its own. The document's parts draw them ("The document").

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
nights, with every way a light is left out of a stack that was made - and a page that shows each chart on them,
with the cases a report meets: one night, six nights, 2,000 lights, a colour master, a run that stopped, nothing
to draw. The page is written by an opt-in case:

```text
set AFTERNIGHT_WRITE_REPORT_SAMPLES=<folder>
Test_ImageIntegration.exe --gtest_filter=ReportChartSamples.*
```

It writes `report_charts.html`, which opens from disk, follows the reader's theme and takes `?theme=light` or
`?theme=dark`. The page carries the report's own stylesheet and script, so a chart looks and answers there as it
does in a report. The cases that hold the classes a chart emits to a rule of that stylesheet, and its colours to
their contrast, are the document's ("The style").

## The document

`ReportHtmlWriter.h` writes a report as one HTML file from its model: `writeReportDocument(model, options)` returns
the document, and `writeWorkflowReport` (`WorkflowReport.h`) writes it to a file. It is the report a run writes,
through `storeWorkflowReport`. It has the header and its six parts.

| Unit (`reporting/`) | Holds |
| --- | --- |
| `ReportHtmlWriter.h/.cpp` | `ReportMarkup`, the one way markup is written; the document's shell, its contents and its footer; the fragments the parts share; the two assets as the document carries them |
| `ReportHtmlParts.h` | Private to the reporting units: what a part is, what every part is written from, the fragments' declarations, and `documentText`, the one way a word of the document is made |
| `ReportHtmlSummary.cpp` | The header, and part 1: `outcome`, `attention`, `made`, `frames`, `nights`, `quality` |
| `ReportHtmlMaster.cpp` | Part 2: a file per master - the picture and its maps, the stack, its lights, how it was made - and the forms of a master that was reused, failed, was not reached, or is a combined colour image |
| `ReportHtmlEarlier.cpp` | Part 3: each master beside the latest earlier run that made it (`before-after`), what changed since the latest earlier run that completed (`changed`), the runs of the folder (`history`); one sentence when there is no earlier run |
| `ReportHtmlShared.cpp` | Part 4: which groups made each master (`lineage`), the calibration masters (`cal`), the registration groups (`registration`) |
| `ReportHtmlRun.cpp` | Part 5: `timeline`, `decisions`, `once`, `resources`, `diagnostics` |
| `ReportHtmlProvenance.cpp` | Part 6: `acquisition`, `equipment`, `software`, `inputs`, `not-stated` |
| `ReportAssets.h/.cpp` | What a document of each form writes for a picture, a path and a message of the run |
| `ReportFigures.h` | The figures a comparison sets side by side, read from a model: the one way both runs' sides are read |
| `ReportArchiveReader.h/.cpp` | A run's archive as a model: the report it keeps, or why it gives none |
| `WorkflowReport.h/.cpp` | The document as a file, in either form; and what an archive keeps of a report - the stored model, the linked document and the earlier runs' pictures (`storeWorkflowReport`) |
| `assets/report.css`, `report.js`, `Report.qrc` | The style and the script, compiled into the library as a Qt resource |

### The markup is XML and HTML at once

The document is written so that an XML parser reads it and a browser shows it. That is what lets the suite read a
report back with `QXmlStreamReader` and assert on its structure - an id that exists once, a link that lands, a
table's cells against the model - instead of matching substrings of its bytes. The XML parse proves half of it;
the rules below and the browsers of the render tool prove the other.

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
- **A character XML forbids is replaced by U+FFFD**: a control character other than a tab and a line break, half
  of a surrogate pair, U+FFFE and U+FFFF. A file's name, a header's value and a line of a log can hold any of
  them. A carriage return in text is left to the line feed beside it, or becomes one.
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
  the figures, which are its lights, its integration, that master's noise and the run's time; a run of several
  leads with the count of its masters and states no noise there.
- **Several targets are grouped by target**: the title says how many, the gallery gives each target its own line
  of lights and integration with its masters under it, and the comparison table a row that names it.
- **A run that stopped leads with what happened**: what was kept, what was not made and why, and what to do, which
  is the action of the issue that says how it stopped. Only a run that made a master is archived, so there is
  always something kept.
- **The issues are in the model's order**, the computed kinds first and the most severe first, each with its
  action when it has one. The run's own warnings and errors follow under their own heading, word for word but for
  the folders the embedded form drops, and are folded away when there are more than five. A run with nothing to
  attend to says so.
- **A reason with no light is listed nowhere.** The table of why lights were left out has a row per state that has
  a light, and the bar's legend an entry per segment.
- **A status is never told by colour alone.** A pill is an icon and a word; a light's state in a table is a swatch
  and its name; a chart marks a light left out with a cross.
- **The same model is the same document.** Nothing reads a clock or a counter, so a report written again from its
  stored model does not change.

The ids a suite and the contents name:

| Part | Id | Blocks |
| --- | --- | --- |
| Header | `top` | - |
| 1. Summary | `summary` | `outcome`, `attention`, `made`, `frames`, `nights`, `quality` |
| 2. Master by master | `masters` | One `file-<slug>` per master, with `file-<slug>-picture`, `-stack`, `-lights` and `-made` inside it |
| 3. Against earlier runs | `earlier` | `before-after`, `changed`, `history` |
| 4. What the masters share | `shared` | `lineage`, `cal`, `registration`; a calibration master's row and a registration group's card carry the group's own slug |
| 5. The run | `run` | `timeline`, `decisions`, `once`, `resources`, `diagnostics` |
| 6. Provenance | `provenance` | `acquisition`, `equipment`, `software`, `inputs`, `not-stated` |

An issue's anchor is one of these (`file-<slug>`, `cal`, `registration`, `outcome`, `frames`), so "See it" leads to
the place that shows what the issue is about; a master's file lists the issues that are about it.

### Each fact has one home

The header and the summary state the run's figures; a later part repeats one only to break it down. A case holds a
fixed list to it - the lights the masters combine, the integration time, each master's noise - by counting where
each is printed: below the summary the first two stand once more, in the line of `software` a person copies, and a
master's noise once more, in the statistics of its own file. Part 3 is the one place a figure is set down again on
purpose: a comparison shows this run's figure beside an earlier run's, and is not counted.

- **A master's file is where the master's facts live.** The picture, the statistics and the spread of its samples;
  what the stack recorded of itself, the astrometry and the photometry, where the time went; its lights, charted
  measure by measure and listed one by one; the settings of its own groups and the files it wrote. What it shares
  with other masters it links to.
- **A file has its sections when the master is on disk** - the picture, the stack, how it was made, and its lights
  when it has any - whatever could be measured of it: a master that was not measured says so and leads to
  `not-stated`. **A master that was not made says why first** and shows no
  picture: the model's sentence, the phases of its finalization that ran, its lights and its settings. **A reused
  master says so**, names the run that made it, and links that run's report when the history holds one and the
  document is the linked form. **A combined colour image** has the picture, its channels, what it was made from,
  and no lights or stack of its own.
- **The first file is open and the others are folded**, so the part shows what a file holds without being as long
  as all of them; a run of one master has nothing to fold. A link into a folded file opens it, and printing opens
  every one. With scripts off a file is opened by its heading, as any `details` is.
- **The picture and its maps are one viewer**: the master, its detail at full scale, and each map the run wrote,
  one shown at a time. A radio button per view picks which, so it works with scripts off, and each view says in a
  sentence or two what it shows and how to read it. The stylesheet states a rule per view, so a viewer holds at
  most ten.
- **A master's lights are in the order they were taken**: one chart with a layer per measure a light of the group
  states and one set of hover bands, where each light sat against the reference, the lights left out with what
  each has to say, and every light in a table a heading sorts. A column no light states is left out, and so is the
  exposure where every light has the same. A cell is sorted by its text, read as a number where it is one, and
  states another key (`data-v`) only where the text does not say it: a time, a figure with a thin space or a power
  of ten in it.

### Against earlier runs

Part 3 is in every report. With no earlier run it is one sentence, and the contents lead to the part itself,
since it has no block to lead to; otherwise each of its three blocks is there when the model has something to say
in it.

- **Before and after is one comparison a target**, its masters one shown at a time. A radio button per master
  picks which and two more pick the whole frame or the detail, so both work with scripts off; the stylesheet has a
  rule per place, so a comparison switches between twelve masters at most and a run with more is given a second
  one. A run of one master has no switch between masters.
- **Two pictures under one divider.** This run's picture gives the box its shape; the earlier run's lies over it,
  cut at the divider. The divider is a range input over the whole picture, which the keyboard moves too; the
  script copies its value to the box (`--pos`) and computes nothing, and with scripts off the divider stays in the
  middle. Two frames of different shapes stand side by side instead, each named, with nothing to drag.
- **The detail is offered only where both runs have one**, and a master whose runs do not both have it keeps
  showing the whole frame when the detail is picked.
- **Each change is put in words where the document is written**: a count as a difference, a time as a difference,
  a measured figure as a share of what it was or as a difference in its own unit. Where a change is for the better
  or the worse an icon and the word say so, never the colour alone; the share of samples a stack rejected is
  marked neither way, since neither more nor less is better by itself. A figure neither run states has no row.
- **A master no earlier run made says it is new**; the masters the run reused are named under the comparison,
  with why they are not in it.
- **What changed lists a change once.** The same setting changed the same way in several groups of a stage is one
  row that names the groups, a master by the name the document gives it and a calibration master with its kind. A
  line under the table says how many other settings of those groups are the same. A value is printed as a group's
  settings print it (`said`): a file without its folder in the embedded form. Before the settings stand the run's
  totals in both runs and the application's version and fingerprints; after them, the frames each group both runs
  have gained and lost, by name, the first few and how many more.
- **Over the runs** draws three figures as a column per run - the latest eight, this run in colour and a run that
  stopped as a cross - and lists every run in a table, this run last. A run whose archive gave no report is a row
  with its id and the reason. A run's report is a link in the linked form alone: the embedded form is one file and
  leads to no other.
- **Printed, every master of a comparison is shown**, each with the whole frame.

### Settings belong to groups

The manifest holds settings on groups, so the document prints them there and nowhere else.

- **In a master's file:** its lights' calibration - the masters they take, as links to where each is stated, then
  the settings - and its own stacking, normalization and drizzle settings.
- **Once, in part 4:** each calibration master's settings and each registration group's.
- **Once, for the run, in `once`:** the execution, the plate solve and the colour combination, with the run's mode
  and scope before them.
- **The mark `≠`** stands on a value that more than half of the groups of the same stage do not use, and its tip
  says what they use (`ReportSettingsMajority`). The stages are the masters' own settings, the groups of lights',
  the calibration masters' of each kind and the registration groups'. With no such majority - two groups that
  differ, or every group its own - nothing is marked: with two groups a reader compares the two cards.
- **A setting the application decided** prints what it resolved to, with what was asked and why as its tip;
  `decisions` lists every such setting of the run.
- **A setting of a mode is printed only in that mode.** The model states a row only when it applies - drizzle,
  comet tracking, an exposure merge, local normalization, manual control points - and the document prints the
  model's rows as they are.
- **A run's words are made readable where the document quotes them.** `ReportDocumentContext::said` replaces a
  group's key inside a sentence the run wrote by the group's name, and in the embedded form drops the folders. The
  run's messages and its log are the exception: they keep their keys and are printed word for word, but for the
  folders the embedded form drops there too.

### One master, several targets, a run that stopped

- **One master.** No gallery and no table comparing masters; the picture is in the header; part 2 is "The master"
  and its file is open; part 4 is "Calibration and registration", with one line in place of the map of groups; a
  registration group that serves one master shows no chart of where its lights sat, since the master's file has it.
  A colour master states its statistics and the spread of its samples per channel.
- **Several targets.** The masters' files, the map of groups and the acquisition table give each target its own
  line, with its masters under it.
- **A run that stopped.** The timeline marks the stage it stopped in and the stages it did not reach, and leads
  back to `outcome`; each master that was not made has a file that says why.
- **An incremental run.** A reused master's file says so. Its figures, its stack and its lights are the ones the
  run that made it stated, when the history holds that run's report, and the file then leads to it; otherwise its
  picture and statistics are read from the master's file and the rest is named as not stated. The comparison
  leaves a reused master out and says why.
- **A report with nothing to read.** With no session nothing is measured, and with no metrics database no light
  states a measure: the sections stay, say what is missing, and `not-stated` lists each thing with its reason.

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
- **Embedded**, the one a person sends: one file with every picture inside it as a `data:` address, each read once
  for each size it is shown at, reduced so its longer side is at most 1,280 pixels and written again as a JPEG of
  quality 82
  (`boundedJpeg`). A path is printed as a file's name without its folders, and in a message of the run the output
  folder's path is replaced by the folder's name and the folder of every file the model names is dropped, in
  either spelling of the separator. A folder is noise to a stranger and says more than its owner may want. A
  picture the embedded form cannot read is left out: it never names a file outside itself. A picture the document
  shows small - on a card of the gallery, in a file's heading, in a table - is carried at 480 pixels
  (`ReportAssets::thumbnail`), so a picture shown small and whole is not in the document twice at its whole size.

Two forms rather than one because a run's own report should stay small and keep its full-size pictures one click
away, and a report meant to be sent must open from an empty folder. The embedded form is an action on a run, not a
setting - the history row's Save As button, "Save Report As One File" - so it works for any run whose archive keeps
its report, and it is never written into `history/`, whose size is bounded.

A case holds the linked form to pictures that are in the run's folder, the embedded one to no `src` but a `data:`
address and no `href` but one inside the document, and both to their sizes: the linked form within 400 KB and
2.5 KB a light, the embedded one within that and 1 MB a master. Those are the scenarios' sizes, whose pictures are
small and smooth. A real master's pictures compress less and a comparison carries the earlier run's pair as well:
the one file of a run of two colour masters of 5,544 by 3,684 pixels, compared with an earlier run, measured
3.3 MB, and that of a run of 714 lights and four masters 3.7 MB.

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
  more on those and on a tile. Another holds every class the document and the charts emit to a rule, but the ones
  that only name what an element is: a chart's kind (`chart-*`), a mark's shape (`c-shape-*`) and `is-empty`.
- **The layout gives way in steps.** The contents stand beside the page from 1,181 pixels; at 900 and under the
  grids fall to one column and the figures and the gallery to two, and the gallery to one at 560; every grid
  child has `min-width: 0`, without which a table widens a phone's layout. A chart or
  a table wider than its card scrolls sideways inside it instead of shrinking; the container can be reached with
  the keyboard and says what it holds.
- **It loads nothing**: no `url(`, no `@import`, no font face. A case holds that.

### The script

`assets/report.js` shows the tip of what the reader points at or reaches with the keyboard, switches and
remembers the theme, sorts a table of class `sortable` by a column - by a cell's `data-v` where it states one and
by its text otherwise - opens a folded section a link leads into or that is about to be printed, and moves a
comparison's divider. It computes no figure of the report: every
number is written when the report is, so the same report is read with scripts off, where only those conveniences
are lost, and so no product logic runs in a browser. A case holds the file free of `fetch`, `XMLHttpRequest`,
`import`, `eval` and any address.

### Looking at a report

An opt-in case writes the report of every scenario the executor archives in both forms, with its stored model, as
the product's archiver left them in the run's folder:

```text
set AFTERNIGHT_WRITE_REPORT_SAMPLES=<folder>
Test_ImageIntegration.exe --gtest_filter=ReportSamples.*
```

Each scenario gets a folder there, laid out as an output folder's history is: `history/<run>/` for the run
reported and for each earlier run it is compared with, each holding what its archive keeps of its report -
`workflow_report.html` beside the `report_previews/` it names, and `workflow_report.json`. The run reported, the
latest, also holds `workflow_report.single.html`, the embedded form. The linked report opens from its folder, its
links to the earlier runs' reports lead to them, and it takes `?theme=light` or `?theme=dark`.

`build/scripts/render_integration_report.cjs` opens those samples in real browsers, as
`render_user_guide.cjs` opens the guide, with the same Node packages and browsers outside the repository (that
script's header says how they are set up):

```text
set NODE_PATH=%TEMP%\afternight-guide-render\node_modules
node build/scripts/render_integration_report.cjs --samples <folder> [--shots <folder>]
```

It opens each scenario's linked report where it lies and its embedded one copied alone into an empty folder, from
`file://` with the network blocked, in Chrome and Firefox, at 1,440, 1,024 and 390 pixels, in both themes, and
fails a load on: a document or a local file that did not load, a request that leaves the file, an `href` or a
`src` that names an outside address, an embedded picture that is not a `data:` address, a console or page error,
a picture that did not load, a page wider than its window, content that spills out of its card, the contents
missing from 1,181 pixels or shown under it, a link that leads nowhere, a contents entry that does not land, a
theme toggle, a measure switch, a switch between a master's views, a sort, a comparison's divider, its switch
between masters or its switch to the detail, or a link into a folded section that does nothing, a tip that does
not show, a serious or critical axe-core finding, and a print render that is not light, shows the contents or
anything else marked as not for print, leaves a section folded, hides a master of a comparison or shows more or
fewer than one layer of a chart of several measures. `--shots` writes a picture of every load
and, from Chromium, a PDF of the print render. The suite proves the document's structure; this proves what a
reader sees.

### Adding to the report

**A fact.** Add its field to `ReportModel.h` as an optional or a list, never a zero for "not stated"; name it once
in `ReportModelJson.cpp`, in the function the writer and the reader both walk; fill it in the unit of
`reporting/facts/` that owns its area, through a seam of `ReportSources.h` when it needs a file or the metrics
database; say in `notStated` why it is absent when it can be; and hold it in that area's suite, with the scenario
that has it. Then print it in its one home.

**A block.** Write it through `ReportMarkup` in the part's unit; state its anchor among the part's anchors under
the same condition it is written under, so a link to it is made exactly when it exists; name it in the part's
entries of the contents; give every class it uses a rule in `report.css`; and hold it in `ReportHtmlPartsTest.cpp`
or `ReportHtmlWriterTest.cpp`, for every scenario where that is cheap. A block with nothing to say is absent. What
several parts need - a card, a panel, a list of facts, a list of settings, a table, a fold - is in
`ReportHtmlParts.h`. A word of the run goes through `said`, a path through `ReportAssets::path` or `message`, a
picture through `image` or `thumbnail`.

**A chart.** Add its function to `ReportCharts.h`: plain values in, one SVG out, every word of it an input, classes
and no colour, a tip per mark, a sentence for nothing to draw. Give its classes a rule in `report.css`, add it to
`ReportChartSamples` so it is on the page of every chart, and hold it in `ReportChartsTest.cpp`. A chart wider
than a phone goes through `writeScrollingChart`; one drawn for a narrow place has a `max-width` in the stylesheet,
so it keeps its size where a narrower window gives it the whole width.

**The user guide.** `docs/user-guide/docs/en/processes/stacking-integration/integration-report.md` tells a reader
how to read a report, in the report's own words: the names of its parts and blocks, of a light's states, of the
pills and of the history row's two actions, and the messages those actions show. A change to any of those words, a
new block, a new state or a new refusal changes that page in the same pull request, and a change to what the report
looks like retakes the page's three pictures, which are renders of a report the application wrote
(`docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md` has their rows).

## The suites, and the runs they build

`Test_ImageIntegration` holds the report in `reporting/`, and what the archiver does with it in `workflow/`:

- `workflow/WorkflowArchiverReportTest.cpp` - the report a run's archive holds: the two files however a run that
  made a master ended, the final figures and the whole log stated in both after the two final steps, a run
  archived with no session, a report that cannot be written, the report written again and saved as one file from
  the stored model or refused by name, a run the history dropped listed without a link, and the bound on what an
  archive writes last. `workflow/test_workflow_archiver.cpp` reads each figure it archives back from the stored
  model.
- `ReportFormatTest.cpp` - every form of the table above, with its edges.
- `ReportModelBuilderTest.cpp` - what the model states of the run, the masters, the calibration and the
  registration.
- `ReportSettingsFactsTest.cpp` - each group's settings, the ones of a mode, and the decisions.
- `ReportFrameFactsTest.cpp` - a master's lights and their weights.
- `ReportRunFactsTest.cpp` - what the model reads from the files and how often: one read of each master and each
  calibration master, the spread of a master's samples, a calibration master's level and noise and the one an
  earlier run measured, a reused master's figures, the groups of lights, a registration group's record, a master's
  solution, the stages a run did not time, the engine's memory, what was written to disk, and what a run with no
  session or no metrics database cannot state.
- `ReportIssueFactsTest.cpp` - the issues: each kind raised by the runs whose records call for it and by no
  other, in order of severity, the thresholds, the run's own messages after them, and the outcome they settle.
- `ReportComparisonTest.cpp` - a run beside the earlier runs of its folder: each master's baseline, a master that
  is new and one that was reused, an archive that keeps no report, the run's totals, a changed setting and an
  unchanged one, the groups and the frames gained and lost, a baseline of another shape, the runs of the folder,
  and the earlier run's pictures copied when the report is stored.
- `ReportArchiveReaderTest.cpp` - an archive read back: the stored report as the model it was built as, each of
  the three refusals by name, and the runs of a folder listed with no image of an earlier run read.
- `ReportModelJsonTest.cpp` - the stored document: every scenario's model and one with every kind of value survive
  it, and what a reader refuses and ignores.
- `ReportScenariosTest.cpp` - the scenarios themselves: each holds what it says it holds, is archived by the
  product's archiver, and is given its report by it.
- `ReportChartsTest.cpp` - each chart, read back from the SVG it returns: a mark per value, a cross per light left
  out, a tip per mark, round ticks that span the data, and what a chart does with nothing, one value, equal values
  and a value that is not a number. Its series are in `ReportChartSamples.h` ("Looking at the charts").
- `ReportHtmlWriterTest.cpp` - the document, read back as XML (`ReportDocumentTestSupport.h`): well-formed for
  every scenario that has a report in both forms, each block there exactly when the run calls for it, every link
  landing, the header's figures and the issues against the model, what a run wrote coming out as text whatever it
  holds, the two forms' pictures and paths, and the style and the script held to their rules.
- `ReportHtmlPartsTest.cpp` - the parts after the summary: a file per master with its sections or the form of a
  master not made, a table's rows against the model's lights and its cells against their values, the mark `≠`
  exactly where most groups use another value, no shared group's settings inside a master's file, a setting of a
  mode only in that mode, the channels of a colour master, one line for a run of one master, the counters only when
  collected, the log as text, `not-stated` entry by entry, and a fixed list of figures at their one home.
- `ReportHtmlEarlierTest.cpp` - part 3: there in every report with the blocks its run calls for, one sentence with
  no earlier run, each master beside its baseline with both runs' pictures and figures, a change for the better or
  the worse said in words, the side-by-side form, the detail only where both runs have one, a run compared with a
  copy of itself, each change listed once, the runs of the folder, and the header's earlier figures. The three
  document suites take a scenario's report from `ReportRenderedScenarios.h`, which archives each scenario once and
  reads the model its archive keeps.
- `ReportSamplesTest.cpp` - writes the report of every scenario that has one where a person or the render tool
  can open it; skipped unless asked ("Looking at a report").

`ReportScenarios.h` is where a suite gets a run from. `buildReportScenario(id)` writes one whole run under a
temporary directory the scenario owns - the masters and the lights through the neutral FITS writer, the metrics
database through the product's `RegistrationMetricsWriter`, the earlier runs of the folder through the product's
`WorkflowArchiver` - and returns its manifest, its result, the archive's context, the session to read it in and the
figures it was built to hold (`ReportScenarioFacts`). `archiveScenarioRun` commits the scenario's own run as the
executor does, stating the time, the place and the version the scenario's context states
(`WorkflowArchiver::setArchiveStamp`), so the archive holds the run's report as a run stores it. An earlier run of
a scenario keeps its report, as a run of this version does, unless the scenario says it is an archive an earlier
version left. The same header holds the file helpers the report's suites share: the session,
the master and light writers, the bare metrics table and an archived run's manifest.
`ReportModelTestSupport.h` builds a scenario's model through the product's sources and looks a master, a block
or a row up by name.

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
| `FiveFiltersAfterTwoEarlierRuns` | The five filters after a full run of the first night's broadband lights under another weighting, an incremental run that made the luminance master again and reused three, and a run whose archive keeps no report | A baseline per master, a master that is new, what changed, an archive that is no baseline |
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

To add a scenario, add its enumerator and its name, list it in `allReportScenarios()` - the list every suite walks,
so a scenario left out of it is built by nothing and fails nothing - raise the count `ReportScenariosTest.cpp`
holds that list to, describe the run in `buildReportScenario` with the builder's
`addBias`, `addDark`, `addFlat`, `addRegistrationGroup` and `addLightGroup`, and give it a case in
`ReportScenariosTest.cpp` that states what it is for. A group that a later run of the same folder must find
unchanged takes a slot of the night of its own (`LightGroupPlan::nightStartMinutes`), so its lights have the same
names and times whatever the other groups take.
