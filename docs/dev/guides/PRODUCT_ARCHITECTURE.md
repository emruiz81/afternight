# The product on the engine

How the product - the platform and consumer runtime libraries, the command-line tool, Controls, Processes, the
ImageIntegration workflow, the application and the extension hosts - uses `AfterNightEngine`, area by area: where
each area's code is, what it asks of the engine, which suite holds it and why it is built the way it is. `AGENTS.md`
states the rules every area keeps ("The product on the engine"); the other guides in this directory describe the engine
itself.

## The libraries under the product

### AfterNightPlatform (`src/AfterNightPlatform/`)

The process services the application owns rather than an engine, in `namespace an::platform`, included as
`<AfterNightPlatform/<Name>.h>`: `Logging.h` (the one spdlog logger per process, its file, ring-buffer and
session-console sinks, and the `LOG_*` macros), `LoggingConfig.h` (the persisted `logging.conf`), `CrashHandler.h`
(Crashpad), `Sha256.h` (the product's file digests, through the x86 SHA instructions where the processor has them),
and `Text.h`, `FileSystem.h` and `SystemMemory.h` for the text, path and memory helpers the product uses. It links no
engine and no Qt - `check_build_graph.py` enforces that, and that the Lite host and the protocol library do not reach
it - and its own `.clang-tidy` enforces the naming table. The services live outside the engine because the engine
owns no globals and writes no log, and the library links no Qt so that the command-line tool and the runtime backend,
which link none, can use it. `Test_Platform` is its suite.

### AfterNightConsumerRuntime (`src/AfterNightConsumerRuntime/`)

What a consumer of the engine owns that the engine does not, Qt-free and Python-free, in `namespace an::consumer`,
included as `<AfterNightConsumerRuntime/<Name>.h>`.

- `EngineSession.h`: the session that owns one `EngineContext`; it checks and records settings that take effect at
  the next construction (`requestSettings`, `applyPendingSettings`, and `reopen` after a failed construction leaves
  it unavailable), hands out `SessionHold`s and `PreviewReservation`s, and admits operations - one bounded queue per
  share, everything serialized at width one, a preview share above it only while a preview runs, waits or a dialog
  holds one.
- `OperationScope.h`: one admitted operation's execution, scratch and stop state, released execution first and
  admission slot last.
- `ImageAsset.h`: the eight-way `AnyImage` under read leases and one write lease (releasable on any thread, writers
  preferred), a process-unique identity, an `AssetRevision` published when the writer lets go, and snapshots as new
  assets. A lease is releasable on any thread because the GUI thread hands its read lease to a worker, which a
  standard shared mutex forbids, and the identity is process-wide because a derived cache can outlive its session.
- `ConsumerMemory.h`: allowances per category admitted before allocation, eviction before refusal, and required
  buffers counted as overruns.

It links the engine and the platform library only - `check_build_graph.py` enforces that and that nothing below it
reaches it - with its own `.clang-tidy`. `Test_ConsumerRuntime` is its suite, and the `fixtures/engine/` adapter runs on
it.

### AfterNightCLI (`src/AfterNightCLI/`)

`AfterNightCLI [options] <input> <output>`, in `namespace an::cli` with its own `.clang-tidy`: one image read,
stretched by the display presentation - each channel its own automatic screen transfer - and written with an explicit
`OutputIntent`, every step one admitted `OperationScope` of an `EngineSession` and every image an `ImageAsset`. It
links the consumer runtime, the engine and the platform library and nothing else - no Qt, no Python.
`CommandLine.h`'s `kOptions` declares every option and its default; the parser and `--help` read it. `OutputPolicy.h`
decides the representation (the input's, or the nearest one the container holds) and refuses PNG and JPEG for a
filter-array mosaic and FITS for an X-Trans one; FITS and XISF outputs carry the input's world coordinates through
the intent, because the presentation drops the cards that hold them. `InterruptWatch` makes Ctrl+C, Ctrl+Break, a
closing console or SIGTERM a stop request, so an interrupted run exits 130 and leaves no file; the other exit codes
are 0, 2 (usage), 3 (input), 4 (processing), 5 (output) and 6 (engine). The tool sweeps no pending output from its
directory, because a payload a crashed run abandoned cannot be told from one a concurrent run has reserved: a run
ended by a kill or a power loss leaves its `.antmp` file for a person to remove. `Test_AfterNightCLI` runs the units in
process and the built binary beside it.

## Controls (`src/AfterNightControls/`)

The reusable Qt layer, `namespace an::ui`: `theme/` (tokens, the proxy styles, animation), `controls/` (the viewer,
the process framework, dockable controls, display modules), `app/` (the application services and coordinators the
application and processes reach through `controls/IAppContext.h`), `session/` (the engine bridge), and `components/`,
`widgets/` and `dialogs/`. The theme rules are `docs/dev/THEME_SYSTEM.md`.

### The session bridge (`session/`)

What Controls reaches the engine through, in `namespace an::ui` with its own `.clang-tidy`.
`session/IEngineSessionProvider.h` lends the process's session; Controls opens none and builds no engine context.
`session/ImageDocument.h`: the record around an `ImageAsset` that a UI interface carries - an identity that outlives its
asset, names, sequence, presentation, open or closed - read under the current asset's lease and written through
`ImageDocumentWriter`, whose `replace` makes another asset the document's image; `tryReplace` makes one without a
writer, waiting for no reader - what reads the image shown keeps reading it - and refused while a writer holds the
document, which claims it under its mutex. `session/CommitTicket.h`: a dialog's
`RequestTracker` issues tickets that supersede and stop the one before, and `verdict()` on the GUI thread decides
whether a result may be published. `session/DerivedResultKey.h`: the key a cached result is filed under. `session/ExecutionScope.h`:
an admitted operation whose sinks copy every payload, coalesce progress, bound diagnostics and deliver to a guarded
receiver on the GUI thread, ending with one terminal report. `session/ImageReads.h`: the images a run reads beside the one it
writes, named on the GUI thread and leased before the run is admitted, one lease each in order of identity with an
edit's own writer at its image's place, and read in the body through `readBeside`, which shares the run's lease and
never waits. Each of those rules keeps a deadlock out: leased before admission, a run never holds a thread an edit
needs while it waits for that edit's writer; in one order, two edits that read each other's image never wait on each
other; and with one lease per image, a second lease never queues behind a preferred writer that waits for the first.
`Test_ControlsSession` holds it.

### The service interfaces (`controls/IAppContext.h`, `app/AppContextHelpers.h`, `common.h`)

`IAppContext::getEngineSessionProvider()` lends the process's session and `getRawOpenPolicyService()` the published
RAW policy. `IViewFactory` takes an `ImageDocument&&` - a view made owns it, a view refused leaves it with the caller -
and a sequence view's document states its `DocumentSequence`; `ITempFileService` takes an `an::SequenceFormat`;
`IOutputIntentProvider::buildOutputIntent(source descriptor, CodecFormat)` answers an output intent per source and
container. The header declares those engine and bridge types rather than including them, so it reaches no engine
header, and a unit that calls or implements a member naming one includes what it uses.
`process_utils::createImageViewOnUiThread` moves a document into a new view on the GUI thread, blocking its worker
until the view exists; it is never called from a task on the thread pool, because the application's closing waits for
that pool on the GUI thread and a task waiting for the GUI thread would never be answered. A document moves rather
than being shared so that a view and its caller never both believe they hold the document the view closes.
`common.h`'s summary reports take an `ImageDescriptor`, an `ImageMetadata` and the caller's `ReportFileFacts`, read the
acquisition through `projectAcquisition`, and keep the product's labels in Controls. `Test_ControlsServices` holds it
against the views its context's factory makes.

### The viewer (`controls/ImageViewControl.h`, `widgets/ImageViewWidget.h`, `widgets/PresentationBridge.h`)

A view owns the `ImageDocument` it shows (`setDocument`, `setSequenceDocument`, `document()`) and closes the one
before. Reads (`loadImageFromFile`, the static preview's calibration by its masters), edits (`editImage`, the mirrors
and quarter turns, Apply Stretch), writes, the overlay's star detection and the widget's presentations run on the
thread pool and publish on the GUI thread only when their `CommitTicket` still commits; a file read's ticket comes
from `RequestTracker::issueUnbound()`. An edit holds the document's writer from its read to its publication and is
recorded before the view shows it. The widget presents through `presentForView` (`widgets/ViewPresentation.h`) what it
paints (`viewDisplayGeometry`): zoomed out, its image's display proxy (`session/DisplayProxy.h`), the engine's integer
average at the largest factor that leaves it at least the painted size, kept per revision and factor; at any zoom, the
region it shows with a margin, presented again when a pan leaves it - judged against the region on its way, when one
is, because that region is painted over the presented one when it lands. A region or a proxy is stretched by an
analysis of the whole image, measured once per revision and kept under its key, never by its own statistics, so the
image is stretched alike at every zoom and wherever it is panned. An image that holds the automatic stretch
already - frames written as they were shown, as the Image List Analyzer's proxies are - is stated so by whoever
shows it (`setImageHoldsAutostretch`), and the autostretch mode presents it as it is. A static preview whose owner
keeps the statistics of what it shows measures none and shows those it is handed (`setMeasuresPreviewStatistics`,
`showPreviewStatistics`). The mask layer is
presented in the same job at the same reduction and region. The bytes are blue-first and premultiplied, taken from the
context the presentation runs in - whose memory a session keeps for the next frame while a preview is open - wrapped in a
`QImage` only through `presentedQImage`, whose cleanup owner keeps them alive, and drawn as they are: no pixmap is made
and nothing is converted on the GUI thread. The engine operations the view runs - read, demosaic by the one `raw/demosaicAlgorithm`
setting, calibrate, measure, orient, detect stars, write - are `controls/ImageViewFiles.h`. The history, attached
processes, the sequence player, the tool windows, the astrometry overlays and the lens histogram are reached through
`controls/ImageViewCompanions.h`, defined beside each component, so the view includes none of their headers. The
widget's `presentationTelemetry()` counts its presentations and keeps what they cost - the engine's time on a worker,
the proxies reduced and their time, the whole-image measurements, the pixmaps made (the lens's alone: the paint makes
none) and the GUI thread's time adopting what it painted - and how far the one painted last was reduced;
`presentedAnalysis()` is the analysis that one was made through.
`Test_ControlsViewer` holds the viewer and its companions.

### The process framework (`controls/ImageRTPreviewControl.h`, `controls/TypedProcessControl.h`, `controls/ProcessExecution.h`)

A run is issued on the GUI thread from a snapshot - the parameters, the channel the selector's item carries
(`selectedChannelScope`), the modulation, the mask, the preview area, the step (`describeTypedProcessStep`) and the
other images it reads (`typedImagesRead`), leased before the run is admitted - and a body reads none of the window:
`executeTypedProcessImpl(source, destination, run, params, scope)` reads a `ProcessSource`, writes a
`ProcessDestination` that starts as the source's planar copy with its cards, and answers an `an::Status`. A process
that declares, for a run's parameters, that its body writes every sample (`typedProcessBodyCoverage`) is handed a
destination whose samples start unwritten wherever the source is planar and every sample of it valid
(`ProcessDestination::forBody`): the pointwise and stretch bodies read the source rather than the destination, and
nothing of the source is copied into a result that overwrites it. `executeTypedProcessUsingChannels` runs the body's
iteration through the engine's channel scope (`applyProcessChannelScope`), which is told whether the destination holds
the source so it copies nothing twice; the framework applies the modulation and the mask (`blendProcessResult`, an
integer image's blend through the engine's compiled integer lanes) and a preview
area's crop and paste (`cropProcessRegion`, `composeProcessRegion`). Previews drain to the latest request and are shown
only when their ticket commits; a preview never holds an Apply back; Apply in place is the view's `editImage`, so the
history records the step before it is shown; a sequence run publishes a whole sequence or nothing. A preview asked for
while a person holds a slider the window watches - every one the `INIT_*SPINBOX_CTRL` macros make - or another control
the window is told is held (`controlHeld`: the curves transformation's curve editor reports a point taken with the left
button, `CurvesWidgetListener::onCurvePointHeld`) is an interactive frame (`ProcessRun::frameKind`), admitted for no
more than the preview reservation, and letting go or holding still for 150 ms asks for one settled frame; every other
request is settled. Frames start no closer than a period apart, a second
over the Processing page's preview rate (`readPreviewFrameRate`), read off the GUI thread when the preview is turned on,
the window activated or a chain of frames begins, and in force for the chains that begin after the read lands. The preview view
shows the target's own asset, shared through `ImageDocument::share` rather than copied, until its first frame lands and
whenever the parameters are what the process declares its identity (`isTypedIdentity`), for which nothing runs. Nothing
runs either for parameters whose result a frame already made: a process that names a key for what a preview of its
parameters shows (`typedPreviewResultKey`) has the result of a frame over the whole target kept under that key, and a
body that makes, from one computation, the images other values of a parameter only choose among hands them to the run
under their keys (`ProcessDestination::keepAlternative`) - Auto Gradient Removal's corrected image, gradient model and
protection mask, from one fit (`removeAndDrawImageAutoBackground`). The frame blends each by its modulation and mask as
it blends its result (`blendProcessAlternative`), and parameters of a kept key over the same revisions of the target
and mask, and the same blend, show the image kept, shared, at once - stretched through the analysis of the image the
process stretches its preview from (`getExecutionAutostretchSource`) kept for the view's presentation, and otherwise run
as a frame, or by the view's own measurement when the process names none, as Auto Gradient Removal does for its model
and mask. What is kept is in the session's memory, up to a few full-size
images, so it goes when the next frame starts, when the target or its mask changes, when the preview closes and when
the process says its parameters changed (`discardKeptPreviews`). An
Apply or a Reset is followed by one preview of the parameters the reset leaves, and a failed preview says why in the
window's banner, once for each cause. A frame's result is shown through `ImageDocument::tryReplace`, so a reader of
the image shown - the view's presentation, the lens, the statistics window - never holds a frame back. The preview
histogram and a sequence run's frames are reached through `controls/ProcessCompanions.h`, the dialog layout helpers
are `controls/ProcessPreviewLayout.h`, and the progress dialog lends a stop token and observers. While a dialog shows
its preview the session keeps the large blocks its frames give back (`EngineSession::recycledMemory`), so consecutive
frames alternate between two blocks of memory already resident rather than faulting new memory in.
A frame is one admitted operation: its worker runs the body, counts what the dialog's histogram shows and presents
what the preview view paints, and the GUI thread hands the presentation to the view (`ImageViewWidget::adoptPresentation`,
filed under the key the view's own request would make) and the counts to the histogram (`IPreviewHistogram::showCounts`);
frames are queued above every other task of the pool. A process declares what an interactive frame may run over
(`previewScaling()`): `PreviewScaling::FullOnly`, the default, runs every frame over the whole target;
`PreviewScaling::Decimated` - Brightness and Contrast, Gamma Correction, Exposure, Saturation, HSL, LCh, Curves
Transformation, Gaussian Blur, Unsharp Mask and High Pass - runs an interactive frame over the target reduced by the
preview view's factor, each pixel the mean of its block
(`reduceForPreview` in `session/PreviewProxies.h`), or at full scale over the part of the target the view shows
(`ProcessRun::proxyFactor`, `ProcessRun::partOfTarget`, `ProcessRun::part`), the reductions kept by the revision they
were made from (`PreviewProxyCache`); and `PreviewScaling::NativeRegion` - the median, bilateral, tighten-edges,
cosmetic-correction and morphological dialogs, the star reduction and the two star mask recombinations - runs it at full scale over the part the view shows at every zoom, presented at the view's reduction,
and every frame over the whole target, settled, where the view shows all of it. Either answers a change made without a
held slider with an interactive frame at once. A body that reads a pixel's neighbours states how far, from the plan it
builds (`previewRegionMargin`, `typedPreviewRegionMargin`; `gaussianReach` and `detailReach` in
`processes/SpatialProcessSupport.h`): a frame over the part shown reads the part grown by that reach within the target
and shows the part alone, so the part's edge is answered as it is over the whole image, and a body that reads another
image of the target's size cuts the same part of it (`ProcessRun::part`, `process_utils::LeasedImage::read`). Such a
frame is painted as a layer over what the view paints while that is the view's presentation of its document at the
frame's stretch, channel and reduction (`ImageViewWidget::paintsDocumentOver`); otherwise it runs over the whole region
the view presents and is its presentation. A `NativeRegion` frame over a part makes no histogram - nothing short of the
whole result stands for its counts - and the settled frame counts the whole result. A drag that moves again while the
settled frame it was owed is on its way stops that frame.
A run over a view of the target (`ProcessRun::readsViewOfTarget`) takes the target's measurement wherever a body
measures its source. The histogram of an interactive frame over a reduction is the target's codes remapped through
the body run over a row of every code
(`countCodes`, `codeRampOf`, `remapHistogramDisplayData`) for a body that maps each channel alone
(`mapsEachChannelAlone`) over an eight- or sixteen-bit target, and otherwise the count of the body run over the target
sampled one pixel per block (`sampleForPreview`); a settled frame counts its whole result. In autostretch a `Decimated`
preview is stretched through the target's own analysis, measured once per revision or taken over from the preview
view, which measured it showing the target (`ImageViewWidget::keptAnalysisOf`). Only an interactive frame's pixels are
approximated, never a statistic: averaging a block shrinks the noise the median absolute deviation measures and
narrows every histogram peak, so a measurement or a count of a reduction would move the stretch and the histogram
between a drag and the settled frame it rests on. The histogram transformation and the
generalized hyperbolic stretch look an eight- or sixteen-bit image's codes up rather than stretch a working copy of it
(`publishStretchOfSource` in `processes/StretchProcessSupport.h`), so their full-resolution frames keep the rate.
What a frame converts is converted once: its source carries the conversions the dialog keeps
(`ProcessSource::reusing`, a `ProcessConversionCache` of the latest revision of each asset) - the forward half of a
component of another encoding, prepared per revision of what the frame runs over and scope request
(`an::prepareChannelScope`), and the source and the mask laid out a plane per channel in the result's representation
for the blend - and an Apply reads none of them. A preview area's crops are cut once per revision and rectangle, and
an interactive frame over the area whose patch is the target's kind of image, every sample of it and of the target
valid, is shown by its patch, which the view paints as a layer over its own presentation
(`ImageViewWidget::adoptPresentationLayer`), its histogram derived from the target's counts less its part's plus the
patch's (`replacedPartHistogram`); a frame that composes pastes a patch of the target's own description into a copy of
the target, and the settled frame counts its composite exactly.
`TypedProcessControl<ParamsT>` is the base every process with execution parameters derives from. A window's
`runTelemetry()` counts its previews and runs by outcome and its shown previews by kind of frame, keeps how long the
shown previews took, and counts what its frames ran over, how they made their histograms, the presentations the view
adopted and the analyses measured, the conversions and crops the frames made and read again, the composite bases they
built, the frames shown by their patch and those over the part shown painted as a layer, with the time the frames' work
and their admissions took.
`Test_ControlsProcessFramework` holds the framework, and `Test_ImageProcessControl` and `Test_ImageRTPreviewControl`
the process and preview windows; the latter also holds the settled previews of the
stretch-based dialogs and of brightness and contrast to the references under
`src/AfterNightTests/fixtures/references/realtime-preview/`, and carries the opt-in drag benchmark
([BENCHMARKS.md](BENCHMARKS.md)).

### The history (`controls/ImageHistoryTracker.h`, `controls/ImageHistoryBackupQueue.h`, `controls/ImageHistoryCompanion.cpp`)

A step is a copy taken under a lease (`copyForHistory`), admitted against the session's
`MemoryCategory::PendingSnapshots` allowance before it is made - an edit's copy waits for room, a state already shown
is `ReservationKind::Required` - and written by the history's `BackupQueue` as the engine's image snapshot
(`writeHistorySnapshot`), samples, metadata and validity mask together, read back only for the identity the history
drew for it (`readHistorySnapshot`). A step is a copy rather than the shown asset, so the history need not know every
path that may write an asset in place through its write lease; and a state already shown cannot be refused without
the timeline ceasing to match the view, so its copy is admitted beyond the allowance. The view reaches the history
through `IViewHistory`, made by `createViewHistory`
in `controls/ImageHistoryCompanion.cpp`: its edit worker runs `editPreparation` - the state the edit replaces written,
the result's copy admitted, a sequence frame written - so `registerEditBeforePublishing` on the GUI thread never
waits; undo, redo, a jump and a removal read on a worker and show through the view's `replaceImage`, the current step
moving once shown. A sequence step references its `DocumentSequence` and keeps the frame it changed, reading the
sequence's own frame through `openProcessFrames`. Each history writes into its own `AN_HISTORY_*` directory and
reclaims only that. The window previews on a worker through `presentForView` and shows the histogram through
`createPreviewHistogram`. A step that is linear data (`isLinearData`, `controls/AutostretchApply.h`) is presented
through the unlinked autostretch the recent-file thumbnails take, so it looks as the dashboard shows it, and any other
step as it is. The step's title and details are `AnElidedLabel`s of a set number of lines, because a label that wraps
to its text moved the preview below it from one step to the next. `Test_ControlsHistory` holds the history, and
`Test_ImageViewControl` the viewer's history cases.

### The file workflows (`app/FileManager.h`, `app/FileWorkflowRunner.h`, `app/FilePolicies.h`, `app/FileCompanions.h`)

The file menu's open, save and recent files run as requests: `FileWorkflowRunner::openFiles`, `openRecentFile` and
`saveFile` return at once, read or write on the thread pool, and ask their questions and make their views on the GUI
thread - a worker never waits for it. `PersistenceCoordinator` publishes the RAW settings as an immutable
`RawOpenPolicy` with a product codec registry of its own (`createProductCodecRegistry`); an open takes the one
published when it is asked for and reads through `readViewImage` with the policy's `readRequest`, which balances a
camera mosaic (`resolveCfaWhiteBalance`, `applyCfaChannelGains`) and turns a camera frame by its recorded orientation.
Every open path demosaics by the one `raw/demosaicAlgorithm` setting through `app/DemosaicTextMaps.h`, whose "Auto"
Bayer pattern is the image's own and never guessed, and an X-Trans mosaic by the setting's tabulated counterpart
(`demosaicMethodForMosaic`). A save builds its `OutputIntent` from the export settings when it is asked for
(`buildExportIntent`, `PersistenceCoordinator::buildOutputIntent`), captures the document's asset and revision, and
`writeViewImage` converts to the intent's representation and refuses a mosaic for PNG or JPEG, and an X-Trans one for
FITS, before writing anything; the view's pending changes are cleared only when it still holds that revision.
Recent-file thumbnails are rendered on a worker (`renderRecentFileThumbnail`); an image that is linear data is
stretched with each colour channel on its own, as the view's autostretch shows an image unless asked to link.
`isLinearData` decides: an `ANLIN` card recording the samples nonlinear, or an encoded transfer function, keeps an image
as it is whatever else it states; then a declaration of linear samples - the card, or a linear transfer function - and,
for an image declared neither way, a camera RAW frame or a header that records a frame's type (`IMAGETYP`) or counts
the frames of a stack (`STACKCNT`, `NCOMBINE`). The product's stacks record no linearity, only those cards. The
nonlinear record goes first because an extension's result keeps the colour space its source was lent with, so a
linear transfer function on it is not evidence. Temporary files go below the session's own
`RuntimeStorageCoordinator::getTempDirPath`, held by a lock file; only an ended session's directory is reclaimed. A
lock rather than an age decides what is reclaimed, because an age would reclaim the history of a running session left
idle and keep a crashed session's files until they expired, and nothing but a session's directory is removed, since
a temporary folder the person chose may hold files of their own.
Sequences are opened and written through the companion seam `app/FileCompanions.h`, defined beside the sequence player.
`Test_ControlsFiles` holds the coordinators.

### The plate solve and the catalogs (`app/PlateSolveService.h`, `app/PlateSolveUtils.h`, `app/GaiaCatalogSource.h`, `app/BundledCatalogs.h`)

`PlateSolveService` keeps its solver order, its runners and `PlateSolveResult`, whose `solution` is a `WcsSolution`
in engine pixel coordinates; `solve` is for a worker and `solveAsync` reports on the GUI thread, running a
`PrepareInput` on its worker first. Every solver's answer is read by `readSolvedWcsHeader` - `parseFitsHeaderCards`,
then `parseWcsCards` with `RADESYS = 'ICRS'` stated on cards that name no frame - and an image's own cards by
`readImageWcsSolution` (`app/PlateSolveTypes.h`), which the viewer, the astrometry window and the embedded-solution
check share and which takes the J2000 equinox with no frame as the ICRS: the external solvers solve against ICRS
catalogs but state only `EQUINOX = 2000.0`, which the engine's card bridge refuses rather than assume a frame, and at
J2000 the ICRS and FK5 differ by tens of milliarcseconds, a small fraction of a pixel of any image the product solves
or shows. A solution reaches an image only as an edit
the history records: `metadataWithPlateSolution` (`withWcsCards`, `PLTSOLVR`, `FOCALLEN`, `XPIXSZ`, `YPIXSZ`) or
`metadataWithoutPlateSolution` (`withoutWcsCards`), then `copyImageWithMetadata` through the view's `editImage`; the
plate-solve window applies an answer only to the document revision it read and asks Clear Astrometry through the
dialog service. The hints are `readPointingHint` and `readOpticalHints`, the epoch `julianEpochYear` of the
acquisition record's exposure start (`app/ObservationEpochUtils.h`). The internal solver detects on the image read in
the session and solves with `solvePlate` against `GaiaCatalogSource::regionCatalog` - the installed chunk catalog's
tiles inside a HEALPix disc, mapped, decoded and carried to the epoch - one region for a hinted solve and one per
blind centre; the astrometry.net web solver is one state machine on the worker (`setAstrometryWebEndpoint` points it
elsewhere). The bundled catalogs are read on a worker. A reader that only asks what lies in a field - the overlays of
every view, Annotate Image - shares one copy of each (`sharedBundledCatalog`, `app/BundledCatalogs.h`): it is read
once, a second reader waits for the first one's read, and it is kept for the engine context the session held when it
was read and read again for another, because a `SkyCatalog` leases the session's engine. It builds no name index: the
bright stars, 83 479 to magnitude 9, then hold about 19 MiB and load in about 40 ms, where the catalog search's copy
with its index holds about 51 MiB. A reader that names a source of its own, and the search by name, read their own
(`loadBundledCatalog`). The overlays (`controls/*DisplayModule`,
made behind the seam in `controls/AstrometryOverlays.cpp`) ask `FieldCatalogQueries` for the field's entries on a
worker, project the last answer at paint and draw an engine pixel coordinate times the scale, with no half pixel to
subtract; the star labels are measured when the view or the answer changes, brightest first, and a fainter star whose
label or marker would cover a brighter one's is left out, so a paint draws a bounded number of them. The SPCC
region is the solution's `fieldCenter` and `queryRadiusDegrees`, its chunks `selectSpectrumChunksForRegion` on a
worker. `Test_ControlsAstrometry` holds all of it.

### The annotated picture (`controls/annotation/`)

Annotate Image makes a picture of a solved image with its objects, stars and coordinates named, in four steps that each
know nothing of the one after it. `controls/annotation/AnnotationSettings.h` is what a person chooses - layers, a level
of detail that writes the catalogue selection, label text, palette, sizes, corners, output - stored as one JSON text
under `annotation/settings` (title excepted) and read back field by field through fixed tables. `buildAnnotationScene`
turns a `WcsSolution`, the bundled catalogues' entries (`queryAnnotationCatalogs`: both catalogues, the field's radius
and two degrees more, on a worker) and the header cards into marks, grid lines clipped to the frame with their edge
crossings, the sky's directions and scale at each corner, and the panel's facts. Entries of different catalogues that
name one object - one position, or one common name close together - become one mark under the designation people use
most (Messier, NGC, IC, Caldwell); two entries of one catalogue never merge. `layoutAnnotations` shapes every label into
glyph outlines (`QPainterPath::addText`, unhinted, at one size and scaled) and places them greedily in the scene's
priority order against an occupancy grid: the decorations first in their corner stacks, then the grid's edge labels,
then each mark's label beside it, on a leader line farther out, or not at all; a place that covers a smaller mark or
crosses another object's outline costs more than a leader line, so a label covers another object only when no line
reaches a free place. A star is named by its proper name, its Bayer or Flamsteed designation or, when every star is
asked for, its catalogue number, and a star left unnamed may be marked alone (`AnnotationMark::markOnly`).
`paintAnnotations` draws a scene through a layout with any `QPainter`. The style sets the text and the lines apart -
typeface, weight, slant, label and panel sizes and backing on one side; line width and opacity, the dark edge behind the
lines, the fill and the star marker on the other - so no choice of one changes the other: the letters' halo follows the
text's size alone. Every length is a fraction of the image's reference length - the geometric mean of its sides - so the
window's preview, a smaller picture and the full-size one are one drawing at different scales and the preview shows
where the picture puts a label. The base under the annotations is the view's own presentation (`presentAnnotationBase`:
`presentForView` at the largest display-proxy factor that leaves it at least the picture's size, stretched through the
whole image's analysis, the view's kept one when it has it), so a picture of any size is stretched as the view stretches
the image. `annotatedPictureAsset` makes the drawn picture an image of the session - eight-bit display RGB, recorded
nonlinear, `derivedFrameCards` plus the solution `resizeWcsSolution` moves to the picture's grid - which
`writeViewImage` writes under the export settings' intent or a new view shows. `AnnotateImageControl` runs each step on
a worker and drops an answer a later request, a change of the image or its closing made stale: the catalogue query once
per solution, the scene and layout once per change of what is shown or placed (a colour only repaints), the preview's
base per stretch and zoom, and the picture with its own read lease taken on the GUI thread before admission. It is one
per view (`ViewTool::AnnotateImage`), opened from the view's toolbar and **Image > Information**, the latter enabled by
`ImageViewControl::canAnnotateCurrentImage`. `Test_ControlsAstrometry` holds the steps and the window, and
`ApplicationMenu` the menu entry.

### Statistics, curves and stars (`widgets/HistogramComputation.h`, `widgets/HistogramCompanions.h`, `widgets/ToneCurveModel.h`, `controls/StarFilter.h`)

A histogram widget draws the `HistogramDisplayData` its owner hands it and counts nothing:
`computeHistogramDisplayData` runs `computeImageHistogram` under the owner's execution over the samples the image's
validity admits, reading every representation through its display range (`widgets/DisplayNormalization.h`, a signed
integer from its smallest value to its largest), and a `HistogramCountScheduler` counts an asset on the thread pool
and shows only the latest count on a widget that still exists. A widget counts nothing because counting needs an
execution, an engine context and a lease on the image, which its owner already holds. A count is admitted as `Apply`,
and takes its turn with
other work, unless it is part of a preview: the real-time preview's histogram, counted again with every preview, asks
for `Preview` and is sure of the preview reservation while other work waits. The histogram window counts the view's
image through it; the process
dialogs' histogram (`createPreviewHistogram`) and the lens's (`createLensHistogram`) are defined in
`widgets/HistogramCompanions.cpp`. The statistics window, admitted as `Apply` for the same reason, measures
`computeImageStatisticsReport`
(`controls/ImageStatisticsReport.h`: one call of the engine's advanced statistics per channel, which returns the basic
statistics with them and is asked for no iterative k-sigma estimate, as fractions of the display range, the clipped
samples left out by a range or a mask joined with the image's validity) on a worker and fills its table on the GUI
thread; `scaledStatisticsFigures` scales a variance by the square. A curve is a `ToneCurveModel` the widget owns,
evaluated by `evaluateCurve`; a structuring element a `StructuringElementGrid`; the midtones and generalized-stretch
widgets draw `evaluateMidtonesTransfer` and `evaluateGeneralizedStretch`. The star windows read one `StarListPtr` of
`DetectedStar` through `filterStars` behind `StarsFilterWindow`, both layouts keeping their figures, the random share
drawn from a seed; the selection draws each fitted ellipse at its engine pixel coordinate times the scale, and
`makeProfiledStarMask` paints the mask through the engine. `Test_ControlsStatistics` holds all of it, and
`Test_Widgets` the curves and histogram widgets' cases.

### The mask drawing tools (`controls/drawing/DrawingDocument.h`, `controls/drawing/DrawingEditQueue.h`, `controls/drawing/ShapeEngine.h`)

`DrawingDocument::create(session, width, height)` holds the paint, the paint as it was before the active stroke and
the stroke's dabs as engine images (`CoveragePlane`), and the mask - the paint with the shapes screened over it - as an
`ImageAsset` of the session. Every edit (`beginStroke`, `addStrokePoint`, `endStroke`, `floodFill`,
`applyLinearGradient`, `importMask`, the shape edits, `undo`, `redo`, `clear`) takes an `ExecutionContext` first and
the mask's write lease before anything else, and is refused with `ErrorCode::Busy`, changing nothing, while anything
reads the mask; `createOutputMask` is a snapshot. The numbers are the engine's drawing primitives: `BrushEngine` keeps
a stroke's points and asks `planBrush`, `placeBrushSegmentDabs` and `stampBrushDabs` for the rest; each segment lands
with `blendLayerPlane` against the stroke base; a fill and a gradient blend with `evaluateLayerBlend`; an import reads
a grey image's samples or a colour image's Rec. 601 luma and holds every sample as a coverage. A shape keeps one
`ShapeLayer` over the rectangle it can reach (`ShapeEngine::layerBounds`), feathered by `featherCoverage` and screened
over the paint inside the rectangle that changed. An undo step keeps the rectangle of paint a change touched, before
and after, and the shape list it replaced, admitted against `MemoryCategory::HistoryCopies` before it is kept. A
window never edits the document directly: `DrawingCanvasController::submitEdit` queues each edit on its
`DrawingEditQueue`, which lands the queue in input order under one operation asked for with `tryAdmit` as a preview
and retries a refusal rather than dropping it - it asks with `tryAdmit` because the GUI thread never waits for the
session, and keeps a refused edit because a stroke missing some of its points would be a different stroke; the canvas
paints the mask from a read lease and redraws every rectangle changed since its last paint. `Test_MaskDrawing`, an
isolated suite, holds the drawing tools and the mask drawing window's builder, and the document to its recorded
drawing expectations.

### Sequences (`controls/SequenceContainers.h`, `controls/SequencePlayerControl.h`, `controls/NewSequenceControl.h`, `controls/ExportSequenceControl.h`)

`openSequenceSource` opens a SER, an image list or a video as the engine's `FrameSource` under the caller's execution
- SER counting the complete frames of a recording that stopped early, a video stream that states no matrix converted
as BT.601 at limited range - and `readSequenceFrame` reads one frame into a planar image; a `SequenceWriter` converts
each frame for its container (SER eight or sixteen bits, AVI and video eight, a video cropped to an even size, a
mosaic grey in AVI and video), begins the container with the first frame and publishes it through `an::SequenceOutput`
only when `finish` succeeds - let go before that, it leaves nothing, an image list's frames included. The companions
the file workflows, the framework and the history declare - `openSequenceDocument`, `writeSequenceFile`,
`openProcessFrames`, `createProcessFrameOutput` - are `controls/SequenceCompanions.cpp`; a process over a video the
product writes no container for writes Matroska. A sequence states the method its mosaic frames are demosaiced with in
`DocumentSequence::demosaic`, which the view takes from the one `raw/demosaicAlgorithm` setting when it shows the
sequence; a worker reads that, never the setting. The player reads a frame on the thread pool through a request
tracker and shows it through the view's `replaceImage` only while it is the frame last asked for; playback is a
GUI-thread timer that asks for the next frame once one has landed; it keeps the sequence's source open under the
engine context's lease and lets it go when the view shows another sequence or closes it. The source stays open between
frames because opening a video indexes every packet of its stream, and a superseded read finishes its one frame and is
dropped rather than stopped, because a read stopped half way leaves the source failed. `makeNewSequence` writes an
image-list manifest naming the files where they are, or a SER of every file demosaiced by the setting;
`exportSequence` demosaics, stretches through Apply Stretch's `autostretchFrame`, fits and writes each frame, one
admitted step per frame; both windows wait for nothing when closed. An image-list row is an
`ImageListProcessHelpers::ImageListEntry` (`controls/ImageListTableHelpers.h`). `Test_ControlsSequences` holds all of
it.

### The extension buffer seam (`app/SharedBufferManager.h`, `app/ExtensionHostManager.h`)

`SharedBufferManager::exportImage` takes an image under the caller's read lease and an `ImageExportRequest` and states
the buffer's format itself (`sharedBufferFormatOf`): the image's own representation, refusing the four the protocol
does not carry by name; `Mono`, `RGB` or the Bayer pattern seen from the exported origin, refusing an X-Trans mosaic;
`Grayscale` for a grey image and, for a colour one, `Linear` while its transfer function is linear and `SRGB` once it
is not or, stating none, once its processing-state cards record it nonlinear - never from the channel count. A
rectangle in the request exports a region and records it and the whole image's size in the descriptor; a region's
sidecar carries the cards with the solution moved to the region, as a crop moves it, or without the world-coordinate
ones when they state none that can be moved. `SharedImageBuffer` holds the protocol
library's file mapping behind a shared lifetime; `borrowFrame` describes it as a `RawFrame` with
`SampleBufferOwnership::Mapped`, which the engine refuses to adopt; `importImage` copies a buffer row by row into an
engine allocation of the session, reads the sidecar's cards into the asset's codec metadata with the acquisition record
and the solution projected from them, lets the mapping go and returns an `ImportedImage`. A mapping is never adopted
as engine storage because the extension host can change or unmap its bytes while the engine reads them, so owning a
buffer's samples is always this explicit copy, counted and refused like any other allocation. A buffer can be used again:
`exportImageInto` exports into a buffer of the export's shape, stating it as a fresh export would and writing its
sidecar only when the rendered cards differ from the bytes the buffer remembers its file holds (`sidecarBytes`);
`restateBuffer` states a buffer anew for another use, `clearBuffer` zeroes it, and `importMapped` imports what a buffer
holds without opening it again. `exportImage` is an allocation and `exportImageInto`, `importImage` an open and
`importMapped`, so each pair cannot drift.
`ExtensionSessionController` reads `target_control->document()` with `tryRead` when it prepares a request
(`PrepareOutcome::Busy` while a worker writes it - the preview is asked for again at the next pump), exports the
auxiliary assets the provider lends under their own leases, and imports a result on the thread pool
(`beginExecuteMaterialization`, `beginPreviewMaterialization`), landing it through the event loop under a generation
`cancelMaterialization` moves, for the pump to present through presenters that receive `ImageAsset&&`; a preview
sub-area lands through the framework's `composeProcessRegion`. `Test_ControlsExtensionSeam` holds the seam, linked
against the library and the protocol library, and launches no host.

### Settings, the about dialogs and the inspectors (`controls/settings/`, `controls/EngineVersionReport.h`, `controls/DashboardFileFacts.h`, `controls/ImageInspectorContent.h`, `controls/ViewTools.cpp`)

The RAW settings reach every read through one service the application context lends, `IRawOpenPolicyService`: the Raw
Images page and a settings import store the settings in the texts every open reads and publish their policy with
`setRawOpenSettings` (an import publishing the mode, the gains and auto-rotate together), and a view's own read - a
static preview, a file it loads - takes the policy published when it is asked and keeps it until it lands, a cached
preview shown again only under the settings it was read with. The policy comes from one service the context lends so
that no maker of a static preview - the dashboard, the integration lists, the process dialogs - has to wire it in, and
none can forget it and read a camera file untreated. `RawOpenPolicy`'s accessors are defined in
`app/FilePolicies.h`. The Processing page shows the session's threads and memory budget beside the stored limits and
says when a change waits for the next start; `controls/settings/ProcessingLimits.h` reads and stores the limits and
gives the session settings the application builds its session with (`sessionSettingsForLimits`). The Export page reads
and stores through `readExportSettings` and `writeExportSettings`, the table every save reads. The product's version is
`productVersion()` - `QCoreApplication::applicationVersion`, which the application states - and the about and system
information dialogs show the engine's (`engineVersionText`) and its libraries' from its own report
(`engineLibraries`, over `engineDependencies`). A dashboard card probes its file with `probeDashboardFile`, background
work admitted with `tryAdmit` that never waits. The FITS header, Exif and summary windows read the view's document with
`inspectDocument` under a lease that never waits and keep what they show while a writer holds it; `openViewTool` opens
every tool window, the astrometry and plate-solve windows once per view. The PixelMath editor highlights
`pixelMathVocabulary()`, the engine's function tables and the syntax's operators. `Test_ControlsDialogs` holds all of
it, with the settings dialog's cases.

### The dock area (`app/UiBootstrapCoordinator.h`, `controls/KDDockWidgetsHelper.h`, `theme/KddwProxyStyle.h`)

Docking is KDDockWidgets, configured once for the process by `UiBootstrapCoordinator::initializeDockManager`: the flags
`kRequiredDockConfigFlags` names, the dragged window's opacity, the smallest dock widget, and `AN_ViewFactory`, which
makes the decorations - `AN_TabBar`, `AN_TitleBar`, `AN_Stack`, `AN_Group`, `AN_FloatingWindow` - that paint from the
design tokens. `KddwProxyStyle` is installed on the dock main window. The decorations and the style carry
`AFTERNIGHTCONTROLS_API`: a window outside the library makes its dock areas from them, and the docking suite makes
its own factory from `AN_ViewFactory` as such a window would.

What the interface asks of the library is held by `Test_ControlsDocking`, as cases against the library itself, because
none of it is the library's documented promise and each fact decides how a window with several dock areas is built:

- **Several main windows in one window.** Main windows are pages of one stack, a dock widget is added to either and
  moves between them by code and by a drag, and the page that is not on screen is never a drop target: the library
  looks for a dock area among the widgets visible under the cursor. A page taken out of the stack into a window of its
  own keeps its layout and takes drops there, and keeps both when it is put back. A main window made with the stack as
  its parent stays a window; it is made with the window as parent and handed to the stack, as the application does.
- **Affinities.** Two sets match when both are empty or they share a name. A dock widget that carries a name no main
  window carries is refused by every main window, is shown no indicator over one, and neither joins nor is joined by
  the floating window of a dock widget that carries none.
- **Who owns a floating window.** With several main windows the library parents a floating window to the first one
  made that shares its affinity, whichever is on screen, and to none when none does; the floating window is destroyed
  with that parent. A main window that is never shown parents floating windows all the same and is never a drop
  target, so floating windows that must outlive every dock area are given one, made first.
- **One saved layout.** A `LayoutSaver` blob holds every main window and floating window and puts dock widgets made
  again under the same names back where they were; a name that is not made again is skipped and its place closed up.
  Three things it does are not what its interface suggests. Applying a layout closes every dock widget it names
  before placing them, so a dock widget made with `DockWidgetOption_DeleteOnClose`, as a view's is, is destroyed by a
  layout applied while it is shown: a layout is applied to views that are not shown yet, or with the option cleared
  for the duration. A saver limited to an affinity name writes the windows that carry no affinity and none of the
  dock widgets in them, so a dock widget is kept out of a layout by `LayoutSaverOption::Skip`, never by limiting the
  saver. And `openedDockWidgetsInLayout` names only the dock widgets that were on screen when the layout was saved -
  not a tab behind another, not a page that was not shown - so what was open is recorded beside the layout, not read
  from it. Restored with `RestoreOption_RelativeToMainWindow`, a layout leaves the window that holds the main windows
  where it is; without it the library sets that window's geometry once for each main window in it.
- **The hooks.** `setDragAboutToStartFunc`, `setDropIndicatorAllowedFunc` and `setDragEndedFunc` each take one plain
  function for the process. The first is asked again at every move while the button is down, so it is a question and
  not an event; it can answer by the main window its draggable is inside, found among the draggable's parent widgets.
  A draggable names its dock widget only when it carries one alone: for a strip of several tabs the dragged one is
  the strip's current tab. The second can refuse by the dock area it is handed. The third is told nothing, runs after
  a floating window's drag and after a tab's with the cursor's place readable and the dock widgets already where the
  drag left them, and does not run for a drag that was refused.
- **A drag started by code.** `Core::DockWidget::startDragging()` puts a floating dock widget in the drag a press
  starts, with the same hooks, indicators and drop. The library's drag controller is not in its installed interface:
  such a drag ends when a release reaches the widget the library drags by, and what is being dragged is known only
  from the hooks. A widget that was pressed and then starts such a drag - a thumbnail pulled out of its window - is
  not told of the release: it clears its pressed state when the drag ends.
- **Decorations by window.** A floating window shows no tab strip and one title bar when its dock widget asks for it
  before the window exists (`setFloatingWindowFlags`) and its group's strip is set to hide with one tab
  (`Core::Stack::setTabBarAutoHide`), while every other window keeps the strip the flags give it. The window that
  shows the drop indicators and the band that marks the landing place are made by the view factory
  (`createClassicIndicatorWindow`, `createRubberBand`), so both can be classes that paint from the tokens.
- **The style.** A style set on a main window is that widget's alone: Qt hands it to no child. What `KddwProxyStyle`
  gives the widgets inside a dock area is the palette `refreshThemeTokens()` sets on the main window; they are drawn
  by their own paint events and by the application's style. Each main window keeps the style installed on it, and
  `installedKddwProxyStyle()` names only the last one installed, so a window with several dock areas refreshes each.

### The library and its suites

`AfterNightControls` (registry class `engine`) compiles every unit of `src/AfterNightControls/`, the session bridge
included, and links the consumer runtime and the engine. Every Controls suite - the `Test_Controls*` lists, the
process, preview and mask drawing suites, and the viewer, dashboard, persistence, widgets, theme and system
information suites - compiles its own cases and links the library, so a class or free function a case calls carries
`AFTERNIGHTCONTROLS_API` and a list names no production unit. A suite links the library rather than compiling its
units so that every case runs the code the application links, and only what a caller outside the library uses is
exported, because a blanket export would pin the layout of classes nothing outside it touches. `Test_ControlsSeams` is
the one exception: the
freed-memory quarantine sees only the classes compiled into its executable, so the close-during-work cases and the
companion wiring case compile the viewer's, the statistics windows' and the drawing tools' closures there, with the
companions' stand-ins in its directory. A test image is a neutral `FixtureImage` made through the `fixtures/engine/`
adapter (`fixtures/TestImageGenerators.h`). The style plugin, `src/AfterNightStylePlugin/`, ships with the product.

## Processes (`src/AfterNightProcesses/`)

Every user-facing process. The pattern per process is three files: `<Name>Process.h/.cpp` (the control logic, deriving from
`an::ui::TypedProcessControl<<Name>Params>`) and `<Name>ProcessUiBuilder.h` (an `an::ui::processes::<Name>ProcessUiElements` struct
with `setupUi()`, the hand-maintained replacement for `.ui` files). `app/ProcessRegistryBuiltins.cpp` registers the
descriptors into `ProcessRegistry`, and `app/ProcessLauncher` opens them.

### Shared helpers and the launch path (`processes/ProcessCommonUtils.h`, `processes/ProcessMetadataUtils.h`, `processes/ImageListProcessHelpers.h`, `app/ProcessLauncher.h`, `app/ProcessRegistryBuiltins.cpp`)

The three headers every process includes reach only the engine. A dialog's text - and the text a standalone tool
persists - reaches the engine through one closed map per choice: `demosaicChoiceFromLabel`, `interpolationFromLabel`,
`transformKindFromLabel`, `stackMethodFromLabel` (a combination and a rejection, which the dialogs name together) and
`normalizationFromLabel`, each answering the engine's name and whether the text was `known`. A text no table holds -
a demosaic method the engine does not implement among them - reads as the map's default but says it was not known, so
a window reading a stored setting can tell a choice it offered from one it never did; "Auto" is in no map because the
dialog that offers it resolves it from what the operation does before it
asks. The "Bilinear" demosaic, in the dialogs, the `bilinear` setting and the previews, is the fast
estimate, `DemosaicMethod::Fast`. A combination's metadata is `combinedProcessMetadata` over `combinedImageMetadata`; a
source's linearity is `declaredLinearity` - its meaning's transfer function, or its `ANLIN` card where the meaning
states none. A calibration master is held to its frames by `checkCalibrationMaster`, an `an::Status`. Every registry
descriptor keeps its id, category, action, launch mode and roles and states the name its process attaches to a view
under, its exclusive access and its still-image requirement; the registry launches a process through a
`ProcessLauncher` acting for the descriptor's id, whose guards read it through `process_registry::launchGuardInputFor`,
the reading the menu's action state shares, and an attached window answers `requiresExclusiveAccess` from its
descriptor. The members that open a process are `app/ProcessEntryPoints.cpp`, compiled only with every process; the
image operations' non-interactive half is `nonInteractiveImageOperations`, beside the channel extractor; the registry
binds Export to `ExportSequenceControl::openFor`. A descriptor also states the user guide's page of its process
(`helpPath`, from the help table beside the registrations), and the launcher states it on the window a launch shows -
the process window shown after the launch that was not shown before it, since an entry point hands its window to
nobody - so that Help, asked from that window, opens the page; `docs/dev/USER_GUIDE_AUTHORING.md` says how the table
is held to the guide. `Test_ProcessesShared`, an isolated suite, holds the helpers, the
launcher and the registry: every registered action opens its real window, or runs its real channel action, through
the library's entry points; a process family's suite (`Test_Processes<Family>`) holds its own processes the same way.

### Pointwise and tone processes (`processes/PointwiseProcessUtils.h`, `Test_ProcessesPointwise`)

Brightness and contrast, gamma, exposure, the arcsinh stretch, colorize, colour saturation and the HSL and LCh
adjustments each run one engine operation over the run's image: a fused brightness, clamp and contrast program;
`buildGammaProgram`; the exposure's own factor as `buildScaleBiasProgram` with the unit clamp, never the stops of
`buildExposureProgram`; the black point and `stretchImage` of the asinh family over the luminance; `colorizeImage`;
`enhanceImageSaturation` over the exact median's background, held to the unit interval; `adjustImageHsl`; and
`adjustImageLch`, which turns a hue on round. `NormalizedSamples` lends floating samples as they are and works an
integer as fractions of full scale - an unsigned one of its largest value, a signed one counted from its smallest,
Float64 for the 32-bit integers - narrowed back to the nearest code; a value rule is judged on the stored samples; the
stretch and the colour operations leave a non-finite pixel as it was. The 32-bit integers take Float64 because Float32
cannot tell their neighbouring codes apart, and a value rule is judged before the conversion because a signed
integer's zero is not zero once it is counted from its smallest value. `runNormalizedChain` fuses a channel plane's
programs. `recordNonlinearResult` states the processing-state cards on the result, drops a linear meaning and records
the engine operation in the lineage, all in one metadata transition stated where the result is (its `codec` carries
the new cards), so nothing of the result is copied to carry them. The preview is the Apply's
operation in every frame, the gamma's included; the HSL and LCh windows keep no colour-space copy on the GUI
thread; a colour-only process refuses a grey image by name. A body
adopts what it makes into `ExecutionScope::session()`. `Test_ProcessesPointwise` launches the eight through the
registry and holds them to the behavior records, each accepted difference with its bound and the pixels it explains.

### Stretching dialogs (`processes/StretchProcessSupport.h`, `processes/HistogramTransformationProcess.h`, `processes/CurvesTransformationProcess.h`, `processes/GHTransformationProcess.h`, `processes/StatisticalStretch.h`)

The histogram transformation is `stretchImage` with a screen transfer per channel - its automatic stretches
`deriveLinkedAutoStretch` and `deriveUnlinkedAutoStretch` at -2.8 and 0.25 - the generalized hyperbolic transformation
`stretchImage` with the dialog's curve and the engine's colour modes, the curves `buildCurveProgram` through
`applyProcessChannelScope` (an exact table for eight and sixteen bits, a sampled table of `kCurvesTableEntries` for
floats, one plan for a preview and an Apply, Monotone Cubic offered beside the four classic rules), and the
statistical stretch `analyzeImageStatisticalStretch` and `stretchImageStatistically`. The curve is fixed by its points
alone, so the curves transformation is the one stretch whose interactive frames run over what the preview view paints
(`PreviewScaling::Decimated`); its editor counts the preview's result for the histogram it draws after each settled
frame, and leaves it as it is while a drag's interactive frames are shown. A stretch is decided from the
whole image it changes: the target's statistics for an Apply, a new image and a preview, each frame's for a sequence,
and the target's - never the crop's - for a preview area (`screenTransferStatisticsFor`, a `StretchStatisticsCache` per
revision); a statistical stretch's preview area is the whole target stretched and cropped. A preview area is a view of
the target, so its own statistics would show a stretch Apply never writes there, and a statistical stretch is not run
over the area alone because the engine refuses an analysis of another extent and normalizes by the whole image's
largest stretched value.
`makeStretchWorkingCopy` is the Float32 or Float64 working copy as fractions of full scale - a signed integer counted
from its smallest value (`readAsFractions`, `writeFromFractions`) - and `publishStretchResult` the result in the
image's representation, with its validity and cards, no transfer function, a lineage entry and the nonlinear
processing state; never copy the source's metadata over the result afterwards. A body that wrote its stretch where
the run writes - the curves transformation's - hands `publishStretchResult` the destination's own image, and the
metadata is stated on those samples where they are. The engine's generalized hyperbolic bias
runs from `kMinimumGeneralizedHyperbolicBias` to `kMaximumGeneralizedHyperbolicBias` (-5 to 15). A dialog's histograms,
clipping figures and symmetry-point picks run on workers that take their read lease before their admission
(`admitDialogWork`), because an Apply in place holds the writer while it waits to be admitted; the histogram counts,
the statistics window, the viewer's presentations and mask layer and the framework's own runs do the same, and a run
leases the other views' images it reads before it is admitted (`session/ImageReads.h`). `applyProcessChannelScope`
takes a component of an image described as sensor codes or a physical quantity as fractions of full scale.
`Test_ProcessesStretching` holds the four dialogs, launched through the registry.

### Spatial and noise processes (`processes/SpatialProcessSupport.h`, `processes/GmicProcessSupport.h`)

Nineteen windows - Gaussian, median, bilateral, banding, NL-Bayes, patch PCA, wavelet, total variation, unsharp,
noise, GRAYCstoration, high pass, alpha sharpen, tighten edges, LCE, LHE, CLAHE, morphology and cosmetic correction -
each run one engine call, or one G'MIC command, over the plane the channel scope hands
`executeTypedProcessIteration`, where the samples lie. `PlaneSelection` narrows a per-channel mask to that plane, and
with `NonFiniteSamples::MeanNothing` the ranking filters (median, banding, morphology, cosmetic) read a NaN in an
all-valid floating image as invalid, and an integer image under its own rule, which leaves the median its sorting
network; `UnitScale` restates a dialog's fractions in stored units; `runOverUnitFloat` is the Float32
working copy for the operations that take fractions. `blurGaussian` and `filterDetail` are exact at any sigma - one
plan up to thirty-two, the stencil family's widest kernel; beyond, on that working copy, one kernel up to the engine's
`kMaxGaussianRadius` on its compiled symmetric path where the plane is more than twice its radius each way, and exact
passes sharing the variance otherwise and wherever the engine refuses that path - an execution without its AVX2 or
AVX-512 rows, or without the memory for the kernel's own plane; an integer plane with every sample valid on that
working copy at any sigma, a Float32 plane of padded rows on it too. A settled frame and an Apply run that exact blur.
An interactive frame runs over the target reduced by the sigma reduced with it while a slider is held, or over the
part the view shows with the blur's reach around it at full scale (`gaussianReach`); beyond thirty-two there it blurs
by the engine's real-time preview boxes (`BlurKind::InteractivePreview`), whose cost does not grow with the sigma.
`filterDetail` composes a plane every sample of which is valid, blurred under `ReflectExcludingEdge`, on that working
copy at every sigma, so the engine's compiled Float32 Gaussian blurs it as it blurs the Gaussian's: the high pass's
Apply is the source less the Gaussian dialog's answer, raised to mid-grey. The engine's detail filter, whose one plan
accumulates the blur in Float64 through the separable stencil, takes the rest - a plane under a validity rule or a
mask, and the unsharp mask, whose blur mirrors the edge sample as scikit-image's does. The high pass and the unsharp
mask declare `PreviewScaling::Decimated`: an interactive frame blurs a reduction by the radius reduced with it, which
the clamp to full scale makes an approximation of the whole result's reduction, and a part at full scale by the
dialog's radius; beyond thirty-two it blurs by the boxes, as the Gaussian's does, and states their reach
(`detailReach`). The settled frame is the Apply's answer.
The median, bilateral, tighten-edges, cosmetic-correction and morphological windows declare
`PreviewScaling::NativeRegion` with the reach their plans state; the other eleven run every frame over the whole
image, each for a measurement of the whole image, a size-made grid or tiling, or a blur without a finite reach.
The six G'MIC processes build their command and call `runGmicCommand`: a session per run, the answer clamped to the
unit interval, a non-finite sample refused before the library sees it, and `refuseWithoutGmic` disabling preview and
Apply where the library is absent. A non-finite sample is refused because some G'MIC commands fault on one and the
others turn most of the image into NaN; the ranking filters instead leave it where it is and rank its finite
neighbours, because registered and stacked frames carry NaN borders. The morphological selection is
`filterImageFootprintOrderStatistic`. CLAHE and LHE record their result nonlinear under their own names through
`markProcessResultNonlinear`, which states the cards where the result is. `Test_ProcessesSpatial` holds all nineteen
to every behavior dimension through their
windows and judges every recorded capture; it reaches the G'MIC adapter only through the processes library, the one
product target `check_build_graph.py` lets link it.

### Mask, star and background processes (`processes/MaskProcessUtils.h`, `processes/MultiscaleComposition.h`)

Mask drawing, range selection, binarize, the star mask, star reduction, dark structure enhancement, the multiscale
linear transform, background extraction and automatic gradient removal each run the engine's operations over the run's
image as fractions of full scale (`NormalizedSamples`). An image a process makes beside its target - a mask, a
background, the stars, a deringing map - is derived, not copied: `derivedImageDescriptor`, `derivedImageMetadata` and
`createDerivedImage` keep the target's acquisition record, solution and lineage and no card but the processing state,
and an output an Apply makes is staged by the body (`StagedOutputs`) and opened beside the target on the GUI thread
once the run lands (`openStagedOutputs`). A derived image carries no other card of its target because the target's
cards say what the target is - its exposure, its filter, its frame type - and a mask, a background or the stars are
none of that. `extractLightness` reads a colour image's CIE L* through a normalized copy, so
it is a floating image. The range selection is `PreviewScaling::Decimated`: an interactive frame runs over the target
reduced, smoothed by the smoothness reduced with it, or over the part shown with the smoothing's reach around it
(`gaussianReach`), and smooths through the Gaussian dialog's `blurGaussian`. A mask of a colour image's lightness cannot
take the target's stretch, so in an autostretched preview at full scale the dialog answers `FullOnly`. A second image a
run reads - a starless image, a drawing - is snapshotted as an asset on the
GUI thread; work a button starts - the background grid, the protection mask, an import or export - runs on the thread
pool under its own operation and lands through a guard. The star mask has one detector (`detectStars`, then
`paintShapedStarMask` or `paintEllipticalStarMask`); the multiscale transform's layer arithmetic is PixelMath over the
engine's layer planes (`process_utils::multiscale::transformInPlace`); background extraction keeps its samples as
positions and refuses a polynomial its samples do not determine; the background previews compute when their button
asks. `Test_ProcessesMasks` holds the behavior records through the windows - star masks by what they paint,
background fits by the smooth surface they differ by - and the behavior dimensions of every process.

### Combinations (`processes/CombinationProcessSupport.h`, `processes/StarProfileStars.h`)

The image blend runs `composeBlend` with the whole blend request into the target's representation; the LRGB
combination `combineLrgb` over a grey luminance of the target's size; the NBRGB combination `combineNarrowbandRgb` in
the target's normalized working copy; the narrowband colour mapper `mapNarrowbandColor` over its layers into a new
Float32 image, one colourized image per layer beside it when asked; the narrowband palette composer
`composeNarrowbandPalette` over planes read, stretched and resized on the worker, its grid of every palette
`composeNarrowbandPaletteGrid` on the thread pool under a Preview operation, the latest request winning; the channel
composition `composeImageChannels` and `convertImageColor` from the dialog's space into the highest of the components'
representations, a top-level window whose Ok runs on the thread pool and composes three sequences frame by frame; the
two recombinations `recombineStarMask`, the profiled one over a mask the engine paints from the profile's stars
(`paintEllipticalStarMask` softened by the exact Gaussian, or `paintShapedStarMask`); and the comparison changes
nothing, its split overlay the second image presented by `presentForView` on a worker as the preview presents the
first. A second image is snapshotted on the GUI thread and read under a lease of the run's own, cropped to a preview
area as the framework crops the target (`LeasedImage`); `BoundImage` reads a frame decoded in its sensor's codes as
fractions of full scale; a combination's record is built whole over its sources (`combinedResultMetadata`: the cards
every source states identically and none of a source's exposure cards, the first source's pointing cards, the
earliest source's observation start with its start cards, and an exposure that is the integration summed over the
distinct acquisitions - each source's `TOTEXP`, else `LIVETIME`, else its exposure times its `STACKCNT` or `NCOMBINE`
frames - stated in the combination's own `EXPTIME` and `TOTEXP` cards) and a recombination's from the starless
image's (`recombinedResultMetadata`), each with its processing-state cards. A second image is the asset the GUI thread
chose because its view can close or change under the run; a combination's record is built over every source because
one source's cards - its exposure, object and frame type - describe that source alone; it points where the first
source pointed because the result is on that source's grid, begins when the earliest source began, and counts the
sources of one exposure start once, with the largest integration and frame count any of them states, because an image
and the copies made of it - its starless image, its stars, a channel taken out of it - hold one acquisition's light,
which a copy may have lost the cards to state; and a recombination keeps the starless image's because a starless
image and its stars are one frame in two parts. `Test_ProcessesCombinations`
holds the behavior records through the windows, every process's behavior dimensions, and what only one does - the
comparison's modes, lens and histograms among them.

### Channel and conversion actions (`processes/ChannelActions.h`, `processes/NonInteractiveProcesses.h`, `app/ChannelExtractor.h`)

The thirty-nine registered extractions and conversions are one table, `process_utils::channelActions()`: each row
names its planes as channel scopes, its views' names and history labels, the representation a conversion makes and the
progress text, and `runChannelAction` runs a row over one image under the caller's `ExecutionScope`.
`NonInteractiveProcesses` runs that call over the image a view shows - read under a lease taken before admission - and
over every frame of its sequence through `openProcessFrames` and one `createProcessFrameOutput` per output under one
admission, and opens what it makes on the GUI thread; a worker never waits for it. A stored channel is
`extractChannelScope`'s copy, keeping the image's cards, meaning and its plane of an explicit mask; a colour component
is the scope's Float32 plane - Float64 for a Float64 image - in the unit interval, keeping only the acquisition record,
the solution, the lineage and the frame's observing context and integration cards (`derivedFrameCards`); a conversion
reads every sample as a fraction of its representation's range and writes it in double to the nearest code, a
full-scale sample saturating, and a conversion to the image's own representation is an exact copy. A component is a
floating plane so that no hue, chroma or lightness is rounded to the image's codes before anything reads it, and it
drops the other cards, which describe the colour image, but keeps the acquisition record, the observing context, the
integration cards and the solution because its pixels are the image's and hold its light. Duplicate,
Extract Frame and the container conversions open or show sequences through the file companions; grey, colour and the
negative are `editImage` edits of a still view and frame-by-frame replacements of a sequence (`greyImageOf`,
`colourImageOf`, `negativeImageOf`, the negative of a signed image the complement of its fractions), and a sequence's
mirrors and turns are `orientedImageOf`. Every failure reaches the user in a message titled as the action and in the
log; only a stop the user asked for is quiet. `Test_ProcessesChannels` holds the table to the registry, the behavior
records through the action as the menu runs it, and each action's dimensions over a still image and a sequence.

### Photometric and spectrophotometric colour calibration (`processes/PhotometricCalibrationSupport.h`, `processes/SpectrophotometricColorCalibration/`)

The photometric window is one edit of the view (`editImage`) whose worker reads the solution from the image's cards,
the installed Gaia catalog's stars inside the field with their BP-RP colours (`GaiaCatalogSource::regionWithColors`)
and calls `calibratePhotometricColor` with the product's star finder and request (`photometricStarDetection`,
`photometricCalibrationRequest`). The spectrophotometric window is a typed process whose body reads the field's
catalog chunks (`SpectrophotometricColorCalibration/SpccCatalogSource.h`), the response repository's profile and white reference
(`SpectrophotometricColorCalibration/SpccResponseRepository.h`) and calls `planSpectrophotometricColorCalibration` and
`calibrateSpectrophotometricColor`, its readiness decided on a worker before Apply is offered; what a run's frames
share is read once per run (`SpccRunInputs`). Both share `PhotometricCalibrationSupport.h`: the target's checks - RGB,
not narrowband, plate-solved, not declared nonlinear - the working copy as fractions of full scale written back in the
image's representation, and the calibrated metadata, the engine's transition then the processing-state cards with the
input's state. An image is known by its `AssetRevision`: a person's attestation of its linear state is an edit whose
evidence is the revision they looked at, and the window knows the image it calibrated by the revision its run
published. A revision states exactly which edit of the image a person looked at or a run calibrated, so any later
edit makes it another image, where a digest of the samples would cost a pass over a large frame and still not name the
edit. An image an integration wrote is calibrated against the sources its registration matched while they hold
for its solution (`locateSpccIntegrationHandoff`, `resolveMatchedSources`), through `readIntegrationMatchedSources`,
defined beside the registration metrics loader. `Test_ProcessesPhotometric` holds both; the catalog and the response
repository are described in `docs/dev/SPCC_*.md`.

### Geometry and filter-array processes (`processes/GeometryProcessSupport.h`, `processes/CfaProcessSupport.h`)

Crop, resize, rotate, the integer and CFA integer resamples, debayer, split and merge CFA and extract sequence are
own-thread windows that turn their dialog into a value on the GUI thread when Apply is pressed (`settings()`) and run
it through two units. `GeometryProcessSupport.h`: each step the engine's plan (`planGeometryStep`, `transformImage`)
under one policy for a geometry change - the acquisition record and the meaning kept, the frame's observing context
and integration cards kept (`derivedFrameCards`: the engine's `withObservationCards` and the frame's own `EXPTIME`,
`TOTEXP`, `LIVETIME`, `STACKCNT` and `NCOMBINE`) and the codec's other cards dropped, an output decision withdrawn, the
validity rule kept where every sample was written and stated as the mask written where one was not, as the view's own
turns follow - and the solution the cards state moved with the pixels of a crop, a resize or an integer resample
(`movedSolution`, the engine's `cropWcsSolution` and `resizeWcsSolution`) with the solver's card, and dropped by a
rotation or a filter-array resample; a step that rescales the frame divides the context's `XPIXSZ` and `YPIXSZ` by
its scale along each axis (`withPixelScale`), a resize by its own and either integer resample by its exact factor;
the dialogs' eight interpolations, Auto saying
what it chooses and the step naming what ran; a resize that repeats the edge sample under the dark-ringing guard and
keeps the source's pixel aspect, a rotation clockwise about the image's centre on a black border, a normalized
floating result held to the unit interval; and the runs, `runInPlace` (an `editImage` edit, or every frame of a
sequence - demosaiced by the method the sequence states, whatever is selected - written into a new sequence the view
shows as one history step) and `runToNewViews` (views of what a still image or every frame becomes, one sequence per
output, each published whole), every failure said under the process's title, an edit the history refuses said once by
the history, and a stop the person asked for quiet. A filter-array resample drops the solution because its planes'
samples sit at different places within each block, which one solution cannot describe, and a resize keeps the
source's pixel aspect because the engine states unequal axis scales as stretched pixels, which a rotation refuses. The
observing context stays because a cropped, turned or resized frame is still that frame, whose plate solve and header
still need its optics, its site and where it pointed, the integration cards because it holds the same light - for a
stacked master its whole integration and frame count - and a rescaled frame restates its pixel size so that the
plate scale its optics imply is still its own. A
crop keeps a mosaic's filters under its samples through its
phase; a resize, rotation or integer resample of a mosaic reads it as grey only when the person says so.
`CfaProcessSupport.h`: `splitMosaicPlanes`, `joinMosaicPlanes`, `extractHydrogenAlpha`, `extractGreen` and
`extractHydrogenAlphaAndOxygenIII`, each refusing a tabulated arrangement by name, and `debayerImage` over the view's
demosaic; Debayer offers the methods the engine implements and reads "From metadata" as the filter array the image
states, an X-Trans one by the method's tabulated counterpart, and refuses a named two-by-two pattern over an X-Trans
image. `Test_ProcessesGeometry` holds every behavior record through the windows and the menu's mirrors and turns, and
every behavior dimension of each.

### Colour calibration tools (`processes/ColorToolsSupport.h`)

Background neutralization, colour calibration, linear match and SCNR are own-thread windows that turn their dialog,
the references it names and the view's mask into values on the GUI thread when Apply is pressed (`settings()`,
`ToolReference`, `viewMaskOf`) and run through `runInPlace`. `ColorToolsSupport.h` holds the rest, every sample a
fraction of full scale in a Float32 working copy, Float64 for Float64 and the 32-bit integers. A reference is the asset
a view showed, read under a lease of the run's own (`ReferenceRead`), or the image the run changes - for a sequence,
the frame its view shows - and is measured once per run (`MeasuredOnce`). The measurements are the engine's
compositions over the dialog's limits as a closed interval, judged with the image's own validity: the neutralization
each channel's exact median and the shared pedestal (`measureNeutralization`), the colour calibration each channel's
white mean above its background's median of every sample, normalized to the strongest channel, or a manual white about
the background (`measureColorCalibration` - the background limits decide only the background mask), the linear match
the normalization domain's scaling match, paired over the shared positions when the images are one size and each on
its own when not (`measureLinearMatch`); a measurement that keeps nothing is refused by name. The applies are
`applyColorCalibration` held to the unit interval (`applyChannelGains`), `reduceImageChromaticNoise` (`suppressGreen`)
and `applyGlobalNormalization` unclamped (`matchLinearly`), brought back to the image's representation and weighed by
the view's mask (`weighByViewMask`) for every tool but the linear match; a result keeps its source's metadata with a
lineage entry, and SCNR's is recorded nonlinear under `scnr`. The neutralization, the colour calibration and the linear
match keep the image's meaning and state no colour calibration, because the spectrophotometric readiness refuses an
image that states one; the colour calibration's background is the median of the reference the person chose as sky,
not `estimateColorBackground`, whose clipped window estimates the sky a photometric calibration's catalog stars sit on.
`selectedSamplesMask` writes every sample of a colour calibration's reference masks, which the window opens once the
result is shown. `Test_ProcessesColorTools` holds every behavior record through the windows - the neutralization and
the linear match, whose estimators are not their records', to the compositions the suite states from each scene's
samples - and every behavior dimension of each.

### PixelMath (`processes/PixelMathProcessSupport.h`, `processes/PixelMathProcess.h`)

The window reads its dialog as a `PixelMathSettings`, the images every view holds, the active view's and the target's
mask as a `PixelMathImageSet` and draws the run's random seed, all on the GUI thread when Apply is pressed, and runs
`runPixelMath` through `runInPlace` (one history step named `PixelMath`) or `runToNewViews` (a new image or sequence
under the dialog's name). `runPixelMath` parses every distinct expression before reading an image, binds only the
images an expression names - each resampled to the output's size with Lanczos-4 and read as fractions of full scale
(`readAsFractions`) in one working representation, Float64 when anything read holds Float64 or a 32-bit integer; `$T`
is the target converted to the output's channels, `$A` the active image, `$M` the target's mask, an image of sensor
codes or physical quantities declared in its own domain so the engine refuses it beside fractions - then resolves and
compiles each expression once and runs it through `lowerPixelMathToPointwise` and `executePointwiseProgram` when it is
a chain and `executePixelMath` otherwise, an expression for one channel lowered over one plane of each image. Local
symbols are variables, global symbols constants. Nothing is clamped; Rescale maps the finite range and leaves a result
with none; the result is narrowed once to the nearest value (`writeFromFractions`). Global symbols are constants
because a value one row left for the next would depend on how rows are scheduled, and nothing is clamped because a
clamp would destroy every image whose samples are not fractions of full scale. A replaced target keeps its
description and record and is stated nonlinear under `pixelmath`; a new image keeps the acquisition record, its
solution only at the target's size. Each frame of a sequence draws from `pixelMathFrameSeed`; the lineage names the
engine operation, each expression's path and the seed: each Apply draws a new seed so that pressing it again draws
other numbers, and the recorded seed reproduces a run at any number of threads. The version-1 `.anpm` file is read and
written through Qt's XML streams. `Test_ProcessesPixelMath` holds the window to its behavior records, the fused path of
a chain and every behavior dimension.

### Star profilers (`processes/StarProfilerWindow.h`, `processes/StarProfilerSupport.h`)

Both windows derive from `StarProfilerWindow`, which runs a detection, a profile read and a profile write on the
thread pool - the read lease before the admission - lands only the run last asked for, holds no lock a worker holds and
shows the stars filter and the profiled mask window without a modal loop; it holds a `StarListPtr`, a filter's result,
a deletion and an ordering each a new list. One detector serves both layouts: `detectProfileStars` reads the image as
fractions of full scale, a colour image's green or its CIE lightness, and runs `planStarDetection` and `detectStars`
at the layout's request - the first layout's threshold the smallest fitted amplitude, its roundness tolerance the
difference of the fitted standard deviations and its size window the mean full width (`firstProfilerDetection`,
`firstProfilerKeeps`), the second's dialog the detector's request. A position is an engine pixel coordinate
everywhere: both layouts' documents are written in that convention, so a document is read through
`process_utils::starsOfProfile` and written as it stands, never shifted. The second layout places each star with
`pixelToWorld` and matches through `crossMatchDetectedStars` against `GaiaCatalogSource::regionWithColors`, the
enrichment kept beside the stars in a `StarEnrichmentTable` keyed by position, because a detected star carries what
the pixels said and nothing a catalog adds. Each window reads and writes its own generation and refuses the other's by
name. `Test_ProcessesStarProfilers` holds both windows over a plate-solved field whose stars are planted at stated
engine pixel coordinates.

### External tools (`processes/ExternalToolSupport.h`, `processes/StarnetProcess.h`, `processes/StarnetStarMaskProcess.h`, `processes/DeepSNRProcess.h`)

StarNet, StarNet's star mask and DeepSNR are own-thread windows that turn their dialog into a `Settings` value and an
`ExternalToolRequest` on the GUI thread when OK is pressed (`settings()`, `toolRequest()`) and run through
`runToNewViews` - one view per answer beside the target - or `runInPlace`, an edit the history records; a still image's
run shows a progress window whose Cancel stops the run, and `GeometryRunOutcome::views` names the views a run opened.
`runExternalTool` is the one hand-off: it plans `prepareExternalToolHandoff` from the tool's profile - `UInt16` for
StarNet2 2.5.3 and DeepSNR 1.2.2, which refuse floating-point images, `Float32` for a tool whose profile takes it -
writes the handed image interleaved through the output transaction into a directory of its own, starts the program in
its own directory with the arguments the request spells (`an::ui::starnet::buildArguments`,
`an::ui::deepsnr::buildArguments`), reads its output with the parsers of `processes/CliProgressParser.h`, reporting
from ten to ninety percent through `ExecutionContext::reportProgress`, kills it when the operation is stopped, reads
each answer through `decodeImage` and `importViewFrame`, holds it to the source's shape, and brings it back: the image
and the star layer through `restoreExternalToolAnswer` in the source's representation, the mask narrowed as the
program's own answer. A StarNet output is `derivedImageMetadata` under `starnet-starless`, `starnet-star-mask` or
`starnet-stars`; DeepSNR's result keeps the image's metadata with every sample valid; every answer's lineage names the
program, its version and its arguments. A sequence is run frame by frame; a mosaic, an image of two or four channels
and one below DeepSNR's `minimumImageSize()` are refused by name - a mosaic because a tool's network would read its
filter array as texture. A StarNet output keeps the target's acquisition record and solution because the pixel grid
the solution describes is unchanged. `shapeStarMask` reduces a colour mask to its
lightness (`extractLightness`) and selects it between the limits (`rangeSelectImage`, then the exact Gaussian).
`Test_ProcessesExternalTools` is also the stand-in program the product is pointed at (`Test_ProcessesExternalTools/StandInTool.h`:
`AFTERNIGHT_STANDIN_TOOL` names the product, `AFTERNIGHT_STANDIN_TOOL_BEHAVIOUR` what it does), reproduces the
hand-off records through it, holds every behavior dimension of each window, and runs the real programs where they are
installed (`AFTERNIGHT_TEST_REQUIRE_EXTERNAL_TOOLS=1` makes a missing one a failure).

### Standalone workflow tools (`processes/core/`, `processes/WorkflowToolFiles.h`, `processes/CalibrationBatch.h`, `processes/StackingBatch.h`, `processes/core/RegistrationRun.h`)

Image Calibration, Image Registration, Image Stacking and the Image List Analyzer share the integration's records
without its model: the settings records are `processes/core/WorkflowSettingsRecords.h`, which `model/IntegrationDataModel.h`
includes, and the registration's are `RegisteredStar`, `RegisteredFrame` and `RegisteredFrameSet` in
`core/RegistrationMetricsPersistenceTypes.h` - engine pixel coordinates, `an::RegistrationTransform`s - which the
metrics writer and loader, `StandaloneRegistrationPersistence` and the integration's `RegistrationWorkAnalyzer`
(through `core/RegistrationWorkState.h`) share; the database's schema, group key and identity hashes are the
integration's, so either reuses the other's registration. The integration's settings dialog, local normalization
dialog, blink comparator and comet registration are reached through `core/WorkflowToolCompanions.h`, defined beside the
units they open or run - shown, never run with `exec()`, answering through a callback. `WorkflowFileList` drives each
table: headers probed through the engine on the thread pool (`probeWorkflowFile`), the integration's figures read from
them, a selection of several rows kept and reported (`onSelectionChanged`). `CalibrationBatch` reads the masters once,
checks them by name and calibrates each light with `calibrateImage` in its own admitted operation, the pedestal in
sixteen-bit codes and dark optimization for a grey light only; `stackStandaloneGroup` states the engine's
`StackRequest` through the integration's choice maps (`standaloneStackRequest`) and runs `executeStack` over files
decoded as it asks, carrying the stack's validity mask into the asset it writes, its weightings the engine's sources of
each name; `core/RegistrationRun.h` is the engine's detector, matcher (`alignStarFields`, `fitRegistrationTransform`, phase
correlation), chromatic-aberration correction in memory and warp onto `planAlignmentCanvas`. Stacking reads every
frame under `ValidityRule::ZeroIsNoData`, so the zero border an alignment leaves where a frame does not reach is
neither combined nor judged by a rejection. The tools call the engine rather than run the integration's stages, which
carry its project, manifest and artifact tracking, while a tool's run is one directory of outputs. The analyzer's proxies
are the file stretched through `autostretchFrame` and written in eight bits, each keeping the autostretch settings
it was written through: the preview plays them only in the autostretch display with those settings, presented as
they are, and the original files in any other display, so its mode selector names what it shows; without
proxies, every display plays the files themselves. Each file's statistics and histogram are measured once, from the file, by the
optimization or one file at a time as frames are shown, and kept while it is listed. Every window
reads its settings on the GUI thread, runs on the thread pool and closes without waiting. `Test_ProcessesWorkflowTools`
holds the four windows and the shared records over frames it writes, the companions stood in.

### The library and its suites

`AfterNightProcesses` (registry class `engine`) compiles every unit of `src/AfterNightProcesses/` - the launcher's
entry points among them - and links the Controls library, the consumer runtime, the engine and the G'MIC adapter, which
`check_build_graph.py` lets it alone link besides the adapter's own suite. Every Processes suite - the
`Test_Processes<Family>` lists, `Test_ImageIntegration` and `Test_ProcessFramework` - compiles its own cases and links
the library, so a class or free function a case calls carries `AFTERNIGHTPROCESSES_API`, and no list compiles a
production unit or a stand-in for one: each test runs the library the application links. Only what a caller uses is
exported, because a blanket export would pin the layout of classes nothing outside the library touches and fail to
build an exported class whose implicit copy cannot be instantiated. A launch is observed as the window it opened: `openedProcessWindows`
in `Test_ProcessesShared/ProcessesSharedTestSupport.h` finds the shown process windows by class, and
`ClosesOpenedWindows`, declared after the context a case installs, closes them before it goes. `ProcessRegistryTest`
launches every registered action for real - each window opens once, of its process's class, over the view or over none
- and every channel action. `Test_ProcessFramework` runs under the fixture harness with one process context for the
whole run (`Test_ProcessFramework/ProcessFrameworkSuiteSupport.h`).

## The ImageIntegration workflow (`processes/ImageIntegration/`)

The large multi-stage stacking workflow: `model/` (frames, grouping, calibration matching, `IntegrationError`),
`workflow/` (manifest build and diff, `ExecutionPlanner`, `WorkflowExecutor`, progress, artifacts, cancellation) with
`workflow/stages/` (Bias, Dark, Flat, LightsCalibration, LightsRegistration, LightsAlignment, LightsNormalization,
LightsStacking, LightsDrizzle), `persistence/` (SQLite project state, autosave, presets), `reporting/`
(the report model and the document written from it), and `tabs/`, `widgets/`, `controls/` and `dialogs/` for the
UI. `lite/` and
`ImageIntegrationLiteProcess.*` are the simplified front end only: shared behaviour belongs in the full model and
workflow and is reused by Lite. How artifacts are written and reused is `AGENTS.md` ("Artifact writing and incremental
reuse") and `docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md`.

### The model and its frame import (`model/`, `core/FileImport.h`, `core/FrameDetection.h`)

`buildDetectedFrameInfoWithIssue(session, path, defaultType, admission)` probes a file's header in a one-thread
`Batch` operation of the process's `EngineSession` (`importProbeRequest`) - waiting for a thread on a worker
(`ImportAdmission::Wait`), never on the GUI thread (`NoWait`) - and refuses an empty path, a missing file and a format
the session's codecs do not read; a file it cannot probe, or a probe not admitted now, is imported with a warning and
its folder's or the caller's kind. The probe asks for one thread because a header is one thread's input and output and
an import probes many files at once, which a request for every thread would run one after another.
`FrameDetection::detect` takes a frame's kind from the cards a probe read through
`readFrameTypeCards` (`IMAGETYP`, `FRAME`, XISF `imageType`), then from the folders on its path (`folderHint`), then
the caller's, and says which decided. `frameMetadataFromProbe` reads the acquisition record, then the cards
`projectAcquisition` reads - the ISO speed before the gain setting, the sentinels `-` and `N/A` as nothing - and the
capture time, whose UTC date is the frame's session, is the exposure start in UTC, then `DATE`, then the modification
time. `IntegrationDataModel::addExternalMaster` takes the `FrameInfo` its caller read (`readExternalMaster` in the
calibration panels) and reads no file. The model's policies share `medianOf` and `scaledMadOf` (`core/MathUtils.h`),
and a stage reports an engine failure through `integrationErrorFromEngine` (`model/IntegrationEngineError.h`): the
stage's message, the engine's cause chain as the details, a code the engine's typed answer refines. The comet stacking
modes' spellings are `core/CometStackingMode.h`. `Test_ImageIntegration` holds the model and import cases over frames
written by `fixtures/common/IntegrationFrameFiles.h`.

### The workflow core and its persistence contracts (`workflow/`, `core/PersistedChoiceMaps.h`)

`WorkflowExecutionOptions::session` names the session a run's stages work in; the run binds `WorkflowStageServices`
(`workflow/WorkflowStageServices.h`) to every stage - the session, the parallel-cores setting as the width of each
operation a stage admits (`admitStageOperation`, purpose `WorkflowStage`), `workflowRawPolicy()` (no balance on either
sensor class) and a codec registry built from it - and changes nothing process-wide: no `tbb::global_control`, no RAW
global; a stage that runs engine work outside an admitted operation is not narrowed. The planner asks the stages what
it does not decide through `workflow/WorkflowStageCompanions.h` (`planLightsTransformOnRead`,
`transformOnReadGlobalCoefficientsReusable`, `lightsAlignmentOutputsReusable`), defined beside each stage; the executor
commits through `IWorkflowRunArchive` (`workflow/WorkflowRunArchive.h`) from the factory its composer sets, and
`createDefaultWorkflowExecutor` (`workflow/DefaultWorkflowStages.h`) composes the nine stages and the archiver.
Headers are read through `FrameHeaderReader` (`sessionFrameHeaderReader`); `WorkflowManifest::validate` checks frame
sizes only with one, and `ExecutionPreparationSnapshot::build` captures the geometry of the lights a stage places
itself - a drizzled group's, and those of a group whose stack warps them as it reads them - and reads the identity a
moved master's header carries, only with one. With that geometry the planner states the identity the stack and the
normalization would state of a group they place - its master, and coefficients measured on a warped plane
(`warpedPlaneGeometryFingerprint`) - so a run that changed nothing they read reuses them. `workflow/ManifestDiff.h`
compares an integration group's settings by the stages that read them (`stackingSettingReaders`, every field of
`StackingSettings` with its stages; a field added without its entry stops the build), and the plan states each
stage's reason from them. A validation on the GUI thread passes no reader, because reading every header there would
block it, and the run validates the sizes. A stage finds a light's registration input on disk as it
reads (`registrationInputPathForFrame`), not in the plan's snapshot, because the run's own calibration and
registration write a light's calibrated and corrected copies after the plan is made. Every fingerprint carries
`ArtifactFamily`'s identity - `kWorkflowEngineGeneration` and the family's revision (`workflow/ArtifactSchema.h`); a
stage that changes what a family computes moves its revision, so an artifact stamped with another revision is
recomputed once. Non-image
documents go through `workflow/DocumentPublication.h`: a bounded envelope (type, schema, identity, length, SHA-256)
published to a `.anpending` sibling, flushed and atomically replaced, a pair's payload before its manifest,
`recoverDocumentDirectory` removing only owned files, every file operation injectable (`DocumentFileOperations`).
Registration records (`workflow/stages/StageParsingUtils.h`) are schema 5 in engine pixel coordinates. A drizzle
descriptor is a schema 1 document (`workflow/DrizzleDescriptorDocument.h`) beside an optional dense payload read only
with its descriptor's identity, length and checksum. Stored choices are read through
`core/PersistedChoiceMaps.h`'s fixed tables; an unknown spelling reads as unknown. The run's `WorkflowImageCache`
holds `AnyImage`s reserved against the session's `DerivedCaches` allowance, with an evictor. Stage code that belongs to
one stage lives in that stage's unit (`CalibrationStackInputs`, `RegistrationOutputPlane`,
`NormalizationArtifactSupport`, `TransformOnReadRouting`). `Test_ImageIntegration` holds the core against the stages'
own companions and the library's default executor.

