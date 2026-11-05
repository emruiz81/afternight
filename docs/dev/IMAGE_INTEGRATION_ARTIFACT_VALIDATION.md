# ImageIntegration artifact validation

How `ImageIntegrationProcess` decides whether a file it produced earlier can be reused, and why that decision reads a
file's header, or nothing at all, instead of hashing its image payload. The workflow reaches the engine through
`AfterNightConsumerRuntime`. The few places that still hash are named in *Where content hashing still lives*.

## The two classes of file

The workflow touches files of two very different kinds, and the distinction drives everything below.

| | Input files | Generated files |
|---|---|---|
| Examples | raw lights, darks, flats, bias | `calibrated_lights/`, `ca_corrected_lights/`, `registered_lights/`, masters, stacked output and its maps |
| Owner | the user | AfterNight |
| AfterNight writes them | never | always; its image artifacts through the engine's output transaction |
| Change between runs | only if the user edits their library | only when AfterNight rewrites them |
| Validated by | path + size + mtime + platform file identity, which enter the fingerprint of everything derived from them | the recorded size and modification time, then the platform file identity, then the identity the file carries in its own header |

Applying one validation scheme to both was the original design error. For a file AfterNight wrote,
into a directory AfterNight created, during a run AfterNight is tracking, a content digest defends
against a threat that does not exist.

## Generated files carry their own identity

A stamped FITS artifact carries two header cards (see *What is stamped today* below for which artifacts):

```
ANARTID = '9fc0769b6ea4be30144c8d50609a1b2c' / manifest artifact identity this file satisfies
ANRUNID = '20261009_120000-001'              / execution that produced this file; provenance only
```

`ANARTID` is the **manifest fingerprint** the workflow already computes for the entry the file satisfies - for a
calibrated light `computeCalibratedLightArtifactHash()`, for a calibration master `computeCalibrationGroupHash()`, for a
stack `computeStackedLightArtifactHash()` - reduced by `workflowArtifactIdentity()` to the characters a card may hold and to
its first 32 of them. A fingerprint is a SHA-256 in hex, so the stamp is its first 32 hexadecimal characters. It is
deliberately not a random UUID:

- **Self-describing.** The file says which manifest entry it satisfies. The tracker records the stamp its writer stated
  when it makes the row, and a later check compares the file's header with that recorded stamp; the row's
  recorded fingerprint is compared separately with the one derived locally from the current inputs and settings.
- **Stable across runs.** A calibrated frame from last week still validates today when its inputs and
  settings are unchanged. A per-run UUID would change every run, so the database would still be
  needed to know which one is current.

`ANRUNID` is the execution that wrote the file (the manifest's `executionId`; for a light written by the standalone
comet registration, the comet run's) and is provenance only. It answers "which run wrote this" when debugging and is
never a reuse key. An identity with no run states no `ANRUNID` card.

The identity lives *inside* the file, so it survives a copy, a move, or a restore. Filenames stay
human-readable, which matters both to users and to the incremental machinery: the path is the
database key (`artifacts.path` is its primary key), so a per-run UUID in the filename would mean no artifact ever
matching a previous run's record.

The contract lives in `src/AfterNightEngine/include/AfterNightEngine/IO/ArtifactIdentity.h`
(`an::ArtifactIdentity`). An identity is checked where it is stated and again where a card is read back, and never
repaired in between: it is nonempty, at most 32 bytes, and spelled with letters, digits, underscore and hyphen.
`ArtifactIdentity::create` refuses anything else, and `artifactIdentityOf` reports a malformed card as
`ErrorCode::InvalidData` with `CodecFailure::InvalidIdentity` rather than reading a different identity out of it. The
reduction is the workflow's, made before the engine sees an identity: `workflowArtifactIdentity()` drops the characters
a card cannot hold and stops at 32, and refuses a fingerprint that reduces to nothing.

### What is stamped today

Every image artifact a workflow stage writes goes through `publishWorkflowImage()` with its identity; it is also what
the standalone comet registration writes its comet-aligned lights through. The table says where each kind of artifact
is recorded and what decides its reuse.

| Artifact | Identity it states | Recorded in | Reuse decided by |
|---|---|---|---|
| `calibrated_lights/*_cal.fits` | the light's calibration fingerprint | `artifacts.db` (`recordArtifact`) | the tracker: size and time, platform identity, else header stamp |
| `ca_corrected_lights/*_ca.fits` | the chromatic-correction fingerprint | `artifacts.db` (`recordArtifact`) | the tracker |
| Bias / dark / flat masters | the group's fingerprint | `artifacts.db` (`recordCurrentRunCommittedArtifact`, which also records a SHA-256 as the run archive's provenance) | the tracker in the stage; the planner's strict check, which decides the same way - size and time, platform identity, else header stamp |
| Stacked master and colour master | the stack-family fingerprint of the group, or of the colour master | `artifacts.db` (`recordCurrentRunCommittedArtifact`) | the stacked master: the tracker in the stage and the planner's strict check, as for the calibration masters; no reuse check reads the colour master's record |
| The master's maps and the comet composite's layers | the master's identity, so the same fingerprint | `artifacts.db` (`recordArtifact`) under the master's fingerprint | the tracker; a map is its master's only while both are recorded under one fingerprint and both files are still the recorded ones |
| `registered_lights/**` | the registered-light fingerprint | `registration_metrics.db`, as size, modification time and platform identity | that recorded revision alone; no header is read |
| Comet-aligned lights | the comet-aligned light's fingerprint | `registration_metrics.db` (comet outputs) | that recorded revision alone |

