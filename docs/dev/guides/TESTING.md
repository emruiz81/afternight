# Testing

What proves the engine correct, from a single contract case to a campaign that injects a fault at
every place an operation can fail. Each layer answers a question the others cannot, and each is held
to having covered something - a campaign that reached nothing prints the same clean line as one that
reached everything, so every one of them counts what it reached.

## The direct suites

One executable per area, under `src/AfterNightTests/Test_Engine*`, each linking the engine and nothing
of the product built on it - except `Test_EngineBenchCommon`, which links no engine:

| Suite | Covers |
|---|---|
| `Test_EngineCore` | Checked sizes and layouts, error codes, the exception boundary, limits, versions, the dependency report |
| `Test_EngineRuntime` | Engine and execution contexts, leases, admission, cancellation, sinks, memory resources, scratch arenas, the scheduler, CPU dispatch, and two contexts in one process |
| `Test_EngineImage` | The image model, metadata, validity, conversion, and every codec, sequence and output transaction |
| `Test_EngineImageOps` | Pointwise programs, statistics, histograms, colour, spatial filters, geometry, multiscale, noise reduction, deconvolution, background, demosaic, and `OperationProgressTest`, the progress contract |
| `Test_EngineKernels` | The kernel contracts, backend conformance across instruction sets, and the device seam against `MockDeviceBackend` |
| `Test_EngineCalibration` | Calibration, global and local normalization, photometric weighting |
| `Test_EngineStars` | Star detection, profiles, PSF fits and flux scales |
| `Test_EngineRegistration` | Registration planning, fitting and transforms, star and channel alignment, comet detection and tracking |
| `Test_EngineAstrometry` | Projections, SIP distortion, WCS cards, catalogs, HEALPix and plate solving |
| `Test_EngineColor` | Photometric and spectrophotometric colour calibration and narrowband composition |
| `Test_EngineStacking` | The stack model, planner, working set, sources, accumulators, methods, rejection, weighting, executor, and drizzle |
| `Test_EngineInterop` | The external buffer descriptions and the OpenCV, FFTW and GSL adapters |
| `Test_EngineFaults` | The fault campaign's nineteen engine scenarios, below |
| `Test_EngineLibraryParity` | Operations held to the library that defines them - OpenCV, GSL, scikit-image - and the RAW corpus |
| `Test_EngineConsumerProbes` | Three consumer shapes built against the published surface only: a command-line one-shot, a file-backed integration pipeline and an asynchronous preview |
| `Test_EnginePublicHeaders` | Every published header compiles alone, and a framework-free client links the library and checks compatibility first |
| `Test_EngineFixtureBridge` | The benchmark fixture bridge refuses rather than repairs |
| `Test_EngineBenchCommon` | The benchmark layer's scenarios and oracles |
| `Test_EnginePythonAdapter`, `Test_EngineGmicAdapter` | The two optional adapters, in both of their configurations |

`Test_EngineOpenClBackend` exists only in a tree generated with the opt-in device backend, and
`AFTERNIGHT_TEST_REQUIRE_OPENCL=1` makes it fail rather than skip on a host without a GPU.

**Tests never touch production storage.** Set `AFTERNIGHT_TEST_STORAGE_ROOT` to a fresh temporary
directory before running a suite by hand. Run `Test_EngineImage` from a working directory on the same
drive as that temporary directory, because one case builds a relative path between the two, and
`Test_EnginePythonAdapter` from the deployment directory, where it finds the deployed Python; elsewhere
it skips half its cases and says why.

**What a case is written against.** A test is written before the code it proves, and a repaired
defect gains the case that would have caught it. A differential case compares a compiled path with the
reference (`ComputeBackend::Reference`); a parity case compares with the library that defines an
operation, using answers recorded by `build/scripts/tools/generate_skimage_goldens.py` where the
library is a Python package that a C++ suite cannot link. A case that proves a guard exists is checked
to bite: the guard is broken on purpose, the case is seen to fail, and the guard is restored.

## The product's suites

Beside the engine's, one executable per area of the product, each under `src/AfterNightTests/` in a directory of
its name: `Test_Platform` and `Test_ConsumerRuntime` for the two libraries beneath the product, `Test_ConsumerFaults`
for the engine's fault harness over the product's paths, `Test_AfterNightCLI`, the `Test_Controls*` suites, the
`Test_Processes*` families, `Test_ImageIntegration` - the largest, its model, workflow, persistence, reporting and
interface subtrees mirroring the production structure - `Test_ProcessFramework`, `Test_ExtensionHostNative`,
`Test_ExtensionRuntimeBackend`, `Test_ThemeSystem`, `Test_Persistence`, `Test_Widgets`, `Test_ImageViewControl`,
`Test_ImageProcessControl`, `Test_ImageRTPreviewControl`, `Test_MainDashboard` for Home's parts in Controls,
`Test_MaskDrawing`, `Test_SystemInformationPanel`, `Test_ShellCore` for the application without a window,
`Test_ShellObservatory` for the Observatory shell's window, the shared parts it shows and the records of the interface,
`Test_ShellParity` (every scenario for each registered shell, each case starting the shell it names and letting its
start settle before it runs) for what a person does in every shell, `Test_AfterNightApp` for the application's start
and end, and
the fixture executables `Test_FixtureNeutral` and `Test_FixtureEngine`. The fuzz targets `Fuzz_Engine*` and `Fuzz_Consumer*` are run by the fuzz campaign below.
`build/scripts/tools/consumer_targets.txt` is the list that is authoritative.