### The calibration stages (`workflow/stages/CalibrationStackInputs.h`, `core/CalibrationMasterSet.h`, `workflow/WorkflowArtifactStamp.h`)

The bias, dark and flat stages state their groups and which masters they apply and run through
`stage_utils::runCalibrationStage`: `loadReadableStackInputs` probes every frame through the run's registry and skips,
by name, one it cannot read or that does not match the group's first readable frame; `stackCalibrationGroup` runs
`executeStack` over a source that decodes each frame once and refuses one changed since it was probed, a flat's dark,
or its bias where no dark applies, applied on the way in through one calibration plan, a rejection left out below
three frames and said. A master dark holds its bias, stacked by the run or supplied, so the dark stage applies no
master and a flat takes its bias only where no dark applies; in CCD mode `CalibrationMasterSet` subtracts the bias from
a dark once per pair, so the dark's factor scales its thermal signal alone. A stage outside a run - with no
`WorkflowStageServices` - is refused. Every image a stage writes goes
through `publishWorkflowImage`, the output transaction stating the manifest entry as its `ArtifactIdentity`
(`workflowArtifactIdentity`), and its record is made after the write committed. The lights stage reads its masters
through `core::CalibrationMasterSet` - each master once, the CCD dark once per pair, the cosmetic maps once per dark -
and calibrates each light with `calibrateLight` in its own admitted operation; a master that cannot be read fails only
the lights naming it, each by name. Every in-memory stage calibrates through `CalibrationMasterSet`, which prepares one
plan per light description. `Test_ImageIntegration` runs the stages through `Test_ImageIntegration/workflow/CalibrationRun.h`.