Two images the workflow writes are not artifacts and are never recorded: the scratch input of the final master's plate
solve, which goes through the same transaction without an identity and is removed after the solve, and the PNG preview
beside the master (`<master>.preview.png`), committed through `QSaveFile`.

A registered light is stamped but is reused by its recorded revision, not by its header, so a registered light whose
file identity changed is warped again even though its header would still match.

## How a reuse decision is made

Three pieces of code ask whether a generated file can be reused, and they ask different questions.

### The tracker

`ArtifactTracker::isArtifactValid()` answers for the files `artifacts.db` records. It requires the file to exist, a row
for its path, and the row's recorded manifest hash, size and modification time (milliseconds) to match the current
fingerprint and the file on disk. It then resolves in one of three ways:

1. **Untouched** - the recorded platform file identity is nonempty and still matches. Nothing has modified the file
   since it was published. One stat; this is the common case.
2. **Moved or restored** - the identity changed, or the row has none, and the row carries a stamp: the header stamp is
   read and compared with the recorded one, and that answer is final. A byte-identical restore under a fresh file
   identity stays reusable; a foreign rewrite that dropped the stamp does not. One header read, not a payload read.
3. **Nothing left to check against** - the identity changed and the row carries no stamp: stale; regenerate. This is
   also the answer for a row that holds a content digest and no stamp - one made before stamping, or when the header
   could not be read at the moment the row was made. The digest is never compared, because comparing it reads the
   whole payload: fail-to-recompute, never fail-to-trust.

The header is read by `sessionArtifactStampReader()`: it admits a one-thread batch operation of the session (waiting for
a thread, so it is for a worker), asks the engine to probe the file with `validatePayload = false`, and reads the
identity with `artifactIdentityOf` in the vocabulary of the format the file's name selects - the FITS card of a FITS
file, the XISF property of an XISF file. A refused admission, a file that cannot be probed and a malformed card all
answer "no stamp", which can only cause a recompute, never a wrong reuse. A tracker built without a reader consults no
stamp and records only one its writer stated, so a moved or restored artifact is recomputed
(`ArtifactTrackerTest.WithoutAStampReaderARelocatedArtifactIsRecomputed`).

A stage records the identity it has just stamped - `recordArtifact()` given the identity - instead of reading it back,
because the read would be the file's first open since its writer closed it. An antivirus filter scans a file as its
writer closes it, and an open that reads the file before that scan ends waits for it. Measured on Windows with Defender
on 2026-11-01: a 187 MB XISF frame opened as soon as it was written waited about 290 ms (a FITS frame of the same size,
under a millisecond), and one opened two seconds later did not wait at all, the scan having finished meanwhile. The
stamp is still read from the file whenever reuse depends on it, so an identity recorded and missing from the header can
only make a moved file be recomputed (`ArtifactTrackerTest.TheIdentityItsWriterStatedIsRecordedWithoutReadingTheFile`).
`recordArtifact()` without an identity still reads the header, for a file whose writer stated none; the masters'
`recordCurrentRunCommittedArtifact()` does too, after `FinalCommittedArtifact::fromFile()` has read the whole file for
its digest.

The row, in `<output>/.afternight/image_integration/artifacts.db` (`PRAGMA user_version` is
`kArtifactTrackerSchemaVersion`):