The test projects are generated with the product's by `01-create_project_files` and built by the `09a`/`09b` scripts
on Windows and the `08a`/`08b` scripts on Linux; `run_tests.bat` and `run_tests.sh` run every suite, each with a
throwaway storage root of its own (`BUILD.md` at the repository root has the commands). Sources are globbed, so a new
file in an existing suite needs only the projects generated again - except in an isolated test executable
(`addIsolatedTestProject` in `build/premake/afternight-test-pm5.lua`, `Test_ImageIntegration` and the Controls and
Processes suites among them), which compiles exactly its list in `build/scripts/tools/isolated_sources.txt`, so a new
file there needs its entry too.

A new suite needs five things in the change that adds it: its directory, with its cases written before the code
they prove; its project in `build/premake/afternight-test-pm5.lua`, following an existing suite; its record in
`consumer_targets.txt`, without which generation stops; its entry in both runners' lists; and its row in every
configuration of `build/scripts/tools/sanitizer_matrix.txt`, run or skipped with its reason. A suite that writes
files owns a temporary root, sets `AFTERNIGHT_TEST_STORAGE_ROOT` before it constructs a `QApplication`, resolves its
paths through `app/ApplicationStorage.h` and removes the root on success and failure alike, as the isolation policy
in `AGENTS.md` requires.

`Test_ShellCore` holds the shell core ("The shell core" in [the product guide](PRODUCT_ARCHITECTURE.md)) with no
window: a case about a class of that library lives there, never in a suite of Controls or Processes, which cannot link
a library above them. The choice of a shell is held to the arguments it is handed by starting the executable again
with `--shell=aurora`, a shell of the case's own registry, on its own command line: the copy runs the one disabled
case, `ShellSelection.DISABLED_OwnCommandLineIsNotRead`, which finds the option in `QCoreApplication::arguments()` and
not in the shell chosen.

A project is held where each part of it lives ("Projects" in [the product guide](PRODUCT_ARCHITECTURE.md)): its
document and its service in `Test_ShellCore` (`ProjectDocument.*`, `ProjectServiceCase.*`), each case over a temporary
root of its own; the New Project dialog, the Projects settings page and the stored settings in `Test_ControlsDialogs`;
and what a person does with the switcher in `Test_ShellObservatory` (`ProjectSwitcher.*`, and `ProjectAtStart.*` over a
shell started for the case). A lock's two cases are made from files: a lock that names a process which ran here and
exited, written as the lock's own library writes one, and a lock that names another machine. A failed write is a real
one - the document held open without sharing on Windows, its folder closed to new files elsewhere. The cases that
compare the switcher's text use short names: the suites' platform draws text about twice as wide as a display does, so
a name of ordinary length is rightly cut there.

What a project keeps of the workspaces is held in `Test_ShellObservatory` (`ProjectLayout.*`, over
`Test_ShellObservatory/ProjectCase.h`, and `ProjectLayoutAtExit.*` over shells started and ended for the case, since an
exit and the next start are two runs of the application), with the parts under it in `Test_ShellCore`: the document's lock and floating views, the service's
record of the workspaces and its leave guard, a workspace made under an id, and `DockHooksTest`'s cases of a saved
layout. A case arranges real views of real files - a view is the project's once the file it shows has joined it, which
the fixture waits for - closes the project as Close Project does and opens it again, and compares each view's group,
its place in its dock area within two pixels, the tabs' order and the floating window's place. Three things are easy
to get wrong in such a case. An empty workspace the person is not in closes, so a second workspace is made once the
views that go into it are open. A view deleted and not closed - what a fixture's teardown does - leaves its dock
widget behind with a guest the dock library still names; `DockHooks` destroys those before it takes or applies a
layout, and a case that reads a dock widget's guest asks Qt for the child, not the library. And the dock library's
view gives `window()` another meaning: the window a floating view is in is `QWidget::window()`.

`Test_ControlsDocking` holds the dock library to what the interface is built on ("The dock area" in
[the product guide](PRODUCT_ARCHITECTURE.md) lists it) and drives the library the way a user does, because the
library's drag controller is not in its installed interface: a drag is a press, moves and a release delivered to the
widget the library drags by - events outside the client area where a floating window has the system's title bar, as on
Windows, and ordinary mouse events on the library's title bar or tab strip elsewhere - and what the library is doing is
read from the hooks it calls, the dragged window's opacity and the drop indicators. The events are sent to the widget
and not injected through the window system: on Windows the library cancels a drag whose button Qt believes pressed
while the system reports it released, which an injected press is. The suite runs under the `offscreen` platform on
every host, the one platform that keeps a cursor a case can place, where the other suites run under `minimal` on
Windows; one case, on Windows, starts the executable again under the native platform to start and end a drag with a
real window and the system's title bar. Its cases are the library's contract, so a case that fails after the library
is updated states a behaviour the interface relies on, to be read before it is changed. What no case can do is hold a
real button down, so whether a window pulled out by a pressed widget follows the cursor is a trial for a person:
`ProgrammaticDrag.HandTrialATaskPulledOutByItsThumbnailFollowsTheCursor` is skipped unless
`AFTERNIGHT_TEST_DOCKING_HAND_TRIAL` is set, then opens a window to drag from under the platform it is given
(`-platform windows`, `xcb` or `wayland`) and prints what it saw when the window is closed.

