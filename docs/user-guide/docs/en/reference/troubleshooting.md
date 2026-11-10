---
kind: reference
summary: >-
  Where to start when AfterNight does not start, an action is unavailable or a result looks wrong: find the symptom,
  then its fix.
---
# Troubleshooting And Diagnostics

## Purpose, scope, audience, prerequisites, expected result, and authority

Use this index when AfterNight does not start, an action is unavailable, an
operation fails, or a result differs from what you expected. Start with the
closest symptom, perform the listed first check, and follow the linked page to
the specific recovery table. The destination page remains authoritative for
the affected control, format, process, algorithm, or workflow.

This page also defines the current diagnostic evidence available for a support
report: version and provenance, environment details, session messages, rotating
file logs, local crash dumps, and workflow reports. You need only a current
AfterNight installation and access to the bundled or online guide. Do not
reproduce a destructive failure merely to collect more evidence.

The index covers the application and navigated English guide.

## Reachability or applicability

- Open **Help > User Guide**, then select **Reference > Troubleshooting And
  Diagnostics**. The bundled guide is preferred; an allowed online fallback is
  used only when no bundled candidate opens.
- Use **About > About AfterNight** for the application version, the engine's
  version and interface generation, project contact, the application's and the
  engine's library versions, license, and recorded implementation provenance. The Dashboard **About** button opens the same dialog.
- Use **About > System Information** for an exportable operating-system,
  application/build, dependency, CPU/GPU/memory, display, and writable-storage
  snapshot.
- Use **View > Session Console** for current-session messages. Use **Edit >
  Settings > Logging** for the independent rotating file-log directory, level,
  size, and retained-file count.
- On Linux and Windows MSVC, Crashpad normally keeps local crash data under the
  per-user AfterNight data directory. There is no automatic upload in the
  default configuration.

This page is a routing reference, not a health check. Opening a diagnostic
dialog does not prove that a file, external tool, catalog, solver, extension, or
processing workflow works end to end.

## Complete reference matrix

### Startup, installation, and Help