| Column | Holds |
|---|---|
| `path` | the artifact's path; the primary key |
| `hash` | the manifest fingerprint it was published under |
| `created_at`, `size_bytes`, `modified_at` | birth time in seconds, size, modification time in milliseconds |
| `content_digest` | the SHA-256 of the whole file for a master, the run archive's provenance; empty for every other row. No reuse decision reads it |
| `digest_provenance` | `current_run_committed` for a master's digest; `readback_verified`, the label `recordArtifact` gives its rows, which carry no digest |
| `artifact_stamp` | the identity its writer stated, or the one read from the header when the row was made without one; empty when there was neither |
| `identity_token` | the platform file identity when the row was made |

A store that states another `user_version`, or whose table is not that format's, is never read or written: the tracker
sets it aside beside itself under a new name, says so in the log, and makes a new one, so every artifact it recorded is
decided again. A row's `digest_provenance` that names no value this version writes reads as `unknown`.

What each way of deciding costs, measured on a 714-frame, 133.6 GB calibrated set (the run that motivated this design):

| Check | All 714 frames |
|---|---|
| stat only (size + mtime + identity token) | 3 ms |
| read the FITS header of every frame | 265 ms |
| a full SHA-256 pass over every frame | 542,000 ms |

The header read was measured with a plain FITS header reader; no measurement of `sessionArtifactStampReader` itself is recorded here.

### The planner's preparation snapshot

Before a run, the planner decides from an immutable snapshot of the same records (`ExecutionPreparationSnapshot`),
which stats the files and reads `artifacts.db` once. Its `isArtifactValid()` has two modes, and neither reads an image
payload:

- `MetadataOnly`, used to decide whether a subset's calibrated lights are all reusable: the row's fingerprint, size and
  modification time must match, and the row must carry some provenance (a digest, a stamp or a platform identity).
  Nothing else is read. The calibration stage asks the tracker again when it runs.
- `StrictContent`, used for the masters - the bias, dark and flat masters and the stacked master, which the snapshot
  calls strict artifacts. They are decided as the tracker decides every row: the fingerprint, size and modification
  time must match; a master whose recorded platform identity still matches is reusable; one whose identity moved is
  reusable only when the identity its own header carries is the stamp its row holds and the manifest entry's identity
  (`workflowArtifactIdentity()`). The header is read once, when the snapshot is built, through the snapshot's
  `FrameHeaderReader` (`FrameHeaderFacts::artifactIdentity`), and only for a moved master whose row holds a stamp. A row
  with no stamp, a snapshot built without a reader, a header that cannot be read and one naming another identity all
  answer "recompute". The row's digest decides nothing.

The snapshot's diagnostics count what it read: `strictHeaderReads` the moved masters' headers, `bytesHashed` the bytes
of the normalization coefficient documents it pinned (the only files it hashes). The executor re-checks the snapshot's
captured facts just before the stages start (`matchesCurrentFacts`): a stat per captured file - its size, modification
time and platform identity - and the drizzle sources' headers again. It reads no master's payload; nothing can rewrite a
master without moving its platform identity, which the stat sees.

### The registration records

Registered lights and comet-aligned lights are not rows of `artifacts.db`. The alignment stage records each one's
size, modification time and platform identity in `registration_metrics.db` (`captureFileRevision`), and a later run
reuses a light when `registeredOutputMatchesRevision()` finds the file at the same path with the same three values.
The stack's frame source holds a frame to the revision it was planned with, checking it before and after it reads the
frame, and refuses one that changed.

### The platform file identity

`ArtifactTracker::fileIdentityForPath()` builds:

```
Windows:  windows:<dwVolumeSerialNumber>:<fileIndex>:<FILE_BASIC_INFO::ChangeTime>
Linux:    linux:<st_dev>:<st_ino>:<st_ctim.tv_sec>:<st_ctim.tv_nsec>
```

`ChangeTime` (NTFS) and `st_ctim` (POSIX) are metadata-change stamps. Neither can be set through `SetFileTime` or
`utimensat`, so any rewrite moves it even when the modification time is faithfully restored. `fileIndex` catches
replace-by-rename, and the volume serial catches a swapped volume. A path that is not a regular file has no token.
`captureFileRevision()` and `FinalCommittedArtifact::fromFile()` read it through an opened handle before and after, and
refuse a file whose identity, size or modification time moved meanwhile. On Windows `captureFileRevision()` opens the
file for its attributes alone, which an antivirus scan of a file just written does not hold up, as it holds up an open
that asks to read (see *The tracker*); the alignment stage captures each registered light's revision as soon as it is
written (`ArtifactTrackerTest.ARevisionIsCapturedWithoutAskingToReadTheFile`).