**A time is measured, not asserted.** A time depends on the host, and a comparison of two times more so, so a case
that measures one is compiled always and skipped unless `AFTERNIGHT_TEST_TIMINGS=1`: it prints what it measured,
records it as the case's properties, and fails only when what it timed did not work. `StepCodecCost.*`
(`Test_ControlsHistory`) is the first: the time to write and to read a 60-megapixel history step in each step codec,
each beside the snapshot codec in alternation, the median of five rounds, with the file's size. It is run in Release
on an idle machine when a step codec or the engine's writers change, and its figures go where the change is
reported. A count - calls, repaints, admissions - is exact and is asserted on every run.

Two cases of `Test_ImageIntegration` write files for a person to look at, and are skipped unless
`AFTERNIGHT_WRITE_REPORT_SAMPLES` names the folder to write them into: `ReportSamples.*` writes the Image Integration
report of every scenario the report's suites build, in both of its forms, and `ReportChartSamples.*` a page of every
chart. `build/scripts/render_integration_report.cjs` then opens those reports in Chrome and Firefox from disk, with
the network blocked, and fails a load that would look or work wrong for a reader - the half of the report's evidence
a suite that parses markup cannot give. It is run by hand when the report's document, style, script or charts
change; `docs/dev/LOCAL_VALIDATION.md` has the commands and `docs/dev/IMAGE_INTEGRATION_REPORT.md` what each proves.

## The interface's records

What the interface is - its actions and menus, the keys it answers, the processes it registers, what its windows are
made of - is recorded in eight text files beside the suites that read them, and a case compares each with the running
product:

| Record | Holds | Held by |
|---|---|---|
| `Test_ShellObservatory/data/ActionInventory.txt` | The application's 186 actions, as AfterNight 1.1's main window declared them, with their text, tool tip, shortcut, icon, whether they can be checked and the menus that hold them; and the menu bar's tree | The shell core's catalogue and menu tree, by `ActionCatalogue.HoldsTheRecordedInventory` and `MenuTree.EqualsTheRecordedMenuBar` in `Test_ShellCore` |
| `Test_ShellObservatory/data/ViewMenuInventory.txt` | The tree of the menu a view shows on a right click | The shell core's view menu, by `ViewMenuBuilderTest.EqualsTheRecordedTree` in `Test_ShellCore`; the Observatory window's, by `ShellScenario.ViewMenuEntries/observatory` in `Test_ShellParity` |
| `Test_ShellObservatory/data/ShortcutInventory.txt` | Every shortcut of the Session Console, and every key press a view takes for itself in each of its three modes; and in its `[window]` section every shortcut AfterNight 1.1's main window answered, the keys a person brings from 1.1 | `Shortcuts.MatchTheRecord` in `Test_ShellObservatory`, for the Session Console's and the views' sections; `NewShortcuts.DoNotCollideWithTheRecordedOnes` there keeps the Observatory window's own shortcuts apart from `[window]` |
| `Test_ShellObservatory/data/ProcessInventory.txt` | What the registry states of every process of the application: its id, names, category, launch mode, guards, outputs and help page | `ProcessRegistry.InventoryMatchesTheRecord` in `Test_ShellObservatory`; the descriptor each process action launches and the name it gives it, by `ActionBindingsTest.EveryProcessActionLaunchesTheRecordedDescriptor` in `Test_ShellCore` |
| `Test_ShellObservatory/data/ConsoleToolbar.txt` | The Session Console's toolbar as 1.1 shows it: every action, label, choice and separator in order with its text, tool tip, shortcut and checkability | The Observatory shell's console drawer, less its own parts, by `ConsoleDrawer.TheToolbarIsTheConsoles` |
| `Test_ImageViewControl/data/ViewToolbar.txt` | The toolbar row an image view builds by default in each of its modes - a standard view, a real-time preview before and after it offers its autostretch controls, a static preview: where the row is, and every action, choice and separator in order with its text, tool tip, checkability and whether it is shown | `ViewToolbar.EmbeddedContentsMatchTheRecord` |
| `Test_ImageIntegration/data/IntegrationPages.txt` | The pages AfterNight 1.1's two levels of tabs in Image Integration reached: the tabs that led to each, the page's name and the class of what it holds | `StageList.HoldsEveryTabPage`, which holds the pages the stage list reaches to the same entries |
| `Test_ProcessFramework/data/ProcessShapes.txt` | What the window of every registered process is made of: the framework class it derives from, how its controls and its preview are laid out, its preview checkbox and where the preview view lands, its button box, its selectors of images and channels, its menus, whether its size is fixed and which window hints it changes, what the person marks on a view for it, and every key sequence its own actions and shortcuts carry | `ProcessCensus.EveryProcessStatesItsShape` |