| Symptom/exact signal | Area | First check | Verified remedy/next page | Why this route | Dependencies/applicability | Trade-off/side effect | Scope | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Windows installer or executable will not run | Package/host | Confirm x86-64, AVX2, and the packaged Windows boundary | Follow [System Requirements troubleshooting](system-requirements.md#troubleshooting) | The package and AVX2 build target define the hard host floor | Windows MSVC package | Reinstallation does not recover unsaved work | Startup | Package/build sources |
| AppImage reports permission denied | Linux package | Check the executable bit | Follow [Installation troubleshooting](../getting-started/installation.md#troubleshooting) | AppImage must be executable before launch | Linux AppImage | Changes only the file permission | Startup | Installer page/AppImage primary docs |
| AppImage reports FUSE or mount failure | Linux runtime | Check FUSE 2 compatibility support | Use the FUSE package or extract-and-run route in [System Requirements](system-requirements.md#troubleshooting) | Mounting and extracted fallback are separate launch paths | Linux AppImage | Extract-and-run creates an unpacked runtime tree | Startup | AppImage primary docs/package source |
| Startup reports a missing Qt, Python, plugin, DLL, or shared library | Deployment | Confirm the package/deploy tree is complete | Reinstall or repeat the numbered deploy step in [System Requirements](system-requirements.md#troubleshooting) | Packaged runtimes are deployed dependencies, not optional downloads | Packaged or source build | Mixing unrelated library versions can make diagnosis harder | Startup | Build/deploy sources |
| **Help > User Guide** opens the web | Help packaging | Check whether bundled HTML exists | Use [Installation troubleshooting](../getting-started/installation.md#troubleshooting) | Online Help is only the fallback after local candidates fail | Network fallback must be allowed | Web use discloses a normal network request | Application | Help resolver/package sources |
| Offline Help is unavailable and online fallback is disabled | Help policy | Check package docs content and `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` | Reinstall a release package with docs; see [Toolbar & Menus troubleshooting](../interface/toolbar-and-menus.md#troubleshooting) | Policy intentionally prevents the fallback | Offline-policy environment | No guide opens until local files are restored | Application | Help resolver tests |
| The application label/capability differs from this guide | Version drift | Record **About AfterNight** version | Confirm the current route in [Toolbar & Menus](../interface/toolbar-and-menus.md) and report the mismatch | The application/source is authoritative when a guide revision is stale | Current build and guide may differ | Do not guess renamed behavior | Application/guide | About/menu sources |
| A first-run control or onboarding card is absent | Dashboard state | Determine whether this is a fresh or returning profile | Follow [First Launch troubleshooting](../getting-started/first-launch.md#troubleshooting) | Onboarding visibility depends on persisted Dashboard state | Dashboard profile | Resetting preferences can discard customization | Dashboard | Dashboard page |

### Files, metadata, saving, and sequences

| Symptom/exact signal | Area | First check | Verified remedy/next page | Why this route | Dependencies/applicability | Trade-off/side effect | Scope | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A file appears in the chooser but does not open, with **File open error**: `Could NOT load file '<path>'. Error description: <reason>` | Decode/input | Check the final suffix, actual stream, and the reason the message gives | Use [Supported Formats troubleshooting](../getting-started/supported-formats.md#troubleshooting) | A recognized suffix is only the first codec gate | Deployed codec/decoder | Repeated open attempts do not repair a corrupt file | One input | Codec/file-manager pages |
| Camera RAW cannot be selected as a save format | Output format | Confirm the source is camera RAW and the destination is processed output | Choose FITS/TIFF/XISF via [File Formats](file-formats.md#troubleshooting) | Camera RAW is input-only | LibRaw input | Output becomes a new processed artifact | Still image | Codec matrix |
| Reopened output has different precision or channels | Numeric conversion | Compare the source sample type with the destination matrix | Use [File Formats troubleshooting](file-formats.md#troubleshooting) | PNG/JPEG/video and some XISF/SER paths convert types/channels | Format-specific | Conversion can quantize or clip | Still/sequence | Codec sources/tests |
| Saved output lacks the visible stretch, channel, mask, annotation, zoom, or overlay | Display versus data | Determine whether the change was display-only | Apply an intended data transform before [Saving And Exporting](../interface/saving-and-exporting.md#troubleshooting) | Standard save writes underlying data, not viewer presentation | Active standard view | Applying a stretch is destructive processing | Still/sequence | Viewer/save pages |
| A file opens with `the file holds samples outside the eight this engine processes` | Decode/input | Check the file's sample format | Convert the file to 8-, 16- or 32-bit integer or 32- or 64-bit floating-point samples in its own tool; see [Supported Formats troubleshooting](../getting-started/supported-formats.md#troubleshooting) | The engine holds eight sample types and refuses others rather than narrowing them | Unusual sample formats | Converting in another tool can change precision | One input | `ImageViewFiles.cpp` |
| **File save error**: `Could NOT save file '<path>'` with `the product writes FITS, XISF, TIFF, PNG and JPEG files only` | Save dispatch | Check the destination suffix | Select a supported suffix in [Saving And Exporting troubleshooting](../interface/saving-and-exporting.md#troubleshooting) | The final suffix selects the container | Writable destination | Renaming alone does not convert existing content | Still | `FilePolicies.cpp`; `FileWorkflowRunner.cpp` |
| Save refuses `a colour filter array mosaic cannot be written as PNG or JPEG` | Mosaic output | Confirm the view still holds an undebayered camera mosaic | Write FITS, XISF or TIFF, or debayer first; see [Saving And Exporting](../interface/saving-and-exporting.md#troubleshooting) | PNG and JPEG cannot state the filter array; nothing is written | Mosaic still image | Debayering is a processing step | Still | `FilePolicies.cpp`; `ImageViewFiles.cpp` |
| Sequence Save refuses `no sequence container the product writes is named by '<path>'` | Sequence save | Check the destination suffix | Choose a sequence container the product writes, or use [Sequence Export](../interface/saving-and-exporting.md#export-a-sequence) to a new file | A sequence save reads every frame and writes it again into the named container | Active sequence | A video container re-encodes and can be lossy | Sequence | `SequenceCompanions.cpp` |
| Exported video dimensions changed by one column or row | Encoder geometry | Compare the requested size with an even width and height | Follow [File Formats troubleshooting](file-formats.md#troubleshooting) | An odd video width or height is cropped by one column or row, and nothing else | Video export | The crop removes one edge column or row | Sequence export | Sequence writer source |
| `.anseq` opens here but fails after moving or on another machine | Manifest paths | Check the frame directory beside it and the referenced frames | Move the complete artifact or rebuild it via [Sequences troubleshooting](../interface/sequences.md#troubleshooting) | `.anseq` is not a self-contained archive | Image-list sequence | Rebuilding can change path layout | Sequence | Image-list source |
| Output failed or was cancelled | Failed write | Read the save or export message; check that no new file appeared | Follow [Saving And Exporting troubleshooting](../interface/saving-and-exporting.md#troubleshooting) | An image save writes a pending file beside the destination and renames it into place only when it is whole; a sequence save or export publishes its file only when every frame is written, so a failed or stopped one leaves nothing | Save/export in progress | Keep a copy of irreplaceable data anyway | File output | Save/export source/tests |
| FITS/EXIF/WCS/SIP fields are missing after reopen | Metadata | Determine source format, destination format, and geometry operations | Use [Metadata troubleshooting](../interface/fits-header-viewer.md#troubleshooting) and [File Formats](file-formats.md) | Preservation is format- and operation-specific; presence is not validity | Reopened output | A geometry change keeps the acquisition record, moves the solution with a crop, resize, or integer resample, drops it for a rotation or a filter-array resample, and drops every other card | Still | Metadata/codec pages |
| `Ctrl+N` does nothing for New Sequence | Inactive hint | Check the File menu or Dashboard card | Use [Sequences troubleshooting](../interface/sequences.md#troubleshooting) | The displayed Dashboard hint is stale; no binding exists | Dashboard/main window | None | Application | Shortcut/action sources |
| Sequence Apply/Export/Convert says `Another process is executing for this sequence, please cancel that job first before running this process` | Sequence state | Wait for or cancel the active operation once | Follow [Sequences troubleshooting](../interface/sequences.md#troubleshooting) | Mutation is guarded while another sequence operation owns the source | Active sequence job | A stopped sequence run publishes no sequence | Sequence | `ImageViewControl.cpp` |

### Interface, focus, targets, masks, and history

| Symptom/exact signal | Area | First check | Verified remedy/next page | Why this route | Dependencies/applicability | Trade-off/side effect | Scope | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Image/menu actions remain disabled | Action state | Confirm a compatible standard view is active | Follow [Interface Overview troubleshooting](../interface/overview.md#troubleshooting) | Action availability depends on view mode, still/sequence state, channels, and metadata | Active view | Selecting another view changes the command target | Main window | Action-state sources/tests |
| A process is disabled, missing, or refuses to open | Process launch | Check category, active target, channels, sequence support, and duplicate/exclusive windows | Use [Process Windows troubleshooting](../interface/process-windows.md#troubleshooting) | Registry guards and process-local validation are distinct gates | Process-specific | Closing another process can discard unsaved settings/work | Process | Registry/process pages |
| The wrong image received the operation | Target/focus | Read the process title/target selector before Apply | Use [Interface Overview troubleshooting](../interface/overview.md#troubleshooting) | Active/focused view and explicit target can differ | Multiple open views | Repeating Apply can compound damage | Image/process | App/process sources |
| A shortcut does nothing or affects another surface | Keyboard focus | Identify active window and focused control; try the visible action | Use [Keyboard Shortcuts troubleshooting](keyboard-shortcuts.md#troubleshooting) | The same chord can be window- or widget-local | Desktop may intercept keys | Repeating uncertain destructive keys is unsafe | Application/control | Shortcut source/tests/Qt docs |
| A linear image looks almost black | Display | Check whether autostretch is enabled | Follow [Image Viewer troubleshooting](../interface/image-viewer.md#troubleshooting) | Stored linear samples can be too faint for an unstretched display | Image loaded | Autostretch is display-only | View | Image Viewer page |
| Toolbar actions disappear at narrow width | Responsive toolbar | Use overflow/expand and compare display resolution | Follow [Image Viewer troubleshooting](../interface/image-viewer.md#troubleshooting) | Controls can move into overflow rather than becoming unsupported | Narrow viewport | No data effect | View/UI | Viewer/theme evidence |
| Preview and Apply differ | Preview model | Check ROI, cache, approximation, masks, sequence path, and process-specific limitations | Use [Process Windows troubleshooting](../interface/process-windows.md#troubleshooting), then the process page | Preview contracts are process-specific and not universal proof | Process target | Full Apply can cost more and change full-frame boundaries | Process | Process framework/destination page |
| A visible mask has no processing effect | Mask state | Check **Enable Mask**, assignment validity, inversion, and the process's mask support | Follow [Masks troubleshooting](../interface/masks.md#troubleshooting) | Show/render mode is independent from processing enablement | Compatible same-size mask | Inversion reverses protection/application | View/process | Mask source/tests |
| A hidden mask still changes Preview/Apply | Mask state | Check enablement rather than Show | Follow [Masks troubleshooting](../interface/masks.md#troubleshooting) | Hiding an overlay does not disable its blend | Assigned enabled mask | Disable only when unmasked processing is intended | View/process | Mask source/tests |
| Undo/Redo did not record a display change | History boundary | Decide whether pixels/sequence state actually changed | Use [Image History troubleshooting](../interface/image-history.md#troubleshooting) | Display channel, zoom, overlays, and preview/reset/cancel are not committed history steps | Standard view | Restoring history changes pixels/sequence state, not display/mask state | View | History/view sources |
| Settings rejects a temporary/log/catalog/output directory | Writable path | Check existence, creation permission, and write access | Follow [Settings troubleshooting](../interface/settings-dialog.md#troubleshooting) | Each settings page validates its own path contract | Selected location | Moving caches/logs can split evidence across paths | Application | Settings source/tests |
| Session Console omits expected older messages | Retention/filter | Check **Show >=**, visible-line cap, Clear, and when the bridge started | Follow [Session Console troubleshooting](../interface/log-viewer.md#troubleshooting) | The console and rotating file sink retain different evidence | Current session | Lower thresholds/limits render more text | Application session | Log bridge/control sources |

### Processing, resources, and Image Integration

| Symptom/exact signal | Area | First check | Verified remedy/next page | Why this route | Dependencies/applicability | Trade-off/side effect | Scope | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A process reports an unfamiliar exact error | Process | Copy the full message and locate the process in the catalog | Open the destination from [Process Catalog](../processes/index.md), then its **Troubleshooting** table | Each process owns its validation, algorithm, and recovery contract | Named process | Generic parameter guessing can hide the root cause | Process | Exact 77-destination manifest |
| **Error executing the process**: `An error occurred when running the process:` followed by a reason | Process framework | Read the reason line; it names what the engine or the process refused | Use the process page's **Troubleshooting** table via [Process Catalog](../processes/index.md) | A process window reports a failed preview or Apply under this title; the same reason is in the log | Preview/Apply | Nothing is applied when the run fails | Process | `ImageRTPreviewControl.cpp` |
| A menu action shows a message titled as the action, `Could not <what it did>: <reason>`; or a process window such as Resize, SCNR, or StarNet shows **Error running <process>** with `An error occurred when executing the <process> process:` and the reason | Immediate action or own-thread process | Read the reason; the run published nothing | Follow [Process Windows troubleshooting](../interface/process-windows.md#troubleshooting) and the process page | Every failure of an immediate action or an own-thread process is reported in a message titled as the action and in the log | Menu action or process window | None; the target is unchanged | Image/sequence | `NonInteractiveProcesses.cpp`; `GeometryProcessSupport.cpp` |
| A run you stopped shows no message | Cancellation | Check the Session Console; the stop is logged there | None needed | A stop you asked for is quiet: it is logged, not reported as an error | Any stopped run | The stopped run publishes nothing | Process/workflow | `NonInteractiveProcesses.cpp`; `ImageRTPreviewControl.cpp` |
| `the HSL adjustment needs an image of red, green and blue` (or the LCh adjustment, colour saturation, or another colour-only process) | Input type | Check whether the view is grey | Use a colour image, or choose a process that works on grey images | A colour-only process refuses a grey image by name rather than inventing colour | Grey target | None | Process | `AdjustHSLProcess.cpp` |
| A G'MIC process has Preview and Apply disabled: `This build of AfterNight carries no G'MIC library, so this process cannot run.` | Build content | Hover the disabled control to read the tooltip | Use a release package, which carries the G'MIC library | The six G'MIC processes refuse to run where the library is absent | Source builds without G'MIC | None | Process | `GmicProcessSupport.h` |
| An action reports `the admission queue is full` | Engine session | Check how much work is running or waiting | Let running work finish or cancel it, then repeat; see [System Requirements](system-requirements.md#threads-and-memory) | At most 64 requests wait for the session's threads at once | Busy session | None | Application | `SessionCore.cpp` |
| A process or action says `Another process is executing for this image, please cancel that job first before running this process` | View state | Check whether another run or an edit is still working on that view | Wait for it to show its result, or cancel it, then repeat | A view takes one run at a time, and an edit holds the image from its read until it is shown, so a second change is refused instead of reading a half-made image | Active view | None | Image | `GeometryProcessSupport.cpp`; `ImageViewControl.cpp` |
| A stage takes longer than expected or the UI appears busy | Performance | Identify stage, dimensions, channels, frame count, preview scope, external tool, and current progress | Use the process page's **Performance** and **Troubleshooting** sections via [Process Catalog](../processes/index.md) | Cost and cancellation points are algorithm-specific | Active operation | Force termination can lose partial output/state | Process/workflow | Destination page |
| Cancel disables but work continues briefly | Cooperative cancellation | Wait for the current cancellation checkpoint | Follow [Process Windows troubleshooting](../interface/process-windows.md#troubleshooting) and the process page | Workers stop at process-defined safe checkpoints | Active job | Repeated close/kill can bypass cleanup | Process | Framework/process sources |
| Memory allocation fails or the system pages heavily | Memory | Estimate decoded frames and intermediates; check System Information | Follow [System Requirements troubleshooting](system-requirements.md#troubleshooting) | Compressed source size is not decoded working-set size | Large image/project | Reducing concurrency can increase elapsed time | Host/workflow | Requirement/workflow sources |
| `the request does not fit the memory budget this engine is configured with` | Memory limit | Compare the memory asked for with the limit the message names; check **Edit > Settings > Processing** | Raise or clear **Memory Limit** and restart AfterNight, or process a smaller group; see [System Requirements](system-requirements.md#threads-and-memory) | A stated memory limit refuses work that would not fit, instead of paging | Memory Limit not Auto | A higher limit can let the system page | Host/workflow | `BudgetedMemoryResource.cpp`; `ProcessingSettingsPage.cpp` |
| Output, catalog, or temp operation reports insufficient space | Storage | Check every source/output/temp/catalog volume | Follow [System Requirements troubleshooting](system-requirements.md#troubleshooting) | Intermediates and staging can exceed final-output size | Writable volumes | Cleanup can remove reusable artifacts; preserve needed results | Host/workflow | Requirement/catalog sources |
| Output is clipped, color-shifted, blocky, or structurally wrong | Result | Stop, retain the input, and compare the process's defaults/limitations | Use the exact process page through [Process Catalog](../processes/index.md) | Color space, precision, boundary, mask, and algorithm rules differ by process | Process-specific | Reapplying can compound irreversible changes | Image/sequence | Destination process page |
| **Run Integration** remains disabled | Image Integration Lite | Read the readiness summary and pending import state | Follow [Quick Start troubleshooting](../getting-started/quick-start.md#troubleshooting) | Lite gates execution on import completion, required lights, valid output, and model validation | Lite project | Do not bypass warnings by guessing frame types | Workflow | Lite/model sources |
| **No supported image files were imported** | Image Integration import | Check suffixes, recursive folder contents, and Session Console | Follow [Quick Start troubleshooting](../getting-started/quick-start.md#troubleshooting) | Import filters unsupported/unreadable/duplicate paths | Input files | Folder import can include unrelated supported files | Workflow | Lite/import sources |
| Calibration match warning appears | Image Integration calibration | Compare type, dimensions, channels, gain/offset/exposure/temperature/filter as applicable | Follow [Image Integration troubleshooting](../processes/stacking-integration/image-integration.md#troubleshooting) | Matching policy and fallback warnings are model-driven | Loaded frame groups | A fallback match can produce inferior calibration | Workflow | Integration model/tests |
| Registration/alignment fails or stars do not match | Registration | Inspect star metrics, reference, transform, WCS/SIP, and exact error | Use [Image Integration troubleshooting](../processes/stacking-integration/image-integration.md#troubleshooting) or [Image Registration](../processes/registration-alignment/image-registration.md#troubleshooting) | Detection, matching, model fit, and resampling are separate stages | Light frames | Looser thresholds can admit false matches | Workflow/process | Registration engine/tests |
| Integration failed or was cancelled | Workflow execution | Preserve the report/artifacts and read the first stage error | Follow [Image Integration troubleshooting](../processes/stacking-integration/image-integration.md#troubleshooting) | The staged workflow records scoped errors, warnings, and completed artifacts | Executed project | Deleting artifacts removes reuse/diagnostic evidence | Workflow | Executor/reporting sources |
| A session, preset, or database `was written by a newer version of AfterNight` | Image Integration persistence | Compare the AfterNight version that wrote it | Open it with that version, or start a new session; this version refuses it and does not change it | A newer schema is refused rather than rewritten | Shared or downgraded projects | None; the stored file is left as it was | Workflow | `SessionDocument.cpp` |
| A stored session `stores <value> for <field>, which this version does not know; it was not changed` | Image Integration persistence | Read the field and value the message names | Set the field again in the window; the window starts empty and keeps its autosave off while it refuses the stored session | A stored choice no table knows is refused by name, not guessed | Edited or foreign session files | The refused session is left on disk as it was | Workflow | `SessionDocument.cpp` |
| An old integration result was reused unexpectedly | Incremental execution | Inspect manifest diff, dirty groups, artifact validity, and run summary | Follow [Quick Start troubleshooting](../getting-started/quick-start.md#troubleshooting) and full [Image Integration](../processes/stacking-integration/image-integration.md) | Valid unchanged artifacts can be reused by design | Existing project/output | Forced rerun costs time/disk and replaces artifacts | Workflow | Manifest/executor sources |

### Astrometry, catalogs, external tools, and extensions

| Symptom/exact signal | Area | First check | Verified remedy/next page | Why this route | Dependencies/applicability | Trade-off/side effect | Scope | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Astrometry controls are absent or disabled | WCS/view state | Confirm a standard still view and valid celestial WCS | Follow [Astrometry Overlays troubleshooting](../interface/astrometry-overlays.md#troubleshooting) | Overlay actions require solved still-image state | Solved still | Catalog/detection layers add rendering work | View | Viewer/WCS sources/tests |
| **No astrometric solution** appears although WCS-like cards exist | WCS validity | Check whether the cards state a complete transform, and which frame and equinox they name | Follow [Metadata troubleshooting](../interface/fits-header-viewer.md#troubleshooting), or solve the image again | Keyword presence alone is not a valid celestial transform; an image whose header states FK5, or an equinox other than J2000 and no frame, shows no solution until it is solved again | FITS-style metadata | Editing cards manually can make state less trustworthy | Still | `ImageAstrometryInfoFormatter.h`; WCS/metadata sources/tests |
| Plate solving fails | Solver | Record selected solver, hints, exact error, catalog/index state, and network policy | Follow [Plate Solving troubleshooting](../processes/registration-alignment/plate-solving.md#troubleshooting) | Local and web solvers have distinct readiness/fallback requirements | Solver/tool/service | Web solving changes the privacy/network boundary | Still | Solver settings/process sources |
| Gaia/SPCC catalog is missing, partial, or invalid | Catalog data | Read the exact descriptor/status and selected root | Follow [Settings troubleshooting](../interface/settings-dialog.md#troubleshooting) | Managed packs, regional caches, and repair states are distinct | Catalog workflow | Download/repair needs network, disk, and staging | Application/process | Catalog sources/tests |
| StarNet2 or DeepSNR is rejected | External tool | Check executable path and reported version | Follow [System Requirements troubleshooting](system-requirements.md#troubleshooting) and External Tools settings | Version detection is necessary but not end-to-end readiness | Separate installed CLI/models | Testing can consume substantial compute and temporary disk | Process/tool | CLI adapter tests |
| StarNet or DeepSNR refuses the image: `takes a grey or colour picture, not a colour filter array mosaic`, `supports grayscale and RGB images only.`, or `requires input images at least <W>x<H> pixels.` | External tool input | Check whether the view is an undebayered mosaic, has two or four channels, or is smaller than the tool's minimum | Debayer first, reduce the image to grey or RGB, or use a larger image | The tools are refused these inputs by name before the program starts | StarNet, StarNet star mask, DeepSNR | None; nothing is handed to the tool | Process/tool | `ExternalToolSupport.cpp` |
| Extension repository is unavailable or disabled by policy | Marketplace | Check network policy, cached state, and repository diagnostic | Follow [Extension Marketplace troubleshooting](../extensions/extension-marketplace.md#troubleshooting) | Live, cached, offline, and policy-disabled states are separate | Repository/network | Enabling networking changes the external boundary | Extensions | Manager/repository sources/tests |
| Extension signature/hash is missing, unknown, or failed | Package trust | Read the exact trust state; do not bypass it | Follow [Extension Marketplace troubleshooting](../extensions/extension-marketplace.md#troubleshooting) | Unknown/failed integrity remains blocked; Unsigned has a separate opt-in/confirmation path | Package install/update | Unsigned code is not authenticated | Extensions | Trust/transaction sources/tests |
| Extension shows **Install Needed**, **Repair Needed**, or **Validation Failed** | Managed environment | Expand Details and read the stored diagnostic | Follow [Using Extensions troubleshooting](../extensions/using-extensions.md#troubleshooting) | Missing, stale, and failed environments require different actions | Managed extension | Repair/reinstall can download packages and replace an environment | Extensions | Status/environment sources/tests |
| Installing or launching an extension reports `Only package/protocol/sdk version 1 manifests are supported`, or a host reports `SDK version mismatch` | Extension version | Read the package's declared package, protocol, and SDK versions | Install a release of the extension built for package format 1, protocol 1, and SDK 1 | Both hosts and every package speak those versions, and another version is refused | Extension package | None; the package is not installed or started | Extensions | `ExtensionManager.cpp`; `ExtensionHostManager.cpp` |
| Extension host becomes unresponsive, disconnects, or crashes | Hosted runtime | Let AfterNight terminate it; preserve status, logs, and any dump | Follow [Using Extensions troubleshooting](../extensions/using-extensions.md#troubleshooting) | Heartbeat, disconnect, crash, and ordinary failure have distinct recorded termination states | Hosted extension | Force-kill can lose cleanup/output | Extensions | Host/status sources/tests |
| Extension reports success but no view changed | Commit/presentation | Check whether a valid commit/presentation request was received | Follow [Using Extensions troubleshooting](../extensions/using-extensions.md#troubleshooting) | Writing a destination shared buffer is not canonical view commit authority | Hosted extension | Do not treat uncommitted memory as saved output | Extensions | Shared-buffer/host tests |

### Diagnostic and support evidence

| Evidence source | Exact route/default | What it establishes | What it does not establish | Persistence/output | Privacy review before sharing | Platform boundary |
| --- | --- | --- | --- | --- | --- | --- |
| About AfterNight | **About > About AfterNight** or Dashboard **About** | Application version, the engine's version and interface generation, project contact, the application's libraries (Qt, KDDockWidgets, SQLite) and the engine's libraries with the version each was built against and the loaded one when it differs, GPL/component licensing, and named provenance notes | Host readiness, successful processing, or complete dynamic dependency health | Read-only dialog; external links can open | Version/contact are low risk; dependency/provenance text may identify the exact build | Supported UI platforms |
| System Information | **About > System Information** | OS/build, application path/version/build type/Qt, the engine's version and its libraries' versions, CPU/GPU/memory, displays, and writable mounted storage | Workflow success, benchmark throughput, peak memory, or external-tool/catalog readiness | **Copy to Clipboard** or timestamped `.txt` **Export to File...**; export folder remembered | Remove machine names, paths, display/storage details, and other unneeded environment data | Windows/Linux fields differ |
| Session Console | **View > Session Console** | Current-session retained messages at chosen visible severity; exact exported visible text | Messages before bridge startup, cleared/trimmed/filtered entries, or complete rotating-file history | Optional visible-text `.txt`; in-memory backlog is session-only | Paths, filenames, tool output, catalog data, and other message content | Application session |
| Rotating file log | **Edit > Settings > Logging**; default `AfterNight.log` under the configured log directory | Persistent messages at the independent file minimum level; rotation defaults to 10 MiB per file and five rotated files plus the active file (up to six files total) | Session Console filtering/retention or messages below the file threshold | `logging.conf` plus active and rotated log files | Logs can contain local paths, filenames, tool output, and failures | Default directory is `%LOCALAPPDATA%\AfterNight\logs` on Windows and `$HOME/.local/share/AfterNight/logs` on Linux; configurable |
| Crashpad database | Normally `%LOCALAPPDATA%\AfterNight\CrashDumps` on Windows MSVC; `$XDG_DATA_HOME/AfterNight/CrashDumps` or `$HOME/.local/share/AfterNight/CrashDumps` on Linux; inspect `pending/` when present | A local minidump produced after a handled crash; annotations include product/version/build type/compiler and later Qt/engine-version/OS values | Root cause without exact matching symbols, or any dump when Crashpad did not initialize/capture | Local Crashpad database; no default upload | Dumps can contain process memory, paths, identifiers, and image/tool context; share only through an approved private route | Linux and Windows MSVC; application-directory fallback is possible |
| Image Integration workflow report | Completed/partially completed project run output | Inputs, validation, stages, outputs, warnings/errors, metrics, parameters, decisions, and provenance recorded by the workflow | Unrecorded external state or proof that every output is scientifically correct | HTML/Markdown report plus related artifacts | Review source paths, filenames, equipment, coordinates, notes, and output locations | Image Integration only |
| Exact error dialog/banner | Copy exact title/message before dismissing | The surfaced validation/failure branch and user-facing context | Earlier warnings, logs, or root cause beyond that branch | Usually transient unless also logged/reported | Can contain paths, filenames, service/tool output, or credentials emitted by an external package | Feature-specific |
| Minimal reproduction notes | Record version, platform, starting state, input class (not private data), exact route, settings changed, first failure, and whether it repeats | A bounded sequence another maintainer can reason about or reproduce | Permission to share private data or a general performance claim | User-created text | Remove secrets, credentials, private targets, full local paths, and proprietary extension data | State the exact platform/build |

## How to use this reference

1. Preserve the original input and any trustworthy output. If an overwrite,
   crash, or cancellation is involved, do not rerun against the same destination.
2. Record the exact visible error, active window/target, action route, and the
   first step that failed. An exact phrase is more useful than “it did not work.”
3. Find the closest symptom above and perform its first check. Follow the linked
   page, where the specific validation, process, format, or workflow owns the
   recovery details.
4. Open **View > Session Console** before reproducing once when additional
   messages are needed. Export the visible session text before Clear or exit.
5. If host/build details matter, export **About > System Information**. Add the
   application version from **About AfterNight** and only the necessary rotating
   log or workflow-report excerpt.
6. Include a crash dump only when a crash occurred, the file matches the exact
   binary/symbol build, and an approved private support route requests it.
7. Stop after one controlled reproduction if the action is destructive,
   expensive, network-backed, or risks the only copy of data.

## Implementation behavior and model

The index deliberately separates four questions: whether an action is reachable,
whether its inputs/settings are valid, whether execution completed, and whether
the output is correct. A visible menu action proves only reachability. Opening a
dialog proves only that the route worked. A completion message proves only that
the surfaced execution path returned without a reported error. Output quality,
interoperability, persistence, and performance require their own evidence.

The Session Console and rotating file sink receive messages through the logging
system but have independent thresholds, retention, and outputs. The console bridge
retains at most 10,000 session entries and the viewer can render fewer visible
blocks; its export writes only the current visible text. File logging defaults to
Info, `AfterNight.log`, 10 MiB per file, and five rotated files in addition to
the active file (up to six files total), and can be set to Off without disabling
the Session Console.

Crashpad initializes before Qt/application startup where supported, continues the
application when initialization fails, and is local-only by default. The database
location follows Local AppData on Windows MSVC and XDG/HOME data on Linux, with
an application-directory fallback. A `pending` child directory is a normal place
to look for unprocessed local dumps, but absence of a dump does not disprove a
crash. Exact matching `.pdb` or `.debug` symbols are required for reliable
symbolication.

A failure reaches you in one of a few shapes. A process window reports a
failed preview or Apply as **Error executing the process** with the reason
below it. A menu action reports a failure in a message titled as the action,
`Could not <what it did>: <reason>`, and an own-thread process window as
**Error running <process>** with the reason; both write the same reason to the
log. A view that another run or an edit still holds says so in a warning
instead of starting a second change. A file open or save reports **File open error** or
**File save error** with the path and an error description. The reason is the
engine's or the process's own sentence, and a refusal names what it refused -
the input, the setting, the version, or the limit. A run you stopped yourself
is not a failure: it is logged and shows no message. None of these messages is
parsed by AfterNight, so the words can change between releases; quote them
whole.

About AfterNight and System Information answer different questions. About is the
version/license/provenance reference. System Information is a live environment
snapshot. Neither is a substitute for the exact error, log excerpt, destination
page, workflow report, or reproducible starting state.

## Inputs, outputs, persistence, and metadata

The index itself reads no product data and changes no state. Diagnostic actions
can copy/export text, and Session Console Clear irreversibly discards the retained
in-memory backlog. Logging settings immediately reconfigure the file sink and
persist in `logging.conf`. Crash dumps and workflow reports are ordinary local
artifacts that can outlive the application session.

Support evidence can contain filesystem paths, filenames, account/service data,
hardware identifiers, mount points, image metadata, sky coordinates, extension
package information, or process memory. Redact or omit anything not required to
diagnose the issue; never publish credentials, tokens, private catalogs, or raw
crash dumps in a public report.

## Performance and resource implications

- Page lookup has no product runtime cost. Lowering log thresholds, raising the
  Session Console visible-line limit, or collecting verbose external-tool output
  increases rendering, memory, and disk use.
- File-log rotation bounds individual file size and retained count, but changing
  either setting changes disk use. A custom log directory must remain writable.
- System Information enumerates live hardware/displays/storage when opened.
  Its values are a snapshot, not a benchmark.
- Crash dumps can be large and may contain sensitive memory. Keep only what an
  approved diagnostic workflow requires.
- Reproducing a processing, catalog, network, or extension failure has the same
  cost and risk as the original operation; evidence collection does not make it
  cheaper or safer.

## Worked lookup example

- Evidence mode: **Source-and-test-derived**.
- Question: “A process closed with an error and no durable result. What should I
  collect before changing settings?”
- Lookup and conclusion: preserve the input and failed destination; record the
  exact dialog text, process title/target, and current settings; open **View >
  Session Console**, filter no higher than the message's likely severity, and
  export the visible text; then open the process through [Process Catalog](../processes/index.md)
  and use its **Troubleshooting** row. Export **System Information** only if the
  failure can depend on platform, memory, storage, dependency, or build state.
  Add the rotating file log only if the console lacks the relevant earlier
  entry. Add a crash dump only if the application actually crashed and matching
  symbols/private transfer are available.
- Limitation: this lookup validates the evidence route and boundaries. It does
  not execute a process, reproduce a failure, prove output quality, or authorize
  sharing private data.

## Warnings and limitations

- Do not convert a troubleshooting attempt into an uncontrolled parameter
  search. Change one relevant setting at a time and retain the last known-good
  input/output.
- Do not overwrite irreplaceable data while diagnosing save/export behavior.
  Image and sequence saves publish a whole file or none, but a mistaken
  destination still replaces what was there.
- A recognized suffix, detected external-tool version, Ready extension state,
  configured solver, or installed catalog is not an end-to-end success test.
- “No message” can mean a threshold, filter, retention, startup timing, or an
  unlogged branch. It is not proof that nothing failed.
- Crash dump symbolication is build-specific. Symbols from another build can
  produce misleading frames even when source files are unchanged.
- Product and package behavior can move ahead of the bundled/online guide.
  Always state both versions when reporting a documentation mismatch.
- This page was written from source, without executing a package, application
  GUI, image, sequence, solver, catalog, external tool, extension, crash,
  clipboard, or file export. Live values, actual dump creation, visual layout,
  output quality, production timing, peak memory, and cross-platform runtime
  behavior remain unclaimed.

## Troubleshooting

| Symptom/question | Verified interpretation or cause | Resolution/next page | Evidence source |
| --- | --- | --- | --- |
| No row exactly matches the symptom | The failure is owned by a more specific process/component page | Search the exact message in the guide, then use [Process Catalog](../processes/index.md) or [Toolbar & Menus](../interface/toolbar-and-menus.md) to reach the owner | Navigated destination inventory |
| Session Console has no relevant entry | The event preceded bridge startup, was cleared/trimmed/filtered, or was not logged | Check threshold and Reload once, then inspect the rotating file log and exact error text; see [Session Console](../interface/log-viewer.md#troubleshooting) | Log bridge/control/config sources |
| File log is absent | File level is Off, directory/configuration is invalid/unwritable, or no qualifying message was written | Check **Edit > Settings > Logging** and [Settings troubleshooting](../interface/settings-dialog.md#troubleshooting) | Logging settings/config sources |
| System Information export fails | Destination is cancelled or unwritable | Choose a writable new `.txt` path; copy only the needed fields if export still fails | `SystemInformationDialog.cpp` |
| No crash dump exists after a crash | Crashpad was unavailable, failed to initialize, did not capture the failure, or the path/platform differs | Preserve logs/exact build details; do not fabricate a dump; use the platform path rules above | Crash handler/app initialization sources |
| A support bundle is too large or exposes private data | Too much unfiltered evidence was collected | Send the minimal exact error, reproduction, version/platform, relevant excerpt/report, and only requested private artifacts | Diagnostic-source boundaries above |

## Related pages

- [Keyboard Shortcuts](keyboard-shortcuts.md) is authoritative for fixed
  bindings, focus/scope, conflicts, platform mappings, and inactive hints.
- [Glossary](glossary.md) defines processing, format, diagnostic, and application
  terms; destination pages retain equations and exceptions.
- [File Formats](file-formats.md) is authoritative for suffix, precision,
  compression, metadata, sequence, and save/export behavior.
- [System Requirements](system-requirements.md) owns package/platform, AVX2,
  runtime, external-tool, catalog, resource-planning, and System Information
  requirements.
- [Session Console](../interface/log-viewer.md) owns current-session filtering,
  search, retention, Clear/Reload, export, and file-log separation.
- [Process Catalog](../processes/index.md) routes every current process/tool to
  its exact controls, algorithm, errors, performance, and Troubleshooting table.

## References and provenance

- AfterNight diagnostic, logging, crash-handler, menu/dialog, and focused-test
  sources.
- Existing destination pages are the authority for each linked remedy. The
  destination manifest records the exact 77 process/tool destinations used when
  the index delegates a process-specific symptom.
- [Qt 6.10 supported platforms](https://doc.qt.io/qt-6.10/supported-platforms.html),
  [Qt QKeySequence](https://doc.qt.io/qt-6/qkeysequence.html),
  [Qt QAction](https://doc.qt.io/qt-6/qaction.html), and
  [Qt QShortcut](https://doc.qt.io/qt-6/qshortcut.html) are the primary upstream
  references for the package-compatibility and shortcut boundaries inherited
  by the linked reference pages; checked 2026-07-20.
- [AppImage FUSE troubleshooting](https://docs.appimage.org/user-guide/troubleshooting/fuse.html)
  and [AppImage quickstart](https://docs.appimage.org/introduction/quickstart.html)
  are the primary upstream launch references; checked 2026-07-20.
- Primary codec/format standards remain cited by [File Formats](file-formats.md),
  where AfterNight's actual implementation and test boundaries are reconciled.

<!-- Media is intentionally Not needed; searchable symptoms and exact links are the useful artifact. -->
