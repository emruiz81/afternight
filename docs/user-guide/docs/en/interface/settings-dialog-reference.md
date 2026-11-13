---
kind: reference
summary: >-
  What a reader looks up about the Settings dialog: when each change takes effect, what saving writes, where the
  settings and catalogs are kept, what a settings file holds, how a catalog download works, and every message.
---
# Settings Reference

This page holds what [Settings](settings-dialog.md) leaves out to stay readable beside the dialog: when each change
takes effect, what saving writes, where AfterNight keeps its settings and data, what a settings file holds, how a
catalog download works, what each part costs, the messages of the dialog, and what to do when something goes wrong.
Each section can be read on its own.

## When a change takes effect

| Page | A saved change takes effect |
| --- | --- |
| General | **Recent Files Limit:** at once. **Recent Processes Limit:** when the dialog closes. **Default Save Format:** at the next save window, the temporary folder for later work, the close question at the next close, **Clear temporary files on exit** at the next exit and start, the extension check at the next start. |
| Raw Images | At the next camera raw file read: a read already on its way keeps the settings it was asked with. |
| External Tools | At the next run of Starnet++, [Starnet++ Star Mask](../processes/star-operations/starnet-star-mask.md), Star Reduction's starless step or DeepSNR, which check the program again. |
| Interface | The theme, contrast, accent, tint, animations and wheel zoom at once, in every open window and view. The file windows at the next one. The dock lock and the main window's size and place at the next start. |
| Export | At the next image you save. |
| Processing | The thread and memory limits at the next start. **Updates per second while dragging:** at the next drag. |
| Logging | At once, and again at every start. |
| Catalogs & Data | Downloads and deletions at once, whether you save or not. The choice of roots and packs when you save. |
| Plate Solving | At the next solve. |

The raw balance of a running Image Integration does not reach the images the viewer opens meanwhile, which follow the
Raw Images page.

## What saving writes

- **The pages you opened, as they show.** The dialog makes every page when it opens, and a page reads its stored values
  only when you first show it. **Apply** and **OK** then write every page you showed, changed or not, one after the
  other, with no undo across pages. Saving a change on another page also rewrites `logging.conf`, and applies it again,
  when you showed the Logging page.
- **The checks of OK.** **OK** checks the pages you showed before it saves: General creates and tests the temporary
  folder, Logging the log folder, Processing refuses a memory limit under 1024 MB, and Raw Images the custom gains.
  External Tools, Interface, Export, Catalogs & Data and Plate Solving check nothing, so a path that fails is saved.
  **Apply** checks nothing at all.
- **Interface.** Once its four appearance choices are stored, the theme, its contrast, accent and tint change in every
  window, the animations follow, and every view takes the new wheel zoom. Its other choices wait for the next file
  window or the next start.
- **Raw Images.** Saving publishes the white balance, the gains and the rotation together, for every raw read asked for
  afterwards. Importing a settings file that states any of them publishes all three when the import is saved.
- **External Tools.** The page shows the stored paths without counting them as changes, then asks both programs for
  their versions. Whether a process can run follows the stored path and its version, which the process checks again,
  not the colour of the line on the page.