The records exist so that a change which reorganises the interface's code - a part of the window moved to another
library, the menus built from a table - proves that it changed nothing a person sees
or presses: the case reads the live objects, so it does not care how they were made. A change that means to move
something says so in the same diff, as an edited entry a reviewer reads: a renamed action, a new menu entry, a
retired process.

Each record is read from the running product - the actions and menus from the shell core's catalogue and menu tree,
the registry and the Session Console's keys over the Observatory window, a view's keys by pressing them on a view -
and holds nothing that follows the host: no geometry and no font. Nor anything that follows what ran before the case: a menu whose entries are what a person did - the recent files, the
favourite and the recent processes - is held by name alone, of the Extensions menu only its own actions, and the
actions are read over one image that was never edited, because Undo, Redo and Maximize View are worded after the
active image. `fixtures/BaselineRecord.h` is the reader: a record is sections of entries, a case compares one section
in order, and the failure names the first entry that differs with what the record holds and what the product has.
A suite below the shell core, which can link neither its action catalogue nor its menu tree, reads the application's
menus in the menu bar the record holds, made of the actions the record holds (`fixtures/RecordedActions.h`,
`fixtures/RecordedMenus.h`): their object names, texts, tool tips, shortcuts and checkability are the catalogue's,
which `ActionCatalogue.HoldsTheRecordedInventory` holds to the same record.

The process shapes are read by launching every descriptor as the application launches it, on a colour view with a
grey one open beside it, under window settings that hold nothing, so no case that ran before decides what a window
restores. A part is recognised by what it does, not by a name a builder gave it: the preview checkbox is the window's
own checkbox named for the preview that, checked, puts the preview view in the preview's pane; a selector of images is
a combo box that lists the two views; the channel selector lists the framework's channel choices. A key sequence made
from a standard key is recorded by the key's name, because its bindings are the platform's. What belongs to an image
view a window shows - its toolbar, its preview-area checkbox - is the view's and is left out. The record holds no
size, for the reason the next section gives.

### A window is made active through the fixtures' helper

Under `minimal` no window is handed the activation when the one that had it closes, and a request for it
(`QWidget::activateWindow()`) is queued among the window system's events, which that platform delivers only as a window
is shown or hidden and never from the event loop. A case that asks and then waits
(`QTest::qWaitForWindowActive`) therefore waits its whole bound - five seconds - whenever a dialog, a menu or a
floating window has come and gone before it, and passes all the same: 75 of the Observatory suite's cases did, six of
its seven minutes. A case or a fixture that needs its window active calls `an::tests::app::activateWindow`
(`fixtures/WindowDriving.h`), which under that platform has a window of its own shown and gone deliver the request,
and under any other waits for the event loop to. `ObservatoryWindow.IsActiveAgainAtOnceAfterAnotherWindowCameAndWent`
holds it.

### A window's size is not measured under the suites' platform

The suites run headless: under `minimal` on Windows, where text is drawn by a box font about two and a half times as
wide as the interface's, and under `offscreen` elsewhere, where the fonts are the host's. A widget's minimum width
under either is not the width a person sees, and is not the same number on two hosts, so no record holds one and no
case asserts that a text fits. Under `offscreen` on Windows the default font resolves to whatever family the font
folder lists first, which is not the interface's either.

A size that matters is measured under the native platform with the product's style installed
(`an::ui::installAnProxyStyle()`, which also sets the application font the design tokens take their body font from),
with every window kept off the screen: an application event filter that sets `Qt::WA_DontShowOnScreen` on each window
as it is polished lets the window be laid out, shown as far as Qt is concerned and painted into a pixmap
(`QWidget::grab`) without ever being mapped, so nothing appears on the desktop and nothing takes the focus. Such a
measurement is run by hand with `QT_QPA_PLATFORM=windows`; it is a number for a design decision, written where the
decision is, not a case of a sweep.

A window that stays a window is held the same way by `fixtures/ClippedControls.h`: `clippedControls` names every
control shown that lies outside its parent, a button, a field or a box smaller than it needs, a combo box narrower than
its longest item and a label too small for its text, `unsizedScrollContents` every scroll area whose content is shorter
than its width needs, and `textIsMeasured` tells the box font apart. A case asserts the scroll areas under any
platform and what is clipped only where text is measured, reporting itself skipped otherwise
(`ExtensionManagerPage.NoControlIsClipped`, `SettingsDialog.NoControlIsClipped`). A window's least size is asserted
under any platform where the window states it and nothing sizes it again (the Settings dialog), and only where text
is measured for a window that is sized for the look as it is first shown (the Extensions dialog, an extension's
settings): under the box font what such a window holds needs more than the window states, and it takes what it
needs, so there it is held to be no smaller.

