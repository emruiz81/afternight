---
kind: reference
summary: >-
  Where to turn when something goes wrong in AfterNight: each symptom with its cause and fix, or the page and section
  that answer it, and what to collect when you report a problem.
rewritten: true
---
# Troubleshooting And Diagnostics

Look a symptom up here when you do not know which page answers it. Every process page and every page of the interface
has its own "When it goes wrong", and the entries below lead to the right one. What belongs to no single page is
answered here in full: starting AfterNight and opening this guide, the folders of your profile, memory and disk,
catalogs and external tools as a whole, the Session Console and the [log file](glossary.md#log-file), and
[crash dumps](glossary.md#crash-dump). The sections follow the order in which you meet AfterNight, from its start to a
report of a problem. To find a message, type a few of its words in the guide's search box, which ++slash++ opens.

## Before you try a fix

- **Keep what you have.** Keep the original files and the last good result, and save each new result under a new
  name. A save replaces a file only once the new one is whole, but it still replaces the file you name.
- **Copy the message.** Copy its title and its whole text before you close it. A message names what was refused: the
  input, a setting, a version or a limit.
- **Change one thing at a time,** and note the values that last worked. Trying values at random can hide the cause.
- **Repeat a costly failure once.** A long run, a download or a solve on the web costs as much the second time.
  Collect what [Reporting a problem](#reporting-a-problem) lists before you try again.
- **Check the result itself.** A command that opens, a tool whose version is detected, an extension that is ready or
  a catalog that is installed shows that the work can start, not that its result is right.

## How AfterNight reports a failure

A failure reaches you in one of the shapes below. The reason in it is the engine's or the process's own sentence.
AfterNight never reads these words back, so they can change between versions: quote them whole.

"Error executing the process", with "An error occurred when running the process:" and a reason
:   A process window's **Apply** or **Take snapshot** failed, and nothing was applied. Read the process page's "When it
    goes wrong", and the [messages of a process window](../interface/process-windows-reference.md#messages).

"The preview failed", over a process window's controls
:   The process refused the preview, and says why in the window's message strip, never in a box. See
    [Process Windows](../interface/process-windows.md#when-it-goes-wrong).

"Error running …", with "An error occurred when executing the … process:" and a reason
:   A window whose run goes on after it closes, such as Crop, Resize, SCNR or Starnet++, failed and changed nothing.
    These windows report in a message box and the log only. The
    [windows that differ](../interface/process-windows-reference.md#windows-that-differ) lists them.

A message titled after a command, "Could not" and what it did, then a reason
:   A command that runs at once, such as **Image > Convert Format**, failed and changed nothing. The Session Console
    records the same sentence. See [Non-Interactive Processes](../processes/utility/non-interactive-processes.md#when-it-goes-wrong).

"Another process is executing for this image, please cancel that job first before running this process", or the same for a [sequence](glossary.md#sequence)
:   Another run, or an edit not yet shown, holds the view, and a view takes one change at a time. The message is
    titled after the window or the command you chose. Wait for that work to show its result, or cancel it in its
    progress window, then try again.

"File open error" with "Could NOT load file", or "File save error" with "Could NOT save file"
:   A file could not be read or written. The path and an "Error description:" follow. See
    [Opening and saving files](#opening-and-saving-files).

A run you stopped
:   A stop you asked for shows no message and publishes nothing. The Session Console records it, as
    `Process '<process>' cancelled by the user` for a window and `Stopped:` and the command for a command that runs at
    once.

No message at all
:   A message can sit below the Session Console's **Show >=**, date from before its record began, or reach the log
    file only. Lower **Show >=** first, then read the log file.

## Starting AfterNight

Setup refuses to install, or AfterNight closes at once without a message
:   **Cause.** The computer is not 64-bit x86, or its processor lacks the [AVX2](glossary.md#avx2) and FMA
    instructions AfterNight is built for. Setup checks only that the computer is 64-bit x86.
:   **Fix.** Run AfterNight on a computer whose processor lists AVX2 and FMA in its specification. No setting lets it
    run without them, on Windows or on Linux.

AfterNight does not start on an older Windows
:   **Cause.** The bundled Qt 6.10 needs Windows 10 version 1809 or later, and setup does not check the version.
:   **Fix.** Update Windows, or use Windows 11.

"Uninstaller not found in registry.", "Error launching the uninstaller." or "Uninstallation failed with exit code: …"
:   **Cause.** Setup found an earlier version recorded but could not remove it, and it stops without installing.
:   **Fix.** Remove the earlier AfterNight from the list of installed apps in the Windows settings, then run setup again.
    [Run the Windows installer](../getting-started/installation.md#run-the-windows-installer) describes the rest.

The AppImage reports "Permission denied"
:   **Cause.** The file lacks the permission to run, or its disk does not allow programs to run from it.
:   **Fix.** Follow [Make the AppImage executable on Linux](../getting-started/installation.md#make-the-appimage-executable-on-linux).

The AppImage does not start, and a terminal speaks of FUSE
:   **Cause.** The AppImage mounts itself with FUSE 2, which the system lacks.
:   **Fix.** Install your distribution's FUSE 2 package, `libfuse2t64` on Ubuntu 24.04. Or start it with
    `./AfterNight-<version>-x86_64.AppImage --appimage-extract-and-run`, which unpacks it to a temporary folder and runs
    it from there.

The system reports a missing library, DLL or Qt plugin
:   **Cause.** The installation is incomplete: the program was copied out of its folder, a file was removed from it,
    or the download was cut short. AfterNight needs every file it was installed with.
:   **Fix.** Run setup again, or download the AppImage again whole. Do not add library files from elsewhere: other
    versions make the fault harder to find. For a build of your own, run its deploy step again after the build, as
    [BUILD.md](https://github.com/emruiz81/afternight/blob/main/BUILD.md) says.

Starting AfterNight shows no window
:   **Cause.** A copy of AfterNight is already running, or is still closing. A second start only asks that copy to come
    to the front, then ends.
:   **Fix.** Look for its window on the task bar or on another screen, or wait until the earlier copy has closed, then
    start AfterNight again.

A box titled "Error" reads "An error occurred:" and a reason, and AfterNight closes
:   **Cause.** Something AfterNight needs at start failed. "The engine session cannot be opened:" names the engine.
:   **Fix.** Report the reason, as [Reporting a problem](#reporting-a-problem) says. A log folder that cannot be
    written does not stop AfterNight: see
    [The Session Console and the log file](#the-session-console-and-the-log-file).

The Dashboard shows recent files instead of the welcome panel on a first start
:   **Cause.** Your profile holds recent files from an earlier installation, which uninstalling leaves.
:   **Fix.** See [Start AfterNight](../getting-started/first-launch.md#start-afternight) and
    [Start with a fresh profile](../interface/dashboard.md#start-with-a-fresh-profile).

## The user guide

++f1++, **Help > User Guide** and the Dashboard's question mark open this guide from the copy installed with
AfterNight: `docs\index.html` in the installation folder on Windows, `usr/share/doc/AfterNight/index.html` inside the
AppImage on Linux. Only when no copy opens does AfterNight open the guide online, at `https://docs.afternightastro.org/`.

The guide opens online, from the network
:   **Cause.** No copy was found, or the system could not open the one it found. A package built on your own computer
    holds the guide only when the guide was built before it, and a build run from its own folder looks for the guide
    built in its repository, under `docs/user-guide/site`.
:   **Fix.** Install a package from the Releases page, which always holds the guide, and check that your system opens
    `.html` files in a web browser. The Session Console names a copy that could not be opened.

"The offline AfterNight User Guide could not be opened, and online fallback is disabled by policy."
:   **Cause.** `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` is set to `1`, `true`, `yes` or `on`, and no installed copy
    opened.
:   **Fix.** Install a package that holds the guide, or start AfterNight without the variable to allow the online
    guide.

"The AfterNight User Guide could not be opened locally or online."
:   **Cause.** No installed copy opened, and the system could not open the online address either.
:   **Fix.** Set a default web browser, or open `index.html` in the `docs` folder of the installation yourself. On
    Windows, the Start menu's **AfterNight User Guide** opens it without AfterNight.

The guide names a label or a command the window does not have
:   **Cause.** The guide and the application are of different versions. The guide's footer names its version, and
    **About > About AfterNight** names the application's.
:   **Fix.** Open the guide with ++f1++ from the AfterNight you run. Report a difference that remains, with both
    versions.

## Your profile and its folders

Your settings, recent files, logs, extensions and Image Integration sessions live in AfterNight's data folder,
`%LOCALAPPDATA%\AfterNight` on Windows and `~/.local/share/AfterNight` on Linux, and so do the catalogs unless you
chose another folder for them. Installing, uninstalling or deleting the AppImage leaves that folder as it is.
[Where AfterNight keeps its settings and data](../interface/settings-dialog-reference.md#where-afternight-keeps-its-settings-and-data)
lists what it holds.

A box titled "Settings" says "The settings database ... was set aside as ..., and the settings start from their defaults."
:   **Cause.** The settings database was written in another format, by another version of AfterNight.
:   **Fix.** Set your settings again, or import a settings file you exported. The old database stays beside the new
    one under the name the message gives.

The settings are still there after you reinstalled AfterNight
:   **Cause.** They live in the data folder, not in the installation.
:   **Fix.** Use **Reset All to Defaults** in Settings, as
    [Return to the defaults](../interface/settings-dialog.md#return-to-the-defaults) describes.

You want to know whether a problem comes from your profile
:   **Cause.** A setting, a stored layout or a session can make AfterNight behave differently from a new installation.
:   **Fix.** Close AfterNight, rename its data folder, and start AfterNight: it makes a new folder and starts as on a
    first start, without your settings, extensions or sessions, or the catalogs kept there. To return, close
    AfterNight, delete the new folder and give the old one its name back.

"The temporary files directory is not writable: …" or "The temporary files directory does not exist and could not be created: …"
:   **Cause.** The folder under **Temporary Files** in **Settings > General** cannot be created or written to.
:   **Fix.** Choose a folder on a local disk you can write to, with room for large files, and choose **OK**. See
    [Settings](../interface/settings-dialog.md#when-it-goes-wrong).

The temporary folder holds many folders named `AN_SESSION_` and fills the disk
:   **Cause.** **Clear temporary files on exit** is off, so each session leaves its folder.
:   **Fix.** Turn the option on in **Settings > General**: the next start removes the folders of sessions that have
    ended, never the folder of one still running.

## Memory and disk

An image in memory takes its width times its height times its channels times the bytes of its
[pixel type](glossary.md#pixel-type), 4 for 32-bit floating point, however small its file is. A process holds
intermediate copies beside it, and a stack holds many frames. The log's line "The engine session is open:" gives, at
each start, the threads and the memory budget in force, and **Settings > Processing** shows them as **In use now**.

"the request does not fit the memory budget this engine is configured with"
:   **Cause.** **Memory Limit:** in **Settings > Processing** is set, and the work needs more than the limit has left.
:   **Fix.** Close views you no longer need, and work on a smaller image or a smaller group of frames. Or raise the
    limit, or tick **Auto**, and restart AfterNight: a new limit applies from the next start. A limit above the free
    memory lets the system page again.

The computer slows down and its disk works without pause during a run
:   **Cause.** With **Memory Limit:** at **Auto**, AfterNight sets no limit of its own. The images and their copies
    outgrow the memory, and the system moves memory to the disk.
:   **Fix.** Close other views and programs, stack in smaller groups, or lower **Max Threads:**, which makes the work
    take longer. A **Memory Limit:** refuses work that would not fit instead of letting the system page, and Image
    Integration has its own **Limit memory usage**. **About > System Information** shows the memory in use and free.

"the admission queue is full"
:   **Cause.** 64 requests already wait for the engine's threads, and one more is refused instead of waiting.
:   **Fix.** Let running work finish, or cancel some of it, then repeat the action. With **Max Threads:** at 1, every
    operation waits its turn.

A new **Max Threads:** or **Memory Limit:** makes no difference
:   **Cause.** Both are read when AfterNight starts.
:   **Fix.** Restart AfterNight. See [Settings](../interface/settings-dialog.md#when-it-goes-wrong).

A run, a save, an edit or a download fails on a full disk
:   **Cause.** The work needs room beyond its result. Each step of an image's [history](glossary.md#history) is written
    to the temporary folder, and a run on a sequence writes a second copy of it there, under a name that starts with
    `SEQ_SWAP`. Image Integration can write calibrated and registered frames to its output folder, beside its
    [masters](glossary.md#master). A catalog download needs room for its parts, and a repair for a second copy.
:   **Fix.** Free space on each disk involved, or move the temporary folder, the output folder or the catalog folder to
    a disk with room, then run again. **About > System Information** lists the free space of every disk AfterNight can
    write to. Removing earlier results of Image Integration frees room, and the next run cannot reuse them.

"Could NOT keep the snapshot of step ..."
:   **Cause.** The disk of the temporary folder is full, and undo cannot reach that step.
:   **Fix.** See [Image History](../interface/image-history.md#when-it-goes-wrong).

## Opening and saving files

A file listed in the Open File dialog does not open, with "File open error"
:   **Cause.** Its extension is one AfterNight reads, and its content is not, or the file is damaged.
:   **Fix.** See [Supported Formats](../getting-started/supported-formats.md#when-it-goes-wrong).

"Unsupported File Type", with "AfterNight can only open supported image, RAW, and video files."
:   **Cause.** One of the selected files has an extension AfterNight does not open, and the whole selection is refused.
:   **Fix.** Select only files of [Files AfterNight opens](../getting-started/supported-formats.md#files-afternight-opens).

"the file holds samples outside the eight this engine processes"
:   **Cause.** The file stores a sample type AfterNight does not work in, and AfterNight refuses it rather than narrow
    it.
:   **Fix.** Convert the file, in the program that made it, to 8, 16 or 32-bit integers or to 32 or 64-bit floating
    point. The conversion can change its precision.

"the product writes FITS, XISF, TIFF, PNG and JPEG files only", "a colour filter array mosaic cannot be written as PNG or JPEG" or "no sequence container the product writes is named by"
:   **Cause.** The extension of the name you typed chooses a format AfterNight does not write for that image or
    sequence. Nothing is written.
:   **Fix.** See [Saving and Exporting](../interface/saving-and-exporting.md#when-it-goes-wrong). Renaming a file
    afterwards does not convert it.

The saved file has fewer bits, lacks the [stretch](glossary.md#stretch) or an overlay, or no camera raw format is offered
:   **Cause.** A save writes the image rather than its display, into a format that holds only some pixel types, and
    AfterNight writes no camera raw file.
:   **Fix.** See [Supported Formats](../getting-started/supported-formats.md#when-it-goes-wrong).

A saved and reopened file lost [header cards](glossary.md#header-card), [EXIF](glossary.md#exif) fields or its [plate solution](glossary.md#plate-solution)
:   **Cause.** Each format keeps part of them, and a change of geometry adjusts or drops the solution and the cards
    that no longer fit.
:   **Fix.** See [FITS Header, EXIF, And Astrometry Metadata](../interface/fits-header-viewer.md#when-it-goes-wrong),
    and the page of the geometry process you applied.

A save or an export failed or was stopped, and you fear for the file it was writing
:   **Cause.** A save finishes the new file before it replaces one, so a failure leaves nothing at the destination and
    a file already there as it was.
:   **Fix.** Read the message, correct what it names and save again. See
    [What saving does to an image](../getting-started/supported-formats.md#what-saving-does-to-an-image).

An exported video is one pixel narrower, or "Export Sequence Error"
:   **Cause.** A video stores an even width and height, or a frame could not be coded or written.
:   **Fix.** See [Saving and Exporting](../interface/saving-and-exporting.md#when-it-goes-wrong).

An AfterNight sequence opens here and not after it moved
:   **Cause.** An `.anseq` list names its frames in the folder beside it, and the two were separated.
:   **Fix.** Move the list and its `-frames` folder together. See
    [Sequences](../interface/sequences.md#when-it-goes-wrong).

## Views, menus and shortcuts

The image commands, or a process, are greyed
:   **Cause.** No view is active, the image is not of the kind the command needs, or another window holds the image.
:   **Fix.** See [Interface Overview](../interface/overview.md#when-it-goes-wrong) and
    [Toolbar & Menus](../interface/toolbar-and-menus.md#when-it-goes-wrong).

A command or a process changed the wrong image
:   **Cause.** Another view was active when you chose it.
:   **Fix.** Undo the step in that view, then see [Interface Overview](../interface/overview.md#when-it-goes-wrong).

A shortcut does nothing, or acts on another part of the window
:   **Cause.** The command does not fit the image, or the shortcut belongs to a view that does not have the keyboard.
:   **Fix.** See [Toolbar & Menus](../interface/toolbar-and-menus.md#when-it-goes-wrong) and
    [Keyboard Shortcuts](keyboard-shortcuts.md).

++ctrl+n++ does nothing
:   **Cause.** The Dashboard's hint names a shortcut that New Sequence does not have.
:   **Fix.** See [Dashboard](../interface/dashboard.md#when-it-goes-wrong).

A [linear image](glossary.md#linear-image) looks almost black, or toolbar buttons are missing
:   **Cause.** The **Linear** display shows samples as they are, and a narrow view moves buttons into **More**.
:   **Fix.** See [Image Viewer](../interface/image-viewer.md#when-it-goes-wrong).

The Dashboard does not come back after the last image closes
:   **Cause.** The Session Console is still open.
:   **Fix.** See [Interface Overview](../interface/overview.md#when-it-goes-wrong).

## Process windows

A process says why it cannot open, or is greyed in the menu
:   **Cause.** No image is active, the image has the wrong channels, or another window already holds the image.
:   **Fix.** See [When a window opens](../interface/process-windows-reference.md#when-a-window-opens) and the
    [troubleshooting of Process Windows Reference](../interface/process-windows-reference.md#troubleshooting).

"the HSL adjustment needs an image of red, green and blue", or the same of another color process
:   **Cause.** A process that works on color refuses a grayscale image, or one with an alpha channel, by name.
:   **Fix.** Use an RGB image. See [Adjust HSL](../processes/color-adjustment/adjust-hsl.md#when-it-goes-wrong),
    [Adjust LCh](../processes/color-adjustment/adjust-lch.md#when-it-goes-wrong) and
    [Color Saturation](../processes/color-adjustment/color-saturation.md#when-it-goes-wrong).

**Preview** and **Apply** are disabled, and their tooltip reads "This build of AfterNight carries no G'MIC library, so this process cannot run."
:   **Cause.** A build without the G'MIC library cannot run the six processes that use it. The packages of the
    Releases page carry it.
:   **Fix.** Install a release package. The six are [GRAYCstoration](../processes/noise-reduction/graycstoration.md),
    [Patch PCA Denoise](../processes/noise-reduction/patch-pca-denoise.md),
    [Wavelet Denoise](../processes/noise-reduction/wavelet-denoise.md),
    [Local Contrast Enhancement](../processes/sharpening/local-contrast-enhancement.md),
    [Local Histogram Equalization](../processes/sharpening/local-histogram-equalization.md) and
    [Sharpen Alpha](../processes/sharpening/sharpen-alpha.md).

The preview and the applied result differ
:   **Cause.** A [preview area](glossary.md#preview-area) limits the preview to part of the image, a drag shows some
    windows a reduced image, or the [mask](glossary.md#mask) on the view blends the result.
:   **Fix.** See [The preview while you drag](../interface/process-windows-reference.md#the-preview-while-you-drag) and
    [Process Windows](../interface/process-windows.md#when-it-goes-wrong), then the process's own page.

The preview lags behind a drag, or a run goes on for a moment after **Cancel**
:   **Cause.** The process is slower than the preview's rate, or the run stops only at its next stop point.
:   **Fix.** See the [troubleshooting of Process Windows Reference](../interface/process-windows-reference.md#troubleshooting).
    Do not end AfterNight to stop a run.

The result is clipped, has the wrong colors or blocky edges, or looks wrong in another way
:   **Cause.** A setting, the mask, or an image the process does not expect, such as a stretched image where it wants
    a linear one.
:   **Fix.** Undo the step in [Image History](../interface/image-history.md), read the process page's "Before you
    start" and "When it goes wrong", and change one value at a time. Applying again on the result adds to the damage.

A run takes far longer than expected
:   **Cause.** The time grows with the pixels, the channels, the frames and the work each setting asks for.
:   **Fix.** Try the settings on a preview area or a short sequence first. The process page's "Boundaries" and
    [What a run costs](../processes/stacking-integration/image-integration-reference.md#what-a-run-costs) of Image
    Integration say what costs time.

## Masks and history

A process changed the whole image, or the part the mask should protect
:   **Cause.** **Enable Mask** is off, the mask is inverted, or the process does not use a mask.
:   **Fix.** See [Masks](../interface/masks.md#when-it-goes-wrong).

The image is still protected after the mask was hidden
:   **Cause.** **Show Mask** hides the overlay only. **Enable Mask** decides whether the mask counts.
:   **Fix.** See [Masks](../interface/masks.md#when-it-goes-wrong).

Undo brought back the pixels but not the display, or a preview left no step
:   **Cause.** A step keeps the image, not the view's display, zoom or mask. A preview, a reset and a cancelled run add
    no step.
:   **Fix.** See [Image History](../interface/image-history.md#when-it-goes-wrong) and
    [What a run leaves](../interface/process-windows-reference.md#what-a-run-leaves).

## Sequences

A new sequence is refused, or a frame stops the player
:   **Cause.** The files differ in size or pixel type, or a file the list names is gone or damaged.
:   **Fix.** See [Sequences](../interface/sequences.md#when-it-goes-wrong).

A command on a sequence ends with no new sequence and no message
:   **Cause.** The run was cancelled.
:   **Fix.** See [Non-Interactive Processes](../processes/utility/non-interactive-processes.md#when-it-goes-wrong).

## Image Integration

**Run Integration** stays disabled in Image Integration Lite
:   **Cause.** The import is still running, or the banner is red.
:   **Fix.** See [Read the banner](../getting-started/quick-start.md#read-the-banner) and
    [Image Integration Lite](../processes/stacking-integration/image-integration-lite.md#when-it-goes-wrong).

An import adds no frames
:   **Cause.** None of the files is a still image AfterNight reads.
:   **Fix.** See [Add your frames](../getting-started/quick-start.md#add-your-frames).

"Missing flat calibration match", "Using fallback dark from nearby session" or another calibration warning
:   **Cause.** No master matches the lights' group, or only a looser match was found, which calibrates less well.
:   **Fix.** See [Image Integration](../processes/stacking-integration/image-integration.md#when-it-goes-wrong) and
    [How masters are matched to lights](../processes/stacking-integration/image-integration-reference.md#how-masters-are-matched-to-lights).

Lights stay unregistered, or their stars do not match
:   **Cause.** The reference holds too few stars, or the detection or the match settings leave too few pairs.
:   **Fix.** See [Image Integration](../processes/stacking-integration/image-integration.md#when-it-goes-wrong) and
    [Image Registration](../processes/registration-alignment/image-registration.md#when-it-goes-wrong). Looser match
    settings can pair the wrong stars: look at the aligned frames.

A run failed or was cancelled
:   **Cause.** A stage stopped for the reason its log gives. Groups finished before it are kept.
:   **Fix.** Keep the output folder as it is, and read the progress window's log. A run that kept a finished group
    also leaves `workflow_report.html` and `workflow_log.txt` in its `history` folder. See
    [When a run stops or fails](../processes/stacking-integration/image-integration-reference.md#when-a-run-stops-or-fails).

A run is over in seconds and writes nothing new, or reuses an older result
:   **Cause.** The output folder holds results of the same frames and settings, and the run reuses them.
:   **Fix.** See [How a run reuses work](../processes/stacking-integration/image-integration-reference.md#how-a-run-reuses-work).

"The last Image Integration session could not be restored and was kept as it is."
:   **Cause.** The stored session holds a value this version does not know, as "The session '…' stores … for …, which
    this version does not know; it was not changed." The window opens empty and saves nothing over it.
:   **Fix.** See [Image Integration](../processes/stacking-integration/image-integration.md#when-it-goes-wrong).

"The session database '…' was written in format …; this version of AfterNight reads format …", or "The saved Image Integration sessions could not be opened and were left as they are."
:   **Cause.** The sessions were saved by another version of AfterNight, or their database cannot be opened. A database
    of another format is set aside beside itself, and new sessions are saved from then on. One that cannot be opened
    is left as it is, and the window saves nothing automatically.
:   **Fix.** See [What the window keeps](../processes/stacking-integration/image-integration-reference.md#what-the-window-keeps).

## Plate solutions, catalogs and external tools

AfterNight opens, processes and saves images without a network. A catalog download, a solve on the web, the extension
repository and the check for extension updates at start use one, and a solve on the web sends the image to that
service.

**Show Astrometry** is missing, or the overlays name nothing
:   **Cause.** The image is a sequence, or its header states no solution AfterNight reads, or a catalog overlay is off.
:   **Fix.** See [Astrometry Overlays](../interface/astrometry-overlays.md#when-it-goes-wrong).

"No astrometric solution" although the header has [WCS](glossary.md#wcs) cards
:   **Cause.** The cards do not state a solution AfterNight reads, such as one in the FK5 frame or of another
    projection.
:   **Fix.** See [FITS Header, EXIF, And Astrometry Metadata](../interface/fits-header-viewer.md#when-it-goes-wrong),
    and solve the image again.

A plate solve fails, or **Solve** stays disabled
:   **Cause.** No solver is set up, the catalog or the index files do not cover the field, or the hints are wrong.
:   **Fix.** See [Plate Solving](../processes/registration-alignment/plate-solving.md#when-it-goes-wrong).

A [Gaia](glossary.md#gaia) or [SPCC](glossary.md#spcc) catalog is missing, partial or in an old layout
:   **Cause.** The pack is not where **Catalog root:** points, a chunk is missing, or a download stopped.
:   **Fix.** See [Settings](../interface/settings-dialog.md#when-it-goes-wrong) and the
    [messages of Catalogs & Data](../interface/settings-dialog-reference.md#catalogs-data).

"Catalog download failed:" and a reason
:   **Cause.** The network or the mirror failed, a chunk arrived damaged, or the catalog folder cannot be written.
:   **Fix.** See the [messages of Download Catalog](../interface/settings-dialog-reference.md#download-catalog).

StarNet2 or DeepSNR stays unavailable
:   **Cause.** The program is missing, too old, or did not answer with its version in time.
:   **Fix.** See [Set up StarNet2 and DeepSNR](../interface/settings-dialog.md#set-up-starnet2-and-deepsnr) and the
    [messages of External Tools](../interface/settings-dialog-reference.md#external-tools).

StarNet or DeepSNR refuses the image, with "not a colour filter array mosaic", "supports grayscale and RGB images only." or "requires input images at least"
:   **Cause.** The image is a mosaic not yet debayered, has neither one channel nor three, or is smaller than the tool
    takes. Nothing is handed to the tool.
:   **Fix.** See [Starnet++](../processes/star-operations/starnet.md#when-it-goes-wrong),
    [Starnet++ Star Mask](../processes/star-operations/starnet-star-mask.md#when-it-goes-wrong) and
    [DeepSNR](../processes/noise-reduction/deepsnr.md#when-it-goes-wrong).

ASTAP or Astrometry.net is set up, and its solves fail
:   **Cause.** Settings checks only that the program exists, not its star database, its index files or a key.
:   **Fix.** See the [troubleshooting of Settings Reference](../interface/settings-dialog-reference.md#troubleshooting).

## Extensions

"Extension repository unavailable:", "Unsigned Extension Blocked" or "Extension Cannot Be Installed"
:   **Cause.** The list could not be read, or a package is unsigned or failed its signature or hash.
:   **Fix.** See [Extension Marketplace](../extensions/extension-marketplace.md#when-it-goes-wrong) and the
    [troubleshooting of Extension Marketplace Reference](../extensions/extension-marketplace-reference.md#troubleshooting).
    Never install a package whose signature failed.

"Only package/protocol/sdk version 1 manifests are supported"
:   **Cause.** The package was made for another package format, protocol or SDK than the version 1 AfterNight speaks.
:   **Fix.** Install a release of the package made for this AfterNight. See the
    [troubleshooting of Extension Marketplace Reference](../extensions/extension-marketplace-reference.md#troubleshooting).

An extension's card shows a state other than **Available**
:   **Cause.** Its environment is missing, out of date or failed to prepare, or the package does not suit this
    AfterNight. A process of a package whose environment failed stays in the menu, and choosing it prepares the
    environment again.
:   **Fix.** See [Using Extensions](../extensions/using-extensions.md#when-it-goes-wrong) and the
    [troubleshooting of Using Extensions Reference](../extensions/using-extensions-reference.md#troubleshooting).

An extension's window disappears, or a launch fails with "Extension host requested unsupported SDK version"
:   **Cause.** The [extension host](glossary.md#extension-host) crashed, stopped answering or came from another build,
    and AfterNight stopped it.
:   **Fix.** See [Using Extensions](../extensions/using-extensions.md#when-it-goes-wrong). The card's
    **Last Crash / Termination** keeps the reason.

An extension reports success, and the view does not change
:   **Cause.** The extension never committed a result. Writing into its buffer is not one.
:   **Fix.** See the [troubleshooting of Using Extensions Reference](../extensions/using-extensions-reference.md#troubleshooting).

## The Session Console and the log file

AfterNight keeps two records of its messages. The [Session Console](../interface/log-viewer.md) keeps the last 10,000
messages of this session in memory, and shows those its **Show >=** lets through, up to its **Max lines**. The log file
keeps them on disk between sessions, at the level set on the **Logging** page of
[Settings](../interface/settings-dialog.md#logging), and is the record to send when the console has lost a message.

- **Where it is.** `AfterNight.log`, in the folder of **Directory:**, by default the `logs` folder of AfterNight's data
  folder. Older copies are `AfterNight.1.log`, the most recent, up to `AfterNight.5.log`, each closed at 10 MB, unless
  you changed the size and the count.
- **What a line holds.** The date and the time to the millisecond, the level, the process and thread numbers, then the
  message.
- **The start of a session.** Each session begins with `AfterNight version <version> - engine <version>`.

An older message is missing from the console, or it shows none about a failure
:   **Cause.** The message came before the console's record began, is older than its 10,000, was cleared, or sits
    below **Show >=** or past **Max lines**.
:   **Fix.** See [Session Console](../interface/log-viewer.md#when-it-goes-wrong), then read the log file.

The log file is missing, or stays empty
:   **Cause.** **Minimum Level:** is **Off (Disable Logging)**, **Directory:** was saved empty, so the log went to the
    folder AfterNight was started from, or `logging.conf` could not be written.
:   **Fix.** See the [troubleshooting of Settings Reference](../interface/settings-dialog-reference.md#troubleshooting).

The log file is in the default folder, not in the one you chose
:   **Cause.** The folder of **Directory:** is on a disk that is gone, or cannot be written. AfterNight then starts
    with the log in its default folder, and a warning in it reads "The log folder '...' cannot be written (...); the
    log is written to '...' instead." When neither folder can be written, the warning reads "Neither the log folder
    '...' nor '...' can be written", the session has no log file, and the
    [Session Console](../interface/log-viewer.md) alone keeps its messages. A folder saved with **Apply** that cannot be
    written leaves the log in its file until AfterNight closes.
:   **Fix.** Reconnect the disk, or choose a folder you can write to in **Directory:** on the **Logging** page of
    [Settings](../interface/settings-dialog.md#logging).

The console still shows messages with the log file's level at **Off (Disable Logging)**
:   **Cause.** The level sets the log file only.
:   **Fix.** See [Session Console](../interface/log-viewer.md#when-it-goes-wrong).

## Crash dumps

When the AfterNight program crashes, its crash handler, Crashpad, writes a crash dump on this computer. Nothing is
uploaded. The handler starts before anything else, and AfterNight starts all the same when it
cannot. Only the AfterNight program has one: the command-line tool `AfterNightCLI` and the extension hosts write no
dump.

| System | Folder of the dumps |
| --- | --- |
| Windows | `%LOCALAPPDATA%\AfterNight\CrashDumps\reports` |
| Linux | `$XDG_DATA_HOME/AfterNight/CrashDumps/pending` when that variable is set, else `~/.local/share/AfterNight/CrashDumps/pending` |

Each dump is a file ending in `.dmp`. Where the system names no such folder, `CrashDumps` lies beside the program on
Windows, and in an `AfterNight` folder beside it on Linux. The `CrashMetrics` folder beside `CrashDumps` belongs to the
handler.

A dump records the product, its version, the build type and the compiler, and, once the log has started, the Qt
version, the engine's version and the system. It holds what AfterNight had in memory, so it can show your user name,
your folders, file names and parts of your images, and it can be large. It is read with the symbols of the exact
release that wrote it: `AfterNight-<version>-windows-debug-symbols.zip` or
`AfterNight-<version>-linux-debug-symbols.zip` on the Releases page. Symbols of another build show misleading places
in the code.

No dump appears after a crash
:   **Cause.** The handler could not start, the program hung or was ended rather than crashed, or the crash was in the
    command-line tool or an extension host. A handler that cannot start is not reported in the log, which starts after
    it.
:   **Fix.** Reinstall AfterNight to restore `crashpad_handler.exe` or `crashpad_handler` beside the program. On Linux,
    start the AppImage from a terminal: it prints "Warning: Failed to initialize crashpad crash handler" when the
    handler cannot start. Send the log and the steps instead, and never a dump of another crash.

## Reporting a problem

Open an issue on [AfterNight's issue tracker](https://github.com/emruiz81/afternight/issues), which the **About** tab
of **About > About AfterNight** also names. Send what lets a maintainer follow the failure, and nothing private.

1. **Name the versions.** **About > About AfterNight** shows the application's version, `dev` for a build that states
   none, and the engine's version with its interface generation. Give the guide's version too when the guide is wrong.
2. **Quote the message,** its title and its whole text.
3. **Write the steps** from a known start: the system, the kind of data rather than the data itself, the commands, the
   settings you changed, the first step that failed, and whether it fails again.
4. **Export the console** before you clear it or close AfterNight: lower **Show >=** to show the lines you need, and
   choose **Export visible session text** in the [Session Console](../interface/log-viewer.md).
5. **Add the computer** when the failure can depend on it: memory, disk, display or a library. Export **About >
   System Information**.
6. **Add the log file's lines** around the failure when the console no longer has them.
7. **For Image Integration,** add the run's `workflow_report.html` and `workflow_log.txt` from its `history` folder,
   which [What a run writes](../processes/stacking-integration/image-integration-reference.md#what-a-run-writes)
   describes.
8. **Keep a crash dump** for when a maintainer asks for it, and send it then by a private route.

What each source says, and what to remove before you share it:

About AfterNight
:   The application's version, the engine's version and interface generation, and the libraries, each with the version
    it was built against and the one loaded when they differ. It says what you run, not whether the computer suits a
    task.

System Information
:   **About > System Information**, or **More actions > System info** on the Dashboard. A report of the application,
    its version, folder, Qt version and build type; the engine and its libraries; the operating system, with the display
    server and the desktop on Linux; the processor and the graphics card; the displays; the memory in use and free; and
    every disk AfterNight can write to, with its free space. It is read from the computer when the window opens, and
    says what the computer has, not how fast a task will run. **Copy to Clipboard** copies it as text. **Export to
    File...** saves it as `AfterNight_SystemInfo_<date>_<time>.txt`, in your Documents folder the first time and later
    in the folder of the last export. A file that cannot be written gives "Export Failed" with "Could not write to file:". It names your
    program folder, your disks and your displays.

The Session Console's export
:   The lines the console shows, as plain text, after **Show >=** and **Max lines**. They name files with their full
    paths, the folders AfterNight uses and what tools printed.

The log file
:   Every message at the log file's level, across sessions, with the process and thread of each. It names the same
    paths as the console.

The Image Integration report and log
:   The inputs, stages, settings, choices, results, warnings and failures of one run. They name your files and folders,
    your equipment, the target's coordinates and your session notes.

A crash dump
:   What AfterNight had in memory when it crashed. Never attach one to a public issue.

Read every file before you send it. Remove your user name, machine name, private paths, coordinates you keep private
and anything an extension printed. Never send a password, an API key, a token or a settings file you exported: it holds
the key of the plate solving web service in plain text. More messages and longer logs cost disk space and make a
report harder to read: send the part around the failure.

## Credits

- [Qt 6.10 supported platforms](https://doc.qt.io/qt-6.10/supported-platforms.html), by The Qt Company, for the
  Windows versions the bundled Qt runs on.
- [AppImage FUSE troubleshooting](https://docs.appimage.org/user-guide/troubleshooting/fuse.html) and the
  [AppImage quickstart](https://docs.appimage.org/introduction/quickstart.html), by the AppImage project, for FUSE,
  the package that provides it and `--appimage-extract-and-run`.
- [Crashpad](https://chromium.googlesource.com/crashpad/crashpad/), by the Chromium authors, the crash handler whose
  database holds the dumps.