This is why mtime alone was never sufficient - it is writable, and coarse on filesystems such as
exFAT - and why the identity token closes that gap for one stat.

## Atomic writes are what make the trust sound

A header stamp cannot detect a **truncated or partially written** file: a crash, a power loss or a
full disk leaves a file with a perfectly valid header, stamp included, and missing pixel data. For a
pipeline writing 133 GB per run that is the realistic corruption mode, far more so than bit rot or
tampering.

So the payload is never written to its destination name. Every image artifact goes through the engine's output
transaction (`IO/OutputTransaction.h`), whose four steps run in a fixed order and each of which can refuse:

```
begin   reserve  X_cal.fits.<pid>-<context>-<operation>.antmp    // same directory, same volume, exclusive create
encode  the codec writes the whole payload into the pending file
commit  prove the pending file holds every byte the encoder reported, flush it to the device,
        rename it over X_cal.fits in one atomic step
abort   on any failure, a stop seen after encoding, or the destructor: remove the pending file
```

- `begin` creates the pending sibling exclusively, so a second transaction for the same destination in the same
  operation, or a sibling a crashed run left under the same three numbers, is refused (`ErrorCode::Busy`,
  `CodecFailure::PendingConflict`) instead of overwritten. The name comes from the process, the engine context and the
  operation, not from a counter, so two contexts in one process and two processes get different names. The identity
  cards are added to the output intent here, so a card limit is met before any file exists.
- `commit` refuses a missing or short payload (`CodecFailure::ShortWrite`) and flushes before it publishes
  (`OutputDurability::FlushBeforePublish`, the default, which `publishWorkflowImage()` does not change). On Windows a
  rename refused by a sharing conflict is retried every 5 ms for at most 250 ms.
- A failure at any step - an encoder error, a full disk, a refused rename, a stop observed after the encoder ran -
  removes the pending file and leaves the destination exactly as it was.

A file at the destination name is therefore complete **by construction**. `publishWorkflowImage()` asks for
`OutputReplacePolicy::ReplaceExisting`, so a rewrite replaces the previous file in one step, and it makes the
destination's directory first when it does not exist.

`an::sweepPendingOutputs()` reclaims what a killed process left behind: it removes the regular files directly below one
directory whose names end in `.antmp`, and nothing else.
The executor sweeps at the start of every run - the output root,
`calibrated_lights`, `ca_corrected_lights`, `registered_lights` and the group directories one and two levels below
them, and the normalization and drizzle document directories - and the sweep assumes no other run is writing there.
The same call removes the workflow's own `.anpending` document siblings (`recoverDocumentDirectory`). The executor then
removes the calibrated copy of every light the manifest no longer calibrates - a light of a group since marked
pre-calibrated, or whose entry has no master and no pedestal - because every stage reads a light's calibrated copy
whenever one is on disk; a copy that cannot be removed fails the run before any stage reads. The
stacking stage also moves the previous master, its preview and its maps aside before it publishes new ones, so the run's
archive keeps the run they came from, and puts them back when the group fails or is stopped.

### Documents that are not images

A coefficient document, a drizzle descriptor, a dense map payload and the sidecars beside them are not images and do
not go through the output transaction. The first three are wrapped in a bounded, versioned envelope - magic, envelope
version, and a header stating the type, the schema, the identity, the payload's length and its SHA-256 - and the
sidecars are plain JSON. `DocumentPublication.h` writes each to a pending sibling the workflow alone names
(`<name>.<uuid>.anpending`), created exclusively, flushed to the device, closed and moved over the destination in one
atomic replace (`MoveFileExW` with write-through on Windows, `rename` on POSIX). A failure at any step removes the
sibling and leaves the destination as it was, and a stop is observed before every step up to the replace and never
after it. A payload and the manifest that names it publish payload first (`publishDocumentPair`), so an interruption
can leave an orphaned payload but never a manifest that advertises one that is missing. `recoverDocumentDirectory`
removes the pending siblings, and the orphans a caller names, in one directory and nothing else.

### The ordering rule

One invariant carries the rest of the design:

> **Write the artifact durably, then record it in the manifest and database. Never the reverse.**

