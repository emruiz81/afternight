# The product on the engine

How the product - the platform and consumer runtime libraries, the command-line tool, Controls, Processes, the
ImageIntegration workflow, the shell core, the application and the extension hosts - uses `AfterNightEngine`, area by
area: where
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

**The view's chrome** (`controls/ViewChrome.h`). A view's toolbar is its first row (`ViewChromeMode::Embedded`, the
default and AfterNight 1.1's row, held item for item by `Test_ImageViewControl/data/ViewToolbar.txt`) or outside it
(`External`, `setChromeMode`, told before or after `initialize`): the view builds no row, and a part a shell places
shows `chromeActions()` - the view's own actions and choices, the same objects in either mode, so their states,
shortcuts and tool tips are the view's wherever they are shown. A member is null where the view's mode has none, and a
choice that is a widget is null while the view does not offer it (a real-time preview's display mode until it offers
its autostretch controls, a static preview's choices while its owner decides them), which the listeners are told
(`onChromeActionsChanged`). Outside the view the toolbar still holds the actions, hidden, and stops its overflow pass:
the pass moves what has no room by hiding the action itself, which every part showing the action follows, so a row
nobody sees must hide nothing. A run that writes the view's image lays its notice over it (`setProcessingNotice`, a
`ViewProcessingNotice` over the canvas): the canvas under the light scrim, the pointer's presses and wheel taken so
nothing pans the image under the run, a pill naming the run with its progress and a button that shows it, and every
action and choice of the toolbar disabled - the actions by a non-exclusive `QActionGroup` and the choices by the
disabled toolbar they are children of, because both give back on `clearProcessingNotice` the state the view set
meanwhile (an action enabled while held disabled is enabled when the group lets it go, one disabled stays so), where
saving and restoring the states would undo the view's own changes. The classic shell sets no notice. The processing
lock, an atomic flag taken on whichever thread starts a run, tells the listeners on the GUI thread
(`onProcessingLockChanged`) of the state it is in when they are told - a notification arriving after a later one tells
nothing new, and a lock taken and let go before the GUI thread could tell is not told. The history answers its steps
as its window lists them - the inherited ones, then the view's own - with the one shown (`IViewHistory::steps`),
moves to one as the window's Jump To Step does (`jumpToStep`, refusing an inherited step) and tells its listeners of
each change (`addStepsListener`): what a strip of the steps needs, read from what the window reads.
`Test_ImageViewControl` holds the chrome: `ViewToolbar.EmbeddedContentsMatchTheRecord`, `ViewChrome.*`,
`ProcessingNotice.*`, `ProcessingLock.NotifiesOnTheGuiThread`, `ViewHistorySteps.*` and
`Timing.PanUnderTheFloatingToolbar` (a pan's frame under a floating toolbar, measured when `AFTERNIGHT_TEST_TIMINGS=1`
asks).

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

### What a process window is made of (`controls/TopLevelProcessBaseControl.h`, `controls/ProcessPreviewLayout.h`)

A process is a window of its own, and several things it does assume that. They are stated here because anything that
shows a process another way - inside another window, beside a view - is built on them, and
`ProcessCensus.EveryProcessStatesItsShape` (`Test_ProcessFramework`) holds every process to the record of its shape
([TESTING.md](TESTING.md), "The interface's records").

- **Three kinds of window.** A process derives from `ImageRTPreviewControl` (through `TypedProcessControl`) when it
  runs over one image with parameters, from `ImageProcessControl` when it runs over one image without the preview
  framework, and from `TopLevelProcessBaseControl` alone when it runs over files or several images. Deriving from
  `ImageRTPreviewControl` does not mean a preview: Spectrophotometric Color Calibration takes the typed framework for
  its runs and shows none.
- **Two layouts hold a preview.** `initializeStandardProcess` is handed the main layout, the preview layout, the
  preview checkbox, the button box and the two selectors, and keeps the two layouts and the selectors; the checkbox
  and the button box it only connects. Most builders make the main layout with `buildMainProcessSplitter`
  (`controls/RTPreviewUiCommon.h`): a splitter named `mainSplitter` whose first pane, a widget, holds the controls
  and whose second holds the preview. Six - the median, Gaussian, morphological, cosmetic, banding and bilateral
  filters - have no splitter: their main layout is a row whose first item is the controls' *layout* and whose last is
  the preview's. Whatever rearranges a process therefore moves widgets in the one case and layout items in the other,
  and deletes neither the main layout nor the preview layout, which the framework goes on reading
  (`findMainProcessSplitter`, `findPreviewPanelWidget`). Three processes are handed no checkbox and always show their
  preview (Image Compare and the LRGB and NBRGB combinations); two show no preview until their inputs are chosen (the
  two star mask recombinations); the Narrowband Palette Composer fills the preview's pane with a grid of its own; and
  Multiscale Linear Transform keeps controls in the preview's pane.
- **The window dresses itself at its first show.** `initializeModernProcessShell` moves the process's own layout into
  a content widget under a header, a banner and a card, and hoists the button box of a preview process to the
  window's foot. Before that show the layout is the builder's; after it, it is the shell's.
- **A window fits and locks its own size at every show.** `showEvent` runs `ensureFixedWindowFitsContent` and
  `ensureModernShellSize` at once and again after 0 and 30 ms. A process marked as of a fixed size
  (`setFixedSizeWindow`, or one whose builder set equal minimum and maximum sizes) is resized to its content and
  locked there, whatever its parent is.
- **The preview checkbox resizes the window.** Unchecking it drops the layout's size constraint, fixes the window at
  the controls' width for one turn of the event loop and then unlocks it; checking it restores the size the window
  had. Both act on the process widget itself.
- **The panes carry their own margins.** A splitter's two panes are laid out with 12 px at the sides and 8 px above
  and below, so a preview view is 24 px narrower than the pane it is in.
- **How wide the controls are.** Measured on Windows under the product's style (Segoe UI 9 pt, 2026-11-13), the least
  width the controls of a preview process take runs up to 604 px (Profiled Starmask Recombination). Fifteen
  processes that show a histogram under their controls take 524 to 534 px, set by the one row of the histogram's
  toolbar; twenty take 360 px or less; Curves Transformation takes 374 px, set by its curve editor's 350 px.
- **A process brings key sequences of its own.** Image Integration's menus carry seventeen as window shortcuts
  (`Ctrl+O`, `Ctrl+Shift+O`, `Delete`, `Ctrl+Shift+Delete`, `Ctrl+G`, `Ctrl+I`, `Ctrl+E`, `Ctrl+Enter`, `Ctrl+1` to
  `Ctrl+8`, `Shift+F1`), with `F5` and four more on its tabs' tables; Mask Drawing carries eight on itself and its
  children. No other process defines one; every process closes on Escape, read in `keyPressEvent`. In a window of
  its own a process meets no other window's shortcuts. Put inside a window that holds the same sequence, neither
  runs: Qt reports two window shortcuts that match as ambiguous and triggers nothing, wherever the focus is.
  Narrowing the process's shortcuts to its widget and children leaves the pair ambiguous while the focus is inside
  the process; accepting Qt's shortcut-override event alone makes the key reach the focused widget, which does nothing
  with it. What works is all three together: the sequences taken off the process's actions and shortcuts, the
  process widget accepting the shortcut-override event of a sequence it holds, and the action run as the override is
  accepted ("A process shown as a page", below). A text field inside the process still keeps the keys it edits with, because it accepts their
  override before the process sees it.
- **`Ctrl+Enter` is the keypad's Enter.** A key sequence written `Ctrl+Enter` is `Qt::Key_Enter`, the keypad's key;
  the main keyboard's Enter is `Qt::Key_Return`, written `Ctrl+Return`. Image Integration's Execute carries the
  first alone.

A window also tells the shell what it is and how its runs go (`controls/ProcessPresentation.h`), so that a shell can
show every open process - as a task in a rail, over the image a run writes - without knowing any of them:

- **What it was launched as.** An entry point makes and shows its window and hands nothing back, so the launcher runs
  each launch inside a `ProcessLaunchScope` (`controls/ProcessLaunchScope.h`) that names the descriptor, and a window
  made inside it takes, as it is made, the descriptor's id (`descriptorId`), its guide page (`helpPath`), the surface it
  is shown on when a presenter takes it and the width of its page's column (`processSurface`, `processColumnWidth`,
  stated as `ProcessDescriptor::surface` and `columnWidth` where the process is registered, a window and the standard
  column until a process is given a page). A window made outside every launch belongs to no descriptor and is a
  window. The scope replaced an event filter that watched the application for the window a launch showed: one
  mechanism now says which descriptor a window belongs to, and it knows before the window registers, so whoever
  hears of the window hears of its descriptor.