- **Catalogs & Data.** Saving writes both roots and both packs, and the folder of the response data of
  [SPCC](../reference/glossary.md#spcc), AfterNight's own `spcc_responses` folder when none is set. A saved pack that covers the pending region clears it. Downloads and
  deletions happen outside saving.
- **Plate Solving.** Saving stores the default solver, or the first one set up when the chosen one is not.
- **Nothing to an image.** No page changes an open image, its header, its
  [history](../reference/glossary.md#history), its [mask](../reference/glossary.md#mask) or the frames of a
  [sequence](../reference/glossary.md#sequence). A [plate solution](../reference/glossary.md#plate-solution) is
  written into an image by the window that solves it, which writes the solution and keeps the rest of the header.

## Where AfterNight keeps its settings and data

AfterNight's data folder is `%LOCALAPPDATA%\AfterNight` on Windows and `~/.local/share/AfterNight` on Linux.

The settings database
:   `afternight_data.db` in the data folder holds every value the dialog stores except logging, under the names the
    controls of [Settings](settings-dialog.md) give, such as `general/recentFilesLimit`. AfterNight never keeps them in
    the Windows registry. A database written in another format is set aside, renamed with that format, and the settings
    start from their defaults, with the notice "The settings database ... was set aside as ..., and the settings start
    from their defaults."

`logging.conf`
:   In the data folder: `log_directory`, `log_level`, `max_file_size_mb` and `max_files`, one per line. AfterNight reads
    it before anything else starts.

`catalogs`
:   In the data folder: the [Gaia](../reference/glossary.md#gaia) and SPCC packs the download assistant installs by
    default, each in a folder named after it.

`spcc_responses`
:   In the data folder: the response data that SPCC reads, installed by the full catalog's download or by SPCC's own
    window.

`catalog_mirrors.json` and `spcc_catalog_mirrors.json`
:   In the data folder. The first is written with the default address the first time the Gaia packs are listed, and
    the first address of its `mirrors` list is where the four Gaia packs are downloaded from, without any further
    check. The second can add SPCC packs, of which only the approved, current and safely named ones are offered.
    Whoever can edit these files chooses where AfterNight downloads catalogs from: protect them.

The temporary folder
:   The folder **Location:** names, else the `temp` folder of the data folder, with one `AN_SESSION_...` folder for each
    session.

The log
:   `AfterNight.log` and its older copies, in the folder **Directory:** names.

A settings file
:   Wherever you export it. The dialog remembers the folder of the last import or export.

## Settings files

**Export Settings...** writes a JSON file of format `1.0`, and **Import Settings...** reads only that format. A file
holds `version`, `exportDate` and these sections.

| Section | Holds |
| --- | --- |
| `general` | The General page as it shows, from `language` to `checkExtensionUpdatesOnStartup`. |
| `raw` | `whiteBalanceMode` as `none`, `daylight`, `camera_as_shot`, `auto_wb` or `custom`, `customWbR`, `customWbG`, `customWbB`, `autoRotate`, `demosaicAlgorithm`. |
| `tools` | `starnetPath`, `deepsnrPath`, `astapPath`, `astrometryLocalPath`, and `astrometryApiKey` in plain text. |
| `processing` | `maxThreadsBatch`, `maxThreadsPreview`, `gpuEnabled`, `preferredGpu`, `memoryLimitMb`, `previewFrameRate`. |
| `interface` | `theme`, `materialBackdropMode`, `themeContrastProfile`, `themeAccentFamily`, `themeSurfaceTintFamily`, `zoomSensitivity`, `zoomRequiresCtrl`, `lockLayoutByDefault`, `rememberWindowSize`, `recentProcessIds`, `favoriteProcessIds`. |
| `export` | `tiffCompression`, `xisfCompression`, `xisfByteShuffle`, `xisfChecksum`, `jpegQuality`, `pngCompression`, `fitsFloatFormat`, `embedExif`, `embedHistory`, `defaultDirectory`. |
| `logging` | `directory`, `level`, `maxSizeMb`, `maxFiles`. |
| `platesolve` | `defaultSolver`, `searchRadius`, `downsample`, `timeout`, `indexPath`, `gaiaCatalogDirectory`, `gaiaCatalogPack`, `useHints`. |

- **Values no page shows.** `maxThreadsPreview`, `materialBackdropMode`, `fitsFloatFormat`, `defaultDirectory` and
  `useHints` are stored and carried by the file, but no page of the dialog shows them, and a still-image save does not
  read `fitsFloatFormat` or `defaultDirectory`. The database also keeps interface states no page shows: the
  Dashboard's choices, the recent and favorite processes and the windows' sizes. Of these, only the recent and
  favorite processes travel in a settings file.
- **What a file leaves out.** The SPCC catalog's root and pack, its pending region, the response data's folder and the
  choices of [response profiles](../reference/glossary.md#response-profile), the extensions' settings, the Dashboard's
  choices, the optics, observer and overlay choices the Plate Solve window keeps, and the catalogs themselves. A file
  is no backup of them.
- **How an import reads.** The whole file is read at once, and nothing else: it never opens an image. A section the
  file lacks leaves the page as it is. A value of `previewFrameRate` that is no number is left out, and one outside 5
  to 60 is brought inside.
- **How an export writes.** The file is written whole to a temporary file first, and only then put in place, so a
  failed export leaves an earlier file of that name as it was.

## How a catalog download works

1. **The manifest.** The assistant fetches the pack's `catalog_meta.json` from its address, and refuses a pack whose
   name is unsafe or whose manifest does not describe it.
2. **The chunks.** It compares what is in **Install to:** with the manifest, and downloads only the chunks that are
   missing or wrong, up to eight at a time, each as a `.part` file first.
3. **The checks.** It checks the size and the SHA-256 hash of every chunk before it keeps it.
4. **The install.** A Gaia pack is assembled in a folder ending in `.installing`. The pack it replaces is moved aside
   to a folder ending in `.backup`, the new one moved in, and the old one put back if that fails. A
   `.install_complete` file marks a finished pack. An SPCC catalog is filled in place, chunk by chunk.
5. **A cancel.** The chunk on its way is removed, and so is a Gaia pack's `.installing` folder. The chunks of an SPCC
   catalog already checked are kept, and the next download takes up from there.

A Gaia pack stays usable in its old place while it is repaired. The sizes the assistant gives are estimates, and a download
needs room beyond them for the `.part` files and the second copy a repair makes. A download that a mirror refuses with
a demand to log in says so (see [Messages](#messages)), and AfterNight then reads a token from `HF_TOKEN` or
`HUGGINGFACE_HUB_TOKEN` only, never from a token file.

## What each part costs

- **Saving.** A save is a small write. The theme repaints every window and the zoom reaches every view, so **Apply**
  can pause the interface for a moment.
- **The temporary folder.** Stacking, sequences and external tools write their intermediate files there. A fast local
  disk with room keeps that work quick, and a slow, small or removable one holds it up.
- **External Tools.** The page asks for the versions away from the interface, but **Detect** asks while the dialog
  waits: up to about four seconds for each copy it tries, three to answer and one to stop.
- **Catalogs & Data.** The page reads the manifests of the roots on the interface each time it refreshes, so a slow or
  network root makes it slow to respond. It does not hash the SPCC chunks, which the assistant does. A full download or
  repair keeps the disk, the processor and the network busy for a long time: keep the computer awake, and leave the
  disk connected until it ends.
- **Plate Solving.** The settings cost nothing. A solve takes longer with a larger image, a poorer position, a wider
  radius, a deeper catalog, a slower solver to start or a slower network. Start from the defaults, and widen the search
  or deepen the catalog only when the field needs it.
- **Raw Images and Export.** The white balance is applied while a raw file is read, and a finer demosaic method takes
  longer, more so on a large frame or a sequence. Compression and quality change how long a save takes and how large
  the file is: look at the file you saved rather than expect a ratio.
- **Logging.** Trace and Debug, and a long rotation, write more to the disk.
- **Extension updates.** The check at start runs in the background and does not hold up the dialog.

## Messages

### The dialog

"Validation Error", with the reason
:   **OK** found a value it cannot store on the page now shown. Correct it and choose **OK** again, or **Cancel**.

"You have unsaved changes. Do you want to discard them?"
:   You closed the dialog with a change not saved. **Discard** loses it, **Cancel** returns to the dialog.

### General

"The temporary files directory does not exist and could not be created: ..." and "The temporary files directory is not writable: ..."
:   **OK** could not create the folder, or write its test file `.afternight_test_write` there. Choose a folder you can
    write to.

"Import Failed" with "Could not open file for reading: ..."
:   The file cannot be read: check that it exists and that you may read it.

"Import Failed" with "Invalid JSON file: ..." or "Invalid settings file format."
:   The file is not JSON, or not a JSON object. Import a file **Export Settings...** wrote, not another file renamed.

"Import Failed" with "The settings file '...' was written in format ...; this version of AfterNight reads format 1.0. Nothing was imported."
:   The file was written in another format, or "states no format version". Export the settings again from a version of
    AfterNight that writes format `1.0`.

"Import Failed" with "No recognized settings were found in the selected file."
:   The file has none of the sections above. Export a file from this version to see the layout it expects.

"Export Failed" with "Could not write the settings file: ..."
:   The folder cannot be written, or the disk is full. An earlier file of that name is left as it was.

### Raw Images

"Custom white balance multipliers must be between 0.1 and 10.0"
:   A gain lies outside the range, which the boxes themselves prevent. Set it inside the range.

### External Tools

"✗ Executable not found" or "✗ Path is not executable"
:   The path names no file, or a file the system will not run. Select the program itself, not a folder, a model or a
    library.

"Could not detect StarNet2 version. StarNet2 2.5.3 or newer is required." and "Could not detect DeepSNR version. DeepSNR 1.2.0 or newer is required."
:   The program answered neither `--machine-info` nor `--version` with a version within 5 seconds. Run it yourself with
    those options, install its package again whole, and select it again.

"StarNet2 2.4.0 detected. StarNet2 2.5.3 or newer is required."
:   The program is older than AfterNight needs, and the process stays unavailable. Install a newer version.

"The configured executable returned machine information for a product other than StarNet2." and the same for DeepSNR
:   The path names another program of the same family. Select the right one.

"⚠ Could not auto-detect. Please select manually."
:   **Detect** found no supported copy on the `PATH` or in the usual folders. Select the program with the folder
    button.

### Processing

"Memory limit of 800 MB is too low. Recommended minimum is 1024 MB."
:   **OK** refuses a limit under 1024 MB. Raise it, or tick **Auto**.

"No engine session is running."
:   The page has no running session to read the limits from. The stored limits still apply at the next start.

### Logging

"The log directory does not exist and could not be created: ..." and "The log directory is not writable: ..."
:   **OK** could not create the folder, or write its test file `.afternight_log_test` there. Choose a folder you can
    write to, or fix the permissions of its parent.

### Catalogs & Data

"Gaia catalog manifest was found, but one or more chunk files are missing or incomplete."
:   A chunk of the pack is missing or has the wrong size. Choose **Download Catalog...** and the pack, whose button reads
    **Verify & Repair**. Do not **Evict** it unless you mean to download it whole again.

"AfterNight does not read this Gaia catalog's format. Download it again with Download Catalog..."
:   The folder holds a Gaia catalog in an older layout, in tiles or with a manifest that does not describe chunks.
    Download the pack again: the assistant replaces it.

"Catalog directory does not contain a valid Gaia catalog manifest" and "Catalog directory does not contain a valid SPCC catalog manifest"
:   The folder holds files but no pack the page can read. Point the root at the folder that holds your packs. Do not
    press **Evict** while the Gaia line reads this: it deletes the folder the line describes, which can be the root
    itself.

"Gaia catalog not installed. No downloadable Gaia catalog packs are currently configured."
:   No pack is installed and none can be downloaded. Select a folder that holds a pack.

"SPCC chunk cache: 2/768 chunks cached (766 missing). Missing chunks are downloaded when needed."
:   Part of the SPCC catalog is on disk. SPCC downloads the rest when an image needs it, so stay online, or complete it
    with **Verify & Repair**, whose button then reads **Download Missing**.

"SPCC catalog metadata was found, but required metadata or sidecar files are incomplete."
:   A file the catalog describes beside its chunks is missing. Repair it with **Verify & Repair**.

"SPCC catalog not installed. Select an existing SPCC catalog directory; no approved downloadable SPCC catalog packs are currently configured."
:   No approved SPCC pack can be downloaded, so the download buttons are disabled. Select a folder that holds a catalog,
    or repair the mirror file.

"No approved downloadable SPCC regional descriptors are configured yet." and "No approved downloadable SPCC regional pack covers this pending region yet."
:   No regional pack can cover the region **Pending region:** shows. SPCC fetches what it needs when it runs. To work
    offline, install the whole catalog.

"Delete the selected SPCC catalog folder?", "This catalog is outside the AfterNight managed catalog cache. Type the exact folder path to delete it:" and "Delete 2 AfterNight-managed SPCC catalog folder(s) under:"
:   The questions of **Delete Selected** and **Delete All Downloaded Catalogs...**, each with the folder it deletes.
    Read the folder before you answer.

### Download Catalog

"This pack is already downloaded in .... Download again to verify and repair it."
:   The pack is installed. **Verify & Repair** checks every chunk and downloads only what is wrong.

"An incomplete installation was found in .... Download again to repair missing metadata." and "A catalog in an unsupported format was found in .... Download again to replace it."
:   **Verify & Repair** completes or replaces it.

"This pack has no manifest URL configured." and "Built-in Gaia downloads are unavailable. Configure a local catalog directory manually."
:   The pack has no address it can be downloaded from, and **Download** is disabled. Repair the mirror file, or select a
    folder that holds the pack on the page.

"Catalog download failed: Hugging Face requires authentication for this dataset. Make the dataset public or restart AfterNight with HF_TOKEN or HUGGINGFACE_HUB_TOKEN set."
:   The mirror answered that it needs a login. A public mirror needs none: check the mirror file. For a private mirror
    you are allowed to use, start AfterNight with one of those variables set to your token.

"Catalog download failed: ..." with a network error
:   The network or the mirror failed. Check the connection and download again. An SPCC catalog keeps the chunks it
    had already checked.

"Downloaded catalog chunk has the wrong size: ..." and "Checksum verification failed for catalog chunk: ..."
:   A chunk arrived damaged and was not kept. Download again.

Messages of the download that begin "Catalog manifest" or "SPCC catalog", such as "Catalog manifest ID mismatch: expected ..., got ..." or "SPCC catalog manifest checksum mismatch."
:   The mirror serves a manifest that is not the one the pack describes. Check the mirror file, or wait for the mirror
    to be repaired.

"Failed to create catalog root directory: ...", "Failed to move existing catalog aside: ..." and the other messages that begin "Failed to"
:   AfterNight could not write in **Install to:**, or could not move a pack it replaces, often because a file in it is
    open. Close what uses the catalog, check the permissions and the free space, and download again.

"Catalog download cancelled."
:   You stopped the download, or closed the window during it. A Gaia pack installed before is as it was, and an SPCC
    catalog keeps the chunks it had checked.

## Troubleshooting

Settings is not in the File menu
:   **Cause.** It is in the Edit menu.
:   **Fix.** Choose **Edit > Settings...**, press ++ctrl+comma++, or use the Settings button of the Dashboard.

A preview follows a dragged slider in steps, or a drag keeps the computer busy
:   **Cause.** **Updates per second while dragging:** caps the previews of a drag.
:   **Fix.** Raise it to follow the slider more closely, lower it to spare the processor and battery. One more preview
    runs when you let go.

**Apply** stored a temporary folder that cannot be used
:   **Cause.** **Apply** does not check the folder.
:   **Fix.** Correct the folder and choose **OK**, which creates and tests it.

The **R:**, **G:** and **B:** boxes are missing
:   **Cause.** They show only while **Mode:** is **Custom**.
:   **Fix.** Choose **Custom**, and return to another mode when you no longer want your own gains.

A change on Raw Images does not change the open image
:   **Cause.** The raw settings reach files read after you save them.
:   **Fix.** Close the image without saving over the raw file, and open the raw file again. To demosaic an open mosaic
    your own way, use [Debayer](../processes/calibration/debayer.md).

**Camera As-Shot** gives the same colours as **Daylight**
:   **Cause.** The file records no as-shot multipliers, so the daylight ones are used.
:   **Fix.** Look at the file's header, and try **Auto White Balance** or **Custom**.

An X-Trans raw file looks the same under every **Demosaic Algorithm:** but one
:   **Cause.** Over an X-Trans mosaic every method runs as three-pass Markesteijn, and **Bilinear (Fast, Low Quality)**
    as a fast X-Trans estimate.
:   **Fix.** Nothing to fix. The image's history names the method that ran.

**Demosaic Algorithm:** makes no difference
:   **Cause.** The image has no mosaic, or you answered **No** to "Open CFA file".
:   **Fix.** Answer **Yes** when you open a mosaic, or use [Debayer](../processes/calibration/debayer.md) to choose the
    method for one image.

A TIFF saved with JPEG compression is not lossy
:   **Cause.** JPEG compression is used for 8-bit samples only, and a deeper image is written with LZW.
:   **Fix.** Nothing to fix. Look at the compression the saved file states, and choose LZW or ZIP/Deflate when you want
    lossless files on purpose.

A saved [FITS](../reference/glossary.md#fits) holds no processing history
:   **Cause.** No file is written with one, whatever **Embed processing history in FITS header** says.
:   **Fix.** Keep the image's history in AfterNight, and do not take the box as a sign of what a file holds.

The [EXIF](../reference/glossary.md#exif) record is missing from a TIFF, PNG or JPEG
:   **Cause.** **Embed EXIF metadata in exported files** is off, or the image was read without one.
:   **Fix.** Tick the box and save again. A FITS or [XISF](../reference/glossary.md#xisf) keeps the header whatever the
    box says.

The Session Console still shows messages with **Off (Disable Logging)**
:   **Cause.** The level sets the [log file](../reference/glossary.md#log-file) only. The Session Console keeps its own
    messages.
:   **Fix.** Use the Session Console's own controls for what it shows, and look in the log folder for the file.

Logging changes were not kept, and no error appeared
:   **Cause.** The page does not report a `logging.conf` it could not write.
:   **Fix.** Check that you may write in AfterNight's data folder, then open the page again to see what was kept.

The log folder reads the default after you saved it empty
:   **Cause.** An empty folder is stored as empty, and the log goes to `AfterNight.log` in the folder AfterNight was
    started from, while the page shows the default.
:   **Fix.** Choose **Reset Page** and **OK** to store the default, and look in the start folder for what was written
    meanwhile.

StarNet2 or DeepSNR stays unavailable after saving
:   **Cause.** The path is empty, missing, cannot run, is too old or failed its check, and Settings saves it all the same.
:   **Fix.** Wait for the line under the path to name a supported version, save, and open the process window again.

SPCC's catalog is ready but its response data are missing
:   **Cause.** The response data's download, which follows the full catalog, failed, was busy, was stopped by closing the
    dialog, or no approved repository was offered. The page says nothing of it.
:   **Fix.** Use **Download Response Profiles** in the SPCC window, and read what its banner reports.

A download button is disabled
:   **Cause.** No approved SPCC pack with a usable address is offered, or, in the assistant, the chosen pack has no
    usable manifest address.
:   **Fix.** Select a folder that already holds the catalog, or repair the mirror file.

A deletion seemed to do nothing
:   **Cause.** The folder could not be removed, often because a file in it is open, or there was nothing there. No
    message says so.
:   **Fix.** Close the processes that read the catalog, check the permissions and the folder the line names, and try
    again, or remove the folder with your file manager once you are sure of the path.

ASTAP can be chosen but its solves fail
:   **Cause.** The page checks only that the program exists and can run, not its version, its star database or a solve.
:   **Fix.** Run ASTAP by itself on the image, install the star database it needs, then solve again with a good position
    and enough **Timeout:**.

Astrometry.net (local) can be chosen but its solves fail
:   **Cause.** `solve-field` exists, but the index files are missing or do not cover the field's scale, or **Local index
    files:** names the wrong folder.
:   **Fix.** Install matching index files, select their folder, and try `solve-field` by itself.

The web service is set up but its solves fail to log in or upload
:   **Cause.** Any text counts as a key, and the key and the network are tried only when a solve runs.
:   **Fix.** Check the key on your nova.astrometry.net account, check that the network allows the upload, and paste the
    key again. Never share an exported settings file.

**Default solver:** shows another solver than the one you chose
:   **Cause.** The chosen solver is no longer set up, so the first one that is took its place.
:   **Fix.** Set the solver up again, or choose another one, and save.

**OLED** cannot be chosen
:   **Cause.** **Theme:** is **Light**, and OLED is for the dark theme.
:   **Fix.** Choose **Dark** or **Follow System** first.

The dock panels did not lock when you saved
:   **Cause.** **Lock dock layout by default** is read at start.
:   **Fix.** Restart AfterNight, or lock the layout now from the main window.