`fixtures/WindowLook.h` holds a window that stays a window to the Observatory look, whichever window it is.
`expectWindowLook(make)` makes the window as the product makes it in each density and holds that it shows a heading that
says a title (an `AnWindowHeading`, or a process window's header) with nothing above it; that the rightmost button of
its footer, the lowest row of buttons that can be seen, is the primary one; that every text meets the contrast rule
against the surface it is painted on, read from the palette roles in the dark theme, the light one and a tinted
preset (4.5:1, and 3:1 for the tertiary and the status texts); that no scroll area's content is shorter than its
width needs; and, where text is measured, that nothing is clipped as it opens or at its least size, that no label is
cut short as it opens, that the window opens within its screen's available area - a second
one is asked before its first show to be larger than the screen - and that its least size fits the room a 1280 x 720
display leaves a window (`kFloorRoom`). `expectModalWindowLook(run, isIt)` holds the same of a dialog the product
runs to its answer, looked at from inside its loop. The cases are `WindowLook.<Window>`, in the suite that owns the
window; each ends with `AN_WINDOW_LOOK_REPORT`, which reports it skipped, saying what its platform could not measure,
where nothing it did measure failed. Started with `QT_QPA_PLATFORM=windows` they measure all of it, their windows
kept off the screen but a dialog run to its answer, which shows for a moment, and write the size each window opens
at and its least size, per density, and its footer's buttons into the report (`--gtest_output`). Two things the
fixture learned: a widget scrolled out of the area that holds it is still visible to its window, so what can be
seen is asked of `visibleRegion()`; and a suite without the product's icon provider has no icons, so a tool button
that shows its icon alone is told by its style, not by its icon.

The Observatory window's pages are walked by `Test_ShellObservatory/ObservatoryPages.h`: `PagesCase::open(page)` opens
each page a person works on as a person opens it - Home with a recent file, Project Home of a project made for the
case, the Integration hub, a workspace, Curves Transformation's page (a preview), Resize's (a mirror), Image
Integration's, Image Integration Lite's and Image List Analyzer's (full pages) - and `leave()` closes what it opened, an
image's dock with its image (deleting a view alone leaves its dock in the workspace, which the suite's other cases do,
so each case starts by removing the docks no image lives in). `Accessibility.*` walks every page for a name and a role
of every interactive widget and for a Tab order that visits each control once in reading order; `SmallWindow.*`,
`DisplaySizes.*` and `Scaling.*` hold the pages at the least window, the display sizes and the device pixel ratios. What
a size leaves of a page depends on the width of its font, so `SmallWindow.NothingIsClippedAt1024x640` and the
`DisplaySizes.*` cases run natively (`QT_QPA_PLATFORM=windows`) and report themselves skipped under the suites' box
font; there a label that elides by design (its least width none) and shows a character and the ellipsis is not clipped.