- **Where its run stands.** `setRunState` states a `ProcessRunState` - idle, running with how far it has come when it
  can tell and what it is doing, succeeded, failed with why, or cancelled - and `reportRunProgress` moves a run on its
  way; both may be called from a worker and reach the shell on the GUI thread, and a report that lands after its run
  ended changes nothing. The framework states it once for every single-image process: `ImageRTPreviewControl` from
  Apply to the run's end, its progress from the run's own reports (`withRunProgress`, beside the progress window's)
  and, over a sequence, from its frames; the geometry support (`processes/GeometryProcessSupport.h`) for the processes
  that run through it, which name themselves on their request (`process`); and each process with a run of its own -
  the top-level windows, Photometric Color Calibration, the star profiler's detection, New Sequence, and Image
  Integration and its Lite front end through `WorkflowExecutionController` - where it starts and ends. A run whose
  result was dropped because its image changed or closed under it ends cancelled: nothing failed.
- **What it targets.** A single-image window tells the shell its target as it is made, when it is pointed at another
  image and when its image closes (`processTarget`).
- **The presenter.** The shell's `IProcessPresenter`, lent through `IAppContext::getProcessPresenter`, is told of both
  (`runStateChanged`, `targetChanged`); a shell that lends none - the classic shell - shows every process as the window
  it is, and nothing of it changes.

### A process shown as a page (`controls/ProcessPageLayout.h`, `controls/PageShortcutScope.h`)

A presenter may take a process into a page of the main window instead of showing its window. Which processes are
pages, and which column each takes, is stated where they are registered (`kPageSurfaces` in
`app/ProcessRegistryBuiltins.cpp`, read into `ProcessDescriptor::surface` and `columnWidth`): the pointwise and tone
processes and the stretching processes, every one in the wide column, since the one row of the histogram's toolbar
under their controls - and Curves Transformation's 350 px curve editor - is wider than the standard one; the
spatial and noise processes and the mask, star and background processes, each in the column its widest row needs; and
the combinations launched on an image (Image Blend, Image Compare, LRGB and NBRGB Combination, NB Color Mapper), every
one in the wide column for its histogram's 380 px chart and toolbar row. Every process with a preview that is launched
on an image is a page (`ProcessSurfaces.EveryPreviewProcessWithATargetIsAPage` reads the census record for it). The
processes with no preview launched on an image are mirror pages (`ProcessSurface::MirrorPage`): the geometry and
filter-array processes (Crop, Rotate, Resize, Integer Resample and CFA Integer Resample, Debayer, Split CFA, Extract
Sequence), the colour tools (Color Calibration, Background Neutralization, Linear Match, SCNR), the photometric and
spectrophotometric calibrations, Pixel Math, the external tools (StarNet, its star mask, DeepSNR) and the two star
profilers. The processes launched on no image, which choose every image they read in their controls, are preview pages
with no single target: Channel Composition, Merge CFA, the two Starmask recombinations and the Narrowband Palette
Composer. Image Integration is a full page (`ProcessSurface::FullPage`): the process whole, under a header that says its
job ("Image Integration as a page", below). Image Integration Lite, Image Calibration, Image Registration, Image
Stacking and Image List Analyzer are full pages too, each what its window lays out, whole ("Image Integration Lite as a
page" and "The tools as pages", below). No process of the application states a window of its own: every descriptor's
surface and column is held to a record (`ProcessSurfaces.MatchTheRecord`,
`Test_ProcessFramework/data/ProcessSurfaces.txt`), and the case fails for a process that opens a window and states no
page. A presenter is asked once, at the process's first `setVisible(true)` and before it is visible (`adopt`); taking it means
`attachToPage(host)`, which arranges the process as a page the first time (`makePageLayout`) and lays it into `host`. A
process that cannot be one answers no page and stays the window it was. The arrangement is generic, made from what the
process already is:

- **The standard parts.** `initializeStandardProcess` records the six parts it is handed (`ProcessStandardParts`):
  nothing finds them by name, since the builders name them four ways. `ProcessPageLayout` reads them and moves by
  reference: a splitter's two panes are reparented, a row's two layouts are taken out and given widgets of their own,
  the preview's pane to the canvas and the controls into a body that scrolls. The main layout stays the process
  widget's layout - its old items hidden - and holds the page, and the preview layout goes on receiving the preview
  view, so everything the framework reads through them still holds. Nothing is deleted.
- **The column.** Beside the canvas, 360 px wide or 520 px for a wide process, 320 and 440 px in a window under
  1200 px wide: a slot for the shell's compact readout, the header (the category a shell names, the process's name with
  its description as the tooltip, Close), the toggles (the process's own preview checkbox, moved; Preview area and
  Compare, which act on the preview view's own checkbox and Switch image), the banner, the body and the footer.
- **The body fits the column** by scrolling up and down only, with the footer always in view; by its top-level group
  boxes as sections (`AnCollapsibleSection`, titled as the groups and in their order, none for a single group) - found
  through the controls' layouts and the layouts of the plain containers they lay out, never inside a group, a tab
  widget, a scroll area or a splitter, and made in the group's own place - whose
  open states are kept in the process settings store under `<process name>#page`; by closing the section used longest
  ago - opened or worked in - when opening another would push the body past its height; and by giving up the controls'
  own side margins, which are its padding, when the controls would be wider than it - fitted at every change of the
  body's width and at every change of the controls' own layout, because their least width moves after the first fit
  (a style, a font, a text that settles) with no change of the body, and a page whose body needs no scroll bar has no
  second resize to say so. The height a section adds is read
  after the hints up the chain from it are dropped: a widget's item keeps the size it last had until its widget asks
  for a new geometry, and a section's change asks only when the event loop turns. Target image and Channel are moved,
  with their labels, to a row at the body's head: the labels on one line and the fields on the next, so two fields lie
  side by side whether each has a label. They are found wherever the controls lay them out - a form, a box, a grid,
  inside a group box (Star Reduction groups its target with the starless image it reads) - and a label is the one
  before its field: the form's, the box's previous item, the grid's previous cell.
- **A process of another shape arranges itself first.** `makePageLayout` is virtual: Multiscale Linear Transform keeps
  the selected layer's controls, the target and what its preview shows in its preview's pane, which the canvas takes
  whole, so its override moves them into its controls - the selected layer's group under the layers it is chosen from,
  its name on a row of its own, the preview's mode a labelled row of the target's form - and takes its global settings
  out of the scroll area of their own the window gives them, with the 480 px least width that area's content was given
  for the window's wider pane, before the generic arrangement runs. NB Color Mapper scrolls all its controls in a scroll
  area of their own beside its preview; its override takes them out of it, so the body scrolls them and its seven
  groups are the page's sections, and disables them during a run where they now are. A scroll area hides how wide its
  content is, which is why the census measured the mapper narrower than it is: as arranged its controls take 508 px.
- **A process with no preview is a mirror page.** Its parts are its window's: its layout, the button box nearest it that
  holds a button a footer shows, and the target and channel selectors it recorded (`initializeTargetViewControlComboBox`,
  `initializeListOfSupportedChannelslComboBox`); `ImageProcessControl::makePageLayout` hands them over, and a process of
  the typed framework with no preview layout is arranged the same way. What the window laid out is moved whole, in
  order and with its stretches, into the body, the window's margins its padding; there is no toggles row. The canvas is
  a view of the target (`ProcessPageLayout::setCanvasView`): a view in `ViewControlMode::st_preview`, its toolbar
  outside it, over the target's own image shared (`ImageDocument::share`) - never copied, never written, since a shared
  document hands out no writer - presented as the target's view presents it, kept fitted until the person zooms it, and
  shown again whenever the target is edited or another target is chosen, before the process reads it. It is the view
  the person works on, `ImageProcessControl::interactionView()`, which is the target's own view while the process is a
  window: a process that marks something on its view - Crop's and Rotate's rectangle and Rotate's turn, the colour
  tools' and the photometric calibration's areas of the target, the photometric calibration's matched stars - marks it
  there, told by `interactionViewChanged` when the page is made, and leaves nothing on the target's view; an area of
  another image a colour tool reads is marked on that image's own view. Image coordinates are the same in both views,
  so a rectangle drawn on the page is the rectangle of the image a window would read. A closed task's view lets the
  image go. A process whose controls do not fit its column as the window lays them out arranges them before the page is
  made, on the page only: the resamplers put the downsample mode under the factor, Resize puts its new size under its
  current size, Crop lays its New Selection button across the readouts' whole width, SCNR's amount slider keeps the width
  it prefers and not the 160 px its window gives it as a least, Multiscale Linear Transform puts Reset Biases under the
  other two layer buttons (each of the four is a few pixels wider than the column in a font wider than Segoe UI, which
  `PageLayout.*FitsItsColumn` measures wherever the platform measures text), the photometric calibration lays its
  background check box across its form (`wrapRowUnder`, `spanFormRow` in `processes/ProcessCommonUtils.h`), the star
  profilers put the last three of the six buttons under their table on a row of their own, Profiled Starmask
  Recombination lays the two check boxes of its stars' filter under that row, and Pixel Math arranges itself whole - its expression tabs take every height the body leaves
  (`ProcessPageLayout::Description::controlsFillTheBody`) and keep at least a third of it, the first tab's function,
  operator and symbol tables become one group under the expressions, which the page makes a section and which serves
  the expression of whichever tab is shown, and the Settings group's row of sizes and formats wraps in two.
- **An external tool runs in its page.** On a page StarNet, its star mask and DeepSNR show no progress window of their
  own: the run's state carries the program's progress (the geometry support's run moves it with the operation's
  reports), the page's run row and the task show it, and the page's Cancel stops the program through a stop of the
  run's own (`ImageProcessControl::beginRunStop`, `cancelRun`; `InPlaceRunRequest::stop`, `NewViewsRunRequest::stop`).
  A run of the geometry support that fails on a page - any of its mirror pages - is said in the page's banner and opens
  no dialog (`GeometryRunWords::page`). The star profilers draw their stars on `interactionView()` and hear a click on a
  star there, and centre a selected star there; the target's own view shows nothing of them.
- **A page with no single target.** A process launched on no image chooses every image it reads: it has no target, so
  its task has none (`Task::singleTarget`, from the descriptor's launch mode), its header names the process alone, its
  rail item is its glyph on a tile, and while its page is shown the image actions act on no image
  (`ViewService::setTaskSubject` with no target) and the looking actions on its canvas alone. The image a process reads
  first is still its target inside the framework - its preview is made over it - but no task's. The canvas shows the
  process's preview - the Palette Composer's grid of previews, which marks itself as the canvas's content with the
  dynamic property `anCanvasContent` - or, while images are still to choose, names them
  (`ImageProcessControl::setMissingInputs`, `ProcessPageLayout::setMissingInputs`), what it holds hidden meanwhile.
  Channel Composition and Merge CFA show their result in a preview of their own: they are processes of the typed
  framework whose target is their first input, the others read beside it as the Starmask recombinations read theirs, in
  every frame over the whole of every input (`PreviewScaling::FullOnly`: Merge CFA's mosaic is twice a plane's size, so
  it has no preview area; Channel Composition's match measures the whole of each component); their result opens beside
  the first input. Three or four sequences are still composed frame by frame by a run of the window's own, which the
  framework's Apply - one target frame at a time - cannot express. These processes hand their button box to the
  framework so a page's footer shows it, and answer Ok themselves.
- **A run on a page opens no progress window.** The preview framework's Apply asks a process for its progress window
  (`getProgressDialogControl`) only in a window of its own; on a page the run shows in the page's run row, which the
  operation's reports move (`withRunProgress`), and the page's Cancel stops it through the run's own stop. A run over a
  sequence states itself as one (`ProcessRunState::sequence`, `sequenceRunningState`): its progress is the share of
  the frames done and `frameProgress` the frame's own, which the frame's operation reports (`withFrameProgress`,
  `TopLevelProcessBaseControl::reportFrameProgress`) and each frame's start sets back to nought, and the run row shows
  them as two bars, Overall and Processing, a slice sliding along Processing while the frame tells nothing
  (`ProcessPageLayout::runBars`). The runs a window keeps of its own do the same on a page: Channel Composition's and
  Merge CFA's over sequences - whose `cancelRun` stops that run, so the page's Cancel reaches it - and the star
  profilers' detection, on a stop the page's Cancel requests (`beginRunStop`). The workflow tools' runs open their
  progress window through one front (`processes/WorkflowToolRun.h`), which opens none on a page and lends the stop
  the page's Cancel requests through the tool's `cancelRun`.
- **A full page** (`ProcessPageLayout::Description::fullPage`, the default arrangement of a process whose descriptor
  states one) is the process whole, with no canvas and no column: what its window laid out is moved into a scroll area
  that fills the page and scrolls only when the page is smaller than the least the process takes. Its own menu bar is
  hidden there and its menus offered (`ProcessPageLayout::menus`); the Observatory shell lists them under the task
  header's menu button, each entry the process's own action. At its head, over the area, the page says the process's
  message (`showProcessMessage`) as a column's banner does - a full page has no window banner to say it in - and
  during a run shows the run's own row in the message's place, with Cancel at its end, which asks the process to stop
  the run (`cancelRun`): a full page has no footer for Cancel to take Apply's place in. What the process laid out is
  not disabled meanwhile: the process disables what it must, as it does behind a progress window, which is not modal. A
  process that shows its run in itself - Image Integration and its Lite front end, whose run's view has its own
  Cancel - says so (`TopLevelProcessBaseControl::showsRunInItself`) and has no such row
  (`Description::runAtHead`).
- **A job** (`ProcessPageLayout::setJobHeading`, `JobHeading` in `components/AnTaskHeader.h`). A process that holds a
  body of work with a name of its own - an integration - says it to its page for a shell's task header to show in
  place of the process's name: an eyebrow, the job's name, its facts on a line, a measure with a bar (a share, and
  whether it is exceeded), acts shown as icon buttons and a primary act shown as the primary button. The acts are the
  process's own `QAction`s, so a button is enabled while its action is, says what it says and triggers it, and nothing
  about the job is kept twice. The process states it once it is a page (`pageLayoutMade`) and again when it changes;
  the page tells one listener. It is a description and holds no widget, so Controls draws the header, Processes fills
  it and neither names the other's classes: a process cannot include a shell, and a shell's header must not know which
  process it serves.
- **Compare names what it shows.** The toggles row's Compare shows the preview's alternate in place of what the canvas
  shows: "Processed" and "Original" for a process, whose alternate is the image before it. Image Compare's alternate is
  its second image, so it names the two "Image 1" and "Image 2" (`ProcessPageLayout::setCompareNames`); its one
  button, Close, is the framework's like every other process's, so the page's footer holds it.
- **The footer shows what the button box holds** - Reset and Help at the left, a stretch, the others, Close, the
  primary (Apply, Ok) - as buttons of its own that press the box's, follow its enabled state and leave its connections
  as they were; the box is kept, out of sight. During a run Cancel takes the primary's place and stops the run as a
  progress window's Cancel does (`cancelRun`): the image keeps the state it had.
- **Page mode turns the window's code off.** The modern shell's header, banner and card are never built; `showEvent`
  fits and locks nothing; the preview checkbox creates and removes the preview and resizes no window; a window's
  geometry is neither restored onto the page nor stored from it - the stored one is kept for the window - while its
  JSON settings are; a message, an error included, is said in the page's banner and opens no dialog; the run's own row
  replaces the banner during a run, with the toggles, Reset, Close and the controls disabled. Esc closes an idle page
  and does nothing during a run; Ctrl+Enter, from either Enter key, runs the primary action (`triggerPrimaryAction`).
  `detachToWindow` makes the process a window of its own again with its page whole inside it, and `attachToPage` takes
  it back; neither touches its parameters, its preview, a run on its way or the lock on its target.
- **A page's own shortcuts win inside it** (`PageShortcutScope`), by the three parts the shortcut trial found: the
  sequences taken off the process's actions and shortcuts, the shortcut override accepted by the process widget for a
  sequence it holds, and the action run. It runs as the override is accepted, not at the key press that follows:
  the press goes to the focused widget first, and a combo box takes any key with text for its keyboard search, so a
  page waiting for the press would run nothing with the focus on one; the press that does reach the page is let go
  by. A shortcut made for a widget and its children still runs only with the focus inside it; a field that edits with
  a key accepts its override first and keeps it; with the focus outside the page the window's shortcut runs. Detached
  to a window, the sequences are put back and the objects are live, as in a 1.1 window. An action that is an entry of
  a menu would lose its keys from the entry with its sequence - a menu draws them from the action's shortcut - so the
  scope writes them into the entry's text after a tab, which a menu draws as an entry's keys, and takes them out again
  when it lets go. An object in a window of its own that the page opened - a dialog's action or shortcut - is left as
  it is: a key pressed in that window never reaches the page, so its sequence could be taken but never run.

### The history (`controls/ImageHistoryTracker.h`, `controls/ImageHistoryBackupQueue.h`, `controls/ImageHistoryStepCodec.h`, `controls/ImageHistoryIndex.h`, `controls/ImageHistoryCompanion.cpp`)

A step is a copy taken under a lease (`copyForHistory`), admitted against the session's
`MemoryCategory::PendingSnapshots` allowance before it is made - an edit's copy waits for room, a state already shown
is `ReservationKind::Required` - and written by the history's `BackupQueue` through the history's step codec,
samples, metadata and validity mask together, read back only for the identity the history drew for it. Every
history, in a project or not, is made with the one codec (`historyStepCodec`, below), under the compression the
settings state when the view is made. A step is a copy rather than the shown asset, so the history need not know every
path that may write an asset in place through its write lease; and a state already shown cannot be refused without
the timeline ceasing to match the view, so its copy is admitted beyond the allowance. The view reaches the history
through `IViewHistory`, made by `createViewHistory`
in `controls/ImageHistoryCompanion.cpp`: its edit worker runs `editPreparation` - the state the edit replaces written,
the result's copy admitted, a sequence frame written - so `registerEditBeforePublishing` on the GUI thread never
waits; undo, redo, a jump and a removal read on a worker and show through the view's `replaceImage`, the current step
moving once shown. A sequence step references its `DocumentSequence` and keeps the frame it changed, reading the
sequence's own frame through `openProcessFrames`. Each history writes into its own `AN_HISTORY_*` directory and
reclaims only that, until a project gives it a home ("A history that outlives its view", below). The window previews on a worker through `presentForView` and shows the histogram through
`createPreviewHistogram`. A step that is linear data (`isLinearData`, `controls/AutostretchApply.h`) is presented
through the unlinked autostretch the recent-file thumbnails take, so it looks as the dashboard shows it, and any other
step as it is. The step's title and details are `AnElidedLabel`s of a set number of lines, because a label that wraps
to its text moved the preview below it from one step to the next. `Test_ControlsHistory` holds the history, and
`Test_ImageViewControl` the viewer's history cases.

**The step codec** (`StepCodec`, `controls/ImageHistoryStepCodec.h`) is what writes a step to a file and reads it
back; a write answers the file the step landed in, so the tracker and its queue know nothing else of a format. There
are two formats and one codec every history writes through. `snapshotStepCodec()` is the engine's image snapshot,
`.ansnap` (`writeHistorySnapshot`, `readHistorySnapshot`). `xisfStepCodec(compression)` writes a step as an XISF
image, `.xisf`, that any reader of the format opens: the samples as XISF stores them, uncompressed or under LZ4 or
Zstandard, with the image's own cards beside them. `historyStepCodec(compression)` is the one a history is made
with: an XISF step, and a snapshot beside the others in the two cases an XISF image cannot be the step - a record
longer than one image's cards carry, and a session whose engine has no XISF adapter (a suite that asks for none) -
so that no edit is refused for the format. It reads a step by the codec the file's extension names, so one directory
may hold both kinds and a step written under one compression reads under any other.

- **Why one codec for every history.** A history outside a project used to write snapshots and one inside a project
  XISF. With the scanner out of the way (below) an uncompressed XISF step costs what a snapshot costs, so one codec
  writes both, and moving a history into a project is moving its files: no step is read or written again
  (`ProjectHistory.MoveInKeepsEveryStep`). Unifying removed no format: the XISF step's record is a snapshot and its
  fallback is one.

- **Why an XISF step carries a record.** XISF states less than a step is. The engine's XISF writer writes the
  preserved cards and the acquisition facts, and a read gives back default semantics, a lineage that begins at the
  file, no output intent, none of another format's cards, a description restated from the file's own (a normalized
  floating image, an unsigned integer one) and no mask. So the file holds two properties this application alone
  reads: `AfterNight:HistoryStep:Record`, the engine's snapshot of the step's metadata, description and identity
  over a frame of one pixel, and `AfterNight:HistoryStep:Validity`, the mask's bits, each compressed and written as
  text across the property's cards. A read takes the samples from the image and everything else from the record,
  and refuses an XISF file that has none. The record is the engine's own snapshot rather than a second encoding of
  the metadata in the product, so a field the engine adds to an image's metadata is in a step without a change
  here; the engine reads a snapshot from a file, so the record is staged in one beside the step for the length of
  the call, under a name shorter than a step's: a step whose own file fits where paths are bounded is never refused
  for a file nobody keeps (`HistoryStepCodec.AStepIsWrittenWhereverItsOwnFileFits`; the record's first name, the
  step's with a second identifier after it, passed 260 characters in a history a few folders deep).
- **What bounds it.** The record travels in the image's cards, and one image holds `kMaxCodecCards` cards of
  `kMaxCodecCardTextBytes` each: `kMaxXisfStepRecordTextBytes` (24 MiB of text, 18 MiB of compressed record) is
  the most a step carries, which a mask with no pattern reaches at about 150 megapixels. The XISF codec refuses a
  longer record when it is written, with nothing published; the history's codec then writes the step as a snapshot
  (`ProjectHistory.AStepBeyondWhatXisfCarriesIsKeptAsASnapshot`).
- **What another reader sees.** XISF has no signed integers and counts rows from the top, so a signed image's
  samples are written as the unsigned codes of the same bits and a bottom-up image in the order it holds its rows.
  The image's cards are written for that reader less the ones an XISF unit cannot spell (a FITS keyword longer than
  eight characters, text outside printable ASCII, a property name that is no identifier), because the encoder
  refuses the whole image for one such card; if it still refuses, the step is written with its record and identity
  alone.
- **Why a step's header opens without the XML declaration.** The one departure from the XISF specification,
  made for a history's steps and for nothing else the application writes. Windows' virus scanner takes a file for
  markup when its first kibibytes read as text and hold an XML declaration or a comment, which is how the
  specification opens every XISF header, and then reads the whole file for tags. The bytes of normalized 32-bit
  floating samples are dense in `<` and `>` - the top byte of every sample from 0.0078 to 0.031 is the first, and
  from 0.125 to 0.5 the second - so each uncompressed step cost the scanner 0.3 to 0.5 s of a processor when it was
  closed, and a read of the step during that pass waited for its end. How the file is written or reopened has no
  part in it: a copy of such a file is read the same way. The step codec therefore asks the engine for
  `OutputHeaderProlog::Blank`: the XISF adapter writes the unit as specified and, in the pending file before it is
  closed, overwrites the declaration and the comment with spaces, so the header keeps its length and every block
  its place. Such a header is still well-formed XML; this application and PixInsight read the unit as any other.
  An image the user saves, and every other XISF file, opens as the specification says
  (`XisfStepCodec.AStepsHeaderOpensWithoutTheXmlDeclarationAndTheComment`).
- **Why there is no FITS codec.** A record would travel in the header, and the engine's FITS writer takes a header
  of at most `kMaxCodecCards` eighty-byte records, about 400 KiB of record: the mask of an ordinary image exceeds
  it unless it compresses to almost nothing. FITS holds such data in an extension, which the engine's FITS adapter
  does not write.
- **What it costs.** For a 60-megapixel Float32 image, measured beside the snapshot (`StepCodecCost.*`), an
  uncompressed XISF step is written and read in the snapshot's time, an LZ4 one is written in about 1.4 to 1.6
  times and read in about 1.7 times at three quarters of the size, and a Zstandard one is written in about twice
  and read in about 2.4 times at two thirds.
- **Why its samples state no checksum.** A snapshot checks every section with a hash that costs next to nothing.
  XISF's own checksums are cryptographic: SHA-1 over the same image takes the write from about 105 ms to about
  305 ms under LZ4, and SHA-256 to about 510 ms. An XISF step's record is checked, being a snapshot; its samples
  are held to their size and, compressed, to decompressing to it. The history keeps no digest of its own either: a
  step is held to what reading it checks.
- **Why a step is not flushed** (`StepDurability`). A step is published whole or not at all against a crash of the
  process; flushed to the device first it would be whole against a power cut too, and that costs the edit that waits
  for it: for a 60-megapixel Float32 step under LZ4, 94 to 129 ms in grey and 273 to 382 ms in colour, and
  uncompressed 71 to 115 ms and 194 to 319 ms (`StepCodecCost.Flushing*`). The write's time comes first, as it did
  for the checksum, in a project as outside one. What a power cut can cost is a step written in the seconds before
  it: one that does not read when a later session first reads it is left out of the history and reported, and the
  image's own file is never the history's to write.

`StepCodecFidelity.*` holds every codec to the same cases - each of the eight representations in grey and in
colour, the description, every metadata object with cards no container spells, the mask, the identity - so a codec
added later is held to them by one line; the history's codec is among them.

**A history that outlives its view** (`controls/ImageHistoryIndex.h`). A step is a whole state and a file that
stands alone, so a history is kept by keeping its directory and saying, beside the steps, what they are. What gives a
history such a directory - a project, for one of its images - hands it a `HistoryHome` (the directory, the
compression, the image's file) through `IHistoryPlacement`, which `historyPlacementOf` answers for a view. Controls
knows homes and nothing of projects.

- **The index** (`kHistoryIndexFileName`, a `HistoryIndex`) lists the steps whose files are written - id, name,
  description, the file's name with its extension, when, and the run that wrote it, which with the file's base name
  is the identity a read requires - the steps the image inherited, the step shown, the step the image's file equals
  and that file's identity. It is written whole, off the GUI thread: by the writer when a step's file lands, and on
  the thread pool when the step shown moves and when the image is saved, one write at a time and always from the
  latest state. A step still being written is not in it, so a process that dies between a step's file and the index
  leaves a file no index lists; `sweepHistoryDirectory` removes those, the record files a killed write left and the
  payloads never published, when the directory is next entered and before anything is written into it.
- **Entering** (`ImageHistoryTracker::enterHome`) never waits. A worker reads what the home holds; then the steps
  recorded from that moment are written there, through the home's compression, and the steps the history already
  has go in as the caller says (`StepsMove`): moved - `moveStepFile` renames a file on one volume and copies it
  across volumes, removing the original only once the copy is whole, under a lock a read of a step shares - or left
  with the session, in which case the index lists what was recorded since.
- **One history keeps a directory at a time.** The history that enters a directory claims it, and it is that
  history's until its last write there has landed. Another history is refused it: a second view of the same file
  goes on with a history of the session's, and never reads, sweeps or writes the first view's steps
  (`ProjectHistory.OneHistoryKeepsAHomeAtATime`). Without the claim the second history attached the first one's
  steps as its own, and the first edit made in it removed the steps the first view had ahead.
- **Closing.** A history in a home does not stop its writer when its view closes. The store is handed to the thread
  pool, where `BackupQueue::finish` lets the steps still waiting be written, the index is written a last time and
  the history's own directory goes; the GUI thread waits for none of it
  (`ProjectHistory.ClosingAViewFinishesItsWritesAndKeepsItsDirectory`). The directory stays claimed, as closing,
  meanwhile (`waitForClosingHistories`), and an entry into it waits for that on its own worker, so the same image
  opened again reads what the earlier view left only once it has. The application's shutdown drains the thread pool
  before it shuts the session, which is what those writes run under; it closes the session's admission first, so a
  step whose write had not begun when the application closed is not written, and the index lists what landed.
- **The file's identity** (`FileIdentity`, `readFileIdentity`): the size, the modification time, and the SHA-256 of
  the first mebibyte. It is what says, without reading an image twice, whether a history kept for a file is that
  file's. It tells a file apart from another of the same size and time only by its head, which is where a FITS or
  XISF file states what it is and where an edit saved by another program lands.
- **Opening again.** When the identity equals the recorded one the kept history is attached around the image as it
  was opened: the step the file equals stands for the opened state and is the one shown, the earlier steps are
  behind it and the ones left unsaved ahead of it, nothing is restored and the view is not marked as changed
  (`ProjectHistory.ReopenShowsTheFileWithTheSavedStepCurrent`). An edit made from there is an edit made after an
  undo. When the step the file equals is no longer kept - the history rolled past it, or its file is gone - the
  opened state stands where it was. A step whose file is gone is left out as the history is attached; one that does
  not read is left out when it is first read; either is said once and the others stand. When the identity differs,
  or the index does not read, the kept history is set aside whole in a folder of its own under
  `kDetachedHistoryFolder`, where its steps still open as images, the person is told, and a new history starts.
- **Saving.** The history hears its view's saves (`fileSaved`) and whether the view shows its file
  (`historyShowsItsFile`), so it knows which step the file equals. In 1.1 Save and Save As are one action, and the
  view takes the path it was written to; so a history stays with its image when the image is saved under another
  name, and the index follows the file.
- **A sequence is given no home.** Its states are its file and the frames changed in it, not files that stand
  alone, and the index has no place for them.
- **Compacting** drops the steps a `HistoryCompaction` does not keep - all but the last few, or all but the state
  the image was left at - and removes their files: through the tracker for a history a view has open, and in the
  directory (`compactStoredHistory`, the index rewritten before the files go, the directory held meanwhile so that
  a view opening the image waits) for one no view has. What it says it frees is what the files take
  (`ProjectHistory.CompactKeepsTheCurrentStateAndFreesWhatItSays`).

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
and stores through `readExportSettings` and `writeExportSettings`, the table every save reads. The Projects page
(`controls/settings/ProjectsSettingsPage.h`) shows and stores what a project starts from through `readProjectSettings`
and `writeProjectSettings` (`controls/settings/ProjectSettings.h`). A shell's Settings dialog is made by
`addSettingsPages` (`controls/settings/SettingsPages.h`) in the order the shell asks for
(`DialogOrchestrationCoordinator::Dependencies::settingsPages`): the classic shell's lists 1.1's pages in 1.1's order
and no Projects page, since it offers no projects; the Observatory shell's puts first what a person changes most -
General, Projects, Interface, Processing, Raw Images, Plate Solving, Catalogs & Data, Export, External Tools, Logging.
The table the palette finds settings in (`settingsSearchPages`) is that order's pages and their rows, and the dialog's
own search finds a page by a row it shows too, so a row added to a page is found without a search word of its own. A
shell whose menu bar shows on Alt hands the dialog a keeper (`Dependencies::keepMenuBarShown`), and the Interface page
then offers "Always show the menu bar", which acts at once, as every appearance row does under the Observatory profile.
The dialog opens within the available area of its screen, its frame included (`holdWithinAvailableArea`,
`app/WindowPlacement.h`), where that area holds its least size: on a 1280 x 720 display its default height put its
title bar above the screen. The Extensions window (`DialogOrchestrationCoordinator`'s:
the Extension Manager, `controls/settings/ExtensionManagerPage.h`, and the installed packages,
`controls/settings/ExtensionStatusPage.h`) says in a package's details how it runs - with the engine's access when its
host is the full one, without it otherwise - and where its processes open: a panel the application draws opens in the
main window where the shell takes processes into pages (the application context lends a process presenter) and in a
window of its own elsewhere, an extension's own window in it, and one with no interface nowhere. A browse entry knows
the second of an installed package alone (`installed_sdk_backend`, `installed_process_interfaces`, resolved by
`resolvedExtensionInterface` as a launch resolves it), so a package not installed says it is known once installed. The product's version is
`productVersion()` - `QCoreApplication::applicationVersion`, which the application states - and the about and system
information dialogs show the engine's (`engineVersionText`) and its libraries' from its own report
(`engineLibraries`, over `engineDependencies`). A dashboard card probes its file with `probeDashboardFile`, background
work admitted with `tryAdmit` that never waits. The FITS header, Exif and summary windows read the view's document with
`inspectDocument` under a lease that never waits and keep what they show while a writer holds it; `openViewTool` opens
every tool window, the astrometry and plate-solve windows once per view. The PixelMath editor highlights
`pixelMathVocabulary()`, the engine's function tables and the syntax's operators. `Test_ControlsDialogs` holds all of
it, with the settings dialog's cases.

### The dock area (`controls/KDDockWidgetsHelper.h`, `theme/KddwProxyStyle.h`)

Docking is KDDockWidgets. The decorations declare the configuration they are drawn for beside them - the flags of
`kDockLibraryFlags`, `kDraggedDockWindowOpacity`, `kDockWidgetMinimumSide` - and `AN_ViewFactory` makes them: `AN_TabBar`,
`AN_TitleBar`, `AN_Stack`, `AN_Group`, `AN_FloatingWindow`, `AN_Separator`, and under the Observatory profile the drop
indicators and band (`controls/DockDropIndicators.h`), painted from the design tokens ("The dock decorations" in
`docs/dev/THEME_SYSTEM.md` says how under each profile). The library's configuration and its three hooks are
process-wide state, which the shell core's `DockHooks` alone sets ("The shell core"); the separator depends on the style
profile, which is chosen after the core is made, so `startApplication` gives it (`applyDockDecorationsProfile()`) once
the profile is in force and before the shell makes a dock area - the library takes it only while no dock widget exists.
A tab can carry an adornment after its name, an icon with a tooltip that runs what it is given when clicked and can
lock the tab's buttons (`an::ui::setTabAdornment`); the decorations draw it and the shell decides what it stands for.
`KddwProxyStyle` is installed on each dock main window, and under the Observatory profile on each tab strip. The decorations and the style carry `AFTERNIGHTCONTROLS_API`: a
window outside the library makes its dock areas from them, and the docking suite configures its process from the same
declaration and makes its own factory from `AN_ViewFactory` as such a window would.

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
  where it is; without it the library sets that window's geometry once for each main window in it. Three more things
  were met when a project came to keep one, and the shell core's `DockHooks` answers each ("Projects"). The library
  fits a layout to the size its main window has as the layout is applied, and a main window that was never on screen
  has none of its own: a layout squeezed into it loses where its splitters were, so one that is not on screen is
  first given the size its layout was saved at and laid out at it. A name the registry does not find is asked of one
  factory for the process, which may answer a dock widget of another name; that is how a layout's names reach dock
  widgets made again. And the library goes on naming the guest of a dock widget whose guest was deleted, writes that
  dock widget into a layout and shows its guest when it applies one: a dock widget that deletes itself when closed and
  has no child widget left is destroyed before a layout is taken or applied.
- **A dock widget's name is its own.** The library tells dock widgets apart by a name each keeps for good, and refuses
  to write a layout while two share one. A view's dock widget is therefore named by a token made for it
  (`an.view.` and a UUID), never by the view's title: a view is renamed, and another may then take the name it had.
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

**The tools as pages.** Each of the four states a full page (`kPageSurfaces`), so a shell that shows processes as pages
holds it whole in one, and in the classic shell - or made outside a launch - it is the window it has always been. None
has menus or key sequences of its own, so its page offers none. A page's primary action
(`triggerPrimaryAction`, Ctrl+Enter) is the tool's own run button, pressed when it is offered: Calibrate Images, Stack
Images, the Analyzer's Optimize For Fast Play, and for Image Registration Align once the registration is current and
Register until then. A run opens no progress window on a page (`processes/WorkflowToolRun.h`): the page shows it at
its head - the run's row says what the progress window says, the group being stacked or the frame being aligned,
through the run state's text - and the page's Cancel stops it (`cancelRun`); what a run says once it has ended, a
stack's warnings or a calibration's, is said at the page's head too. `PageLayout.*` in `Test_ProcessesWorkflowTools`
hold each tool to its page - whole in the page's area, no menus, the run button the primary action, scrolling only
where the centre is smaller than the least the tool takes - and `AppliesAsInAWindow` of each tool's suite holds that
the page writes what the window writes, sample for sample, with no progress window.

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

**The integration jobs** (`ui_models/IntegrationJobsModel.h`). A job is a session of a session database: the one Image
Integration keeps outside any project (`applicationSessionDatabasePath()`, in the application's local data) and the
open project's, in its folder's integration folder (`kProjectIntegrationFolder`). A job's facts are read where they already are rather than kept twice: its
frames from its session's document, and what its last run came to from the record every run leaves in its output
folder as it ends, whatever its end (`WorkflowRunStateStore::saveRunRecord`, at `runRecordPath()` beside the last manifest,
format `afternight-integration-run` version 1, read strictly). The record exists because the archive keeps only runs
that made a master: a run cancelled at registration leaves no history, and the jobs' list must still say "Cancelled at
Registration". `workflowRunRecord` states how a run ended - cancelled; failed, or partly done when it left a master all
the same; partly done when a group failed; with warnings; or completed - with the stage it stopped at by the stage's
name, the masters of the lights it left (made or still valid) and how long it took; the executor writes it before each
of its returns, into an output folder that is there. The model reads the databases and the records on the thread pool
(`readIntegrationJobs`) and changes its rows on the GUI thread, a later read dropping an earlier one; a database that is
not there gives no job and is not made by being read. What an open Image Integration runs is told to it
(`setRunningJobs`), so the running job is first, and reads what its run reports, without a read. A window opens on a
named session (`ImageIntegrationProcess::openSession`): what it showed is saved first, and a run or an import on its way
refuses it. `Test_ImageIntegration` holds the record (`IntegrationRunRecord.*`) and the model (`IntegrationJobsModel.*`).

**Image Integration as a page.** Its descriptor states a full page, so a shell that shows processes as pages holds it
whole in one, and in the classic shell - or made outside a launch - it is the window it has always been: nothing of
what follows runs in a window.

- **Its header says its job** (`pageLayoutMade`, `publishJobHeading`): the session's name, as the jobs' list names it;
  its facts (`ui_models/IntegrationJobFacts.h`) - its lights, their groups, the time the included ones integrate, what
  their files weigh, and the setup they say (readout mode, frame size, gain, offset, each where the frames say it, or
  how many setups they are of when they do not agree); the disk its runs write to, what is free there and what a run
  would write, with a bar; Undo and Redo, the Edit menu's; and Execute Integration, the Run menu's, as the primary act.
  The facts are read from the model on the GUI thread when a signal of the model says they moved, a burst gathered into
  one refresh. What the disk adds - the sizes of the lights' files, the room free in the output folder or, while it is
  not made, where it will be made - is read on the thread pool (`readIntegrationJobDisk`) when the lights or the folder
  change, when a run ends and when the page is shown again, and a read that lands after a later one was asked for is
  dropped: a slow volume holds no window. What a run would write is one estimate (`estimateIntegrationOutput`) that the
  header and the Summary tab's disk line both read, so the two cannot disagree. It is counted group by group, as a
  run writes: a stacked image each - a drizzled one as many times larger as its scale squared - with its rejection
  maps; the calibrated lights where they are kept on disk; and the registered lights of a group that keeps them or
  stacks a comet. A drizzled group places its lights itself and writes no map of their pixels, and a group that
  lets its registered lights go has them warped as its stack reads them, so neither writes a registered light:
  counted for the whole job, one drizzled group made every light weigh a map and every group keep its registered
  lights, several times what a run writes. A light whose layout the import did not record still counts as a colour
  mosaic, three channels once registered, which the disk line's tooltip says.
- **Its menus and its keys.** The six menus are the page's (`ProcessPageLayout::menus`), entry for entry; the Frames
  menu's acts are also the buttons its frame pages carry over their trees. Its key sequences are held by the page's
  shortcut scope, so `Ctrl+O` adds files with the focus in the page and opens an image with the focus elsewhere in the
  window. `triggerPrimaryAction` runs Execute Integration when the job is ready and no run is on its way; in the window
  Execute's sequence is the keypad's Enter alone, and on a page the scope adds the main Enter key to it.
- **An import is shown in the page.** The import's progress is a part of its own (`widgets/ImportProgressPanel.h`): the
  file on its way, the counts and the time left, the issues met, Pause, Resume and Cancel. A window shows it in a dialog
  modal to that window (`dialogs/ImportProgressDialog.h`), behind which the window waits; a page shows it in a band over
  its stages (`beginPageImport`) and makes its own acts wait instead - the stages, the foot and every entry of the
  menus are disabled, the header's buttons and the page's keys with them - so that an import cannot meet a removal, a
  reset or a run it was never written to meet, while the rest of the application stays usable. Esc in the page cancels
  the import and leaves the page; closing the page destroys the panel, which cancels it; nothing joins the job from a
  cancelled import.
- **Closing the page is closing the window**: the session is saved at once by its autosave, the settings are stored,
  and nothing is asked - the window asks nothing either.
- **In a project the job is the project's** ("A project for a job", under Projects). A window made while a project is
  open keeps its session in the project's own database (`projectSessionDatabasePath`, in the folder the project keeps
  its jobs in), and a job with no output folder yet is given one there, named as its session. A run that starts makes
  the job the project's (`joinProjectForRun`, at each of the five places a run is launched): the open project, or the
  one the application gives the job in its output folder. The session is moved into the project's database
  (`moveSessionInto`) - saved there, removed from the database it was in, so the hub lists the job once, with its
  project - under a name of its own when the project already holds another job of its name that writes elsewhere; and
  when the run ends its masters and stacked images are recorded as the project's images (`recordRunOutputs`). The
  output folder is not changed by any of it: a manifest prepared before the project was made is the one that runs.
- **A run is shown in the page.** What follows a run is one widget, `WorkflowRunView`
  (`widgets/WorkflowRunView.h`): the stages the run goes through as a list (`AnStageList`) whose stage on its way
  carries its progress, the group it is at and, under its line, the parts of what it does to that group as the run
  names them; what the run does now; a bar for the whole run; the file it is at; the log; the time it has taken and,
  once it can tell, the time it has left; Cancel; and Open Report and Close once it has ended. The launch
  (`WorkflowExecutionController`) asks the widget a run is launched from to show the view in itself
  (`IWorkflowRunHost`, `workflow/WorkflowRunObserver.h`). A window of its own answers no, and the launch opens the
  Processing window (`dialogs/WorkflowProgressDialogV2.h`), which holds the view and adds what a window adds: it stands
  over the application until the run has ended, closing it during a run asks the run to stop, and the run's end is said
  in a message over it. A page answers yes (`ImageIntegrationProcess::showWorkflowRun`): the view takes the place of
  the stage's page, beside the list (`StageListNavigator::showRun`), and no window opens, so the rest of the
  application stays usable and leaving the page leaves the run running - the view and the run's controller live with
  the process, not with what is on screen. While the run is on its way the page's own acts wait for it, as its window
  waits behind the Processing window (`holdPageForRun`): the stages' pages and the list's foot are disabled and so is
  every entry of the menus but the Navigate menu's and the help - a job is not edited under its run, whose completion
  reads the model. The stages can still be looked at: a row selected, or a Navigate entry, shows its page, and the
  header's primary act, which in place of Execute is "Show progress" with how far the run has come, shows the run
  again. When the run ends the acts are given back (`releasePageAfterRun`, from `refreshAfterRun`, which every launch's
  completion calls) and the view is shown with how the run ended, until its Close puts it away and the stage that was
  shown is shown again.
- **Where a run stands** is one figure for every front end (`workflow/WorkflowRunStanding.h`): the step the run is at
  among the stages its manifest names, the stage's name, and how far the whole has come - the stages passed and the
  share of the one on its way, each stage weighing the seconds the run is expected to spend in it
  (`workflowStageWeights`, from `WorkflowTimeEstimator::estimateStageSeconds`), so that a run that spends nine tenths
  of its time registering is not half done as it starts to. The view computes it, holds it from going back, and tells
  it; the launch makes it the run state of the process the run was launched from - "Step 5/9: Registration", 58 % -
  which is what a shell's rail, a task's header and the jobs' table say of the run without knowing what a workflow is.
  The time left is the time taken scaled by what is left, said only after ten seconds and a twentieth of the way.
- **Its stages are a list, in place of the window's tabs** (`widgets/StageNavigator.h`). The eight pages - the four
  frame sources, calibration, registration, stacking and the summary - are the same widgets whatever moves between
  them, and the process reaches them through one seam: a `StageNavigator` answers for a page, for the page shown and
  for showing another, and is told what the process has to say of each - a source's frames, what the validation finds
  in a page. In a window it is `TabStageNavigator`: the two levels of tabs, which say those in their labels and their
  badges. When a main window takes the process as a page (`presentStagesAsAList`, once, before the first show) the
  pages leave the tabs for a `StageListNavigator` (`widgets/StageListNavigator.h`) and the tabs go. A part of a page
  that must show another page - a frame shown in the lights' tree from the stacking page - asks the navigator above
  itself (`StageNavigator::of`), never a tab by its name: under the list there is no tab to find. A seam, rather than
  tabs with their bars hidden, because the two say a stage's state by different means, and the process should know
  neither.
- **The list's rows** are `kStageRows` (`ui_models/IntegrationStageStates.h`): a row for each of the eight pages, in
  the order a run reaches them, the summary last, and no page shown by two rows. Normalization and drizzle are stages
  of the workflow with no page of their own - their settings are the stacking page's, per group - and so no rows:
  two rows more that opened the stacking page read as two pages that were not there. The stacking row stands for
  the three stages, as the registration's stands for the registration and the alignment: it is marked by the stage
  that asks most, its line says the groups and their method - or the drizzle, where a group is drizzled - its tooltip
  says each of the three, and a run at any of them is at its row, which then names the step. The Navigate menu's
  actions and their keys select the row of their page. `Test_ImageIntegration/data/IntegrationPages.txt` records the
  pages the tabs reach, and `StageList.HoldsEveryTabPage` holds the list to them: the same pages, the same widgets,
  each by a row of its own.
- **A row's mark and line** are read from three places and guessed from none (`integrationStageRows`). The job as it
  stands, from the model on the GUI thread (`stageModelFacts`), with what the validation says of each page - the count
  and the words the tabs' badges show. What the output folder holds of its runs, read on the thread pool
  (`readStageRunFacts`): the manifest the job would run now, built from a snapshot of the model and adjusted as a run
  adjusts it, compared with the manifest the last successful run left (`stageStandings`, over `compareManifests`) - a
  stage the comparison says must run again is out of date, and with it every later stage that reads what it makes - and
  the record of the last run, whatever its end. And a run on its way: the launch tells the widget it was launched from
  each progress the run reports (`IWorkflowRunObserver`, `workflow/WorkflowRunObserver.h`). A row is, in this order:
  running; failed, for an error of its page's validation or the stage the last run failed at; out of date; a warning,
  for a lesser issue or the stage the last run was cancelled at; done; ready; not started. The calibration is a
  stage only where the calibrated lights are written (`LightsCalibration` in the manifest's sequence): a job that
  calibrates its lights in memory, as they are read, runs no calibration stage, so its row has no state to be in -
  it is not started, and says how many lights are calibrated and that it is in memory - where a mark of its own
  would say done of something no run makes. The lights have no stage
  of their own - they are what a run takes - so their row is done once a run that succeeded took them as they are,
  at the first stage that reads them: their calibration where it is a stage, or their registration. A
  later stage would leave the lights' row waiting above a calibration that is done, which reads as a contradiction;
  this way the rows that are done are the ones before the first that is not. The read compares two
  manifests and reads no image and no artifact: whether a file on disk is still the one a manifest names is the plan's
  to say when a run is prepared, with its stat of every artifact, and is not asked at every edit. One read is on the
  pool at a time, a burst of changes is gathered into one, a read that lands after a later one was asked for is
  dropped, and none is made during a run, which writes the folder it would read.
- **The list's foot** holds the job's output folder - the window's own label, field and button, moved - and the
  project file's acts, each a button for an act or a menu of the Project menu's own: import, export, the recent
  projects, the presets and Reset Settings. A path is long and the column 216 px, so the field has the foot's whole
  width, under a row of its label and the button that chooses the folder, and says the whole path when it is pointed
  at; beside the button it had about half of that. The window's foot, with its Close and Reset, is not shown on a
  page: the header closes the page and the list's foot resets.
- **The pages fit the centre of a main window.** A page with a side panel keeps it as an aside
  (`presentSidePanelAsAside`): the frame pages' - the preview over the grouping, the statistics, the issues and the
  quick actions, which stand in two rows there - and the calibration page's. The aside opens 336 px wide and a
  person drags the line at its left between 288 and 480 px (`kStageAsideWidth`, `kStageAsideMinWidth`,
  `kStageAsideMaxWidth`): the preview is as tall as its width lets it, and a fixed 288 px left it too small to judge
  a frame by, while a panel as wide as a window's would take the frames' room. The asides are one aside: a width
  dragged on one page is every page's (`asideWidthChosen`), and the one the next page opens with
  (`kPageAsideWidthSetting`, in the interface settings). The least stays 288 px, so the page's least size is what
  it was. The width a person gave a panel in a window is neither applied to the aside nor overwritten by it. The
  stacking page lays its execution options in two
  rows (`layExecutionOptionsInRows`), where the window's one row set the whole window's least width. A page arranged
  before it was ever shown is told to measure again: a layout that was never activated tells none above it of a
  change, and the page behind the one shown would otherwise keep its old least width until visited. With the list the
  page's least size is 1006 x 539 at the compact density, from 1437 x 637 with the tabs.

**Image Integration Lite as a page.** Its descriptor states a full page too, and it is its window's layout whole, with
no menus of its own. It answers the same seam as the full workflow (`IWorkflowRunHost::showWorkflowRun`): a run it
starts on a page is shown in the page, not in the Processing window - the run's view takes the process's place under
the page's head until the run has ended and its Close is pressed, which gives the page back to the process, whose own
banner then says how the run ended. The page's primary action is Run Integration, and the page's Cancel is the view's
(`cancelRun`). It holds no job and keeps no session, so its header names the process, and it joins no project.
`setRunPipeline` states the stages a run goes through in place of the workflow's own, for the suites: the manifest is
still the one the process prepares from its model. `PageLayout.ImageIntegrationLiteFillsItsPage` and `LitePage.*` in
`Test_ImageIntegration` hold it.

`IntegrationPage.*` in `Test_ImageIntegration` hold the process as a page - its menus against the window's bar, every
sequence the record of the process windows holds for it, Execute, closing and the job it says - and in
`Test_ShellObservatory` the page in the Observatory window; `StageList.*` hold the list - the pages it reaches, the
rows' states for a job of each standing, the Navigate menu and the keyboard, the foot, the aside - and
`IntegrationStageStates.*` the rows and the standings by themselves; `IntegrationJobFacts.*` and
`ImportProgressPanelTest.*` hold the facts and the panel.

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

### The shell core (`src/AfterNightShellCore/`: `core/IShell.h`, `core/ShellRegistry.h`, `core/ShellSelection.h`, `core/ApplicationCore.h`, `core/ApplicationContext.h`, `core/ApplicationEvents.h`, `core/ApplicationSession.h`, `services/`, `views/`, `actions/`, `commands/`, `readout/`, `projects/`)

`AfterNightShellCore` (`an::ui::shell`, registry class `engine`) is the application without a window: what every
shell needs and none owns. A *shell* is a main window and the few answers only a window can give; the stores, the
models, the actions and the services it shows belong here, so that a shell stays thin and one can be replaced, or
another added beside it, without the application's behaviour moving with it. The library sits above Processes and
Controls and below the application: `check_consumer_layering.py` refuses an include of it from Controls or Processes
and an include of the application or the full host from it, `check_build_graph.py` holds the generated projects to
the same order and refuses it on an extension host's link line - a host shows a process's window in a process of its
own and has no main window to put a shell in - and `check_include_layers.py` keeps the engine from naming it. It
ships with the product (`ship product`); the application links it.

`core/IShell.h` is the contract a shell implements: `IShell` (build the window, show it at its saved geometry, save
the geometry, the Session Console's visibility, a theme change), `IViewHost` (where a new view goes, by `ViewOrigin`:
a view the user asked for takes the user to it, one a process made leaves the place on screen to the shell),
`IStatusDisplay`, and `ShellDescriptor`, what the application knows of a shell before it makes one - its id, its
display name, the style profile it is drawn in (`StyleProfile`, `theme/StyleProfile.h`: put in force before the first
widget exists, as `THEME_SYSTEM.md` says under "Profiles") and the function that makes it. The contract names the views, the dock library's main window, the
process presenter and the projects a job reaches (`IShell::jobProjects`, null in a shell that offers none) by
declaration only, so a unit that includes it reaches no widget header through it.

`ShellRegistry` holds the descriptors of the shells one run can make, in the order they were registered; the
application owns one as a local object, never a global. An id is a lowercase word (`classic`, `observatory`),
matched as it is written, because it is typed on a command line and stored in a setting; a second descriptor under
an id, an id of another shape and a descriptor that cannot make its shell are refused. A descriptor the registry
hands out stays where it is while more are registered.

`ShellSelection::resolve` chooses the one shell a run makes: the first of `--shell=<id>`, the environment variable
`AFTERNIGHT_SHELL`, the setting `interface/shell` (default `classic`) and the default that names a registered shell.
A source that names no registered shell, and an option with no value, is passed over with a warning in the log and
in the returned `ShellChoice`: a mistyped option never stops the application from starting. The value is joined to
the option (`--shell=observatory`), so the argument after a bare `--shell` - a file to open, perhaps - is never taken
for a shell. The arguments are the ones `main` received, handed in by the caller: on Windows the application gives
`QApplication` an argument list of its own, so `QCoreApplication::arguments()` is not what the user typed and is never
read. The setting is read from the global settings store, so the stores must be open before a shell is chosen, and a
change to it takes effect at the next start.

`ApplicationCore` (`core/ApplicationCore.h`) is what the application holds that is not a window. It owns the
persistence stores - the settings database, its executor and the stores kept in it - and opens them in `openStores()`,
which makes no widget, so that a shell can be chosen from the setting before the style it asks for is set; it reads
what lives there as they open: the folder last opened, the RAW policy (`rawOpenPolicy`, `buildOutputIntent`), the recent
files; and it makes the run's temporary and thumbnail folders (`runtimeStorage()`). It holds the application's
listeners (`services/AppEventBus.h`), told in the order they subscribed - a listener that goes while another is told,
as a window closed by what it was told does, is not told after it went - the process windows that are open
(`services/OpenProcessRegistry.h`, used from any thread), and the lists the menus are built from, each with its storage
and its limit: `RecentFilesModel` (the file `recent_files` in the data folder, `general/recentFilesLimit`),
`RecentProcessesModel` (`interface/recentProcessIds`, `general/recentProcessesLimit`) and `FavoriteProcessesModel`
(`interface/favoriteProcessIds`). The file workflows still edit the recent files through the list itself, as they did
when the window held it, and save it through the model. `main` makes the core on the GUI thread before the window and
hands it to the window, which wires its coordinators to what the core holds; the window's members of the same names
forward to it, and the core outlives the window.

The core also holds the services the application context answers with, and each answers what only a window can
through the shell it is given (`ApplicationCore::setShell`). `services/DialogService.h` shows the messages, each modal
over the window of the widget that asked for it or, when none did, over the shell's window (`dialogParentFor`), and
opens the settings where the shell says. `services/FileDialogService.h` opens the file dialogs over the same window,
drawn by the system or by Qt as `interface/useSystemFileBrowser` says, in the folder their context remembers or the
one opened last, and remembers under the context the folder the person chose from. `views/ViewService.h` holds the
views' list and makes views through `ViewCreationCoordinator`, and tells the shell of each change through `IViewHost` -
a view added, placed beside the neighbour it was made from, activated, removed (after it left the list), renamed (with
the name it had), maximized and restored, and home once the last one goes - and shows the active view's state through
`IStatusDisplay`. A view made through it as the context's `IViewFactory` is a process's (`ViewOrigin::Process`): the
shell keeps its place on screen. The file workflows are handed `userViews()`, a factory whose views are the person's
(`ViewOrigin::User`), and the shell is asked to show its views right after it hears of one. `subjectView()` is the
view the image actions act on and `lookingView()` the one the looking actions act on: the active view, unless a shell
showing a task names the task's target and canvas (`setTaskSubject`), until it clears them or one of them goes. A task
with no single target names none: the image actions then act on no view and the looking ones on its canvas alone.
`services/ProcessLaunchService.h` launches the registry's processes - one that acts on an image acts on the subject
view - prepares an extension's environment before it launches the extension's process, and lists the extensions once
each, by name (`extensionFamilies`). `services/ExtensionService.h` is the installed extensions: it registers the
packages' processes - the ones the start-up catalogue names at once, every one once a worker has found them - binds each
to the extension host that runs it, puts them in the process menus, prepares their environments and removes the ones no
package uses on the worker, and finds the packages again after an install or a removal in the folders it watches. It
owns the extension host's launch coordinator, tells it of the views and the theme, and gives the SDK the sessions talk
to what it reads of the application. What a service needs that is neither the core's nor the shell's to give - the
image selector, the settings dialogs, an extension's questions and settings, the confirmations of the process lists -
it is handed as its `Hooks` by the window, which sets them as it is made and clears them, with the shell, as it goes.

The actions belong to the core too (`actions/`). `ActionCatalogue` makes the application's 186 actions once, from one
table (`ActionSpec`: the text, the icon's key, the shortcut and its context, whether it can be checked, its group - the
menu of the menu bar it belongs to), each under the object name the classic window always gave it, which
`actions/ActionIds.h` spells as a constant; a unit of Controls is handed them as an `an::ui::ActionLookup`
(`app/ActionLookup.h`) and never names the catalogue. The actions are the core's children: a shell's window hosts them
while it lives (`host`) - they are then its children in Qt's object tree, as a window's own actions are, which is what
a test, an accessibility client or `findChild` reads - and gives them back as it goes (`release`), so they live from
the core's start to its end. `ActionBindings` connects them to what they do, once, to targets the shell gives and can
change: the file workflows, the image operations and the mask overlay by their members, the dialogs, the layout, and a
process's action to the registry descriptor it launches; binding a process's action names its descriptor by the
action's text - the name the menus give a process - and an action whose descriptor is missing is withdrawn and says
it is still loading. `ActionStateCoordinator` sets which actions are offered and checked from the image the image
actions act on and the view the looking actions act on, and tells a menu its state by its id, which the shell's menu
of that id follows. `ProcessMenuModel` holds what the process menus list beyond the catalogue - the favourite and the
recent processes, and each extension's processes, bound to their descriptors, in the menu of their category, and
each extension once in the Extensions menu, its family's menu listing the extension's processes first where a shell asks
for them (`FamilyEntries::ProcessesAndLinks`: the Observatory shell's menu tree; the classic window's holds the links
alone) - and says what changed; a shell makes its menus from it on demand. Each action of an extension's process carries
the quiet tag "extension" (`process_registry::extensionProcessTag`) in `parts::kMenuTagProperty`, which the Observatory
profile paints after the entry's text, the entry widened for it; a catalogue action an extension bound loses it when the
extension goes.
`MenuTree` declares the menu bar as data, entry for entry the classic window's, and builds a menu bar or one menu of it
over the catalogue: a shell that builds its own menus builds them from it. `ViewMenuBuilder` builds the menu a view
shows on a right click, entry for entry the classic window's - the menus it shares with the menu bar, which the shell
names by their ids, Current Window and Show In File Browser - and the menu of a view's tab, which makes its view the
active one so the catalogue's file actions act on it.

Every command a person can name is in one index the command palette searches (`commands/CommandIndex.h`, held by the
core as `ApplicationCore::commands()`). The index is pure - it includes no widget header and makes no window
(`CommandIndex.IncludesNoWidgetHeader` reads what it includes) - so it is searched and ranked in the suites with no
widget, and a keystroke over a thousand commands is asserted under 16 ms in Release: it is a search of a list held in
memory, the commands collected once as the palette opens (`refresh`), each title and keyword folded then. Its
providers (`commands/CommandProviders.h`) read the application only through the functions they are handed, which the
core binds to its services: `ActionCommands`, the catalogue's actions a menu holds, subtitled by the menus a person
reads down to them (`MenuTree::pathsOf`) - an action that launches a process is left to the process, so a process is
listed once, and the five actions of the 1.1 window no menu or binding reaches are no command; `ProcessCommands`,
every descriptor of the registry, dimmed with what stands in its way - the registry's reason, or what the launch guards
say of the image the image actions act on - but never for having no image at all: such a process runs, and its launch
says there is no image, as the menu's does, because a palette that dims every process with no image open would hide
the guards' own message; `ImageCommands`, the open views and the recent files; and `SettingsCommands`, the settings
pages and their rows. An extension's process is listed under its family - its extension's name, which
`ProcessMenuModel::familyOf` answers - and found by it, with the quiet tag "extension", which the palette draws after the
title, its tooltip saying the process runs in its own process (`CommandRow::tag`, `AnCommandPalette::tagRect`). The rows come from `controls/settings/SettingsSearchTable.h`, a table in Controls of each page's
title, search words and rows (each labelled field and check box, with the group it sits in) read without making a
page; `CommandIndex.TheSettingsTableIsWhatThePagesShow` makes the pages and holds the table to them, so a page that
gains, loses or renames a row fails there until the table says so, and a label a page shows in two groups is two rows,
told apart by their group. A query's first character narrows it (`>` processes, `@` images, `:` settings); a text ranks
an exact prefix of the title over a word prefix (of the title's words in order, or of a keyword) over a subsequence,
then the commands run most recently first; the groups keep one order (processes, images, views, settings), six rows
each until the palette's header is clicked. The palette (`components/AnCommandPalette.h`, in Controls) shows what an
`ICommandSource` answers and includes nothing of the shell core: the index is one such source. The commands run last,
twenty of them, are stored in `interface/paletteRecentCommands` as they change and read again as the stores open; the
favourites are the favourite processes the menus list, so a star in the palette and Manage Favorites edit one list.

The readout - the column of the active view's figures, and the compact readout a task pins above its process - is fed
by `readout/ReadoutController`, which a shell makes over the core and hands the widgets it shows
(`components/AnReadoutColumn.h`, `components/AnCompactReadout.h`, in Controls, which include nothing of the shell core:
they show what they are handed and tell what the person asks). It follows the active view as the core's listeners are
told of it, docked or floating, or, while a shell shows a task, the task's preview through an `IReadoutFrameSource`.
More widgets on screen must never cost the GUI thread an engine call, so a change - another view, the image updated,
another resolution, a settled frame - only schedules an update, a burst within 150 ms makes one, and an update measures
only what changed: the channel the view shows picks the curves drawn, and the view's name, its solution and its facts
are read where they are kept, with nothing measured. A measurement is one operation on the thread pool: the image's
read lease first (an edit holds the writer while it waits to be admitted), then an admission as `Preview` through
`tryAdmit`, so the readout never queues in front of a run and a refusal costs nothing but dimmed figures until a try is
admitted; then the histogram's counts and the statistics window's figures in the same operation - so one ticket, one
admission and one lease answer for both, which is why the readout counts with `computeHistogramDisplayData` rather
than through a `HistogramCountScheduler`, whose counts wait in the admission queue. A refusal is tried again by itself -
after 150 ms, then twice as long each time up to 600 ms (`kRetryLongest`) - and not left for the next change: an image
just opened is refused while the view's own work runs and nothing changes after it, so the readout stayed empty until
the person did something. The clipped samples are
left out as the statistics window leaves them out when it opens, so the six figures the card shows are the window's
for the same image and range. What lands is shown only when its `CommitTicket` still commits: a view closed or left
while it is measured shows nothing of it. In a task only a settled frame is measured, because only an interactive
frame's pixels are approximated, never a statistic: an interactive frame dims the figures and keeps them, and the
original's figures are measured once, beside the first settled frame, for the change each cell shows against them. The
column's folded cards and its being collapsed are kept in the global settings store (`interface/readoutCards`, an
empty line once every card was opened again - the store keeps a null string as no value; `interface/readoutCollapsed`);
until a line is kept, the Image card starts folded in a window under 820 px high. The chips of the astrometry card are
bound to the view's own overlay actions, which the view names (`astrometryGridAction` ... `astrometrySipMapAction`,
`astrometryOverlayAction`, `astrometrySettingsMenu`), so a chip and the view's menu and shortcut are one switch.

A view whose toolbar is outside it is shown by `views/ViewChromeBinder`: one floating toolbar and one history strip
(`components/AnFloatingToolbar.h`, `components/AnHistoryStrip.h`, in Controls, which read nothing of the view but what
they are handed) bound to a view, the toolbar showing its actions, choices, zoom and name, the strip its history's steps
and its Undo, Redo and Image History; a chip clicked moves the image there through the history, and More opens the view's
menu as the host fills it (`setMenuFiller`, a `ViewMenuBuilder`'s), as a popup that never waits. Told another view, the
binder shows that one and hears nothing more of the first - it is the view's listener and its history's, and leaves
both - and a view that goes is shown no more, its history's listener taken away while the history is still there (the
view tells its listeners before its history goes). The toolbar and the strip may show two views, a task page's canvas
and its target; a host makes the pair and its binder, so two binders never share a toolbar.

The arrangement of the views is the core's too (`views/`). The dock library keeps its configuration and three hooks -
a question before a drag starts, one before a drop indicator shows, a call when a drag ends - for the whole process,
each hook a plain function that captures nothing. `DockHooks` is the one unit that sets them: the core configures the
library as it is made (`DockHooks::configure`, once for the process, from the decorations' declaration: the library
takes its flags and its minimum only before the first dock widget or main window exists, so no shell has made one yet),
and the dispatcher installed as the three hooks answers for any number of subscribers - a drag or an indicator allowed
when every subscriber allows it, the end of a drag told to each with the main window and the dock widget the drag that
ended named, which the library does not say. A saved layout is the process's as well - one blob for every main window
and floating window, and one factory the library asks for a name it does not find - so `DockHooks` is also where a
layout is taken and applied (`saveLayout`, `restoreLayout` with a resolver of names; "The dock area" says what the
library does there that had to be answered). `DockLayoutOperations` acts on a dock area the shell names (`DockTarget`)
or on the window a view is in: it gathers the views into one tab group, splits them into a grid of equal shares, fits
them to their windows, and locks the arrangement. A workspace's target (one that names a workspace) takes the views
docked in its dock area alone (`viewsDockedIn`: a view in a floating window is in none); a shell's only dock area (a
target with no workspace) takes every listed view, as 1.1's Window menu did. A lock is a subscriber: it refuses a drag
out of the locked main window and an indicator into it, and nothing of another, and marks the dock area locked
(`setDockAreaLocked`, Controls' `controls/KDDockWidgetsHelper.h`); the decorations ask `layoutChangesLockedAt` of the tab, the
title bar or the strip a double click lands on - the whole layout locked, or the dock area it is in - so a double click
floats nothing in a locked workspace and floats in a free one. The lock of a shell's only dock area is the whole
layout's, as the classic window's lock always was: a floating window's drag refused too, and the indicators inhibited.
The lock offers Unlock Layout in place of Lock Layout, and `offerFor` offers what another dock area is when it comes on
screen. Fit Images To Window takes the shell's target when the shell gives one (`ActionBindings::Targets::fitAll`);
the classic shell gives none and fits every image. `views/WorkspaceModel` is a shell's workspaces with no widget: their
names ("Workspace N", the lowest number free) and order, the current one, each one's lock and whether it is in a window
of its own; the only one never closes, and the observers hear every change. `services/WindowGeometryStore` keeps where the main window was in the interface
settings and shows it there - when the person asked for it, at the saved place if that place is on a screen there is
now, never smaller than the window's minimum, and maximized otherwise - and `services/FileDropHandler` takes a drag
carrying an image or a sequence and hands every local file a drop carries to the opener the shell names.

The open processes are a shell's tasks (`tasks/TaskModel`), with no widget. `OpenProcessRegistry` tells its observers
of each window it holds and forgets, on the GUI thread and outside its lock, and the model makes a task of each - in the
order they were opened, the windows open when it is made among them - named by the descriptor the window states it was
launched as (its name, its category, a glyph from the descriptor's icon, else its action's, else a generic one), and
takes it away when the window goes. What a task's run is and which image it targets the shell's presenter tells it, as
the window tells the presenter (`setRunState`, `setTarget`) - a task of a process launched on no image has no single
target (`Task::singleTarget`), whatever image the process reads, and takes none; `tasksOn` and `runningTaskOn` answer what a view shows of
them. The current task is the one the person looks at; a run that succeeded or failed while its task was not current
leaves an unseen result, which making the task current clears, the ended run with it: so a shell can mark a result the
person has not seen without the model knowing what showing is. The model is a shell's, made over the core's registry
by the presenter that feeds it.

The sessions of the extension host are tasks too, External ones (`TaskPresentation::External`): the window a session
shows is its host's, in another process, and is never embedded in the main window - a child window of another process
shares its input queue with its parent on Windows, so a hung extension could freeze the application, and a Wayland
client cannot embed another's surface at all. The launch coordinator is the model's second feed
(`app/ExtensionSessionFeed.h`, `IExtensionSessionFeed`, which `ApplicationCore::extensionSessions()` answers and a
suite replaces with a stand-in): it tells where each session stands after every launch and pump - its host starting,
ready or not responding, the run of its window and whether its host raises that window - and once how it ended. A
standing is read from the session's controller (`standingOf`), and a report (`ExtensionSessionReport`) tells only what
changed. The run is the host's word, `session.activity`, or the application's own execute in flight; a host's run holds
no lock on its image, so an External task never blocks a view (`runningTaskOn` leaves it out). A session that ends as it
should - its window closed, the application stopped it - takes its task away; one whose host crashed, stopped answering
or failed to start (`endOf`) leaves its task failed with why (`Task::hostFailed`) until the person closes it, so a dead
extension is never silently gone. Show asks the host to raise its window (`raiseExternal`) and Close ends the session or
dismisses the failed task (`closeExternal`). `views/ViewThumbnailService` makes the small picture of a view a task's item shows: the
whole image as the view presents it - its stretch and its channel, what the view states when it is on screen and what
it stated last when it is not - reduced for display and covering a 60-pixel square, in one operation on the thread pool
admitted as `Preview` through `tryAdmit`, through the analysis the view keeps of the image or one measured when it keeps
none, published only while its commit ticket commits; made again when the image is loaded or updated, asked for again a
little later when the session refused it, and dropped with its view. Night is not applied there: the part that shows a
thumbnail shows it in Night's form. A view going tells its listeners from a copy of their list, so the service and the
presenter retire a follower they let go during that telling rather than delete it under the view's loop.

`ApplicationContext` (`core/ApplicationContext.h`) is the application's side of `IAppContext`, made over the core and
a shell, and naming no window: the settings, the process settings, the temporary files, the folders remembered, the
thumbnails, the output intents, the RAW policy and the session (`ApplicationCore::setEngineSessionProvider`) are the
core's; the messages and the file dialogs are its `DialogService` and `FileDialogService`; the listeners, the recent
files and the open process windows its `AppEventBus`, `RecentFilesModel` and `OpenProcessRegistry`; the view factory,
registry and manager its `ViewService`; the status line is the shell's `IStatusDisplay`, and the default parent, the
process presenter (`IAppContext::getProcessPresenter`, null where every process is a window) and the projects a job
reaches (`IAppContext::getJobProjects`, null in a shell that offers none) are the shell's; the
state of the actions is the core's `ActionStateCoordinator`. What is still a window's - the theme it applies, the
recent and dropped files it opens - reaches it through the `Dependencies` the context is handed.

The core is also the receiver of the work a thread hands the GUI thread (`core/ApplicationEvents.h`): each of its
`...Async` calls posts one event to the core and returns, never waiting, and the event runs once on the GUI thread
against the core's `IApplicationEventTarget` - `ServiceEventTarget`, which answers through the view and dialog
services and the core's action state, and hands the window what is still its own (the recent files' thumbnails and
menus). An
event posted through the context's event bus by any library runs there too. The core being the receiver is what keeps
an event from running after it: Qt lets go of a receiver's pending events when it goes. The window discards the events
still pending and takes its target away as it goes, so nothing handed to it runs once it is gone; an event that throws
is reported to the target and logged, and the next one runs.

`Test_ShellCore` compiles its cases and links the library, with no window: `ShellRegistry.*` and `ShellSelection.*`;
`ApplicationCore.*` (the stores under the case's storage root, before any widget exists), `ApplicationEvents.*`
(posted from a worker, run once on the GUI thread; none after the core or its target is gone), `AppEventBus.*`,
`OpenProcessRegistry.*` and the three lists' cases; and the services' cases over a shell that records what it is asked
(`Test_ShellCore/ShellCoreViewSupport.h`): `ApplicationContext.*` (who answers each service, the shell's window, a view
placed and maximized, the status line, home), `DialogService.*` and `FileDialogService.*` (the window a dialog belongs to, the folders,
the system's or Qt's browser), `ViewService.*` (real views of a session the case opens: names, the active view, the
order a removal is told in, a worker's request, who asked for a view, the subject and the looking view) and
`ProcessLaunchService.*`; `CommandIndex.*` (the ranking, the prefixes, the groups, the recent and favourite commands
and the providers over fixed lists, the settings table against the pages, the keystroke's time) and
`CommandIndexInTheApplication.*` (every recorded action and process found by its text in a core, the commands run
last read again by the next one); `ReadoutController.*` (real views of a session: the view followed, one measurement a
burst, a refusal while every thread is held, a view closed while it is measured, no engine call on the GUI thread, a
task's settled and interactive frames, the folds kept), `StatisticsCard.FiguresEqualTheStatisticsWindowsForTheSameImageAndRange`,
`AstrometryCard.EachChipTogglesItsOverlayOnTheViewAndFollowsItsShortcut`, `ReadoutSummaries.*` and `Timing.ReadoutUpdate`
(the GUI thread's time in an update, measured when `AFTERNIGHT_TEST_TIMINGS=1` asks); `ViewChromeBinder.*` (real views
of a session: a view's actions and history shown, another view bound, two views at once, two binders, a view that
goes, More's menu); `TaskModel.*` (process windows over real views, made inside launches of the case's descriptors: the
names and the glyph, two tasks of one process, the running task on a view, the unseen result, a window gone, the
observers in order) and `ViewThumbnailService.*` (the square through the view's stretch, a refusal while every thread is
held and its retry, a new image, a view gone). The actions' cases are held to the records of the classic window beside `Test_ShellClassic`
(`Test_ShellCore/ShellCoreRecords.h`): `ActionCatalogue.HoldsTheRecordedInventory` and `MenuTree.EqualsTheRecordedMenuBar`
read the record of the actions and the menu bar, `ActionBindings.EveryProcessActionLaunchesTheRecordedDescriptor` the
record of the processes; `ActionState.*` is the coordinator's table, a state of the application a row; and
`ProcessMenuModel.*` the categories, the lists and an extension's processes and family. A suite below the shell core that
builds the classic window's menus builds them over the actions the record holds (`fixtures/RecordedActions.h`).
`DockHooksTest.*` asks the library's own hooks about real main windows and dock widgets; `DockLayoutOperationsTest.*`
groups, splits, fits and locks real views in a main window the case makes; `ViewMenuBuilderTest.EqualsTheRecordedTree`
reads the record of the view's menu; `WindowGeometryStoreTest.*` and `FileDropHandlerTest.*` need no window.

### The shells

The application holds two shells, each a library over the shell core: `AfterNightShellClassic`, the window of
AfterNight 1.1, and `AfterNightShellObservatory`, the window of places and tasks. One run makes one of them, chosen at
the start (`--shell=<id>`, `AFTERNIGHT_SHELL`, the setting `interface/shell`, the default `classic`), and each names the
style profile it is drawn in. A shell is a window and what only a window answers; everything else is the core's, made
and started before the shell is: `ApplicationCore::initialize` makes and installs the application context, opens the
session from the limits the settings store states, puts the interface's settings in force so the first widget is made
under them, sweeps the temporary folders and readies the extensions' SDK, and `ApplicationCore::start` finds the
installed extensions, whose processes join the process menus' data. The context is the core's (`ApplicationCore::
context`): made over the core alone, it answers what is the shell's through the shell the core holds, and a shell's
window hands it the little it still answers itself (`ApplicationContext::setDependencies`) and takes it out of the
global as it goes, so the windows destroyed after it find none. A window is therefore made over an application that is
ready - its session open, its settings in force, its extensions known - and holds only its own parts. No shell includes
the other, and nothing below a shell includes one (`SourceGuards.ShellsDoNotReachEachOther`, `check_consumer_layering.py`,
`check_build_graph.py`).

### The classic shell (`src/AfterNightShellClassic/`: `ClassicShell.h`, `AfterNightApp.h`)

`AfterNightShellClassic` is the main window of AfterNight 1.1, kept whole in a library of its own. `AfterNightApp` (a
`QMainWindow`) implements `IViewHost` and `IStatusDisplay` for the core's services: it docks a new view beside its
neighbour or on the right of its dock area, lists the views in its image selector (`ViewControlCoordinator`, which
keeps the selector and the docks' order and no longer the list), shows its dashboard as home and its dock area as the
place of views, and fills its status bar. It composes its own coordinators - `ClassicUiBootstrap`, which builds its
three pages (the dashboard, the dock area, the maximized view) into its central widget; `ClassicWindowLayout`, its dock
tool bar, the switch between dashboard and dock area and the maximized view; `MenuActionManager`, its process, recent
and favourite menus; `AfterNightAppUiBuilder.h`, its widgets - and Controls' (`DialogOrchestrationCoordinator`,
`PersistenceCoordinator`, `AppLifecycleCoordinator`, …), each handed what it needs of the shell core through its
`Dependencies`, and hands the core's `ApplicationContext` what it still answers - the theme, the recent and the
dropped files. Its lock, group, split and
fit, the view's menu, its saved place and the files dropped on it are the core's (`DockLayoutOperations`,
`ViewMenuBuilder`, `WindowGeometryStore`, `FileDropHandler`) over its one dock area. It hosts the core's actions while
it lives, its builder making its menus and its tool bar of them, gives the action bindings their targets, and makes its
process menus from the core's process menu model whenever it changes.

The application's session is opened by the core before the window is made (`ApplicationCore::initialize`), once the
settings store has been read, from the processing limits stored there (`applicationSessionSettings`:
`sessionSettingsForLimits` with the product's codecs), before anything reads an image; the window is refused over a
core that is not initialized and started. The core lends that session (`IEngineSessionProvider`) once it is open, and
the persistence coordinator's RAW policy (`IRawOpenPolicyService`) and output intents
(`IOutputIntentProvider`). The view factory and the events that carry an image move an `ImageDocument`; an event whose
document no view takes closes it, and no entry point makes a worker wait for the GUI thread. The extension seam's hooks,
which the core's extension service sets, land a result as an edit the history records, a preview through
`replaceImage`, generated images and output masks as new documents, and hand an execute an output mask of the target's
shape with every sample zero; a view snapshot is exported under a read lease taken without waiting; a workflow's
sequence output is opened on a worker.

`ClassicShell` (`ClassicShell.h`) is that window behind the shell contract: made over the core, it makes and readies the
window when it is built, with the session the core was given (`ApplicationCore::setApplicationSession`), forwards the
contract's calls to it, and destroys it - its views, history and extension sessions with it - when it goes.
`classicShellDescriptor()` is how the application registers it: `classic`, "Classic". The library links the shell core
and what is below it; no shell includes another, and neither the shell core nor Controls nor Processes includes a shell
(`SourceGuards.ShellsDoNotReachEachOther`, `check_consumer_layering.py`, `check_build_graph.py`). Its code is 1.1's,
moved and renamed, not rewritten, so it carries no `.clang-tidy` of its own. `Test_ShellClassic` links it and runs the
real window in one session the suite owns: `ApplicationMenu` holds every menu behaviour, `ProductSmoke` every format
read, a save read back and a process of every framework family, and `Test_ShellClassic/ApplicationDriving.h`'s
`RealWindowCase` drives the window as a person does; the records of the 1.1 interface are beside it
(`Test_ShellClassic/data/`). What a case of any shell's window shares - running the event loop until something holds,
answering the dialogs a menu action opens - is `fixtures/WindowDriving.h`.

### The Observatory shell (`src/AfterNightShellObservatory/`: `ObservatoryShell.h`, `ObservatoryWindow.h`)

`AfterNightShellObservatory` (`an::ui::observatory`) is the window of places and tasks, drawn in the Observatory style
profile. `ObservatoryWindow` (a `QMainWindow`, at least 1024 x 640) is made of regions sized from the density's
metrics: the top bar across the window (`chrome/TopBar`, `MetricTokens::barTop`), the rail under it at the left
(`AnIconRail`, `MetricTokens::rail`), the centre - a stack of one page per place, each kept alive while another is
shown, so changing place computes nothing - the column right of the centre, and the status line along the bottom right
of the rail (`chrome/StatusLine`, `MetricTokens::statusLine`). Home is `pages/HomePage`, a page that scrolls, made of
Controls' Home parts (below); the workspaces' place is a stack of their pages (`WorkspaceHost::stack`), each a `pages/WorkspacePage`
over the dock area `workspaces/WorkspaceHost` makes for it. A dock area is a main window of the dock library, named
`an.workspace.<id>`, made with the shell's window as its parent and handed to its page (made under the page it would
stay a window of its own), drawn by the decorations' factory and a `KddwProxyStyle` of its own, and joined to the dock
library's hooks through `DockHooks`; its target names its workspace, so its lock is its own. Before any workspace the
host makes the two anchors, main windows inside the shell's window that are never shown - made plain child widgets, as a
library main window is a window of its own until a layout takes it: `an.anchor.images`, with no affinity, which the
library parents a floating image to, and `an.anchor.tasks`, with `an.task` and `an.console`, which the console's window
is parented to. A view is docked beside its neighbour, in the neighbour's workspace, or on the right of the workspace on
screen; maximized, it covers its workspace's page while its dock keeps a stand-in, the layout locked while it is unless
the person had locked it already. With no image open the window shows Home, and the workspace once one is. A process
opens as a task of the window, a page of its centre ("Every open process is a task", below).

Home's parts are Controls', and hold no shell: `controls/HomeQuickActions` (the dashboard's quick actions as a row, Open
file the accent's and each with its shortcut, the others in More, and the customization in More and on the row's
right-click), `controls/HomeContinueRow` (the dashboard's `FeaturedFileCard` and `FeaturedFilePresenter` over the recent
images - a sequence is never the featured image - and the last sequence's card with its Resume),
`controls/HomeRecentGrid` (a tile per recent file, as a grid or a list, with its format's tag, a sequence's with a film
glyph and its frame count, and the tile's menu, the dashboard thumbnail's five entries) and `controls/HomeWelcome` (with
no recent file: the logo and the version, three cards to start with and under them one line that says projects are
optional, with New project..., the drop zone with Open file, and the first-run panel's six things AfterNight does,
`dashboardFeatures()`). The page asks for what they ask in the dashboard's signals, which the window answers as it
answered the dashboard. New project is not one of the dashboard's actions: the classic dashboard shows those too, and
has no projects. The page adds it to the row itself (`HomeQuickActions::addRowAction`), after the dashboard's actions
and before More, where the dashboard's settings neither move, hide nor reset it. Home and the classic dashboard read and write the same settings,
through `controls/DashboardSettings.h`: the quick actions' order and hidden ones (`DashboardQuickActionLayout`, whose
customization menu is the dashboard's), the pinned recent files and the pinned featured file, the sort and the search -
each loader bringing the settings to their format first, so that no part reads settings of another format whichever
part reads first - and the recent files are ordered by one function, `DashboardUtils::visibleRecentFileOrder`, which the
dashboard's `RecentFilesGrid` uses too: what a person arranged in one shell is what the other shows. A tile's picture
and facts are read off the GUI thread (`readHomeRecentFile`): the thumbnail decoded on the thread pool, and the facts by
`probeHomeRecentFile` - an image's description, or a sequence's container and frame count (`probeDashboardSequence`) -
as background work a busy session refuses. Home reads the recent files when the window says they changed - where the
file manager told the dashboard - never because it is shown, so changing place asks the engine for nothing. A tile, and
the sequence's card, read again when their thumbnail changes - a file is a recent file before its thumbnail is written -
and when a busy session left the facts of a file on disk unread. The grid or list choice is one of the dashboard's
settings (`interface/dashboardRecentArrangement`), so the next Home shows the recent files as the last one did.

The rail holds the places - Home, the workspace (shown only while an image is open: with none, Home is the place) and
Integration, which opens Image Integration - and at its foot the Session Console, which it opens and closes, and
Settings; the place on screen is the one marked. The column right of the centre is the readout (`AnReadoutColumn`) fed
by the shell core's `ReadoutController`, which follows the active view: it is shown beside the workspace only - Home
is no place to read an image, so the column is hidden there even while one is open - and is told the window's size at
every resize so it narrows and folds as the window does.
The status line (`chrome/StatusLine`) writes what the classic status bar writes of the active image - its facts, its
zoom, the position under the cursor - from the same functions (`widgets/ImageStatusTexts.h`, which the classic
`ImageInfoStatusBarWidget` writes through too), "RA -" and "Dec -" in the tertiary colour while there is no position. The
menu bar (`chrome/MenuBarOnAlt`) is the shell core's menu tree over the catalogue's actions, hidden unless the person
keeps it (`interface/alwaysShowMenuBar`): Alt alone shows it and gives it the focus, Alt and a menu's first letter
opens that menu, and it hides once its menus close and the focus is elsewhere. Every catalogue action is the window's
own (`QWidget::addAction`), so its shortcut works with the bar hidden. The process menus are made from the core's
`ProcessMenuModel`, whose own entries - clear the recent processes, manage and clear the favourites - ask through the
shell core's `actions/ProcessListDialogs.h`.

The workspace is where the views are. A view placed in it is told to keep its tools outside itself
(`ImageViewControl::setChromeMode(ViewChromeMode::External)`, set where the shell places it): the workspace's page holds
one `AnFloatingToolbar` centred 12 px above the bottom of its dock area, placed again at every resize, and one
`AnHistoryStrip` under it, bound to the workspace's active view by a `ViewChromeBinder` that follows the core's active
view (an `AfterNightAppListener`); with more than one docked view the toolbar names the one it acts on, its More opens
the view's menu, and a maximized view keeps both. A view that floats in a window of its own gets the small toolbar
over itself (`workspaces/FloatingViewChrome`), bound to it alone, and gives it back when it docks; the dock widget's
`isFloatingChanged` says when, since a double click or the strip's button floats a view without any drag. A sequence
keeps its player strip at the bottom of its own view, where a toolbar centred at the bottom would cover it: the
workspace's page, a floating window's chrome and a task's page hand their toolbar the strips shown under it
(`chrome/ToolbarClearance`, which finds them and has the toolbar placed again when one is shown, hidden, moved or
resized), and the toolbar is lifted to 8 px above those it would cover; over an image, or with the sequence behind
another view's tab, it is where it always is. The layout icons (`workspaces/WorkspaceToolbar`) sit at the right of the top bar while the workspace is shown: the classic dock
tool bar's lock button and its Group All Images, Split All Images and Fit Images To Window - the catalogue's own
actions - acting through `DockLayoutOperations` on the workspace's target. A right click on a view opens the view's
menu at the pointer and one on a tab the tab's menu, both from `ViewMenuBuilder` (the tab's through an event filter on
the strips, which tells a tab's view by its dock widget); `F11` maximizes and `F11` or `Esc` restores. While a view is
maximized the layout is not the person's to change: Lock, Unlock, Group and Split are withdrawn, and once it is
restored the lock offers what the layout is not - Lock while it is free, Unlock while it is locked - where 1.1 offered
neither until its tool bar's button was used. With every image floating the only workspace lists them
(`pages/FloatingImagesPage`: a picture, the full name and `Dock here` a row, `Dock all here`, `Open file...`). The
status line gains the selector of the active view, the readout's button and the Console chip; files dropped on the
window open through the core's `FileDropHandler`; and `interface/lockLayoutByDefault` locks the layout at the start.

The top bar (`chrome/TopBar`) holds, from the left, the logo (drawn in its Night form under Night), the project
switcher (the open project's name, or "No project", at most 220 px wide with a longer name cut in its middle; its menu
is filled by the window each time it shows - "Projects", below), the breadcrumb of the place and its item (Home;
"Workspace 1" and the active view's full name, elided in the middle by `AnBreadcrumb`, which ends before the field), the
palette's field centred in the bar (`chrome/PaletteField`: 340 px wide, 240 px under a 1200 px window, an icon button
under 1100 px), and at the right the layout icons, Night, Help, Information (About, About Qt, System Information) and
More, which is the menu tree as a menu; its icons are the themed icon manager's and are set again at every change of
the theme. The palette is Controls' `AnCommandPalette` over the core's `CommandIndex`, opened by the field or `Ctrl+K`,
centred over the centre and the column 12 px under the bar with `scrimSoft` laid over them while it is open; a command
it runs goes through what the menus run - a process through the launcher's guards - and is kept among the commands run
last. The shell's own actions (`ShellActions.h`: Home, Command Palette, Readout, Night, the densities) are a table
beside the catalogue's that only this shell holds, added to the window and to the menu bar's View menu, and offered by
the index through a provider the window adds and takes back (`CommandIndex::removeProvider`); Night switches the
tokens' Night and applies the theme again, the densities store `interface/density` and apply it. `Ctrl+W` is set on the
catalogue's Close by this shell when it builds, and given back when it goes: the catalogue's table and the classic
shell keep 1.1's shortcuts.

Several workspaces are the host's `WorkspaceModel` (ShellCore). The rail lists Home, a workspace each - the digit of its
place on each while there are several, the window glyph on one in a window of its own - and Integration; a workspace's
icon shows it, and its menu (a right click, or a press held) holds New Workspace, Rename..., Move to New Window, Group
All Images, Split All Images, Fit All Images to Window, Lock Layout and Close Workspace, acting on that workspace. The
shell's own actions add New Workspace, Rename Workspace..., Move Workspace to New Window and Close Workspace to the
Window menu and the palette, acting on the current workspace - the one on screen, or the one in the active window of its
own. A workspace with no docked view shows its quiet page: the only one lists the floating images, any other says it is
empty (`FloatingImagesPage`); a drag about to start shows a quiet workspace's dock area so the drag can land there. A
workspace left with no docked view closes, unless it is the only one; New Workspace adds "Workspace N" and shows it. The
tab's menu adds Move to Workspace after Float - the other workspaces, a separator, New Workspace - and the person
follows the view. Rename asks for the name in `workspaces/RenameWorkspaceDialog`. Move to New Window shows the workspace
in `workspaces/WorkspaceWindow`, titled with its name, a header of layout icons over its page; activated, it makes its
workspace current; closed, it asks whether to return the workspace or close its images. Close Workspace closes its
images as Close does, each asked about as Close asks; one kept keeps the workspace. Each workspace's chrome binds its
own active view, and Lock Layout and Unlock Layout are offered as the workspace on screen is.

The Session Console is the window's drawer (`chrome/ConsoleDrawer`), between the centre and the status line and spanning
the centre and the column, on every place: it is no dock of a workspace, so opening it changes no place. It holds 1.1's
`LogViewerControl`, made when it is first opened and kept while the window lives, so its lines, its scroll position and
its filters stay as the person left them. Its header is the console's own toolbar, in its order, with the drawer's
parts about it - the title first, a stretch and the count of the session's warnings as a tag before Search, then Float
and the close button - each marked as the drawer's (`ConsoleDrawer::kOwnPartProperty`); `LogViewerControl` lends the
toolbar and its Search entry for it and is otherwise unchanged, and the record of its toolbar
(`Test_ShellObservatory/data/ConsoleToolbar.txt`, written from the classic window's console) holds both shells to one
console. It is 220 px high at first and resized by a band along its top edge between 120 px and 60 % of the window's
height. `Float` moves the console into a floating window of the dock library - a dock widget with the affinity
`an.console`, which no workspace and no image's window shares, made with `LayoutSaverOption::Skip` so no saved layout
holds it, with one title bar and no tab strip - and `Attach` brings it back; the console is the same widget
throughout. Closing its window closes the console, which opens in its window again while it floats. A warning or an
error that arrives while the console is closed is counted on the rail's Console item and on the status line's chip
until it opens. The rail's item, the chip, the catalogue's Session Console action and `IShell::setSessionConsoleVisible`
open and close it; whether it is open, its height and whether it floats are kept (`interface/consoleDrawerOpen`,
`interface/consoleDrawerHeight`, `interface/consoleFloating`), the height read as the window is made and the rest put
back once it is first shown. The classic shell's console is the dock of 1.1, unchanged.

Every open process is a task (`tasks/TaskPresenter`), the shell's `IProcessPresenter`: it takes a process whose
descriptor states a page - a preview, a mirror or a full page - into a page of the centre (below) and leaves a process
that states a window the window it was - no process of the application states one, so only a process made outside a
launch is left so - and it
keeps the shell core's `TaskModel`, fed by what the windows tell it, and a `ViewThumbnailService` that follows the
tasks' targets. The rail lists the tasks under the places, in
the order they were opened, under a hairline drawn only while there are any: each its target's thumbnail with the
process's glyph, or the glyph on a tile for a process with no single target; dimmed with the window glyph, since a
task in a window floats; the ring and its arc while the run is on its way and says how far it is; a result's dot for a
run that ended while its task was not the one looked at - the task whose window is active - which showing the task
clears. A click, and the task menu's Show, raise and activate its window or show its page; the menu's Float as Window
floats a page and its Attach brings it back (below), neither offered for a process shown as a window of its own, and
Close is refused while the run is on its way. An extension's session is an item from its start, its tool tip saying it
opens in its own window (`RailItem::external`): a click and Show ask its host to raise that window, offered only while
the host raises it, the menu has no Float or Attach, Close ends the session, and a host that died leaves the item's
failed dot until it is closed. A view a run writes
carries the notice that names the running task and how far it has come, with Show, set as its processing lock is taken
and as the run moves on - the lock is taken before the run is stated, so the notice names the task once it is - and
cleared as the lock is let go and the run ends; a task's run on the view blocks it even without the lock, since an edit
of a still image takes the image's writer rather than the view's lock. Another view goes on as usual. The tab of a view a task is open on carries the task's
glyph, its tool tip naming the task and, during a run, that the image is locked until the run ends; its click shows the
task. The history strip of the view a workspace's chrome shows adds the pending step of the task open on it,
"writing" while the run writes it. The presenter watches every view the window is told of (`IViewHost::viewAdded`), for
its lock and its name, and goes first as the window goes, so what the process windows tell as they go reaches none.

A process taken into a page lives in a `pages/TaskPage` of the centre, kept alive while another place is shown: the
task's header (`AnTaskHeader`: its image's thumbnail, the process and the image, where its run stands, Close) over the
process arranged as a page ("A process shown as a page", above), whose canvas carries the page's floating toolbar over
the preview view and the target's history strip under it, and whose readout slot carries the compact readout. A mirror
page's canvas is its view of the target, which the floating toolbar serves and the looking actions act on, and it
carries no compact readout, having no preview to read. A page with no single target names its process alone in its
header. A full page has no canvas and no column: the process lies whole under the header, whose menu button lists the
process's own menus (`TaskPage::makeProcessMenu`) and is not shown for a process with none - Image Integration Lite and
the workflow tools. A full page runs work of its own and applies nothing to an image: the end of its run is said as
finished, never as applied, and offers no Undo; ended while another place is shown, it is said in the window's
transient message with the way to its task. Where the process holds a job - Image Integration - the header says
the job instead (`AnTaskHeader::setJob`, from `ProcessPageLayout::jobHeading`, said again each time the process states it
anew): three lines high - the eyebrow, the job's name, its facts - with the job's measure and its bar, its acts, the menu
button and its primary act before Float as window and Close; the measure is left out where the header is too narrow to
leave the name its least width. Floated, the task is the same page in its window, header and keys included. Showing
a task's page makes its target the active view and names the target and the preview to the view service
(`setTaskSubject`): the image actions act on the target and the looking ones - the zoom, the fit - on the canvas
(`ImageOperationsCoordinator::Dependencies::lookingViewControl`); Close, Close All Others and Maximize View are
withdrawn (`ActionStateCoordinator::Dependencies::taskShown`), since an image is closed from its workspace and a task by
its own Close or Esc. The readout follows the task's preview through a `tasks/TaskReadoutSource` - the frames the
preview shows, told by `ImageRTPreviewControl::setPreviewFrameListener`, against the target's image - into the page's
compact readout; leaving the page gives the actions and the readout back to the active view. The source never calls
the process as it goes: the task model hears of a window as its base class is destroyed, after its preview's part is,
so the listener the process holds reads a flag the source clears. The breadcrumb names the process and the image, and
the status line shows the preview's facts and zoom. A run's header shows its progress in a bar along its foot and the
page's own row in the banner's place; a run that succeeds is said in the window's snackbar with how long it took and
Undo, which undoes the target's step, and the header says "applied N s ago" for a minute. Apply (`actionApply_Task`,
the shell's own action, `Ctrl+Enter` from either Enter key) runs the current task's primary action with the focus
anywhere in the window; inside the page the page answers the key itself. A view a process makes while a page is shown
(`ViewPlacement::origin`, `ViewOrigin::Process`) lands in a workspace and leaves the person on the page: the snackbar
says where and offers Show. When a task's process closes, its page goes and the place shown before it comes back.

A page floats in a window of its own and attaches again (`tasks/TaskWindow`): from its header's button, its rail menu,
the palette (`actionFloat_Task`, `actionAttach_Task`, `actionClose_Task`, the shell's own, in the Window menu) or by
dragging. The window is a floating window of the dock library holding the whole page - header, canvas, column - under
the title "process - image", rather than a window of the product's own, so it gets the decorations and the drag of
every other floating window: its dock widget has the affinity `an.task`, which no workspace and no image's window shares,
so it docks nowhere and nothing docks into it; the layout saver skips it; it asks before it exists for one title bar and
no tab strip, the system's where the library uses it; the library parents it to the anchor that shares its affinity,
which keeps it above the shell's window whatever workspace is shown. It is a window (`FloatingWindowFlag::UseQtWindow`)
and not a tool window, because Qt runs the parent window's shortcuts in an active tool window and a floating task must
answer its own keys only - the page's, `Ctrl+Enter` and `Esc` among them. Its least size, 720 x 480, is given to the page
while it floats, since the library takes a window's least size from what it holds. It opens over the centre of the
place now shown - laid out first, since the workspace's column takes its share back as the page leaves - at the size
its process's window was last given, from the process settings where a 1.1 window keeps its size, within the screen
when the screen can hold the least size; the size it is given is kept there when the task attaches or closes. The rail's
item stays, dimmed with the window glyph; a click raises the window, and the task whose window is active is the current
one. The page's compact readout keeps reading the preview while it floats, through a readout controller of its own.
Closing the window by its title bar is not the library's to do: the page refuses the close the library hands it and
the window asks the task to close, which a run refuses; and the press on the title bar that closed it is ended there and
then with a release the library hears, or the library would start a drag of the window on its way out at the next move
of the mouse over anything it drags by. Attach takes the page back into the centre as the current task, then - once
the window it floated in has gone, which takes the activation with it - activates the main window and gives the focus
to the page. Floating and attaching move the page only: the process, its parameters, its preview, a run on its way and
its lock on the image are the same objects. At the application's exit the floating windows go after the processes, which
are stopped, drained and deleted while their windows still hold them, as every process window's are.

The drags are decided by `tasks/TaskDrag` from where the cursor stands against the main window - its frame, its rail,
its centre - which a `TaskDrag::Surface` answers (the window's, or a suite's with an injected cursor and geometry). A
task's rail thumbnail pressed and moved the drag distance shows its ghost (the rail draws it); inside the frame nothing
else happens and a release there changes nothing. Leaving the frame floats the task with its window's top under the
cursor and hands the press to the library (`Core::DockWidget::startDragging`), which follows the cursor until the
release - the system's move on Windows, the library's own moves on X11 - and the rail's ghost ends. Under Wayland,
whose compositor alone moves a window (`windowFollowsTheCursor`), the ghost goes on and the task floats where the
button is released. While a task's window is dragged - pulled out of the rail, or by its title bar, as the dock hooks
tell (`DockHooks`'s `dragAboutToStart` naming its dock widget, its window's moves, `dragEnded`) - the zone under the
cursor, the rail or the centre, is covered by the attach overlay, drawn as the band over a view's landing place, and
the rail marks the task's place with a line (`AnIconRail::setInsertionMark`). Released there the task attaches, once
the library has finished with its drag; released over the top bar, the column, the status line or outside, it stays a
window. Under Wayland the library drags a task's window as a drag-and-drop of Qt's - the window stays where it is - so the window's
own place says nothing: the main window reads the drag's events instead (`ObservatoryWindow::taskWindowDragEvent`, an
application filter, while a task's window is dragged there). Each move over the rail or the centre takes the drop and
drives the overlay and the rail's mark; a move to another widget sends a leave and an enter at once, so the overlay goes
only when no enter follows; over anything else the drop is not taken. The drop itself is ignored, so the library ends
its drag as one not dropped on its own areas and tells so, and the task attaches from where it was dropped
(`TaskDrag::windowDragEnded`); a drag that ends with no drop is `windowDragCancelled`. The points are the window's
own, which is why its frame there is its client area. `setDragPlatform` lets a suite give the window another platform's
answer, and `WaylandDrag.*` drives it with the drag's events.

When the tasks do not fit the rail, its overflow item counts the hidden ones and opens a list of every task, one line
each: its picture, its process and image, and where its run stands - running and how far, a result not seen, in a
window of its own (`railItemDescription`); choosing one shows it as a click on its item does. The end of a run is said
to assistive technology as an announcement from the task's page, or the window for a task with none
(`QAccessibleAnnouncementEvent`): "Curves Transformation applied to M20" politely, "... failed: why" at once
(`AnnouncementPoliteness::Assertive`), and "... stopped".

The window composes Controls' coordinators as the classic window does - the file manager, the image operations, the
mask overlay, the dialogs, the theme manager, the lifecycle coordinator that asks before it closes and keeps the
window's place - hosts the core's actions while it lives, gives the action bindings their targets and the core's
services their hooks, and takes all of it back as it goes, after destroying the process windows still open and before
the stores close. `ObservatoryShell` is the window behind the shell contract: made over the core, it is refused over one
that is not initialized and started or holds no open session, sets itself as the core's shell, makes and readies the
window when it is built, and destroys it when it goes; `observatoryShellDescriptor()` registers it as `observatory`,
"Observatory", in the Observatory profile. The library carries the shell core's naming table (`.clang-tidy`), and its
tree is held by the look guards (`SourceGuards.NoLookLiteralsInShellsAndComponents`,
`.TokensAreNotCachedInConstructors`). `Test_ShellObservatory` links it and drives the real window, started and ended in
the application's order by `ObservatoryWindowCase` (`Test_ShellObservatory/ObservatoryWindowCase.h`).

**The Integration hub** (`pages/IntegrationHubPage.h`). The rail's Integration, and Integration in the View menu and the
palette (`actionShow_Integration_Hub`, the shell's own), show a place of the centre rather than a process: the three
ways to stack - the guided one shown as planned, its Start disabled, so the person sees what is coming without a button
that does nothing; Image Integration's New integration; Image Integration Lite's New quick integration - the four
single steps, each launching its process by its registry's id - every card opening its task, a page of the centre
shown at once in the hub's place - and the integration jobs of the window's
`IntegrationJobsModel`, searched by name or project and shown for every project, for none or for one. The window keeps
the model: its sources are the database outside any project and the open project's, set again as a project opens or
closes; a run of Image Integration that moves or ends tells it, the end reading the jobs again for the record the run
left. Open on a job opens Image Integration on that job's session - the window open, or one launched - and shows its
task; Show on the running one shows its task. While the hub is shown the breadcrumb reads Integration and the column is
hidden, and a view a process makes lands in a workspace with a snackbar's Show, as on a task page: the hub is a place a
person works from, and a master a run publishes must not take them off it. The hub's rows are widgets, made again when
the jobs change: a handful of jobs, each with a tag and a button, read rather than edited. `IntegrationHub.*` drives it.

### Projects (`src/AfterNightShellCore/projects/`, `src/AfterNightShellObservatory/projects/ProjectFlows.h`, `dialogs/NewProjectDialog.h`, `controls/settings/ProjectSettings.h`)

A project is optional: a folder a person chooses that keeps what a body of work gathers - the history of its images,
its workspaces, its integrations, its notes - while the images stay where they are. Without one the application works
as it always has, so nothing below runs until a project is made or opened, and the classic shell, which offers none,
never opens one.

**The folder and its document.** `project.anproj` is one JSON object of the format `afternight-project`, version 1
(`projects/ProjectDocument.h`): the project's id and name, when and how it was made, its object when it has one, how
its history's steps are compressed, its images, its workspaces, the images that floated, its integrations and when it
was last opened. A workspace's lock and the floating images were stated later than the rest and may be absent, read
as not locked and none. Beside it
the folder holds `.lock` while the project is open, the folders of its history, its workspaces, its integrations and
its thumbnails, and its notes (`kProjectHistoryFolder` and the names beside it in `projects/ProjectService.h`). The
document is read from bytes a person can copy, edit or truncate, so reading returns the document or a
refusal with a reason and never anything else: a file that cannot be read or is larger than a document is (16 MiB),
bytes that are no JSON object, an object of another format, a newer version - refused saying so; there is no older
one - and a member that is missing or states what a project cannot. An id is one to sixty-four letters, digits, `-` and
`_`, because an image's history is kept in a folder named by its id and a document must not name a place outside the
project. Writing is whole or not at all: a document its own reader would refuse is not written, and the bytes go to a
file beside the document that takes its place once complete (`QSaveFile`), so a write that fails leaves the document as
it was. `Fuzz_ConsumerDocuments` holds the reader to answering whatever its input, and to reading back what the writer
writes.

**An image is referenced twice**: by its path, and by its path from the project's folder. A project moved with its
images - the folder renamed, a disk given another letter - still finds each through the second, and the record then
follows the file (`ProjectService::locateImage`, and `addImage` for a file opened from its new place). An image on
another drive has no relative path and is found by its own alone.

**The service** (`projects/ProjectService.h`, the core's `ApplicationCore::projects()`) makes, opens and closes
projects and keeps the ones used last. It owns no widget and shows no dialog: what it cannot do is an answer
(`ProjectOutcome`: the refusal, the reason as a person reads it, the folder, and who holds a lock), which a shell says.
One project is open at a time, and a refusal leaves the open one as it was: the new project is made or opened first
and the old one closed only then, once its leave guard lets it go. The document is written before a change is told,
and a change that cannot be written is not made. It is used on the GUI thread only; a document is a few kilobytes.

**The lock.** `.lock` is a `QLockFile` with no stale time, so a lock is never taken for being old. The application
runs once on a machine, which leaves two cases. A lock whose process no longer runs on this machine is a crash's, and
is taken over without a question - the lock's library decides that from the process, the machine and the boot the file
names. Any other is a folder shared with another machine or another user: it is refused naming the machine and the
application that hold it, and `ProjectLockPolicy::TakeOver` - the person's `Open anyway` - removes it and takes it,
because a crashed session on another machine cannot be told from a running one. A lock a running process on this
machine holds is not removed even then.

**The projects used last** are a setting (`projects/recent`: a JSON array of each one's folder and the name it was
opened under, the last ten, most recent first; `projects/last`). The names are kept beside the folders so that the
switcher's menu is filled without reading any project's file: a folder on a disk that is not there is listed, and
opening it says so. `projects/openLastOnStartup` opens the last one at the start, and
`projects/defaultLocation`, `projects/createForIntegrationJobs` and `history/compression` are what a new project starts
from (`readProjectSettings`). A project records the compression it was made with; the setting is where the next one
starts, and what every history outside a project takes. `projects/compactAskThresholdGb` is the size of history
above which a project asks to compact it.

**The history of a project's images** (`projects/ProjectHistoryBinder.h`, the core's
`ApplicationCore::projectHistory()`). The binder hears every view the shell tells it of and gives the history of the
one whose file is the project's image its home, the project's folder for that image
(`ProjectService::historyDirectoryFor`), under the project's compression. An image joins in three ways: opened from
a file while a project is open, once the view holds its image; saved while a project is open, when it is not the
project's yet - a process's result that was never a file is the project's from its first save, and its steps go in;
and, for the images open when a project is made, when the person says so, their steps moved in or left with the
session. A view that was open before a project was opened is not that project's until it is saved: the project keeps
what was made in it. A sequence is recorded as the project's and keeps its history in the session, and so does a
second view of a file whose first view keeps the project's history. Saving a project's
image under another name takes the project's record to the new file (`ProjectService::setImagePath`), as the view
takes the path; an image of the project that already was that file gives way. Closing the project leaves its
histories in its folder, and a view of one of its images that is still open then - a shell that closes a project's
images with it leaves none - goes on with a history of the session's, the steps the project keeps still what its
undo restores. The binder shows nothing: a file that changed outside the application and a step whose file is gone
are said through the shell's notice or the dialog service, without blocking. It compacts an image's history through
the view that has it open or, with none, in the folder on a worker.

**Storage** (`ProjectService::storageReport`) is measured on the thread pool and told on the GUI thread: each
image's history with its steps, the histories set aside, the history folders no image names, the integration data,
the frames a run can make again, the rest of the folder and what is free on its drive. The frames a run can make
again are the folders `kIntermediateFrameFolders` names - calibrated, corrected and registered lights - at any depth
of the jobs' folders, and `cleanIntermediateFrames` removes those folders and nothing else, on the thread pool, and
tells the bytes of the files it removed: one enumeration (`measureJobs`) feeds the report and the clean, so what is
said to be freed is what is freed.

**A project for a job** (`projects/JobProjects.h`, the core's `ApplicationCore::jobProjects()`; `app/JobProjects.h` in
Controls). An integration reaches the application's projects through one interface of Controls, `an::ui::IJobProjects`,
which the application context answers (`IAppContext::getJobProjects`, read through `an::ui::jobProjects()`) with what
its shell offers (`IShell::jobProjects`): the core's `JobProjects` in the Observatory shell while its window lives, and
nothing in the classic shell and in the extension hosts, where a job runs as it always has. It is a member of the
context, as the process presenter is, and no state of its own: a shell that offers no projects answers none, and so
does a context that says nothing. The processes library includes no shell core and knows nothing of how projects are
kept. `JobProjects` answers the interface from the project service:

- **The open project**, as a job sees it: its folder, its name and the folder it keeps its jobs in (`kProjectIntegrationFolder`).
- **A project for a job that runs with none open** (`projectForJob`), when the settings ask for it
  (`projects/createForIntegrationJobs`, on unless they say otherwise): the job's output folder becomes a project -
  `ProjectService::createInFolder`, which puts the document and the project's folders beside whatever the folder
  holds and touches nothing of it - named as the folder, its origin an integration job. A folder gets one project: one
  that is a project already is opened, as Open Project opens it. With the setting off, with a project open, or where
  the folder cannot hold a project - said as any refusal is - the job gets none and runs outside any project.
- **What a run made**: the job is recorded in the project's document by the name of its session and the folder it
  writes to (`ProjectService::addIntegration`), and its masters and stacked images as the project's images.

A project made of a job's output folder holds that job's files in its own folder, so its storage counts them: what
lies there that is not the project's own - its document, its lock, its notes, its history, workspaces, thumbnails and
integration folders - is the job's, its frames a run can make again counted apart; and a folder of the project's
history folder that holds a run's manifest is that job's archive of a run, which the job keeps under the same name
(`kProjectHistoryFolder`), and not an image's undo history. The Summary page, for its part, lists as a run only a
folder that holds a run's manifest, so an image's history beside the archives is no run there. A project a person
made holds its jobs in its integration folder alone.

**In the Observatory window** `ProjectFlows` connects the shell's five project actions - New Project..., Open
Project..., Switch Project..., Close Project and Project Settings..., the window's own, so the classic shell's records
of actions and menus do not hold them - to the service, keeps the top bar's switcher naming the open project and fills
its menu (the open project checked, the ones used last, New project..., Open project..., Close project). New Project
opens `NewProjectDialog` (Controls: the name, the location and the folder they make, the object, the compression, and
with images open whether they join the project and whether their undo history moves into it, with the size the shell
states for it); the dialog makes nothing and is told what a folder is by the shell,
so it holds no project logic of its own: a folder that holds files and is no project withdraws Create project, and one
that is a project already offers `Open project`. Dialogs and refusals are opened over the window and answered when the
person answers; none runs a loop of its own. The window tells the binder of each view it is told of, which is all it
does about an image joining a project. As the window starts the last project is opened when the settings
ask, before the window shows, and one that cannot be opened is said in the window's transient message, the
application starting with no project.

**A project keeps the workspaces and the images open in them** (`projects/ProjectLayoutStore.h`; the Observatory
window owns one over its workspaces). Outside a project nothing is kept and nothing is opened again at the start, as
in 1.1. What is kept is in two places, because the dock library's own list of what a layout has open names only what
was on screen. The project's document records the workspaces - each with its id, its name, its lock, the images
docked in it and whether it is the one used last - and the images that floated in windows of their own, each view by
the image it showed and the name its dock widget had (`ProjectService::setWorkspaces`). Beside it, in the project's
folder of workspaces, the file `kProjectLayoutFileName` names is one layout of the dock library for every workspace's
dock area and every floating image window (`DockHooks::saveLayout`). Only a view of an image of the project is recorded, one of each: an image no file
holds, and an image the project does not hold, is not opened again. A task's window and the console's are in no
layout, since their dock widgets are made to be skipped by one, and nothing of a task is recorded.

It is written when the project is left and when the application ends. **Leaving** is asked by the service itself
(`ProjectService::setLeaveGuard`): before `close()` closes the project, and before another project takes its place
once that one is read, locked and sure to open, so a project that is refused costs the open one nothing. The window's
guard writes the arrangement and then closes the project's images, each asked about as Close asks; one the person
keeps open keeps the project open, and the other project is refused quietly (`ProjectRefusal::OpenProjectKept`). An
image that is none of the project's stays open. A project's images close with it because the project is where they
are found again: left open with no project they would be opened a second time beside themselves when the project is
opened again. At exit the arrangement is written once the person has agreed to the exit and before the views close
(`AppLifecycleCoordinator`'s notice of the close).

**Restoring** (`ProjectLayoutStore::restore`, asked by `ProjectFlows` once a project has opened) makes the window's
workspaces the project's, each under the id it had - a dock area is named by its workspace's id, which is what the
layout names it by - closing the ones the project does not list; opens the listed images through the file workflow,
on workers, each view made and left undocked (`ObservatoryWindow::place` asks the store whether it `takes` a view);
and, once every one is shown or has failed, applies the layout to them (`DockHooks::restoreLayout`), sets the locks
and shows the workspace used last. A name of the layout stands for the view opened for the image the document lists
under that name. An image whose file is at neither its recorded path nor its path from the project's folder is left
out, the layout closes up its place, and the images left out are said once, in one message over the window:
`Locate...` asks where each is now, takes the project's record there and opens it in its workspace, and `Remove from
the project` forgets them. A view the layout does not place - a project whose layout is gone or cannot be applied -
is docked in its workspace as a new view is. A view that was on screen before the project opened is none of its
workspaces' and floats, as the library floats what a layout does not know. The GUI thread waits for none of it; while
the images open the arrangement is not saved and the project is not left. `ObservatoryWindow::continueProject` shows
the workspace used last - the one the person is in, which a restore makes the one the project recorded - and
`continueLabel` names it.

**Project Home** (`pages/ProjectHomePage.h`, `workspaces/WorkspaceMiniature.h`). While a project is open, Home is the
project's page: the window's `showHome` shows it, and a project that opens while Home is shown takes Home's place. The
page reads the open project from the service and what only the window knows is told to it: the workspaces as they are
now - not the record, which is written only as the project is left - each with its arrangement read from its dock area
(`miniatureLayoutOf`: the dock library's groups in their places, their tabs in their order), and where each image is
open. A miniature is painted from the pictures of a thumbnails' service of the window's own, which follows the views
docked in the workspaces while a project is open; the task presenter's follows only the views tasks target and forgets
the others, so it cannot be shared. The storage is measured on the thread pool (`storageReport`, whose report now tells
the frames a run makes on its way - the folders `kIntermediateFrameFolders` names - from the masters it keeps, and says
it was not measured on the GUI thread), and the images' pictures, sizes and dates are read off the GUI thread as Home's
tiles are. `Clean intermediate files...`, on the Storage card, is offered while the storage shown counts such frames:
the window asks first, saying what is freed - the report's own figure - and then has the service remove them
(`ObservatoryWindow::cleanIntermediateFiles`); nothing is removed while an integration runs, which reads and writes
such frames. The notes are the project's notes file (`kProjectNotesFileName`), written a moment after the last keystroke and when the page goes. The Integrations
card lists the jobs of the project's own database from the window's `IntegrationJobsModel`.

**A heavy history asks once.** As a project opens, its storage is measured; when its undo history is over
`projects/compactAskThresholdGb` the window says so once, in its transient message, with `Compact history...`. The
Compact history dialog (`dialogs/CompactHistoryDialog.h`, Controls) is told each image's steps, plans the choice through
`ProjectHistoryBinder::planCompaction` for what it frees and compacts through `compact`: it knows no project and waits
for neither.

**Home's projects.** With no project open, Home shows the projects used last: the last one in the Continue row, four in
the Projects row with the Open project... tile (`controls/HomeProjectsRow.h`). What a card says is read from each
folder on the thread pool (`readProjectSummary`) and only while no project is open: on Windows a file cannot take the
place of one a reader holds open, and the open project's document is the one the service writes - a reader of the
projects used last made the images a new project took fail to join it. The document's writer tries a refused commit
again a few times, for a reader that happens to hold another project's document as it opens.

### The application (`src/AfterNightApp/`: `app/ApplicationMain.h`, `app/ApplicationStart.h`)

The executable composes a shell and owns what outlives it. `main.cpp` calls `runApplication` (`app/ApplicationMain.h`),
which installs the platform crash handler before any Qt object, states the product's version (`kProductVersion`, in
`core/ApplicationSession.h`; the engine's is a crash annotation beside it) - a version of its own because an
extension's minimum version names the product - initializes the platform logger, owns one `ApplicationSession`, makes
the `ApplicationCore` a shell is made over, registers the shells it can make (`ShellRegistry`) and hands the core both.
It starts them in one order (`startApplication`, `app/ApplicationStart.h`): the core's stores first - they make no
widget, and a shell is chosen from one of them - then the shell chosen among the registered ones (`--shell=<id>`,
`AFTERNIGHT_SHELL`, the setting `interface/shell`, the default; a source that names none is passed over with a warning
in the log), then the style under the profile the chosen shell names (or the developer switch
`AFTERNIGHT_STYLE_PROFILE`; `ShellChoice::styleProfile`), before the first widget exists, then the core initialized and
started (`ApplicationCore::initialize`, `ApplicationCore::start`), then the shell made over the core and built;
`showApplication` shows it where it was. The Interface page offers the choice of a shell only when the application
registers more than one (`IInterfaceSettingsRuntimeService::availableShells`), and the choice takes effect at the next
start. At the end `stopApplication` has the shell keep its geometry and tears the application down
(`shutDownApplication`): admission closed, the shell destroyed - its window, views, history and extension sessions, the
context it installed cleared - the windows closed on the way out deleted, the thread pool drained and what it posted
back delivered, the session shut down, checked; then the logger and the crash handler. `Test_AfterNightApp` compiles
the application's start and holds its order and its end (`ApplicationStart.*`, `ApplicationTeardown.*`) and the session
the application owns (`ApplicationSession.*`).

### The shells' parity (`Test_ShellParity/ShellDriver.h`, `Test_ShellParity/ShellScenarioTest.cpp`, `Test_ShellParity/Capabilities.txt`)

What a person does to the application is written once and run against every registered shell. `ShellDriver` is the
handful of things a scenario does to a shell - open a file, name the active view and the views, close a view answering
its question, run an action by its id and read whether the shell offers it enabled, launch a process by its id, press
Apply and cancel a run, read the status line and a view's menu, drop files, close the window - each done as that shell
lets a person do it, with one driver per shell (`ClassicDriver` drives the 1.1 window through its menus and tool bars,
its file dialog and its process and progress windows; `ObservatoryDriver` the Observatory window through its menu tree,
its file dialog, its process windows and its status line, whose placeholders it does not read). `ShellScenario` is a value-parameterised case instantiated for
every registered shell, named `ShellScenario.<Case>/<shell>`; its fixture starts a shell as the application starts it -
the registry, the choice on the command line, the style, the shell built and shown - and ends it as the application
ends it. The run over a sequence is cancelled before its first frame - the thread pool held while Apply and Cancel are
pressed - so the case does not race the run. A shell that cannot yet do what a scenario asks fails it until it can, and
a shell that is retired leaves the instantiation and the scenarios stay.

`Test_ShellParity/Capabilities.txt` is the register of the interface's capabilities, a record each: the `shared` cases of code every
shell uses, and for each shell the cases that drive its window - `pending` while a shell has none.
`check_shell_capabilities.py` holds it to the suites: every case it names is defined, a scenario is named on the line
of the shell it ran for, no capability is listed twice, and every capability has an `observatory` line;
`--require-shell <shell>` leaves nothing of that shell pending. A capability the interface gains is added to the
register in the change that adds it.

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

### The full extension host (`src/AfterNightExtensionHost/`: `HostRuntimeSession.h`, `ExtensionAppContext.h`; `controls/ExtensionNativeProcessWindow.h`)

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
it run the module's code. The native window (`controls/ExtensionNativeProcessWindow.h`, in Controls, so that the
application's hosted process is built on the same class) is a real-time preview window (`ImageRTPreviewControl`) over the
host's mirror of the bound view: its body (`setPreviewBody`) lends each frame's source - the image, its reduction by the preview view's
factor, or a part cut from the image with where it lies stated - and the manual mask laid over it to the extension's
`execute_preview` at the frame's own size and takes what it wrote back (`app/ExtensionPreviewFrames.h`, in Controls:
`lendPreviewFrame`, `takePreviewFrameResult`). The frames reuse their buffers through the window session's `PreviewFrameBufferPool`, which
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

The host states `kHostedPanelCapability` in its hello, and an acknowledgement that states it too
(`HostedPanelService.h`: `servesHostedPanel`, `showsNativeWindow`) makes it serve a panel the application draws (see
"A hosted extension process" below) instead of showing its window. It then sends `ui.describe` with the parameter list
its window would read (`inspectedParamsSchema`) once its runtime is ready, and serves the panel's `ui.event` between
requests: `parameters_changed` reaches the extension's `on_parameters_changed`; `clicked` runs the action over the image
the session is bound to - the view's source buffer the application lent, imported (`hostedViewSource`) - and answers
with `ui.patch`, the values it set or why it failed; `tool_record` is what the extension's settings reads answer first,
as the window's tool configuration answered them. An event that comes while a request is in flight waits for it to end,
and one the extension's run reads is kept for the session loop (`takeKeptPanelEvents`), as a request's await keeps any
other message. A request that lends a writable output of its kind's role (`lentOutputOf`) is written into that output
- nothing of the source copied into it - and answered with it as lent; any other is answered from an output the host
makes, as before. An Apply's progress and its end reach the application as `session.activity`, a validation's refusal
says why in the result's detail, and `on_input_changed` and `on_process_close` are called as they are with a window.
With no window the session ends through the windowless close handler when the extension closes its process window, as
a headless host's does, and the host tears down in the same order.
`Windowless.*` (`Test_ExtensionHostNative`) holds the hosted mode's decisions and messages, and
`ExtensionHostManagerTest.AHostServingAHostedPanelWritesIntoTheOutputsItIsLent` (`Test_ProcessFramework`) the real host
serving one. `Test_ExtensionHostNative`, an isolated suite linking the interpreter, borrows one session the run owns through a
capsule record and runs the host's startup and teardown scenarios against the real backend and interpreter, each in its
own process, and holds the window's frames and the seam they cross (`NativeWindowPreviewFrames`, `HostPreviewFramesTest`); its
opt-in `NativeWindowPreviewBenchmark` drags the window over a frame through the seam ([BENCHMARKS.md](BENCHMARKS.md)). The Lite host links only the protocol library; `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md` is the
boundary.

### A hosted extension process (`controls/ExtensionHostedProcess.h`, `app/ExtensionHostedSession.h`)

The application draws the process of an extension that describes its parameters while the extension's code stays in
its host: a **hosted panel**. Who draws a process is its manifest's `capabilities.interface` - `panel`, `window` or
`none` (`ExtensionInterface`) - and where the manifest is silent, what the parameters read when the package was found
say (`resolvedExtensionInterface`, `interfaceOf` for a bound process): a `runtime` extension's list is a panel, since
every one is written for the native process window, and a `protocol` extension's is one when it states the window's
look (`process_window_meta`'s `meta` entry), since a Lite extension's `get_params()` meant nothing to the application
before it could draw one; with no such list the extension shows a window of its own. Both sides state
`kHostedPanelCapability` in the handshake (the host in its hello, the application in its acknowledgement), and the
application asks for it (`ExtensionHostLaunchRequest::hosted_panel`, `ExtensionHostLaunchCoordinator::hostsPanels`) for
a process of one image whose interface is a panel and whose parameters were kept: a `runtime` extension's only when a
shell takes processes into pages - an `IAppContext` with a process presenter, the Observatory shell - so the classic
shell keeps the host's window (`setHostsPanels` states another policy); a `protocol` extension's in every shell, since
the Lite host has no window to draw it in, the process then a window of its own where no presenter takes it. A host that
does not state the capability is acknowledged without it and shows its window as before. An extension with no interface
(`none`) is launched to show none - the full host with `--no-process-window` - and its session raises no window.

Both hosts serve a panel the same way, and the protocol library holds what the two share: reading a panel's event
(`hostedPanelEventFrom`), the description and an action's answer (`hostedPanelDescription`, `hostedActionAnswer`) and the
output a request lent (`lentOutputOf`). The Lite host binds the buffers a frame or an Apply lends to image tokens, runs
`afternight.lite`'s `_serve_panel_request` over them - the extension's `execute_preview()` or `execute()` - and answers
as the full host does; a request is held as the one on its way and run on the host's thread between turns of its event
loop, a cancel that comes first answering it unrun and one that comes while it runs read when the extension next
reports its progress or asks whether it was cancelled (`LitePythonHostBridge::pumpWhileRunning`), which answers the
heartbeat too. So a `protocol` extension gets the panel linking nothing new - the description is protocol data - and
`check_build_graph.py` holds the Lite host to the protocol library alone. The calls that describe a process window are
`afternight.ui_protocol`'s (`process_window_meta`, `hide_process_window`, `close_process_window`), which `afternight.ui`
hands out unchanged. An extension that hides its process window has the application put its task away (`ui.hide`,
`PanelHideMessage`, `IProcessPresenter::hideTask`: the Observatory shell leaves its page for the place shown before, or
minimizes the window it floats in, and the task stays in the rail); one that closes it ends its session as a closed
window does (`runtime.closed`), and the process closes with it. `ExtensionHostedProcess` is the native window's class in a
page's shape (`ExtensionNativeProcessWindow::PageShape`): the same panel, target selector, view selectors, manual mask,
tool configuration and histograms, laid out as a framework process - a splitter of its controls and its preview, or the
controls alone for an extension that does not preview, and a box of Reset, Close and Apply the framework presses - so a
shell arranges it as any process; its source view is the application's view of the target, never taken into it. It is
a preview page when its manifest declares a preview and a mirror page otherwise, which it answers itself
(`processSurface`) and the coordinator states in the launch scope it makes the process under, so the task reads the
same; a panel wider than the standard column (`controls_panel_width`) takes the wide one. The page's header carries the
quiet tag "extension" (`ProcessPageLayout::setTag`).

What the process and its host say to each other is protocol data, untrusted on the application's side and refused whole
at the first error. The host tells the extension's parameter list once its runtime is ready (`ui.describe`,
`PanelDescribeMessage`), and the process draws it through `setParamDefs`, exactly as the window does; the parameters
kept from the package's discovery are drawn at once. A change in the panel is told as `ui.event` `parameters_changed`
(the whole set, the keys and the reason, as the window's extension hears them), an action as `clicked` with the
parameters, and what the panel's tool configuration resolved as `tool_record`; the panel delivers `LastOfABurst`, so
of a slider's drag only the last change crosses. An action's answer comes back as `ui.patch` (`PanelPatchMessage`): the
values it set, or why it failed. A host's `ui.describe` and `ui.patch` wait in the session's `panel_inbox` for the
process, and the manager writes no `ui.event` to a session whose panel the application does not draw.

`ExtensionHostedSession` is what the process shares with its session, and it outlives the session: a run makes its
request on its worker (`submit`), the GUI thread sends it when the host is ready and nothing else is on its way
(`dispatch`), hears the answer when the session's messages are pumped (`route`, before a commit the extension made
itself lands through the presenters) and hands it to the waiting worker; the GUI thread waits for none of it. The host
takes one request at a time, so a request waits its turn. A request its run gave up on is withdrawn while it waits and
cancelled at the host once sent (`withdraw`); what it lent stays lent until the host has answered, since the host may
still write it, and a host's cancel ends nothing when it is slow to come - the cancel grace applies to other sessions
alone, and a frame stopped is answered when the extension next looks. A failed or cancelled execute is answered by
`execution.result` for a hosted session; any other execute completes by `view.commit`. When the session ends every
request on its way is answered with why (`unbind`), and the process is told how it ended.

The preview is the framework's, its body the remote one: each frame's source, its output and the manual mask are lent
from a `PreviewFrameBufferPool` the hosted session keeps in the session's own directory, the output under the
protocol's `Preview` role (`ExtensionPreviewFrame::outputRole`); the host's `execute_preview` writes the output it was
lent, and the result is taken back and shown only when the frame's ticket commits. A frame asked for before the host is
ready, or after its session ended, is refused without a word, and the host's readiness asks for one again. A result
buffer the application lent the request is held to the descriptor it was lent (`validateExecutionOutputDescriptor`).
The frames are traced, a line a frame, to the file `AFTERNIGHT_PREVIEW_FRAME_TRACE` names, by the side that lends them:
the application for a hosted process, its `run` span the request's whole way to the host and back. The Apply is the
framework's: the target is exported once, a destination of its shape allocated beside it and the manual mask lent, the
host's `execute` writes the destination, and the result is the view's edit, one step of its history, under the target's
lock for the run's length; a cancelled Apply commits nothing and the session goes on. The extension's settings are the
application's store (`ExtensionSdkService::selectExtensionSetting`), read first through what the tool configuration
resolved, as the window always answered them.

While its host starts the process says so: the page appears as the session starts - or, for a first start whose
environment is still to be prepared, before the preparation, which says what it does in that page rather than in a
window of its own (`ExtensionLaunchPreparations::setStartingPage`; the extension service keeps what the manifest declares
of each process, `ExtensionService::openStartingPage`, and the coordinator makes the page, `openStartingPage`, which the
launch after the preparation starts its session into; closing the page stops the preparation, and a page no session was
started into closes) - its body headed by a quiet row: a turning arc, "Starting" and the process's name, and under them
what the preparation does and how far the host has come, a line a step (`ExtensionHostedSession::StartStep`: its host
started, the extension loaded). The parameters kept from the
package's discovery are drawn at once, disabled with the target, the tool's configuration, Reset and Apply
(`ExtensionNativeProcessWindow::setControlsEnabled`, `acceptsRuns`) until the host is ready. A host that fails - it
crashed, stopped answering, or went when neither the application nor the extension asked it to, which for a hosted
session is a failure (`endOf`) - leaves the process: every request on its way is answered with why, so a run ends and
lets the target's lock go, quietly, since the process says it itself; its controls are disabled, its run is failed (the
rail's failed dot) and its banner, in the error tone, says the extension stopped responding with Restart and Close
(`TopLevelProcessBaseControl::showProcessMessageWithActions`, a page's `ProcessPageLayout::setMessageActions`, the
banner's `AnInfoBanner::setActions`). Restart starts a session again into the same process
(`ExtensionHostedProcess::Hooks::restartSession`), which is starting again until its host is ready. A process shown
after a run that ended while its host starts or after it failed is shown as its host stands once the run's end returned.

`HostedProcess.*` (`Test_ControlsExtensionSeam`) holds it over a stand-in host - a local socket's far end and an idle
process of the suite standing for the host's (`Test_ControlsExtensionSeam/HostedPanelStandIn.h`) - among them
`HostDiesLeavesThePageAndReleasesTheLock`, `RestartBringsThePanelBack` and `NeverWaitsOnTheGuiThread`; `HostedTasks.*`
(`Test_ShellObservatory`) holds the page, its starting row, its failure, and a task put away and closed by its
extension. The extension regression
(`ExtensionRegression.*`, `Test_ProcessFramework`) runs every subject of the full host that works on one image on both
paths, its window's and the hosted one's - each request lending the output the host writes - and expects the same
answers of both; a hosted subject tells the parameter list the application kept for its window; and the real-time
preview demo is launched hosted through the coordinator over a view, previewed, its host killed, restarted and applied.
The protocol templates run on the Lite host: the panel template is described, previewed, applied and cancelled hosted;
the window template is asked to raise its window; the template with no interface runs, commits and closes.
The hosted drag is measured beside the window's (`HostedProcessPreviewBenchmark`, [BENCHMARKS.md](BENCHMARKS.md)).

**The frames' files stay mapped in the host.** The application's frame pool lends the same files frame after frame, and
a file mapped anew for every frame is touched afresh page by page - three times the window path's cost of a transform
over a full frame. The host keeps the mappings of the files the frame it serves names (`KeptFrameMappings`,
`HostedPanelService.h`; `SharedBufferManager::openBuffer` through the mapping held before), and lets every other go
before it answers a request - an Apply keeps none, its files being its own. A file mapped in the host cannot be removed
on Windows, so the pool keeps what it lets go until the host has answered a request sent after it went
(`PreviewFrameBufferPool::keepRemovalsForTheHost`, `requestSent`, `requestAnswered`); closing the pool, after the host
was stopped, removes the rest.

### An extension's panel (`controls/ExtensionPanelDescription.h`, `controls/ExtensionParamPanel.h`)

An extension states its controls as data, in two vocabularies: a process window's list of parameters (`get_params()`),
flat, whose `section`, `tabs`, `tab` and `end_tabs` entries open and close groups; and a tree of widgets, which an
extension's settings and a panel sent through the protocol use. `ExtensionPanelDescription` holds both as one tree of
`PanelNode`s - a kind, the fields it states and, for a group, its children - and states nothing about a window: a
`meta` entry's `panel_variant` is the description's variant, and the rest of the entry (its size, its preview, its
target) stays the window's. One tree for both, so that one builder draws every panel and a built-in process's
parameters can be stated in the same terms. `panelKinds()` is the one place a kind is declared: every kind with its
fields, what each holds, what an absent field means and its bound. `panelDescriptionFromParamDefs` reads the list by the
rules the window has always drawn it by - a parameter with no section before it opens one titled "Parameters"
("Creative Controls" in the custom look), a `tab` with no `tabs` opens a set, a slope graph sits in the page beside the
sections, an entry with no id or of a type the window never drew is left out and named - so that a panel built from the
tree is the one the window built from the list; `panelDescriptionFromWidgetTree` reads the tree. A description is
untrusted data: `validatePanelDescription` refuses it whole at the first error and names the node - a kind or a field it
does not know, more than 512 nodes, groups nested deeper than 32, a text past its bound or holding a control character,
a list past its bound, a range whose least is above its most, a negative step, a choice with no entries, a parameter or
an action with no id, two values under one id. The description's own form (`panelDescriptionToCbor`,
`panelDescriptionFromCbor`) is what it is carried in, and a description read back from it is equal to the one written;
`Fuzz_ConsumerPanel` holds the three readings and the validator to answering, and every description the validator
accepts to that round trip.

`ExtensionParamPanel` (`controls/ExtensionParamPanel.h`) builds the controls of a description, each kind as the process
window has always drawn it - the widgets, their names and marks, their layout and the order they are made in, which is
the order the Tab key follows: a section the reading opened for a parameter is made when that parameter's row is placed,
after the row's own widgets, as the window made it (`PanelNode::opened`). It keeps what the panel holds - each
parameter's value and default, the readouts that follow a parameter, the conditions that show or enable a control - and
takes values, defaults and patches (`value`, `enabled`, `visible`, `choices`, `text`, `tooltip`) without a word, refusing
a patch it cannot make whole. What a person does is its one event, `controlEvent`: a value changed, an action clicked, a
curve pressed and released. Delivered `LastOfABurst`, the changes one control makes in one turn of the event loop are
said once, its last, after the turn - what a panel whose events cross to another process wants; `EachChange` says each
as it is made, as the native window's extension has always heard them on its GUI thread. The panel names no window: what
needs the showing process - the target selector and the manual mask's editor (`PanelKindRole::Hosted`), the preview's
own histogram, a slider held - it asks of its `ExtensionPanelHost`, and a hosted control's value it holds as its own
(`registerHostedValue`). `ExtensionNativeProcessWindow` is rebuilt on it: the window keeps its meta entries, the credits
it shows with its description, the target selector, the manual mask, the manual preview's button and banner, the view
selectors' lists and the histograms' counts, and builds nothing of the panel itself.
`NativeWindow.ControlInventoryIsUnchanged` (`Test_ExtensionHostNative`) holds the window's panel to a record taken
before the builder moved - every widget of the panel built from each bundled extension's and author template's
parameters and from two schemas that use every kind, and the order the Tab key reaches them - and the panel's own cases
are `ParamPanel.*` in `Test_ControlsExtensionSeam`.

The panel draws a tree of widgets too, each kind as an extension's settings section has always drawn it
(`controls/ExtensionPanelWidgets.h`, used by the panel alone): `ExtensionUiWidgetHost` is a panel that keeps the
protocol's terms - the `ui.describe` description and the patches as they came, a patch refused when it is not the
session's - reads its tree through `panelDescriptionFromWidgetTree`, and says each change as `widgetEvent` the moment
it is made; `ExtensionSettingsSection` is built on it, as is the dialog a settings action collects its fields in. A
group holds nodes of its own vocabulary; the two stand side by side only at the top of a panel. Every kind and field is
recorded in `Test_ControlsExtensionSeam/data/PanelKinds.txt`, which `PanelDescription.KindsMatchTheRecord` holds to
`panelKinds()`, and the extension author's reference, [EXTENSION_PANEL_KINDS.md](../EXTENSION_PANEL_KINDS.md), is
generated from that record by `build/scripts/tools/panel_kinds_reference.py` and never written by hand, so the page
names no kind or field the product does not draw.

### Processes an extension contributes (`app/ExtensionSdkService.h`, `app/ExtensionManager.h`, `actions/ProcessMenuModel.h`)

An extension's processes are its manifest's, and one that keeps a catalogue of its own - scripts a person enables,
models a person downloads - states while it runs the entries a person chose: `processes.contribute`, announced by the
capability `contributed_processes` in `runtime.hello`, which both hosts state and a host that did not state is refused
(`ExtensionSdkService::dispatchProtocolMessage` is told the hello's capabilities). It carries the extension's whole list
each time, so an empty one withdraws every entry and no message says what changed. A list is untrusted data, refused
whole at the first error with its reason answered to the host and logged: the protocol library reads it
(`contributedProcessEntriesFromCbor` - at most 512 entries, a safe `id_suffix` stated once, a `display_name` of at most
80 characters, a `group` of 40 and a `summary` of 200, plain text, the fields an entry has and no other), since it is the
library a fuzz target and the Lite host can link, and the application then checks it against the extension's manifest
(`validateContributedProcesses` - no manifest process's `id_suffix` or action, a category the registry has, a host
process the manifest has), which only the application knows.

The application keeps the validated list itself, under `extensions/contributedProcesses/<extension id>` in the global
settings store - beside the extension's own settings under `extension/<id>/`, which no key the extension states can
reach - so the menus are whole at the next start with no host started: asking each host at start would start a process
of every extension before the window shows. `ExtensionService` gives each package it registers its kept list
(`attachContributedProcesses`; `ExtensionLoadResult::contributed_processes`), when the list still holds against the
package's manifest, and registers the packages again once a list is kept, on a queued call, so the session's message is
answered first. A whole discovery forgets the lists of the extensions it no longer finds
(`forgetContributedProcessesOfRemovedExtensions`): removal is told by no package's manifest stating the extension's id
(`ExtensionLoadResult::declared_id`, read even of a manifest that is refused), so an update or an invalid package keeps
its list, and a package whose manifest cannot be read keeps every list, since which extension it is cannot be told.

`ExtensionManager::registerDiscoveredProcesses` registers the entries after every manifest's processes, so that none
takes a manifest process's id: a descriptor each, named for the entry, with its extension's availability and reason and
its host process's capabilities, `is_contributed_process`, its group and its summary. `bindDiscoveredExtensionLaunches`
binds each as its host process is (`contributedProcessBinding`): the same host, class, environment, launch mode and
surface, under the entry's id and name - so its task, its rail item and the recent processes name the entry, not the
package - with `--contributed-process <id_suffix>` added, which the extension reads as
`current_session().contributed_process`. `ProcessMenuModel` gives an entry an action of its own, named by
`default_extension_action_object_name`, never a catalogue action, which a manifest process of the same name may take:
an entry is data an extension states while it runs, and taking a menu entry of the application's would let it stand in
for one. The action is in the family's menu after the family's own entries, each `group` a submenu made where its first
entry is, in the order the extension stated them; in the category's menu only when the entry states one; and in the
palette under "<family> - <group>". Palette rows are one line, so an entry's summary is its action's tooltip and status
tip, and what the palette finds it by. The recent processes and the favourites keep an entry by its id and drop one the
registry no longer holds at their next refresh, as they drop a removed extension's process.

### What an extension may do to its view (`app/ExtensionViewRequests.h`, `controls/ShapeOverlayDisplayModule.h`)

A host reads the view its session is bound to and commits to it. Four requests reach further: `view.overlay` (shapes
drawn over the view), `view.selection` (its selection area), `view.display` (its zoom, a point to centre, fit to window,
its display mode) and `view.history` (an Undo or a Redo). Each is announced by a capability of its own in
`runtime.hello` (`view_overlay`, `view_selection`, `view_display`, `view_history`), which both hosts state, since the
requests are protocol data and their calls live in `afternight.ui_protocol`; a host that did not state one is refused
it. Each is untrusted data, read by the protocol library and refused whole at the first error with its reason - the
readers are what the fuzz target holds - and then answered by the application for the session
(`ExtensionSdkService::setViewRequestHandler`, `answerViewRequest`), on the GUI thread when the session's messages are
pumped. Nothing waits: an Undo is asked for and answered accepted, and the host learns what the view became from the
`view.updated` every change of its view sends.

A request names no view. It acts on the view the session is bound to, and a session bound to none is refused - a host
must never reach a view a person is working on and the session was not given. For an extension drawn in a page
(`ExtensionSessionController::requestView`), the display acts on the page's canvas and the shapes are drawn there, as
the page's looking actions act on its canvas and the person looks at it; the selection area and the history are the
task's target's, as the page's image actions act on its target.

What a session leaves is the session's (`ExtensionViewRequests`, a member of its controller) and goes with it: when it
ends, which a host that dies ends too, and when the view it is bound to closes. The shapes are data the session keeps -
at most 4,096 shapes and 262,144 points, a request applied to a copy and kept only when the whole holds - handed to the
view by owner (`ImageViewControl::setOwnedShapes`), which draws each owner's through a display module of its own made
through the companion seam (`createShapeOverlay`): the overlay belongs to the view and goes with it, and the view's own
suite holds the seam with a stand-in. `ShapeOverlayDisplayModule` draws only the shapes the paint reaches
(`ImageViewWidget::rectBeingPainted`; a pan paints the strip it uncovers), in the colours the extension stated drawn as
Night shows a colour, and takes no click. The shapes are in no saved image, no project and no annotated picture.

The selection area is the view's one rectangle, the one a crop and a preview's part use, so a view whose area another
process holds - an enabled crop tool listening on its scroll area (`ImageCropController::holding`) - refuses. The
session turns it on with a crop tool of its own (`AREA_SELECTION_STYLE`), set to the rectangle the host states kept inside
the image, or, with none, waiting for the next drag; a person moves and sizes it as always, and the host is told once,
when the mouse is released, by a `view.updated` whose state carries the rectangle - never for each move, which would send
a snapshot of the view each time. An extension whose preview runs over a part already holds the area through its own
preview: the request sets that same rectangle and tells the preview it moved. The session's end turns off an area it
turned on and leaves one it did not. The display request takes the view's own zoom (within its range), centre, fit and
display mode by the name the view's state reports, a mode the view has not refused by name; the history request takes
the path the view's Undo and Redo actions take, refused with nothing to undo, while a run holds the view and until the
session's own commit has landed - the history must not move under a result still on its way. No request clears a
history: its steps are the person's.

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
  coefficients and a project's document, `Fuzz_ConsumerProtocol` the extension protocol's messages, frames, buffer descriptors and error
  records, and `Fuzz_ConsumerPanel` the descriptions of an extension's panel, each held to what its writer reads back. `run_sanitizer_campaign.py` makes every active consumer suite of
  the registry a row of each configuration and refuses one whose consumer libraries are not instrumented; the platform
  library's containers are unannotated because Crashpad's static libraries are not instrumented
  (`applyEngineSanitizer({ unannotatedContainers = true })`); `thread` runs the Qt-free suites and skips the rest by
  reason.