A recorded artifact then always refers to a complete file, and "if the manifest says it is there,
trust it" holds by construction. A run that dies between the two leaves a whole file with no record, which the next run
overwrites. The stages record only after `publishWorkflowImage()` has returned, and the stacking stage ends its own
admitted operation (`scope.end()`) before it records, because reading the stamp is an operation of its own that the
session admits only once the writing one has let go.

## What this deliberately does not catch

An in-place rewrite that preserves the size, the modification time **and** the header stamp is
indistinguishable, to the tracker, from a byte-identical restore of the same artifact: the rewrite moves the platform
identity, so the header stamp decides, and the stamp is intact. Only re-reading every byte could separate them, which
is exactly the cost this design removes.

That gap is acceptable because the failure it replaces - a truncated or partial payload - cannot
occur any more, and the accidental-corruption cases that remain change either the size or the header.
`ArtifactTrackerTest.StampPreservingPayloadRewriteIsAcceptedByDesign` pins this behaviour so it is
not "fixed" by reintroducing content hashing. A master is held to the same limit: the planner decides it as the tracker
does, and the launch check is a stat
(`PlannedMasterReuseTest.TheLaunchCheckHoldsARecordedMasterToItsStatAndReadsNoneOfItsSamples`).

If another tool rewrites a frame and strips unknown keywords, validation fails and AfterNight
recomputes. That is the safe failure direction - fail-to-recompute, never fail-to-trust.

What the identity does not defend against is a file rewritten in place by something that keeps its
`ANARTID` card, its size and its modification time: a master edited that way is reused on its header, as
every intermediate is, and the SHA-256 its record holds - the run archive's provenance, taken once after
AfterNight's own write committed - then describes the file AfterNight wrote rather than the one on disk.
No reuse decision reads that digest. A person who edits a generated file and wants it recomputed changes
its name or removes its card.

## Where content hashing still lives

A digest settles the reuse of no image: calibrated, corrected and registered lights, the maps and the masters are never
hashed to decide it. Every fingerprint carries the engine generation and its family's semantic revision
(`kWorkflowEngineGeneration`, `ArtifactFamily` in `ArtifactSchema.h`), so an artifact computed under another
generation or revision fails the fingerprint comparison before anything else is consulted and is recomputed
once rather than reused through a stored digest. The masters carry a stamp like every other artifact, so the tracker
and the planner decide their moved-or-restored case by header and not by digest; a row that holds a digest and no stamp
is recomputed (`PlannedMasterReuseTest.AMasterRecordHoldingADigestAndNoStampIsRecomputedOnceTheMasterMoves`). What still
hashes, and why:

| What | Digest | Why it stays |
|---|---|---|
| Masters (bias, dark, flat, stacked, colour) | SHA-256 of the whole file, read once after the write committed by `FinalCommittedArtifact::fromFile()` through one opened handle and recorded as `content_digest` | When the run archive keeps the stacked master's payload, its digest is written beside the archived copy (`sha256`, `digestProvenance`, `sourceModifiedAtUtc`) as provenance for a user-facing deliverable. Nothing decides reuse from it |
| Normalization coefficient documents | the sidecar's `coefficientPayloadSha256`, the SHA-256 of the whole document file; checked by the normalization stage before it reads the document and by the planner when the group's reuse is decided, and pinned by the snapshot | The digest is embedded in the sidecar as a content identity of a document that is bounded in size, not used as a cache check on an image |
| Every document envelope | the payload's length and SHA-256 in the envelope header, verified on every read | A truncated or substituted payload is refused rather than read; each reader states the most payload it accepts - 64 MiB by default, 4 MiB for a drizzle descriptor, `kMaxLocalNormalizationDocumentBytes` plus 1 MiB for the coefficients |
| Dense drizzle payloads | the length and SHA-256 its descriptor states, checked before the payload is decoded | A payload is read only beside the descriptor it was written for; its size follows the source extent, two Float32 per pixel |

`ArtifactTracker::contentDigestForFile()` therefore remains for the coefficient documents alone. `Bytes hashed` in the
run report counts what the run's accumulator is handed: the masters' post-commit digests, one read of each master a run
writes, and nothing for a master it reuses. The snapshot counts the coefficient documents it pins in its own
diagnostics (`ExecutionPreparationDiagnostics::bytesHashed`); the planner's and the normalization stage's coefficient
checks pass no accumulator. None of these counts scales with the number of intermediate frames, and preparing a plan
over recorded masters hashes none of them
(`PlannedMasterReuseTest.APlanDecidesARecordedMasterWithoutReadingItsSamples`).