### Registration and alignment (`workflow/stages/RegistrationFrames.h`, `workflow/stages/RegistrationOutputPlane.h`, `core/CometRegistrationProductBridge.h`)

`stages/RegistrationFrames` is how both stages and the comet registration reach a light: `probeRegistrationFrame`
reads its header (extent, filter, exposure start, its own `WcsSolution` and distortion), `readRegistrationImage` reads
it as Float32 fractions calibrated by the master set and demosaiced, and `warpRegistrationFrame` resamples it onto a
plane: the engine's warp writes only where the light's pixels reach, so the plane holds exact zero wherever they do not,
and reads the light's own edge sample past its edge (`KernelBorderMode::Clamp`) rather than a step to zero, which a
cubic or Lanczos-4 kernel would ring on. The chromatic-aberration correction warps each channel the same way, and a
channel keeps its own samples where, moved, it does not reach. The registration detects each frame in its own admitted operation with typed progress and fits star matching,
phase correlation, control points and the comet track in engine pixel coordinates over the frames as they are,
recording each transform at schema 5 as `ExplicitNone`; an unmatched frame is registered by sampling its solution - its
cards or `PlateSolveService`'s answer - against the reference's. The stars are matched as the frames hold them, so a
transform already carries the frames' relative distortion and the alignment resamples through it alone; a group whose
distortion a projective map cannot follow needs a polynomial model. The Gaia enrichment runs only while
`enrichWithGaiaCatalog` holds. Stored control points and comet observations carry `"pixelConvention"`. Every
corrected, registered and comet-aligned light goes through `publishWorkflowImage`; `planRegistrationOutputPlane` plans
the canvas from the transforms and `registrationOutputPlaneSolution` moves the reference's solution onto it, dropping a
distortion the alignment undoes. Only a mosaic canvas moves the registration hash. The transform-on-read confirmation
is `confirmLightsTransformOnRead` in `workflow/WorkflowStageCompanions.h`, defined beside the stacking stage. Both
stages read their lights ahead, and the alignment writes them behind, through `workflow/stages/StageFramePipeline.h`
(below). `Test_ImageIntegration` writes its star fields through `Test_ImageIntegration/workflow/StarFieldFrames.h`.