The page cases of the process families, `PageLayout.<Process>FitsItsColumn` (`Test_ProcessesShared/PageLayoutCheck.h`),
are such measurements kept as cases. Under the suites' platform they hold what no font decides - every control in the
column, none left beside the preview in the canvas, and reached by Tab (a checkable button of an exclusive group through
the group's one Tab stop, as Qt navigates such a group), Target image and Channel at the body's head, no group left
empty and no button box left among the controls (the window's buttons are the footer's), in both densities of the Observatory look, at 600 px of height - and report themselves skipped for the rest; the
box font is told by every character being one width. Started with `QT_QPA_PLATFORM=windows` they also hold that nothing
in the body lies past the column's side; their page hosts are kept off the screen there. A content a scroll area of the
controls holds is measured too: its least width is not the area's.
`PageLayout.<Process>AppliesAsInAWindow` applies the same parameters in a window and through the page and compares the
results, the target's or the new image's; a process that draws its result at random from a seed of its own - Noise
Generator - is held instead to changing the image in both, and Image Compare, which applies nothing, to comparing on its
page as in its window (`PageLayout.ImageCompareComparesAsInAWindow`). Which processes must be pages is held by
`ProcessSurfaces.EveryPreviewProcessWithATargetIsAPage` (`Test_ProcessFramework`), from the census record: every one
recorded with its preview beside its controls and launched on an image. A mirror page is held the same way, its surface
named to the check (`expectPageFitsItsColumn(..., ProcessSurface::MirrorPage)`), which also holds its canvas to be the
view the process works on; its Apply is compared by `expectMirrorPageAppliesAsTheWindow`, which presses the primary
button as soon as it is offered and compares the target's next step or every image the run made. A process that marks
a rectangle on its view is held to marking it on the page's view of the target -
`<Process>.SelectionOnThePageGivesTheWindowsResult` and `<Process>.TargetViewShowsNoSelectionInPageMode` - by a drag
sent to the view's viewport at 100 % (`Test_ProcessesShared/SelectionDrag.h`), where a point of the image is a point of
the viewport, offset, in a window and on a page alike.

A record is made once. With `AFTERNIGHT_WRITE_BASELINE_RECORDS` naming a folder a case writes its section there
instead of comparing - the only case in which it writes, and never beside the suite - and a person reads what was
written before checking it in. From then on a record is edited by hand, entry by entry, by the change that means to
move it. It is never written again whole: a record written from the product agrees with whatever the product does,
a defect included, and the diff of a rewritten record hides the one entry that mattered among the ones that did not.

### What a person does, in every shell

`Test_ShellParity` runs the scenarios of the interface - open an image, split, group and fit, lock the layout, maximize,
rename and duplicate, undo and redo, apply a process, cancel a run over a sequence, the launch guards, a view's menu,
every action reachable in the catalogue's state, the recent files, a drop, the status line, the questions a close asks,
a theme change, the settings at a page - against every registered shell: each is a `ShellScenario` case instantiated
for each shell id (`ShellScenario.ViewMenuEntries/observatory`), started and ended as the application starts and ends
it, and driven through that shell's `ShellDriver`. A scenario reads the records above where a record holds what it
shows.

`Test_ShellParity/Capabilities.txt` names, for each capability of the interface, the cases that prove it - the
`shared` ones, of code every shell uses, and the `observatory` ones, which drive the Observatory window - and
`check_shell_capabilities.py` holds every name to a case the suites define and refuses a line of another kind. A
capability with no shared case is an empty `shared` line, and one the Observatory window has no case of yet is
`pending` on its line.

## The source guards

Seven rules of the source tree are held by cases that read the sources themselves, the `SourceGuards` cases of
`Test_ImageIntegration` (`Test_ImageIntegration/unit/SourceGuardsTest.cpp`); a change to any C or C++ file under
`src/` runs them (`Test_ImageIntegration --gtest_filter=SourceGuards.*`), whatever other suites it reaches:

| Case | Holds |
|---|---|
| `NoRawStdMinMaxCallsInSrc` | No source calls `std::min` or `std::max` by its bare name, which the Windows SDK's macros would expand |
| `NoStyleSheetsInSrc` | The product sets and reads no stylesheet, names no stylesheet style and no `.qss` file, and the tree holds none: the look is painted from the design tokens |
| `NoDefaultQSettingsInSrc` | The product makes no `QSettings` but the one `an::ui::applicationSettings()` returns: any other is the platform's native store, which no test storage root redirects |
| `ShellsDoNotReachEachOther` | A shell is reached only by the application that composes it and the suites that drive it: no shell includes another's header, and neither the shell core, Controls, Processes nor a suite of theirs includes one |
| `NoClassicProfileIsLeft` | No source names the Classic style profile of AfterNight 1.1's window, neither that window's library nor its suite is in the repository, and no source includes a header that went with them - the window's, its suite's and its parity driver's, the parts only it showed, Image Integration's window of a run's progress, and the switches that chose a path of AfterNight 1.x |
| `NoLookLiteralsInShellsAndComponents` | In the shell core, the Observatory shell and `components/`, no colour and no font size is written as a literal: no `QColor` made from numbers, a name or a string (as a temporary, a variable, `QColor::from...` or `qRgb`), no Qt colour constant but `Qt::transparent`, no `"#RRGGBB"` string, no point or pixel size set from a number |
| `TokensAreNotCachedInConstructors` | In the shell core, the Observatory shell and `components/`, no constructor - its initializers, its body and a lambda made there - calls `tokens()` or the dark or light set: a part reads them where it paints, sizes itself or is polished |

`NoStyleSheetsInSrc` and `NoDefaultQSettingsInSrc` read the product and not the test tree, whose cases read a widget's stylesheet to prove it empty and
make settings files of their own in temporary folders; and they read code, not comments, so a comment may name what
the code may not do. The two look guards read the trees whose look is held to the tokens - the shell core, the
Observatory shell and the reusable components - and not the theme's tree, where the look is decided; a shell's tree
joins their list when it is made. `NoDefaultQSettingsInSrc` lists the lines it allows - the function that opens the settings file
and its one wrapper - and fails when a listed line is gone, so the list cannot outlive what it allowed. A guard is
shown to fail on a violation planted in a scratch file before it is relied on.

## The fixture layers

The product's suites build their inputs and hold their answers through three layers under
`src/AfterNightTests/fixtures/`, so that an expected value is never whatever the engine under test
happens to compute:

| Layer | Directory | Reaches | Held by |
|---|---|---|---|
| Neutral | `fixtures/common/` | No engine and no Qt - nothing but the standard library | `Test_FixtureNeutral` |
| Harness | `fixtures/harness/` | The platform library, and Qt for `fixtures/harness/QtTestHarness.h` only | `Test_FixtureNeutral` |
| Engine adapter | `fixtures/engine/` | The engine's published surface, through the consumer runtime | `Test_FixtureEngine` |

Each fixture executable is an isolated test target that compiles exactly its list in
`build/scripts/tools/isolated_sources.txt`, and the target registry states which of them may reach
the engine - the adapter's alone - so the neutral layer cannot reach it by accident. Every fixture
executable runs under the Qt harness, so a product suite composes the same harness with the adapter
and nothing else. The helpers directly under `fixtures/` - the scoped environment variable, temporary
path and temporary directory guards, the in-memory database and the headless application - are
engine-free, and `fixtures/TestImageGenerators.h` makes its frames through the adapter.

**A description is stated in stored units and quantized once.** `fixtures/common/FixtureImage.h`
describes a test image by its extent, mono or RGB channels, one of the eight representations the
engine holds, what the numbers mean (normalized linear, sensor code or physical quantity), a filter
array, a pattern - constant, gradient, checker, seeded noise, gradient noise or a star field whose
stars are each stated by position, width and peak - with per-channel levels, single-sample overrides
for a non-finite value or a signed zero, and header cards. `materializeFixture` computes every sample
once - nearest with ties to even and saturated for an integer type, the IEEE conversion for Float32 -
so every suite copies the same bytes rather than deciding how to convert. `describeFixture` is the
canonical text of a description, `fixtureDigest` its SHA-256, and `FixtureSamples::digest` the SHA-256
of the samples: the two identities a recorded result names. `checkFixture` refuses what the engine
cannot hold unchanged - a physical quantity in an integer, a filter array on colour, a non-finite
integer, a card the file layout writes itself, a value longer than 32 characters or a comment longer
than 24. `fixtures/common/FixtureCatalogue.h` holds the standard descriptions, which between them
reach every representation, mono and colour, signed samples, saturation at both ends, floats far
outside zero to one, a Bayer array, NaN, both infinities, a negative zero and the cards a capture
program writes; `behaviorFixtures()` adds the inputs the behavior corpus runs the product's actions
over, and `fixtureNamed()` finds a description in either list.

**An input file is written and read without the engine.** `fixtures/common/FixtureFits.h` writes a
description's samples as one primary FITS image - the canonical offsets for unsigned sixteen and
thirty-two bits and signed eight, a `ROWORDER` card saying which row is stored first, `BAYERPAT` for a
filter array and the description's cards - and reads one back the way the engine reads it, a bottom-up
file turned top-down. Because the reader is not the engine's, it also checks what the engine wrote.
`fixtures/common/FixtureStatistics.h` states per-channel statistics over the finite samples by its own
definition, so a test that compares the engine's statistics compares them with something that is not
the engine's.

**A reference expectation is a record, compared by its classification.**
`fixtures/common/FixtureExpectation.h` is the record. It names its input by name and by both digests,
its producer - engine, version and interface generation - the configuration, compiler, platform, date
and commit it was captured from, a classification, and the result samples in full, one row per line,
with the SHA-256 they must reproduce. Three classifications:

- `exact`: integers equal, floats bit-identical, except that any NaN matches any NaN - so a signed
  zero is not the other zero;
- `tolerance`: an absolute plus a relative bound per sample, a non-finite sample matching only its
  equal; and
- `accepted`: the difference is expected and the record says why; the samples are still compared and
  reported, and never fail.

A different shape or representation never matches, whatever the classification. Records are text with
CRLF endings, several to a file, one file per operation under `fixtures/common/expectations/`, the
operation key prefixing each record's id; the behavior corpus keeps one file per product action under
`fixtures/common/expectations/behavior/`, with its case list in
`fixtures/common/expectations/behavior/cases.tsv`, and `fixtures/common/expectations/sdk/` holds the
runtime module's recorded contract and the extension buffer sidecars. A reader finds the directory from
`AFTERNIGHT_FIXTURE_EXPECTATIONS` or by walking up from the executable and the working directory. A
NaN whose sign or payload is not the quiet default is written `nan(0x<bits>)`, so a record reads back
to the samples its digest was taken from.

**The checked-in records are the reference, and nothing in the tree regenerates them.** Every reader
first checks that a record still describes the input the catalogue builds today, by name and both
digests, so a description that changes makes its records stale and every reader refuses them; a new
or changed input gets a record of its own rather than an edited one. The engine adapter's suite also
holds that no record it compares was produced by the engine under test, because holding the engine
to its own output proves nothing; a record that cannot be captured again says so in its provenance,
naming its producer `AfterNightEngine reference abi=-` and its commit `-`. A record that names an
accepted difference keeps its reason in the record, and the case that reads it bounds the difference
in samples and size, so the reason cannot grow quietly.

**An input another release wrote is kept as it wrote it.** `fixtures/common/settings/afternight-1.1.0.db` is the
settings database AfterNight 1.1.0 wrote, driven through its own Settings dialog, and
`fixtures/common/settings/PROVENANCE.md` says how and what it holds. `SettingsCarryOver` in `Test_Persistence` opens a
copy of it as this version opens the store it finds, holds that no key of it is lost or changed and reads the choices
it carries over, and refuses the file when its SHA-256 is not the one the provenance states. No build of this tree is
that release, so it cannot be captured again: it is never regenerated, and a store of another release is a file of its
own beside it.

**A record can be specific to a build.** A behavior record is the reference build's answer, and the
reference build is MSVC on Windows. A case another build does not reproduce keeps that build's own
record beside it, in `behavior/<compiler>-<platform>/` named as the record's provenance names the
build, and only that build compares the case with it;
`fixtures/common/expectations/behavior/clang-linux-x64/` holds two. A build
record exists only for a case its build does not reproduce, states the same operation, input,
classification and bound as the common record it stands beside, and a difference that comes from the
machine rather than the build is a defect to repair, not a build record.

**A suite's own references are captured, reviewed and checked in.** Where a suite holds the engine to
references of its own making - the real-time preview's, the runtime module's contract - a capture run
sets `AFTERNIGHT_FIXTURE_CAPTURE` to a directory and `AFTERNIGHT_FIXTURE_CAPTURE_COMMIT` to the commit
the libraries were built from, and the suite writes fresh records there to be reviewed and checked in
instead of comparing.

**The harness owns what a suite runs inside.** `fixtures/harness/TestHarness.h` holds the storage
root - the runner's `AFTERNIGHT_TEST_STORAGE_ROOT`, or one of its own that it removes and forgets,
restoring the variable - with the process's temporary directory redirected into a short directory it
owns, the platform logger writing under the root, and the platform crash handler with its dumps under
the root and the suite's name and product version as annotations. `fixtures/harness/QtTestHarness.h`
adds a headless `QApplication`, the test message filter and the Fusion style, created last and
destroyed first: Qt caches the temporary directory the first time anything asks, and the application
may hold files under the root while it runs.

## The campaigns

Four campaigns run the engine under conditions a single case cannot state. Each has a registry that
says what must be run and a runner whose `--check` mode is in the common check list, so a registry
that falls behind the generated tree fails before anything is run.

| Campaign | Registry | Runner | What it holds |
|---|---|---|---|
| Fuzzing | `fuzz_targets.txt` | `run_fuzz_campaign.py` | Six engine targets - descriptor, conversion, pixel math, metadata, codec, sequence - and three over the product's documents, protocol messages and panel descriptions (`Fuzz_ConsumerDocuments`, `Fuzz_ConsumerProtocol`, `Fuzz_ConsumerPanel`), each run to a stated budget. On Clang, `AFTERNIGHT_FUZZ=1` builds them with libFuzzer over libraries instrumented for coverage, and `--libfuzzer` runs them, refusing a library without counters; elsewhere a seeded driver replays and mutates their corpora reproducibly. Each counts what it reached. |
| Sanitizers | `sanitizer_matrix.txt` | `run_sanitizer_campaign.py` | The address, leak and thread configurations over every suite and each focused pipeline. A row is refused unless both the suite and the engine beside it carry the sanitizer's runtime, and a row that ran no test fails. No suppression names this engine. |
| Faults | `fault_points.txt` | `run_fault_campaign.py` | Each of `Test_EngineFaults`' nineteen scenarios, and of `Test_ConsumerFaults`' fifteen product scenarios, runs once uninjected to count its failure points, then once per point: allocation refused, cancellation requested, I/O or an external callback failing. Every run must return a typed failure with no partial result, leak nothing, finish inside its bound, and leave a destination whole or absent. |
| Conformance | `conformance_matrix.txt` | `run_conformance_campaign.py` | Every direct suite and every case the registry names passes, read from the suite's own report rather than its exit code, because a suite that ran nothing exits zero; the registry is held to the generated tree in both directions, an `optional` suite runs when it is generated, and every skipped case is reported by name. |

All four registries and runners are in `build/scripts/tools/`, and each runner's module documentation
states the rules it holds a run to.

## The checks

The common check list in `docs/dev/LOCAL_VALIDATION.md` is run from the repository root on
Windows and Linux. Most need nothing but a checkout; three read the generated projects, and
`check_deployed_runtimes.py` reads a deployed directory. Grouped by what they protect:

| Protects | Checks |
|---|---|
| The layering and isolation | `check_include_layers.py`, `check_build_graph.py`, `check_header_architecture.py`, `check_forbidden_constructs.py`, `check_process_state.py` |
| The published surface | `check_public_api.py`, `check_public_documentation.py` |
| The documentation | `check_engine_documentation.py` |
| Cost | `check_budgets.py` |
| What is built, deployed and shipped | `check_deployed_runtimes.py`, `check_installer_payload.py`, `check_gpu_backend_optionality.py`, `remove_gpu_proof.py` |
| Dependencies and safety | `check_dependency_provenance.py`, `check_safety_register.py` |
| The product's layering, state and targets | `check_consumer_layering.py`, `check_consumer_state.py`, `consumer_targets.py check`, `product_measurement.py check` |
| The interface's capabilities | `check_shell_capabilities.py`, which fails a case the capability register names that no suite defines, a scenario named on another shell's line, a capability listed twice, one with no `observatory` line and a line of another kind |
| The campaigns' registries | the `--check` mode of each campaign runner and of `run_performance_campaign.py`, and `generate_skimage_goldens.py check` |
| The licence of every source | `check_license_headers.py`, which fails a source file that does not open with its SPDX licence and copyright lines, or states a licence other than its own |
| The links of the developer documents | `check_markdown_links.py`, which fails a link in `docs/dev/`, the contributor guides, the README or the repository's root documents that leads to no file |

Every checker in that list has a paired test script beside it, named for it with a test suffix -
`check_consumer_state_test.py` beside `check_consumer_state.py` - and run whenever the checker changes.
It plants the defects the checker's rules exist for - in a synthetic tree or a copy of the real one, never in the checkout - and proves each is reported.