No reuse check consults a master's digest - not the tracker the calibration and stacking stages ask when they run, not
the planner's strict check and not the launch check.

## Implementation map

Paths that begin `.../` are below `src/AfterNightProcesses/processes/ImageIntegration/`.

| Concern | Location |
|---|---|
| The identity type, its keywords, alphabet and limit, and reading and stating its cards | `src/AfterNightEngine/include/AfterNightEngine/IO/ArtifactIdentity.h`, `src/AfterNightEngine/src/io/output/ArtifactIdentity.cpp` |
| The output transaction, the pending suffix and the sweep | `src/AfterNightEngine/include/AfterNightEngine/IO/OutputTransaction.h` and `PendingOutput.h`, `src/AfterNightEngine/src/io/output/OutputTransaction.cpp` |
| The workflow's identity and publication of an image | `.../workflow/WorkflowArtifactStamp.h/.cpp` (`workflowArtifactIdentity`, `publishWorkflowImage`) |
| Reuse decision, header read and record publication | `.../workflow/ArtifactTracker.h/.cpp` (`isArtifactValid`, `sessionArtifactStampReader`, `recordArtifact`, `recordCurrentRunCommittedArtifact`, `FinalCommittedArtifact`) |
| The artifact tracker's schema, the engine generation and every family's revision | `.../workflow/ArtifactSchema.h` |
| Planning-time view of the same records | `.../workflow/ExecutionPreparationSnapshot.cpp` (`MetadataOnly`, `StrictContent`, `matchesCurrentFacts`), with the header it reads through `.../workflow/FrameHeaderReader.h` |
| Documents that are not images | `.../workflow/DocumentPublication.h/.cpp`, `.../workflow/DrizzleDescriptorDocument.h`, `.../workflow/stages/NormalizationArtifactSupport.cpp` |
| The interrupted-run sweep | `.../workflow/WorkflowExecutor.cpp` (`sweepAbandonedWrites`) |
| Registered and comet-aligned lights' revisions | `.../workflow/stages/LightsAlignmentStage.cpp` (`registeredOutputMatchesRevision`), `.../core/StandaloneCometRegistration.cpp` |
| Coverage, engine | `src/AfterNightTests/Test_EngineImage/OutputTransactionTest.cpp`, `src/AfterNightTests/Test_EngineImage/ArtifactIdentityTest.cpp` |
| Coverage, workflow | in `src/AfterNightTests/Test_ImageIntegration/workflow/`: `test_artifact_tracker.cpp`, `ArtifactIdentityTest.cpp`, `test_execution_preparation_snapshot.cpp`, `test_smart_stage_skipping.cpp`, `PersistenceContractsTest.cpp`, `WorkflowRuntimeTest.cpp`, and the stage suites `test_calibration_stages.cpp` (with `PlannedMasterReuseTest`, the planner's and the launch check's decision on a master a run recorded), `test_lights_calibration.cpp`, `RegistrationStageTest.cpp`, `AlignmentStageTest.cpp` and `StackingStageTest.cpp` |

## Two hazards to remember when adding a new writer

**Keyword inheritance.** A product derived from a stamped image must not inherit that stamp, or a registered frame could
validate against a calibrated frame's manifest entry. The output transaction closes the hazard for every writer that
goes through it: it withholds the source's `ANARTID` and `ANRUNID` cards from the encoder whether or not an identity is
stated, and withdraws any identity card the caller's own intent carries in favour of the one it states, so a derived
product carries its own stamp or none. An identity travels in the output intent, so stating one without an intent is
refused (`ErrorCode::IncompatibleArguments`). `OutputTransactionTest.IdentityTravelsInTheIntentAndIsNeverInheritedFromTheSource`
covers it. A new writer therefore calls `publishWorkflowImage()` (or `an::writeImage()` with an `OutputRequest` that
states `identity`), never an encoder directly, and records a file the tracker should know about afterwards, through a
tracker built with `sessionArtifactStampReader()`. Never write a generated artifact directly to its destination name.

**Card limits and vocabulary.** An identity is at most 32 bytes of letters, digits, underscore and hyphen, so a full
64-character SHA-256 cannot be stated as it is; `workflowArtifactIdentity()` reduces it before the engine sees it. The
tracker reads the stamp from the FITS vocabulary (`CodecNamespace::Fits`), and the workflow names its generated files
with FITS extensions; a stamp written into another container's vocabulary would read as no stamp and fall through to
recomputation.