### Normalization and the drizzle documents (`workflow/stages/NormalizationArtifactSupport.h`, `workflow/stages/StackDrizzlePixmapIndex.h`, `core/NormalizationAnalysis.h`)

The normalization stage measures a group's lights where its stack reads them - as read
(`NormalizationMeasurementPlane::Frames`), on the current registered lights or the registration inputs warped onto the
registration's plane in memory (`RegisteredCanvas`), or warped onto a drizzled group's analysis plane
(`DrizzleAnalysisPlane`, the canvas `resolveDrizzleGroupGeometry` plans) - in one admitted operation, with the engine's
global matches, overlap solve and local fit. A drizzled group is measured on the analysis plane, the canvas before its
scale, so its coefficients hold at any drizzle scale: a Global group's matches apply as they are, and a Local group's
document is applied through the plane mapping `drizzleLocalNormalizationPlane` states
(`Algorithms/Normalization/DrizzleLocalNormalization.h`), which the stack hands the engine beside the document, so every
surface is read where a drizzled pixel lies on the plane, exactly at each pixel's centre, and the stack measures no
normalization of its own. A grid's nodes span the frame it was fitted on, so restating the grids onto the drizzled
extent would move them by up to `(1 - 1/scale) / 2` pixel; measuring on the drizzled lights would cost the scale squared
in pixels and put the drop's holes under the detection and the filters.
`localNormalizationRequestFor` (`core/NormalizationUtils.h`) states every
value of the engine's local request and the settings fingerprint hashes it; `core/NormalizationAnalysis.h` is the rest
of the local settings - the analysis filters, the ranking figures of the single-best and integrated reference
strategies and the PSF flux scale, composed over the engine's detector. An integrated reference names no frame in the
engine's document; the set's `referenceIndex` is the light scales were read against. The coefficients are an
enveloped `normalization-coefficients` document beside a schema-6 sidecar (`publishNormalizationArtifact`,
`checkNormalizationArtifact`, `recoverNormalizationArtifact`); the drizzle stage writes one descriptor document per
light, its engine identity computed over the geometry as the document states it, dense payloads under
`DenseCheckpoint` and a schema-4 sidecar (`readDrizzleDescriptorIndex`). Each is reused while its sidecar agrees; an
older or corrupt pair is replaced once a new one is whole; a newer sidecar fails the group unless it is reprocessed.
The stack reads the coefficients through `stackNormalizationInputsFor`
(`workflow/stages/StackNormalizationArtifacts.h`). `Test_ImageIntegration`'s `LightsNormalizationTest`,
`LightsDrizzleTest` and `NormalizationDocumentTest` hold them.

### Stacking and the final publication (`workflow/stages/LightsStackingStage.h`, `workflow/stages/StackFrameSources.h`, `workflow/stages/StackMasterWcs.h`, `processes/core/LinearChannelMatch.h`)

A group's lights reach `executeStack` in one admitted operation through `stage_utils::WorkflowStackFrameSource` - each
frame decoded through the run's registry, held to the revision it was planned with, calibrated by the master set,
demosaiced and warped when its route warps - by one of six routes: as read, the registered images held to their
recorded revision, the registration inputs warped as they are read (`decideLightsTransformOnRead`, the workflow's
reasons alone), drizzled through `DrizzledStackFrameSource` on the drizzle canvas, the comet-aligned images, or the
stars-plus-comet composite (`buildStarsPlusCometComposite`: split with the uncovered zeros masked, blended at each
canvas's origin). A frame on a warped plane states `ZeroIsNoData`, every sample that is not a number restated as the
uncovered zero (`zeroNonFiniteSamples`), because a frame states one rule and that marker costs one pass where a mask
beside each band would cost every kernel a read; the normalization reads its frames the same way, so both measure the
same samples. Where every frame is a file the stack combines as it is stored - not calibrated in memory, not
warped, not a mosaic, Float32 in planes - the source reads it a band of rows at a time through a row reader on the
run's registry (`StackSourceAccess::ConcurrentRegion`, `stackFrameBandPlan`), so the stack streams a tile of every
frame from disk, holds a tile's bytes rather than every frame and reads its input once under a memory limit. A
compressed file is decoded in pieces of many rows, which the source states (`decodeGranularityRows`) and the stage hands
to the planner, so a tile holds a piece's rows where the memory allows rather than decoding it again for every band; the
other routes read whole frames, and the run's log says which form a group read with and why. The stored texts are
read by `integrationStackChoice` through the closed maps, an unknown one refused
by name, and `stackingSettingsNotes` says what the stack cannot honour; weights come from the registration's records,
a colour light's one star width - measured in green - weighing every channel, because widths of each channel's own,
measured on two real colour sessions, moved the weights by a few percent and a stack's star width by a third of a
percent at most (`docs/dev/FRAME_WEIGHTING_INPUTS.md`), and the number-of-images weighting from each light's own
count of combined frames (`combinedFrameCount`);
the normalization stage's document is applied through `stackNormalizationInputsFor` where it describes the frames, the
stack measuring its own otherwise; the exposure merge is buckets blended by `exposureMergeBlendWeight`. Progress is the
stack's typed reports; the quality record keeps clustered pixels and every rejected element apart. The master's
solution is decided - and solved through a scratch file when asked - before the first write
(`buildMasterWcsCandidate`, `solveFinalMasterWcs`), its cards from `masterCodecMetadata` and a mosaic's pointing from
`mosaicPointingCards`. The master, maps, composite layers and colour master go through `publishWorkflowImage` stamped
with the group's entry; the admission is let go before the records, because reading a stamp admits an operation of its
own; a map is current while it and its master share a recorded fingerprint (`isStackingAuxiliaryOutputCurrent`). The
colour master and the channel composition share `LinearChannelMatch`, the one owner of its limits.
`Test_ImageIntegration`'s `LightsStackingTest`, `CometStackingTest` and `StackingRecordsTest` hold it.

### Persistence, reporting and windows (`persistence/SessionDocument.h`, `workflow/PreviewThumbnailGenerator.h`)

A session is a schema 3 document (`persistence/SessionDocument.h`, `sessionSchema: 3`): every engine-facing choice
stored by the key `core/PersistedChoiceMaps.h` gives it, each set of image points with its pixel convention. The same
document is the session database's row, a preset's body (`{"presetSchema": 3, "configuration": ...}`) and a project
file. The integration reads the formats it writes and nothing else: a project, preset or session of another format is
refused with a message naming it, the format found and the one this version reads (`sessionFormatRefusal`), and nothing
is changed; a session database or an output directory's artifact records of another format are never read, written or
deleted - each is set aside beside itself under a new name (`setAsideStore`, `app/ApplicationStorage.h`), the log says
so, the window names the sessions' new name, and a new one is made; a registration database of another format refuses
the run, left as it is; a transform record or a last run's manifest of another format is never reused. The fingerprints
hash a choice's key. A window refused its stored session starts empty and keeps its autosave off. The report reads
statistics with `readViewImage` and the
engine's statistics, header facts with `probeImage` and weights through `deriveStackWeights`
(`core/StackWeightingInputs.h`), the measurement and request the stack uses, all in the `EngineSession*` it is given.
It is built once, as the archiver commits the run, into a model the archive keeps (`kStoredReportFileName`); the
document is written from that model, and written again or saved as one file from it on the thread pool, with no
image read. Previews are `generateBoundedPreview` - the largest-sample shrink presented through `presentForView`.
When a report is written, what it reads, how its values are printed (`reporting/ReportFormat.h`) and the runs its
suites build are in [IMAGE_INTEGRATION_REPORT.md](../IMAGE_INTEGRATION_REPORT.md). Every weighting's dashboard
preview shows the
weights the stack applies, and the stack and
the preview state the group's floor as the photometric raw floor. Every question and settings dialog of the
integration and Lite opens with `open()`; the shared file pickers (`app/FileDialogUtils.h`) and `QMenu::exec` context
menus stay blocking: the pickers are the product's shared wrappers, which run their dialog's own loop, so a
non-blocking form belongs in them, for every process, and a context menu's loop is the platform's idiom and holds no
engine work. `Test_ImageIntegration` holds all of it.

### The frame pipeline and performance (`workflow/stages/StageFramePipeline.h`, `memory/BudgetedMemoryResource.h`)

The registration and the alignment read their lights ahead, and the alignment writes them behind, through
`workflow/stages/StageFramePipeline.h`: `pipelineWidths` splits a run's width between a reader, the computing
operation and, for a stage that writes, one or two writers - below four threads the stage runs one light at a time -
and `ReadAhead` and `WriteBehind` keep each light's result in its order
whatever finishes first, each admitted through `WorkflowStageServices::admitStageOperationAtWidth`. The registration
measures a light's detection channel on its own workers while the light before it is searched and hands the
measurement to `detectStars`' overload that takes it; the alignment's Lanczos warp is a compiled row held to the kernel
conformance bound. An engine context with no memory budget returns a block of a mebibyte or more to the platform on a
thread of its own (`BudgetedMemoryResource::kDeferredReleaseBytes`), never in an address-sanitized build: returning a
large block unmaps every page of it on the freeing thread, at a cost that grows with the processors that touched it,
so another thread takes that wait, while a sanitized build returns at once so that a use after release is found where
it happens. A run's
report measures at the run's width (`core::parallelCoreWidth`), and the FITS codec reads an uncompressed region of
32 MiB or more on the operation's workers when it is granted eight or more, each worker reading its strips through the
file's one reader, which OpenFITS lets any number of threads read at once; a reader admitted beside a computing
operation is granted fewer and reads on one. The engine-side lessons - compiled
runs where MSVC leaves a loop scalar, interval rules read as value windows, held selections - are in `AGENTS.md`
("Performance and parallelism").

## The application and the extension hosts

### The application (`src/AfterNightApp/`: `app/ApplicationSession.h`, `app/AfterNightApp.h`, `app/AfterNightAppContext.h`)

A thin shell: `AfterNightApp` (a `QMainWindow`) composes coordinators that live in `AfterNightControls/app/`
(`UiBootstrapCoordinator`, `ViewCreationCoordinator`, `WindowLayoutCoordinator`, `DialogOrchestrationCoordinator`,
`PersistenceCoordinator`, `AppLifecycleCoordinator`, …), and `AfterNightAppContext` implements the `IAppContext`
service interfaces. `main.cpp` installs the platform crash handler before any Qt object, states the product's version
(`kProductVersion`; the engine's is a crash annotation beside it) - a version of its own because an extension's
minimum version names the product - initializes the platform logger and owns one `ApplicationSession`;
`AfterNightApp::initialize` opens it once the settings store has been read, from the processing limits stored there
(`applicationSessionSettings`: `sessionSettingsForLimits` with the product's codecs), before anything reads an image.
The session opens there because the limits it is opened from are in the settings store the window opens: opening it
in `main` would mean reading that store twice, or opening with defaults and replacing a session the window may already
lend. `AfterNightAppContext` lends that session (`IEngineSessionProvider`), the persistence
coordinator's RAW policy (`IRawOpenPolicyService`) and output intents (`IOutputIntentProvider`). The view factory and
the events that carry an image move an `ImageDocument`; an event whose document no view takes closes it, and no entry
point makes a worker wait for the GUI thread. The extension seam's hooks land a result as an edit the history records,
a preview through `replaceImage`, generated images and output masks as new documents, and hand an execute an output
mask of the target's shape with every sample zero; a view snapshot is exported under a read lease taken without
waiting; a workflow's sequence output is opened on a worker. Teardown is `shutDownApplication`: admission closed, the
window destroyed - its views, history and extension sessions, the context it installed cleared - the windows closed on
the way out deleted, the thread pool drained and what it posted back delivered, the session shut down, checked; then
the logger and the crash handler. `Test_AfterNightApp` compiles the application's three units and runs the real window
in one session the suite owns: `ApplicationMenu` holds every menu behaviour, `ProductSmoke` every format read, a save
read back and a process of every framework family, and `Test_AfterNightApp/ApplicationDriving.h`'s `RealWindowCase` drives the window as a
person does.

### The runtime backend (`src/AfterNightExtensionRuntimeBackend/`)

The module the full host loads by name for a `runtime` extension keeps its five C exports at interface version 2 and
every `_afternight_runtime` binding's name, keywords and defaults, in `namespace an::sdk`, linking the consumer
runtime, the engine, the platform library and the interpreter - no Qt, no product library, which
`check_build_graph.py`'s `check_runtime_backend_isolation` holds, and on Linux a link that refuses an undefined symbol
(`-z defs`). Its module seat (`RuntimeSeat.h`, `gRuntimeSeat` in `consumer_state.txt`) owns one `EngineSession`:
`an_runtime_backend_initialize` opens it, idempotently and again after a shutdown; the probe before it builds a
one-thread context and a one-by-one image and lets both go; neither starts an interpreter; and
`an_runtime_backend_shutdown` is refused through the last error, naming what holds the session, while a capsule, an
asset, an operation or a hold remains. The host borrows the session through `_session_capsule()`, a capsule named
`afternight._afternight_runtime.session` holding a `RuntimeSessionCapsule` that `acceptRuntimeSessionCapsule` checks
against the consumer runtime's interface and the engine (`RuntimeSessionCapsule.h`). The session reaches the host
through a capsule rather than a sixth C export, which would move the interface version and change every loader of the
module for a handle only the full host uses. A `_RuntimeImage` is an
`ImageAsset` in compact planar storage read as fractions of full scale, with the SDK's own card list beside it;
`to_numpy()` lends the asset's samples writable and uncopied under a base capsule that keeps the asset, and
`ExportedAsset` moves the pixel generation at every binding on an exported image and at the last array's release;
`from_numpy()` makes a new asset. A registration dictionary states pixel centres at integer coordinates, moved by half a
pixel into the engine's convention. `Test_ExtensionRuntimeBackend/runtime_contract.py` records every binding's answers in
`fixtures/common/expectations/sdk/runtime-bindings-current.json`, and every difference from the recorded reference
contract, `fixtures/common/expectations/sdk/runtime-bindings.json`, is an entry of
`fixtures/common/expectations/sdk/runtime-bindings-differences.json`, held both ways.
`Test_ExtensionRuntimeBackend` loads the module by name, starts the host's isolated interpreter, and runs each
lifecycle case as this executable started again with `AFTERNIGHT_RUNTIME_BACKEND_SCENARIO` naming it.

### The full extension host (`src/AfterNightExtensionHost/`: `HostRuntimeSession.h`, `ExtensionAppContext.h`, `ExtensionNativeProcessWindow.h`)

The host opens no session: `RuntimeBackendModule` (`HostRuntimeSession.h`) loads the backend by name, refuses an
interface version other than 2 and answers `an::Status` for its probe, initialization and shutdown, a refused shutdown
being the module's last-error text; after the interpreter starts, `HostEngineSession::borrowFromInterpreter` reads
`_afternight_runtime._session_capsule()` through `an::sdk::acceptRuntimeSessionCapsule`, keeps the session and lets
the capsule go, and `ExtensionAppContext` lends it (`setEngineSessionProvider`) before any native window exists - a
missing or mismatched capsule stops the start. Teardown is `shutDownHostRuntime`, run from a guard on every way out of
`main`: admission closed, the native windows and host-local views destroyed, the pool drained and its posted work
delivered, the borrow let go, the interpreter finalized, the backend asked to shut down and unloaded only when it
accepted. The host keeps the session and lets the capsule go because its native windows use the session on the GUI
thread without the interpreter's lock, and a backend that refused stays loaded because the session and every image of
it run the module's code. The native window is a real-time preview window (`ImageRTPreviewControl`) over the host's mirror of the
bound view: its body (`setPreviewBody`) lends each frame's source - the image, its reduction by the preview view's
factor, or a part cut from the image with where it lies stated - and the manual mask laid over it to the extension's
`execute_preview` at the frame's own size and takes what it wrote back (`HostPreviewFrames.h`: `lendPreviewFrame`,
`takePreviewFrameResult`). The frames reuse their buffers through the window session's `PreviewFrameBufferPool`, which
the runtime holds: a buffer of a frame's role, size and representation is lent to the next frame that asks for one and
to no two at once, an output is cleared and its own sidecar reset to the source's cards at every frame, and the pool
keeps a gibibyte idle, the least recently lent going first; the window's close and the host's teardown close it, and
the application, having stopped a session, removes the host's frames directory (`extensionHostPreviewFramesRootPath`
in `app/ExtensionCommon.h`). Each frame states what each step of its way cost (`PreviewFrameSpans`), which the host
writes a line a frame to the file `AFTERNIGHT_PREVIEW_FRAME_TRACE` names. What an interactive frame may run over is the
manifest's `capabilities.preview_scaling` and `preview_region_margin`, passed at launch with the Processing page's rate
(`nativePreviewHostArguments` in `app/ExtensionHostManager.h`), and an Apply runs `execute` over the mirror's own image
(`sourceImage()`). The runtime runs one Python call at a time in a lane; a frame's run is stopped when an Apply, a
parameter action or the close needs it, and since a frame's run may ask the GUI thread for a setting or a log line, the
GUI thread never waits for one: an Apply, a parameter action or a target's rebind pauses the frames and runs from
`whenRunIdle` once the frame on its way has landed, and the application's request in flight (`HostInFlightExecution.h`)
holds them from its due time and lets them go at its end, however it ends. Curves are `ToneCurveModel`s and
histograms count through a `HistogramCountScheduler` per widget, the first real-time histogram being the frames'; the
manual mask is a `DrawingDocument` of the session. `main.cpp` exports an Apply's source with
`SharedBufferManager::exportImage` and imports results with `importImage`, removing the files it lent; every sample
type it sends Python is the protocol's name (`HostImageDescription.h`). The context publishes no RAW policy and builds output intents from the export settings it
was handed. The host is heartbeat-supervised by the application, so nothing may block its event loop.
`Test_ExtensionHostNative`, an isolated suite linking the interpreter, borrows one session the run owns through a
capsule record and runs the host's startup and teardown scenarios against the real backend and interpreter, each in its
own process, and holds the window's frames and the seam (`NativeWindowPreviewFrames`, `HostPreviewFramesTest`); its
opt-in `NativeWindowPreviewBenchmark` drags the window over a frame through the seam ([BENCHMARKS.md](BENCHMARKS.md)). The Lite host links only the protocol library; `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md` is the
boundary.

### The extension regression (`Test_ProcessFramework/test_extension_regression.cpp`, `ProtocolSdkContractTest`)

Every bundled extension and author template is run on the host its backend names - previewed, executed, committed and
cancelled as far as it offers each - discovered and provisioned as the application does and held to what its own code
computes at parameters that move the answer, because a pass-through satisfies an answer's shape alone and several
subjects are the identity at their defaults. The standalone dialogs are driven by a subclass of the test's own,
because no message reaches the PySide6 dialogs they open, and in Release only, because the PySide6 wheels attach to a
release `QApplication` alone. A cancelled
preview shows nothing and the next lands; a cancelled execute commits nothing and ends its session. Both hosts and
every package speak package format 1, protocol 1 and SDK 1, and another version is refused. `ProtocolSdkContractTest`
in `Test_ExtensionRuntimeBackend` holds the calls of `afternight.lite`, `afternight.protocol`, `afternight.ui` and
`afternight.ui_protocol`, recorded beside the runtime modules in `fixtures/common/expectations/sdk/runtime-bindings-current.json`. The sidecar beside a
lent buffer states the buffer's own structural cards, each card's record (`formatFitsCardRecord`) and each Exif field
as Exiv2 describes it (`describeExifFields`), a long field whole, and a preview area's solution moved to it; a
difference from the recorded reference sidecars is named in
`fixtures/common/expectations/sdk/shared-buffer-sidecar-differences.json`. The
`scientific_core` profile shares scikit-image's dependencies. A case that provisions through discovery makes its
environment under `shortExtensionEnvironmentsRoot()` (`Test_ProcessFramework/ExtensionHostTestSupport.h`).
`AuthorTemplateExecution` in `Test_ProcessFramework` runs the author templates on the host each one's backend names.

### Packaging

The canonical product is the package profile marked for the packagers in `build/scripts/tools/package_profiles.txt`,
and both packagers produce it: the private LibRaw and RawSpeed packages ship with the engine that imports them, and
the public LibRaw, OpenCV and Little CMS runtimes stay out (`check_installer_payload.py` holds both packagers to the
profile). The public runtimes stay out because no shipped binary imports them - they are deployed for the suites that
compare against them - and the check runs each packager's own payload rule rather than a list, because editing a
checker alone is never evidence of what an installer contains. Both test runners set
`AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1`.

## Measurement and safety

- **The product measured through its own workflow.** `AfterNightWorkflowDriver` runs the ImageIntegration workflow in
  one engine session through the product's own import, the shared preparation in `workflow/WorkflowPreparation.h` and
  `WorkflowProgressController`, and records the engine's own accounting beside the resident peak.
  `product_measurement.py` `archive`, `run` and `compare` archive a deployment from a clean commit, run the paired
  protocol, keep each side's first images and compare every later run's and the two sides' sample by sample;
  `--path application` measures each deployment's application through its own windows instead
  (`product_application_driver.ps1` through Windows UI Automation, and on Linux `product_application_driver_linux.py`
  through AT-SPI on a private virtual display), and the executor writes the engine's memory over a run into its log
  (`WorkflowExecutor::Result::engineMemory`). An archive keeps a deployment with its symbols and provenance so that a
  later comparison runs it rather than a rebuild, and every run's storage root and settings are its own, so that a
  measurement reads none of the host user's.
- **The real-time preview measured through a dialog.** `Test_ImageRTPreviewControl`'s opt-in
  `RealtimePreviewDragBenchmark` drags the brightness and contrast dialog's slider over a frame and reports the previews
  shown, their kinds and times, the GUI thread's pumps, the processor time, the page faults and what the presentations
  cost, from the window's and the view's telemetry; [BENCHMARKS.md](BENCHMARKS.md) says how to run it.
- **The safety campaigns over the product.** `EngineSessionSettings::memoryResource` builds a session's context on a
  caller's resource, and `Test_ConsumerFaults` sweeps the fault harness over the product's paths - a history step's
  snapshot and an undo's read, a view's read, an extension buffer's import, a SER sequence's publication, a stage's
  stamped image, a document and a payload with its manifest through their pending siblings, the standalone stack's
  group - held to `fault_points.txt` beside `Test_EngineFaults` (`suite=` names the executable).
  `Fuzz_ConsumerDocuments` reads the integration's envelopes, sessions, transform records, drizzle descriptors and
  coefficients, and `Fuzz_ConsumerProtocol` the extension protocol's messages, frames, buffer descriptors and error
  records, each held to what its writer reads back. `run_sanitizer_campaign.py` makes every active consumer suite of
  the registry a row of each configuration and refuses one whose consumer libraries are not instrumented; the platform
  library's containers are unannotated because Crashpad's static libraries are not instrumented
  (`applyEngineSanitizer({ unannotatedContainers = true })`); `thread` runs the Qt-free suites and skips the rest by
  reason.
