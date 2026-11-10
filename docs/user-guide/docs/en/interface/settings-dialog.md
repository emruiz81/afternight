---
kind: interface
menu: Edit > Settings...
summary: >-
  The Settings dialog: nine pages of preferences for the whole application, from temporary files, camera raw files and
  saving to processing limits, logging, catalogs, external tools and plate solving.
---
# Settings

Settings holds the preferences that apply to the whole application rather than to one image. Open it with
**Edit > Settings...**, ++ctrl+comma++ or the Settings button of the [Dashboard](dashboard.md). Some windows open it on
the page they need: Plate Solve and Photometric Color Calibration on **Plate Solving**, Spectrophotometric Color
Calibration on **Catalogs & Data**, and DeepSNR and the two Starnet++ windows on **External Tools**. The rest of
AfterNight waits while the dialog is open. No page needs an open image, and saving a page never changes one: a setting
reaches the next file you open, save or process. Only the catalog buttons act at once, because they download and delete
files on disk.

```figure
owed: true
image: ../assets/images/interface/settings-dialog.webp
alt: >-
  The Settings dialog on its General page. On the left, a search box over a list of nine pages with icons, General
  highlighted. On the right, the title General with a Reset Page button at the far right, then boxes of settings: Basic
  Settings with a greyed Language box, a framed note that localization is not implemented, Default Save Format at TIFF,
  Recent Files Limit at 20 files and Recent Processes Limit at 10 processes, then Behavior with two ticked boxes, and
  Updates. Reset All to Defaults sits at the bottom left, and Apply, Cancel and OK at the bottom right.
caption: The Settings dialog as it opens, on the General page of a new profile.
pins:
  - {n: 1, x: 19.0, y: 10.8}
  - {n: 2, x: 19.0, y: 33.0}
  - {n: 3, x: 40.0, y: 10.9}
  - {n: 4, x: 40.0, y: 15.4}
  - {n: 5, x: 21.0, y: 93.5}
  - {n: 6, x: 64.5, y: 93.5}
```

## What you see

1. **Search settings...** Narrows the list to the pages whose title or keywords hold what you type.
2. **The pages.** General, Raw Images, External Tools, Interface, Export, Processing, Logging, Catalogs & Data and
   Plate Solving. The dialog opens on General, unless a window opened it on another page.
3. **The page's title and Reset Page.** **Reset Page** returns the settings of the page shown to their defaults.
4. **The page's settings.** Grouped in boxes, with notes in framed panels. A page longer than the dialog scrolls.
5. **Reset All to Defaults.** Returns every page to its defaults.
6. **Apply, Cancel and OK.** Save and stay, leave without saving, or check, save and close. While a page holds a change
   you have not saved, the title reads `Settings *`.

## What you can do

### Change a setting

1. Choose a page in the list. It reads its stored values the first time you open it, and again whenever you return to
   it without unsaved changes.
2. Change what you need. The title gains its asterisk and **Apply** is enabled.
3. Choose **OK** to check the folders and values you set, save and close. **Apply** saves and keeps the dialog open,
   without those checks.
4. To leave without saving, choose **Cancel** or press ++esc++, then answer **Discard** to "You have unsaved changes. Do
   you want to discard them?".

Saving writes every page you opened in this dialog, not only the one you changed. A page you only looked at is stored
as it shows, so a default it displays becomes a stored value. Most changes reach the next file, preview or save, and
the thread and memory limits wait for the next start: [Settings Reference](settings-dialog-reference.md) lists when
each one takes effect.

### Find a page

Type a word in **Search settings...**: part of a page's title, or a word such as `Gaia`, `Threads`, `EXIF` or `Theme`.
The list keeps the pages that match, and moves to the first of them when the page shown no longer matches. Clear the
box to see all nine again.

### Set up StarNet2 and DeepSNR

Install each tool first, with all the files its installer provides.

1. Open **External Tools** and choose **Detect** beside **StarNet2:**, or the folder button to select the program
   yourself. The line under the path reads "Checking StarNet2 version...", then "✓ StarNet2 2.5.3 detected" or what is
   wrong.
2. Do the same beside **DeepSNR:**.
3. Choose **OK**. The windows of [Starnet++](../processes/star-operations/starnet.md),
   [Starnet++ Star Mask](../processes/star-operations/starnet-star-mask.md) and DeepSNR check the saved program again
   before they run.

A path whose check fails is saved all the same, and the process stays unavailable until the line names a supported
version.

```figure
owed: true
image: ../assets/images/interface/settings-dialog-external-tools.webp
alt: >-
  The External Tools page. Under the title AI Star Removal and Noise Reduction, a StarNet2 path ending in starnet2.exe
  with a folder button and a Detect button, a green line reading StarNet2 2.5.3 detected and a framed note on the
  version required, then the same for DeepSNR, with a green line reading DeepSNR 1.2.2 detected.
caption: External Tools with StarNet2 and DeepSNR found and their versions supported.
```

### Install a Gaia catalog

The internal plate solver and [PCC](../reference/glossary.md#pcc) need a pack of the
[Gaia](../reference/glossary.md#gaia) DR3 catalog on disk. **Configure Gaia Catalog...** in Photometric Color
Calibration opens the dialog on **Plate Solving**: the packs are on **Catalogs & Data**, as the note of that page says.

1. Open **Catalogs & Data** and choose **Download Catalog...** under **Gaia DR3 Astrometric Catalogs**.
2. In **Download Catalog**, choose a pack and read **Size:**. Check **Install to:**: it starts at AfterNight's own
   `catalogs` folder, whatever **Catalog root:** says on the page.
3. Choose **Download**. The window fetches the pack's manifest, downloads its chunks and checks each one, and
   **Cancel Download** stops it.
4. When it reads "Catalog installed to ...", choose **Close**. The page's **Catalog root:** and **Catalog pack:** now
   name the pack, and the line under the root reads "Selected Gaia catalog: Gaia DR3 G<=14 (gaia_dr3_g14)" or the
   pack you chose.
5. Choose **OK**. The files were on disk already: saving keeps the page's choice of pack.

!!! danger "Evict deletes without asking"
    **Evict** deletes at once, with no question, the folder that the line under **Catalog root:** describes. When that
    line reads "Catalog directory does not contain a valid Gaia catalog manifest", the folder can be the root itself,
    with everything in it: the catalog of [SPCC](../reference/glossary.md#spcc) too when it shares the folder, as it
    does by default. Read the line before you press it.

```figure
owed: true
image: ../assets/images/interface/settings-dialog-catalogs.webp
alt: >-
  The Catalogs and Data page. Under Gaia DR3 Astrometric Catalogs, a Catalog root path ending in catalogs, a green line
  naming the selected Gaia catalog G<=14, a Catalog pack box, Download Catalog and Evict buttons and a framed note.
  Under SPCC Calibration Data, a second Catalog root path with an amber line saying the SPCC catalog cache is empty, a
  Catalog pack box, four buttons in two rows and a framed note about on-demand catalog fetches.
caption: Catalogs & Data with the Gaia DR3 G<=14 pack installed and nothing downloaded for SPCC yet.
```

### Prepare the data of SPCC

SPCC downloads the parts of its catalog an image needs when it runs, so most of the time nothing needs doing here.

- **To work offline**, choose **Download Full Offline Catalog...**. The same assistant installs the whole catalog, about
  42 GB, and then, in the background, the [response profiles](../reference/glossary.md#response-profile) SPCC reads.
  Keep the dialog open for a moment after the catalog is installed, until those have arrived.
- **To fetch one region**, use **Download Needed Region** in the SPCC window, which records the image's region and opens
  this page. **Pending region:** shows it, and **Download Matching Region...** installs an approved regional pack that
  covers it, when one exists.
- **To repair a pack**, choose **Verify & Repair** and pick the pack in the assistant.

Save afterwards, as for Gaia.

### Set up plate solving

Open **Plate Solving**. The internal solver is ready once a Gaia pack is ready and saved on **Catalogs & Data**. For
ASTAP, choose **Detect** or select its program under **ASTAP**. For the web service, paste the key of your
nova.astrometry.net account in **Web API key:**. Then choose the solver to try first in **Default solver:**. It offers
only the solvers that are set up, and the others are tried after it. The warning of the Plate Solve window names
External Tools, but its **Configure...** button opens this page, where the solvers are.
[Plate Solving](../processes/registration-alignment/plate-solving.md) says how each solver uses these values.

### Move your settings to another computer

1. On **General**, choose **Export Settings...** and save the file. It holds your tool paths and the web API key in plain
   text: keep it private.
2. On the other computer, open Settings, stay on **General** and choose **Import Settings...**.
3. Close the "Import Successful" message and choose **OK** at once, before you open another page.
4. Open Settings again to check the values.

The logging values of the file are written and applied as soon as you import it, and **Cancel** does not take them back.
The file holds no catalog and no SPCC choice.

### Return to the defaults

**Reset Page** and **Reset All to Defaults** ask first, then fill the page, or every page, with its defaults. Nothing is
stored until you choose **Apply** or **OK**, and **Cancel** still discards them. The defaults are the ones the blocks
below list, with one exception: **Reset Page** on Raw Images sets **Mode:** to **None (No correction)**, where a new
profile starts at **Daylight**.

## Controls

### The dialog

```controls
window: The dialog
controls:
  - name: Search settings...
    kind: field
    default: ""
    effect: >-
      Shows only the pages whose title, or one of whose keywords, holds what you type, in any case. When the page shown
      is hidden, the dialog moves to the first page left.
    range: >-
      Each page has keywords of its own: **General** answers to `Temp`, `Import` or `Startup`, **Raw Images** to
      `Debayer` or `White Balance`, **External Tools** to `StarNet` or `Noise Reduction`, **Processing** to `Threads`,
      `Memory` or `GPU`, **Catalogs & Data** to `SPCC` or `Gaia XP`, and **Plate Solving** to `Gaia`, `ASTAP` or
      `API Key`.
    tradeoff: >-
      Some keywords name what the page does not hold: **Raw Images** answers to `sRGB`, `Adobe RGB` and `ProPhoto`, and
      **Interface** to `Backdrop`, `Mica` and `Acrylic`. A word no page matches empties the list.
    persistence: It changes no setting, and is empty each time the dialog opens.
  - name: Pages
    kind: radio
    default: General
    options: [General, Raw Images, External Tools, Interface, Export, Processing, Logging, Catalogs & Data, Plate Solving]
    effect: >-
      Shows a page. A page reads its stored values the first time you open it, and again each time you return to it
      while it holds no unsaved change. A page with unsaved changes keeps them while you visit others.
    enabled: The dialog opens on **General**, or on the page the window that opened it asks for.
    tradeoff: Every page you open is written when you save, as it shows, even when you changed nothing on it.
    persistence: The dialog does not remember the last page it showed.
  - name: Reset Page
    kind: button
    default: ""
    effect: >-
      Asks "Are you sure you want to reset all settings on the "General" page to their default values?", with the
      page's name, and on **Yes** fills the page with its defaults.
    enabled: For every page.
    tradeoff: The defaults are only shown. **Apply** or **OK** stores them, and **Cancel** discards them.
    note: Its tooltip reads "Reset all settings on this page to defaults". **No** is the default answer.
  - name: Reset All to Defaults
    kind: button
    default: ""
    effect: >-
      Asks "Are you sure you want to reset ALL settings to their default values?" and, on **Yes**, fills all nine pages
      with their defaults, the pages you have not opened as well.
    tradeoff: >-
      The question adds "This action cannot be undone.", but nothing is stored until **Apply** or **OK**, and **Cancel**
      still discards it all. Saving then writes all nine pages.
    note: >-
      **No** is the default answer. Catalogs already on disk stay there, and the catalog folders return to AfterNight's
      own `catalogs` folder.
  - name: Apply
    kind: button
    default: ""
    effect: >-
      Saves every page you opened and keeps the dialog open. The theme, the animations and the wheel zoom of open views
      change at once, and a lower **Recent Files Limit:** trims the list at once.
    enabled: While a page holds an unsaved change.
    tradeoff: >-
      It skips the checks of **OK**, so it can store a temporary or log folder that does not exist or cannot be
      written.
    scope: Each page is saved on its own, in the order of the list, with no undo across pages.
  - name: Cancel
    kind: button
    default: ""
    effect: >-
      Closes the dialog without saving. With unsaved changes it asks "You have unsaved changes. Do you want to discard
      them?": **Discard** closes, and **Cancel**, the default answer, returns to the dialog.
    range: ++esc++ and the window's close button do the same.
    tradeoff: >-
      What a page did at once stays done: a catalog downloaded or deleted, the logging values of an imported file, a
      log folder **Open Log Directory** created.
  - name: OK
    kind: button
    default: ""
    effect: Checks every page you opened, saves them all and closes the dialog.
    range: >-
      General checks the temporary folder, Logging the log folder, Processing the memory limit and Raw Images the custom
      gains. The first page that fails is shown, with "Validation Error" and the reason, and nothing is saved.
    enabled: Always. It is the default button, which ++enter++ presses.
    tradeoff: The folder checks create a folder that does not exist yet.
  - name: Settings
    kind: readout
    default: Settings
    effect: The dialog's title. It reads `Settings *` while a page holds an unsaved change.
```

### General

```controls
window: General
controls:
  - name: "Language:"
    kind: combo
    default: System Default
    options: [System Default, English]
    effect: Kept for a later release. It changes nothing.
    enabled: >-
      Never. Its tooltip reads "Language selection is not yet implemented", and the note under it "Localization is not
      yet implemented".
    persistence: Stored as `general/language`.
  - name: "Default Save Format:"
    kind: combo
    default: TIFF
    options: [TIFF, FITS, PNG, XISF, JPEG]
    effect: Chooses the file type a save window offers first when you save an image.
    scope: >-
      Save windows of still images. A sequence is offered `.ser` or `.anseq` whatever this says, and the type you choose
      in the save window wins.
    persistence: Stored as `general/defaultSaveFormat`.
  - name: "Recent Files Limit:"
    kind: spinbox
    default: "20"
    widget: {min: 1, max: 100, step: 1}
    effect: How many files **File > Open Recent** and the Dashboard keep, with the suffix `files`.
    tradeoff: >-
      Saving a lower limit drops the oldest files from the list, with their thumbnails, at once, and refreshes the
      menu and the Dashboard. The files stay on disk.
    persistence: Stored as `general/recentFilesLimit`.
  - name: "Recent Processes Limit:"
    kind: spinbox
    default: "10"
    widget: {min: 1, max: 100, step: 1}
    effect: How many processes the recent processes menu keeps, with the suffix `processes`.
    tradeoff: >-
      The list is trimmed when the dialog closes, oldest first, however it closes. Favorites and process presets are
      not touched.
    persistence: Stored as `general/recentProcessesLimit`.
  - name: Confirm before closing unsaved images
    kind: check
    default: "On"
    effect: >-
      On, closing an image with unsaved changes, or AfterNight while such an image is open, asks first. Off, they close
      without asking.
    tradeoff: Off makes it easy to lose an edit you have not saved.
    persistence: Stored as `general/confirmCloseUnsaved`, and read each time an image or AfterNight closes.
  - name: Clear temporary files on exit
    kind: check
    default: "On"
    effect: >-
      On, AfterNight deletes its temporary files when it closes, and again in the background when it next starts. Off,
      they stay.
    tradeoff: Off keeps intermediate files you may want, and fills the disk over time.
    persistence: Stored as `general/clearTempOnExit`.
    note: >-
      Scratch files that an ended or crashed session left in the system's temporary folder are removed at start, once
      they are old enough, whatever this says.
  - name: Check for AfterNight updates on startup
    kind: check
    default: "Off"
    effect: Kept for a later release. AfterNight does not look for its own updates.
    enabled: Never. Its tooltip reads "Application update checks are not implemented yet."
    persistence: Stored as `general/checkAppUpdatesOnStartup`.
  - name: Check for extension updates on startup
    kind: check
    default: "On"
    effect: >-
      On, AfterNight checks the official extension repository in the background a moment after it starts, and says when
      an extension has an update. Off, it does not look.
    tradeoff: The check uses the network.
    persistence: >-
      Stored as `general/checkExtensionUpdatesOnStartup`, and read at the next start. The update message's check box
      "Disable extension update checks on startup" turns it off too.
  - name: "Location:"
    kind: field
    default: The system's temporary folder, then AfterNight
    effect: >-
      The folder where AfterNight writes the intermediate files of its work, such as temporary sequences, each session
      in a folder of its own whose name starts with `AN_SESSION_`. Later work writes there.
    range: >-
      A folder path, which the folder button or typing sets. **OK** creates a folder that does not exist and writes a
      test file, `.afternight_test_write`, which it removes at once. It refuses with "The temporary files directory
      does not exist and could not be created: ..." or "The temporary files directory is not writable: ...". An empty
      box passes.
    tradeoff: >-
      A slow, small or removable disk slows or breaks the work that writes most, such as stacking and sequences. The
      note under the box reads "Used for intermediate processing files. Ensure sufficient disk space."
    persistence: >-
      Stored as `general/tempFilesLocation`. Until a folder is stored, AfterNight works in the `temp` folder of its data
      folder while the box shows the system's temporary folder, and saving the page, changed or not, stores what the box
      shows. **Reset Page** puts back the system's temporary folder.
  - name: Import Settings...
    kind: button
    default: ""
    effect: >-
      Reads a settings file and fills the pages with its values, to be stored by **OK**. "Import Successful" then says
      "Settings have been imported. Click OK to apply them."
    range: >-
      A JSON file of format `1.0`, as **Export Settings...** writes it, with the sections `general`, `raw`, `tools`,
      `processing`, `interface`, `export`, `logging` and `platesolve`. A section it lacks stays as it is. Another format
      is refused, and nothing is imported, with "The settings file '...' was written in format 2.0; this version of
      AfterNight reads format 1.0. Nothing was imported." or "... states no format version; ...".
    tradeoff: >-
      Values another page shows in this dialog are written over the imported ones when you save, so import in a newly
      opened dialog and choose **OK** at once. The file can set your tool paths and your web API key.
    scope: >-
      The `logging` section is written to the logging configuration and applied as soon as the file is read, and
      **Cancel** does not take it back. The other sections are stored by **OK** or **Apply**, and imported raw settings
      reach the next raw file you open.
    persistence: >-
      The open window, "Import Settings", starts in the folder of the last import or export, else in your Documents
      folder, with the filter `JSON Files (*.json)`.
  - name: Export Settings...
    kind: button
    default: ""
    effect: >-
      Writes the settings to a JSON file, `afternight_settings.json` unless you name it otherwise, and says "Settings
      have been exported to:" with its path.
    range: >-
      `.json` is added to a name without it. The file holds format `1.0`, the time of the export, the General page as
      it shows, and the stored raw, tool, processing, interface, export and plate solving values with the logging
      configuration, the recent and favorite processes and the Gaia catalog's folder and pack among them. Unsaved
      changes of the other pages are not in it.
    tradeoff: >-
      It is no complete backup: it leaves out the SPCC catalog, its pending region and response data, the extensions'
      settings and the Dashboard's choices. It holds your folders and the web API key in plain text.
    persistence: >-
      The file is written whole or not at all. A file that cannot be written gives "Export Failed" with "Could not write
      the settings file: ..." and leaves an earlier file of that name as it was.
```

### Raw Images

These settings reach the camera raw files read after you save them, on every path that opens one: **File > Open**, a
drop, a recent file and an image preview. An image already open, or one being read at that moment, keeps the settings
it was read with.

```controls
window: Raw Images
controls:
  - name: "Mode:"
    kind: combo
    default: Daylight
    options: [None (No correction), Daylight, Camera As-Shot, Auto White Balance, Custom]
    effect: >-
      Where the white balance of a camera raw file comes from. **None (No correction)** uses equal gains,
      **Daylight** the camera's daylight multipliers, **Camera As-Shot** the multipliers the camera recorded for the
      shot, **Auto White Balance** a measurement of the frame, and **Custom** the three gains below.
    range: >-
      **Camera As-Shot** falls back to the daylight multipliers when the camera recorded none. **Auto White Balance**
      measures a mosaic as a grey world, with both greens summed as one channel.
    tradeoff: >-
      Every choice but **None (No correction)** changes the colours a raw file opens with. **Camera As-Shot** and
      **Auto White Balance** depend on what the file records and contains.
    scope: >-
      On a colour-filter-array sensor the balance is applied to the mosaic when the file is read. On a full-colour
      sensor the raw decoder applies it, and a full-colour frame, such as a linear DNG, opens with each colour in its own
      plane. The note under the group says so.
    persistence: >-
      Stored as `raw/whiteBalanceMode`. A new profile starts at **Daylight**, and **Reset Page** sets **None (No
      correction)**. A stored value the page does not know reads as **Daylight**, as every open reads it.
    note: >-
      Every mode stretches a code at the sensor's white level to full scale, and the gains are computed in double
      precision. A raw read never brightens a frame otherwise: the page has no automatic brightness.
  - name: "R:"
    kind: spinbox
    default: "1.000"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 3}
    effect: The red gain of **Custom**. Higher strengthens red.
    enabled: Shown only while **Mode:** is **Custom**.
    tradeoff: A strong ratio between the gains gives a cast, or clipping, that is hard to remove later.
    persistence: Stored as `raw/customWbR`, with six decimals. A stored value of zero or less reads as 1.
    note: >-
      **OK** refuses a gain outside 0.1 to 10 with "Custom white balance multipliers must be between 0.1 and 10.0", which
      the box itself already prevents. A gain below 0.2 or above 5 is accepted without a warning.
  - name: "G:"
    kind: spinbox
    default: "1.000"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 3}
    effect: The green gain of **Custom**. It changes the colour and the overall brightness.
    enabled: As **R:**.
    persistence: Stored as `raw/customWbG`, as **R:**.
  - name: "B:"
    kind: spinbox
    default: "1.000"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 3}
    effect: The blue gain of **Custom**. Higher strengthens blue.
    enabled: As **R:**.
    persistence: Stored as `raw/customWbB`, as **R:**.
  - name: Auto-rotate based on EXIF orientation
    kind: check
    default: "On"
    effect: >-
      On, a camera raw frame is turned by the orientation its camera recorded, any of the eight, the mirrored ones
      included. Off, it keeps the orientation the decoder gives it.
    scope: Colour-filter-array and full-colour sensors alike. It does not apply to other formats.
    tradeoff: A quarter turn swaps the width and height, which counts when frames of one night must match.
    persistence: Stored as `raw/autoRotate`.
  - name: "Demosaic Algorithm:"
    kind: combo
    default: RCD (Ratio Corrected Demosaicing) - Recommended
    options:
      - RCD (Ratio Corrected Demosaicing) - Recommended
      - AMaZE (Aliasing Minimization and Zipper Elimination)
      - LMMSE (Linear Minimum Mean Square Error)
      - AHD (Adaptive Homogeneity-Directed)
      - VNG4 (Variable Number of Gradients)
      - Bilinear (Fast, Low Quality)
    effect: >-
      The method AfterNight demosaics a colour-filter-array image with when you answer **Yes** to "Open CFA file",
      whatever opens it: **File > Open**, a drop, a recent file or a new sequence.
    range: >-
      Every method is written for a two-by-two Bayer mosaic. An X-Trans raw file opens as a six-by-six mosaic and is
      demosaiced in camera RGB: **Bilinear (Fast, Low Quality)** by a fast X-Trans estimate, every other choice by
      three-pass Markesteijn. The image's history names the method that ran.
    tradeoff: >-
      **Bilinear (Fast, Low Quality)** is fast and soft. The others take longer for finer colour detail, as the note
      under the box says: "Higher quality algorithms may significantly increase loading time." Keep the original raw
      files and compare fine detail before you settle on a method.
    scope: Not used for an image without a mosaic, or when you answer **No**.
    persistence: Stored as `raw/demosaicAlgorithm`. A stored method the page does not know reads as RCD.
```

### External Tools

```controls
window: External Tools
controls:
  - name: "StarNet2:"
    kind: field
    default: ""
    effect: >-
      The StarNet2 command-line program that Starnet++, Starnet++ Star Mask and the starless step of Star Reduction run,
      under **AI Star Removal and Noise Reduction**. It reads "Path to StarNet2 CLI executable..." while empty.
    range: >-
      StarNet2 2.5.3 or newer, `starnet2.exe` on Windows and `starnet2` elsewhere. The note under it reads "StarNet2 CLI
      2.5.3 or newer is required. Current installers provide starnet2, ONNX Runtime/CoreML models, and native
      mask/unscreen outputs."
    tradeoff: >-
      The check runs the program. A supported version says only that the program answers: it does not check its
      models, its runtime or what it produces. Install it from a source you trust and keep it with the files of its
      package.
    persistence: Stored as `tools/starnetPath`, empty after **Reset Page**. A path that fails its check is saved as well.
  - name: "DeepSNR:"
    kind: field
    default: ""
    effect: >-
      The DeepSNR command-line program that DeepSNR runs. It reads "Path to DeepSNR CLI executable..." while empty.
    range: >-
      DeepSNR 1.2.0 or newer, `deepsnr.exe` on Windows and `deepsnr` elsewhere. The note under it reads "DeepSNR CLI
      1.2.0 or newer is required. Current installers provide deepsnr and model weights in the DeepSNR support folder."
    tradeoff: As for StarNet2. The page neither chooses nor checks the model weights DeepSNR uses.
    persistence: Stored as `tools/deepsnrPath`, empty after **Reset Page**. A path that fails its check is saved as well.
    note: >-
      The DeepSNR window checks the program again, for up to 3 seconds, and reads "DeepSNR is configured and ready to
      use." where this page says "✓ DeepSNR 1.2.2 detected".
  - name: Status line
    kind: readout
    default: ""
    effect: >-
      The line under each path. It reads "Checking StarNet2 version..." while the program is asked, then "✓ StarNet2
      2.5.3 detected" in green, or what is wrong in red.
    range: >-
      "✗ Executable not found", "✗ Path is not executable", "Could not detect StarNet2 version. StarNet2 2.5.3 or newer
      is required.", "StarNet2 2.4.0 detected. StarNet2 2.5.3 or newer is required." or "The configured executable
      returned machine information for a product other than StarNet2.", and the same for DeepSNR. An empty path shows
      no line.
    enabled: >-
      The page asks both programs when you open it, and again when you change a path, in the background, so the dialog
      stays responsive. A result is shown only while the path is still the one asked about.
    note: >-
      Each program is asked for `--machine-info`, and for `--version` when that answer is not machine information, both
      within 5 seconds. A program that runs out of time is stopped, which can take one second more.
  - name: Browse...
    kind: button
    default: ""
    effect: >-
      The folder button beside each path. It opens "Select StarNet2 Executable" or "Select DeepSNR Executable" to choose
      the program.
    range: >-
      It starts in the folder of the path, or the nearest folder of it that exists, else in your home folder. On
      Windows its filter is `Executable files (*.exe)`.
  - name: Detect
    kind: button
    default: ""
    effect: >-
      Looks for the program on the `PATH`, then in the usual install folders, and fills in the first one whose version
      is supported. When it finds none it says "⚠ Could not auto-detect. Please select manually."
    range: >-
      On Windows `C:\Program Files\StarNet2\bin`, `C:\Program Files\StarNet2`, `StarNet2` in your home folder, and
      `AppData\Local\StarNet2\bin` and `AppData\Local\StarNet2` there, with `DeepSNR` in place of `StarNet2` for DeepSNR.
      On Linux `/usr/bin`, `/usr/local/bin`, `/opt/StarNet2/bin`, `/opt/starnet2/bin`, `~/StarNet2` and `~/.local/bin`,
      likewise.
    tradeoff: >-
      The dialog waits while it looks, up to 3 seconds for each copy it asks, and a second more for one that does not
      answer.
```

### Interface

```controls
window: Interface
controls:
  - name: "Theme:"
    kind: combo
    default: Dark
    options: [Follow System, Light, Dark]
    effect: The colours of AfterNight's windows. **Follow System** takes the light or dark choice of your system.
    scope: The interface only, never an image or what is saved.
    persistence: Stored as `interface/theme`, and applied when the page saves successfully.
  - name: "Theme Contrast:"
    kind: combo
    default: Balanced (Recommended)
    options: [Balanced (Recommended), Soft, High, OLED]
    effect: >-
      The contrast of the dark theme. **Soft** lowers contrast and saturation, **High** raises both, and **OLED** draws
      pure black backgrounds for OLED screens.
    enabled: >-
      **OLED** is greyed while **Theme:** is **Light**, and choosing **Light** while **OLED** is chosen returns to
      **Balanced (Recommended)**.
    persistence: Stored as `interface/themeContrastProfile`.
  - name: "Accent Family:"
    kind: combo
    default: Blue (Default)
    options: [Blue (Default), Teal, Amber, Amethyst, Rose]
    effect: The colour of buttons, selections and other interactive parts.
    persistence: Stored as `interface/themeAccentFamily`.
  - name: "Card Surface Tint:"
    kind: combo
    default: Neutral (Default)
    options: [Neutral (Default), Ocean, Mint, Amethyst, Rose, Amber]
    effect: A faint colour on the surface of cards and grouped boxes.
    persistence: Stored as `interface/themeSurfaceTintFamily`.
  - name: Enable UI animations
    kind: check
    default: "On"
    effect: >-
      Small animations, such as the highlight of a card under the pointer and the opening of collapsible sections. Off
      removes them, for less motion or less work.
    persistence: Stored as `interface/animationsEnabled`, and applied to open windows when the page saves.
  - name: "Mouse Wheel Zoom Sensitivity:"
    kind: slider
    default: "50"
    widget: {min: 1, max: 100, step: 1}
    effect: >-
      How far one step of the mouse wheel zooms a view, from **Slow** to **Fast**, shown as a percentage beside the
      slider. Higher zooms further for each step.
    tradeoff: A very high value crosses the zoom range in a few steps, a very low one needs many.
    persistence: Stored as `interface/zoomSensitivity`. Open views take it when the page saves.
  - name: Hold Ctrl key to zoom with mouse wheel
    kind: check
    default: "Off"
    effect: On, the wheel zooms a view only while you hold ++ctrl++. Off, the wheel always zooms.
    tradeoff: On avoids zooming by accident, at the cost of a key for every zoom.
    persistence: Stored as `interface/zoomRequiresCtrl`. Open views take it when the page saves.
  - name: Use system file browser
    kind: check
    default: "On"
    effect: On, open and save windows are your system's own. Off, they are AfterNight's.
    range: On by default on Windows and Linux, off elsewhere.
    tradeoff: >-
      On Linux some system file windows, of KDE or GTK, have trouble reaching hidden folders, whose names start with a
      dot. Its tooltip says so there.
    persistence: Stored as `interface/useSystemFileBrowser`, and read by the next file window.
  - name: Lock dock layout by default
    kind: check
    default: "Off"
    effect: On, AfterNight starts with its docked panels locked, so they cannot be moved by accident.
    tradeoff: Saving it does not lock or unlock the panels now. The layout command of the main window does that.
    persistence: Stored as `interface/lockLayoutByDefault`, and read at the next start.
  - name: Remember window size and position
    kind: check
    default: "On"
    effect: >-
      On, the main window's size, place and maximized state are kept when AfterNight closes and restored at the next
      start, and process windows keep their size. Off, the main window starts maximized and process windows open at
      their own size.
    range: A kept place that lies on no screen any longer opens the main window maximized.
    tradeoff: After a change of screens, a kept size can suit the new screen badly. Turning the box off starts maximized.
    persistence: Stored as `interface/rememberWindowSize`, beside the kept size and place.
    note: >-
      The note under this group reads "Window layout is automatically saved when closing the application." It means the
      main window's size and place. The arrangement of the docked panels is not kept between sessions.
```

### Export

These settings are read when you save an image, never a [sequence](../reference/glossary.md#sequence). The file name's
extension chooses the format: they change how a TIFF, [XISF](../reference/glossary.md#xisf), JPEG or PNG is written,
not which format you get.

```controls
window: Export
controls:
  - name: "Default Compression:"
    object: tiffCompression
    kind: combo
    default: None (Fastest, Largest Files)
    options:
      - None (Fastest, Largest Files)
      - LZW (Lossless, Good Compression)
      - ZIP/Deflate (Lossless, Better Compression)
      - JPEG (Lossy, Smallest Files)
    effect: >-
      The compression of a TIFF, under **TIFF Settings**. LZW and ZIP/Deflate keep every value. JPEG loses some, at a
      fixed quality of 95 that **Quality:** does not change.
    range: JPEG is used only for 8-bit samples. A deeper image is written with LZW instead, and the log says so.
    tradeoff: >-
      None writes fastest and largest, and the lossless choices cost time. Keep a scientific master as a lossless TIFF,
      a FITS or an XISF: a JPEG-compressed TIFF loses data.
    persistence: Stored as `export/tiffCompression`. A stored value the page does not know is shown, and written, as LZW.
  - name: "Default Compression:"
    object: xisfCompression
    kind: combo
    default: None (Read by Every XISF Reader)
    options:
      - None (Read by Every XISF Reader)
      - zlib (Lossless)
      - LZ4 (Lossless, Fastest)
      - LZ4-HC (Lossless, Smaller, Slower to Write)
      - Zstandard (Lossless, Smallest)
    effect: >-
      The compression of an XISF's pixel data, under **XISF Settings**. Every choice is lossless, and a compressed file
      is written in pieces of 4 MiB that a reader decodes one at a time.
    range: >-
      zlib and LZ4 are read by every XISF reader, Zstandard only by readers of Revision 1 of the specification, as the
      note under the group says.
    tradeoff: >-
      Compression costs time when the file is saved and opened. LZ4 is the fastest and Zstandard usually the smallest.
    persistence: Stored as `export/xisfCompression`. A stored value the page does not know is shown, and written, as None.
  - name: Shuffle the bytes of each sample before compressing
    kind: check
    default: "On"
    effect: >-
      Stores every sample's first byte, then every sample's second, before compressing, which makes 16-bit and
      floating-point images compress better. The pixels read back are the same.
    enabled: Only beside an XISF compression. An 8-bit image is never shuffled.
    tradeoff: Off usually writes a larger file.
    persistence: Stored as `export/xisfByteShuffle`.
  - name: "Checksum:"
    kind: combo
    default: None
    options: [None, SHA-256]
    effect: >-
      **SHA-256** writes a checksum of the XISF's pixel data, which a reader checks before it uses them. A file whose
      pixels do not match is refused when it opens rather than read wrong.
    tradeoff: The hash costs time at every save and open.
    persistence: Stored as `export/xisfChecksum`.
    note: No SHA-3 checksum is offered, since PixInsight refuses an XISF unit checksummed with SHA-3.
  - name: "Quality:"
    kind: slider
    default: "95"
    widget: {min: 1, max: 100, step: 1}
    effect: >-
      The quality of a JPEG file, from **Low** to **High**, shown as a percentage. Higher keeps more detail in a larger
      file, as the note under it says.
    tradeoff: A JPEG loses data at any quality. Never keep a master only as a JPEG.
    scope: JPEG files only.
    persistence: Stored as `export/jpegQuality`.
  - name: "Compression Level:"
    kind: slider
    default: "6"
    widget: {min: 0, max: 9, step: 1}
    effect: >-
      How hard a PNG is compressed, from **Fast** to **Small**. Higher writes smaller files more slowly, and the pixels
      read back are the same.
    tradeoff: No size or speed is promised for a level, since it depends on the image. Look at the file you saved.
    scope: PNG files only.
    persistence: Stored as `export/pngCompression`.
  - name: Embed EXIF metadata in exported files
    kind: check
    default: "On"
    effect: >-
      On, a TIFF, PNG or JPEG carries the camera's EXIF record and the other header cards the image was read with. Off,
      it carries none.
    scope: A FITS or XISF file always carries the image's header, whatever this says.
    tradeoff: The record can name your camera, the time of the shot and other details. Mind it before you share a file.
    persistence: Stored as `export/embedExif`.
  - name: Embed processing history in FITS header
    kind: check
    default: "Off"
    effect: Kept for a later release. No file is written with a processing history, as the note under the group says.
    tradeoff: A ticked box is no sign that a file holds its history. Look at the saved header.
    persistence: Stored as `export/embedHistory`.
```

### Processing

A note at the top of the page says that the thread and memory limits are fixed while AfterNight runs. Saving stores
them for the next start, and open images and their [history](../reference/glossary.md#history) are not touched.

```controls
window: Processing
controls:
  - name: Auto
    object: threadsAuto
    kind: check
    default: "On"
    effect: >-
      On, AfterNight's processing uses every logical core of the computer, under **Parallelism**. Off, **Max Threads:**
      sets the number.
    persistence: Stored as `processing/maxThreadsBatch`, which holds `0` for **Auto**. Used from the next start.
  - name: "Max Threads:"
    kind: spinbox
    default: The number of logical cores
    widget: {min: 1, step: 1}
    effect: The most threads AfterNight's processing uses at once.
    range: >-
      Up to the number of logical cores, which the note "Detected logical CPU cores" gives, taken as 4 when the computer
      does not say.
    enabled: While **Auto** is off.
    tradeoff: More threads finish long work sooner. Fewer leave the computer responsive for other programs.
    persistence: >-
      Stored as `processing/maxThreadsBatch`, and used from the next start. A larger number stored on another computer
      is kept as it is.
  - name: In use now
    object: processingThreadsInUseLabel
    kind: readout
    default: ""
    effect: >-
      The threads the running session uses, as "In use now: 16 threads.", or "No engine session is running."
  - name: The thread limit takes effect the next time AfterNight starts.
    kind: readout
    default: ""
    effect: A notice that the stated limit differs from the one in use.
    enabled: Shown while they differ, after saving as well, until AfterNight starts again.
  - name: Previews
    kind: readout
    default: ""
    effect: >-
      How the session shares its threads with real-time previews, as "Previews: while a preview is open, 4 of the 16
      threads are kept for previews, and a preview also uses every thread no other work needs; when none is open, all of
      them are available to processing."
    range: >-
      One thread is kept for a session of two to four threads, and a quarter of them, rounded down, for a larger one.
      With one thread it reads "Previews: with one thread, a preview waits its turn with every other operation."
    note: It is no setting. The session decides it from its threads.
  - name: "Updates per second while dragging:"
    kind: spinbox
    default: "30"
    widget: {min: 5, max: 60, step: 1}
    effect: >-
      The most previews a process window starts in a second while you drag one of its sliders, under **Real-time
      Preview**. The previews of a drag run on the threads kept for previews. Letting go, or holding the slider still for
      a moment, runs one more with every thread a preview may use.
    tradeoff: >-
      Lower uses less processor and battery while you drag. Higher follows the slider more closely. A slow process shows
      fewer previews than the rate allows.
    persistence: >-
      Stored as `processing/previewFrameRate`, and read at the next drag once you return to the process window, with no
      restart. An open extension window of the full kind keeps the rate it opened with.
  - name: Enable GPU Acceleration
    kind: check
    default: "Off"
    effect: Kept for a later release, as the note "GPU acceleration is planned for a future release." says.
    enabled: Never.
    persistence: Stored as `processing/gpuEnabled`.
    note: >-
      A warning under it reads "Currently, GPU is only used by extension-provided AI tools, which have their own GPU
      settings."
  - name: "Preferred GPU:"
    kind: combo
    default: Auto (Use Best Available)
    options: [Auto (Use Best Available)]
    effect: Kept for a later release.
    enabled: Never.
    persistence: Stored as `processing/preferredGpu`.
  - name: Auto
    object: memoryAuto
    kind: check
    default: "On"
    effect: On, AfterNight sets no memory limit of its own, under **Memory**. Off, **Memory Limit:** sets one.
    tradeoff: Without a limit, a large stack can take most of the computer's memory.
    persistence: Stored as `processing/memoryLimitMb`, which holds `0` for **Auto**. Used from the next start.
  - name: "Memory Limit:"
    kind: spinbox
    default: Three quarters of the computer's memory
    widget: {min: 512, step: 1024}
    effect: >-
      The most memory, in MB, the engine holds for images at once. Work that would need more is refused with "the
      request does not fit the memory budget this engine is configured with", which names neither amount.
    range: >-
      Up to the computer's memory, which the note "Total system RAM" gives. **OK** refuses less than 1024 with "Memory
      limit of 800 MB is too low. Recommended minimum is 1024 MB."
    enabled: While **Auto** is off. With **Auto** on, the box shows three quarters of the memory only as a starting value.
    tradeoff: Too low a limit refuses large stacks and images, as the warning under the group says.
    persistence: Stored as `processing/memoryLimitMb`, and used from the next start.
  - name: In use now
    object: processingMemoryInUseLabel
    kind: readout
    default: ""
    effect: >-
      The memory limit the running session uses, as "In use now: 24576 MB.", or "In use now: no memory limit."
  - name: The memory limit takes effect the next time AfterNight starts.
    kind: readout
    default: ""
    effect: A notice that the stated limit differs from the one in use.
    enabled: Shown while they differ, after saving as well, until AfterNight starts again.
```

### Logging

Logging keeps its values in a file of its own, `logging.conf` in AfterNight's data folder, so that AfterNight can read
them before anything else starts. Saving the page rewrites the file and applies it to the running session at once, as
the note "All logging settings take effect immediately when saved." says.

```controls
window: Logging
controls:
  - name: "Directory:"
    kind: field
    default: The logs folder of AfterNight's data folder
    effect: >-
      The folder of the log file, `AfterNight.log`, and its older copies, under **Log File Location**. On Windows it
      starts at `%LOCALAPPDATA%\AfterNight\logs`, on Linux at `~/.local/share/AfterNight/logs`.
    range: >-
      Its folder button opens "Select Log Directory", in the folder named or the nearest one of it that exists, else in
      your home folder. **OK** creates a folder that does not exist and writes a test file, `.afternight_log_test`, which
      it removes. It refuses with "The log directory does not exist and could not be created: ..." or "The log directory
      is not writable: ...".
    tradeoff: >-
      Moving the log splits it between two folders. A log names your folders and what AfterNight did, so mind it before
      you share one. A folder that cannot be written at the next start, such as one on a disk that is gone, sends the
      log to the default folder, with a warning that names the folder refused.
    persistence: >-
      Stored as `log_directory` in `logging.conf`. An empty box passes **OK** and writes the log to `AfterNight.log` in
      the folder AfterNight was started from. The page then shows the default folder again, and the next save stores it
      and moves the log there. **Reset Page** stores the default on purpose.
  - name: Open Log Directory
    kind: button
    default: ""
    effect: Opens the folder of the box in your file manager, or the default one when the box is empty.
    tradeoff: >-
      It creates the folder first when it does not exist, before anything is saved, and says nothing when it cannot.
      **Cancel** does not remove it.
  - name: "Minimum Level:"
    kind: combo
    default: Info (Recommended)
    options: [Trace (Most Verbose), Debug, Info (Recommended), Warning, Error, Critical, Off (Disable Logging)]
    effect: >-
      The least severe kind of message the log file keeps, with every more severe kind, under **Log Level**. **Off
      (Disable Logging)** writes no file.
    tradeoff: >-
      Trace and Debug help a report of a problem, write much more and name more of your system, as the note "Lower levels
      include more detail but may impact performance and disk usage." says.
    scope: The log file only. The Session Console keeps its own messages whatever this says.
    persistence: Stored as `log_level` in `logging.conf`. A level the page does not know shows as Info.
  - name: "Maximum File Size:"
    kind: spinbox
    default: "10"
    widget: {min: 1, max: 1000, step: 1}
    effect: The size, in MB, at which the log file is closed and a new one begun, under **Log Rotation**.
    tradeoff: Larger files keep longer stretches in one file. Smaller ones turn over more often.
    persistence: Stored as `max_file_size_mb` in `logging.conf`.
  - name: "Maximum Files to Keep:"
    kind: spinbox
    default: "5"
    widget: {min: 1, max: 100, step: 1}
    effect: How many older log files are kept. Older ones are deleted, as the note under the group says.
    tradeoff: Saving a lower number deletes the surplus files at once.
    persistence: Stored as `max_files` in `logging.conf`.
```

A `logging.conf` that is missing or cannot be read gives Info, 10 MB and 5 files, and a number outside the boxes'
ranges is brought inside them. The page does not report a `logging.conf` it could not write: open the page again to see
what was kept.

### Catalogs & Data

A **Catalog root:** can be a pack's own folder or a folder that holds packs. **Catalog pack:** lists the packs found
there, or, when there are none, the packs AfterNight can download: the four Gaia packs, and the SPCC packs that are
approved and current. An empty root means AfterNight's own `catalogs` folder.

```controls
window: Catalogs & Data
controls:
  - name: "Catalog root:"
    object: gaiaCatalogDirectoryPicker
    kind: field
    default: The catalogs folder of AfterNight's data folder
    effect: >-
      The folder where the Gaia pack is looked for, under **Gaia DR3 Astrometric Catalogs**. The internal plate solver
      and Photometric Color Calibration read it, as the note "Used by internal plate solving and PCC. SPCC Gaia XP
      sampled catalogs are managed separately below." says.
    range: >-
      The line under it reads "Selected Gaia catalog: Gaia DR3 G<=14 (gaia_dr3_g14)" in green for a ready pack, "Gaia
      catalog manifest was found, but one or more chunk files are missing or incomplete.", "AfterNight does not read
      this Gaia catalog's format. Download it again with Download Catalog...", "Catalog directory does not contain a
      valid Gaia catalog manifest" or "Gaia catalog not installed. Use Download Catalog... to install a pack."
    tradeoff: >-
      A pack is ready when its manifest describes chunks and every chunk is there at its size. Moving the root can leave
      the internal solver and PCC without a catalog until you choose a ready pack.
    scope: >-
      A folder that is itself a pack is used as it is. Otherwise the pack of **Catalog pack:** is taken from under it,
      or the one pack the folder holds.
    persistence: Stored as `platesolve/gaiaCatalogDirectory`. **OK** does not check it, so a wrong folder is saved as well.
  - name: "Catalog pack:"
    object: gaiaCatalogPackCombo
    kind: combo
    default: Gaia DR3 G<=14 (gaia_dr3_g14)
    options: [Gaia DR3 G<=12 (gaia_dr3_g12), Gaia DR3 G<=14 (gaia_dr3_g14), Gaia DR3 G<=16 (gaia_dr3_g16), Gaia DR3 G<=18 (gaia_dr3_g18)]
    effect: >-
      The Gaia pack, by its depth. A deeper pack holds fainter stars, for a narrow or sparse field. Choosing a pack does
      not install it.
    range: >-
      About 3.1, 17, 78 and 305 million stars, for about 188 MiB, 1,028 MiB, 4,758 MiB and 18 GiB. G<=14 is the pack
      AfterNight recommends. With packs in the root, the list holds those instead.
    tradeoff: A deeper pack costs download, disk, checking time and time for every search of the catalog.
    persistence: Stored as `platesolve/gaiaCatalogPack`.
  - name: Download Catalog...
    kind: button
    default: ""
    effect: >-
      Opens **Download Catalog** with the four Gaia packs, to install one. A successful install fills **Catalog root:**
      and **Catalog pack:** with it, to be kept by **OK** or **Apply**.
    enabled: Always, since the four packs are always offered.
    tradeoff: >-
      It uses the network, the disk, and the processor to check every chunk. The files stay on disk whether you save
      the page or not.
  - name: Evict
    kind: button
    default: ""
    effect: Deletes the folder of the selected Gaia pack from disk, at once, with no question and no message.
    enabled: While the folder that **Catalog root:** and **Catalog pack:** point to exists and is not empty.
    tradeoff: >-
      When **Catalog root:** names a folder that holds no Gaia pack but other files, as AfterNight's `catalogs` folder
      does with only SPCC's catalog in it, it is that folder **Evict** deletes, with everything in it. Check the line
      under the root first. Nothing brings the files back, **Cancel** included, and a deletion that fails shows only as
      the line staying as it was.
  - name: "Catalog root:"
    object: spccCatalogDirectoryPicker
    kind: field
    default: The catalogs folder of AfterNight's data folder
    effect: The folder where the sampled Gaia XP catalog of SPCC is looked for, under **SPCC Calibration Data**.
    range: >-
      The line under it reads "Selected SPCC catalog: ... (local-full)" for a ready pack, "SPCC chunk cache: 2/768 chunks
      cached (766 missing). Missing chunks are downloaded when needed.", "SPCC catalog metadata was found, but required
      metadata or sidecar files are incomplete.", "Catalog directory does not contain a valid SPCC catalog manifest" or
      "SPCC catalog cache is empty. SPCC can download needed chunks on demand, or use Download Full Offline Catalog...
      for offline use."
    tradeoff: >-
      The page looks only at which files are there, without checking their contents, so it stays quick. A partial cache
      needs the network when SPCC runs on a region it lacks.
    persistence: Stored as `catalogs/spccCatalogDirectory`. **OK** does not check it.
  - name: "Catalog pack:"
    object: spccCatalogPackCombo
    kind: combo
    default: SPCC Gaia DR3 XP Sampled Local Full (spcc-gaia-dr3-xp-sampled-full-2026b)
    options: [SPCC Gaia DR3 XP Sampled Local Full (spcc-gaia-dr3-xp-sampled-full-2026b)]
    effect: >-
      The SPCC pack, either the whole catalog, `local-full`, or a regional pack you installed. The whole catalog holds
      29,648,017 sampled-XP records, about 42.4 GiB to download and 44.4 GiB on disk.
    range: >-
      The packs found in the root, else the packs AfterNight may download. A pack that is superseded, not approved, not
      current or has no usable address is left out.
    tradeoff: The whole catalog spares SPCC every download, at that cost on disk. A regional pack serves only its region.
    persistence: Stored as `catalogs/spccCatalogPack`. Unset, the first pack of the list is used.
  - name: Download Full Offline Catalog...
    kind: button
    default: ""
    effect: >-
      Opens **Download Catalog** with the SPCC packs. A successful install of the whole catalog also installs, in the
      background, the first approved repository of response data when the one in use is not ready.
    enabled: While at least one approved SPCC pack can be downloaded.
    tradeoff: >-
      The download of the response data, about 26 MB, shows no progress and no error on the page, and stops if you close
      the dialog. Its folder is stored as soon as it ends. When it did not arrive, **Download Response Profiles** in the
      SPCC window fetches it.
    note: >-
      Its tooltip reads "Download the full SPCC Gaia XP sampled catalog for offline use. Any other required SPCC data is
      prepared automatically if needed." Mirror files can add regional packs to the assistant despite the label.
  - name: Verify & Repair
    kind: button
    default: ""
    effect: >-
      Opens the same **Download Catalog** as **Download Full Offline Catalog...**, whose button then reads **Download
      Missing**, **Verify & Repair** or **Download** for the state of the pack chosen there.
    enabled: As **Download Full Offline Catalog...**.
    tradeoff: >-
      The assistant does not choose the page's pack for you, despite the tooltip "Repair or reinstall the selected SPCC
      catalog pack." Choose it, and check **Install to:**.
  - name: Delete Selected
    kind: button
    default: ""
    effect: Deletes the folder of the selected SPCC pack from disk, at once, after a question.
    range: >-
      Inside AfterNight's `catalogs` folder it asks "Delete the selected SPCC catalog folder?" with the folder, and
      **Cancel** is the default answer. Elsewhere it asks you to type the exact folder path, and deletes only when what
      you type is that path.
    enabled: While the folder that the root and the pack point to exists and is not empty.
    tradeoff: The question is no backup. A deletion that fails shows only as the line under the root staying as it was.
  - name: Delete All Downloaded Catalogs...
    kind: button
    default: ""
    effect: >-
      Asks "Delete 2 AfterNight-managed SPCC catalog folder(s) under:" with the folder, and deletes every SPCC pack in
      AfterNight's own `catalogs` folder.
    enabled: While that folder holds at least one SPCC pack.
    tradeoff: >-
      It leaves the Gaia packs, an SPCC pack elsewhere and the response data alone. A pack it fails to delete is not
      named.
  - name: "Pending region:"
    kind: readout
    default: ""
    effect: >-
      The region of an image that the **Download Needed Region** button of SPCC recorded, as "M42: RA 83.82208 deg, Dec
      -5.39111 deg, radius 0.8931 deg, G <= 17.6", with a second line: "Matching approved regional pack: ..." or "No
      approved downloadable SPCC regional pack covers this pending region yet."
    enabled: Shown only while a region is recorded.
    persistence: >-
      Stored as `catalogs/spccRegionalCacheRequest`, with the image's name and place, until a saved choice of pack covers
      it.
  - name: Download Matching Region...
    kind: button
    default: ""
    effect: >-
      Opens **Download Catalog** with the approved regional packs that cover the pending region. Once a pack that covers
      it is installed and the page saved, the region is cleared.
    enabled: Shown with **Pending region:**, and enabled when a pack covers it.
    tradeoff: A regional pack covers only its region, not the whole sky.
```

### Download Catalog

```controls
window: Download Catalog
controls:
  - name: "Catalog pack:"
    kind: combo
    default: The first pack of the list
    options: [The first pack of the list, Every other pack the button offers]
    effect: >-
      The pack to install. A name adds "(Recommended)", and "(Downloaded)", "(Partial cache)", "(Incomplete install)"
      or "(Unsupported format)" for what is already in **Install to:**, with that folder as its tooltip.
    enabled: Not while a download runs.
    tradeoff: It does not start at the page's pack. Choose the pack you mean.
  - name: "Size:"
    kind: readout
    default: ""
    effect: >-
      What the pack needs, as "Download and install approximately 1.0 GB." or "Download 42.4 GB, installed approximately
      44.4 GB."
    tradeoff: >-
      The sizes are estimates. A repair needs room for a second copy of what it replaces, and a download for the parts
      on their way.
  - name: "Description:"
    kind: readout
    default: ""
    effect: >-
      What the pack holds and what it suits, as "Recommended starter pack (~17M stars, G<=14). Balanced depth and
      storage for plate solving and photometric workflows."
  - name: "Install to:"
    kind: field
    default: The catalogs folder of AfterNight's data folder
    effect: The folder the pack is installed under, in a folder named after the pack.
    enabled: Not while a download runs.
    tradeoff: It starts at AfterNight's own folder, not at the page's **Catalog root:**. Check it before you download.
  - name: Browse
    kind: button
    default: ""
    effect: Opens "Choose catalog directory" to choose **Install to:**.
    enabled: Always, while a download runs as well.
  - name: Status line
    kind: readout
    default: ""
    effect: >-
      What the window does, under a progress bar: "Fetching catalog manifest...", "Downloading chunk 3 of 49 (...)...
      12.0 MB / 1.0 GB", "Verifying chunk 3 of 49 (...)...", then "Catalog installed to ...", "Catalog download
      cancelled." or the reason it failed.
    range: >-
      Before a download it says "Select a destination and start the download.", "This pack has no manifest URL
      configured.", or what it found in **Install to:**, such as "A partial SPCC chunk cache was found in ... (2/768
      chunks cached, 766 missing)."
  - name: Download
    kind: button
    default: ""
    effect: >-
      Installs the pack. It reads **Download Missing** for a partial SPCC cache, and **Verify & Repair** for a pack that
      is installed, incomplete, or in a format AfterNight no longer reads, which it downloads again in place.
    enabled: For a pack with a usable manifest address. Hidden while a download runs.
  - name: Cancel Download
    kind: button
    default: ""
    effect: >-
      Stops the download. A Gaia pack installed before stays as it was, and the chunks of an SPCC catalog already
      checked are kept.
    enabled: Shown only while a download runs.
    note: Closing the window by ++esc++ or its close button stops the download too.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. After a successful install, the page takes the pack and its folder.
    enabled: Not while a download runs.
```

### Plate Solving

```controls
window: Plate Solving
controls:
  - name: "Default solver:"
    kind: combo
    default: The first solver that is set up
    options: [The first solver that is set up, Internal Gaia, ASTAP, Astrometry.net (local), Astrometry.net (web)]
    effect: >-
      The solver tried first, under **General**. If it fails, the others that are set up are tried, as its tooltip
      says: "Preferred solver. If it fails, the other solvers are tried automatically."
    range: >-
      Only a solver that is set up can be chosen: **Internal Gaia** with a ready, saved Gaia pack, **ASTAP** and
      **Astrometry.net (local)** with a program file that exists and can run, **Astrometry.net (web)** with any text in
      **Web API key:**. **Astrometry.net (local)** is offered on Linux only.
    enabled: >-
      A solver that is not set up is greyed, with the tooltip "Configure this solver before selecting it as the
      default." The box reads "No configured solver" when none is set up.
    tradeoff: Being set up is not the same as working, since no solver is run here.
    scope: The Plate Solve window, and the plate solving of Image Registration, PCC and Image Integration.
    persistence: >-
      Stored as `platesolve/defaultSolver`. A choice no longer set up gives way, when the page saves or opens, to the
      first set up in the order Internal Gaia, ASTAP, Astrometry.net (local) and Astrometry.net (web).
  - name: "Search radius:"
    kind: spinbox
    default: "10.0"
    widget: {min: 1, max: 180, decimals: 1}
    effect: >-
      How far from the position the image's header or the hints give, in degrees, the solvers look for the field. With
      a position, a solution whose centre lies farther away is refused and the next solver tried.
    range: >-
      The internal solver searches within it, ASTAP gets it as `-r`, `solve-field` as `--radius` and the web service as
      `radius` with the position. Without a position a solver searches the sky its own way.
    tradeoff: A larger radius forgives a poor position and costs time. A smaller one fails when the position is off by more.
    persistence: Stored as `platesolve/searchRadius`.
  - name: "Downsample:"
    kind: spinbox
    default: "2"
    widget: {min: 1, max: 8, step: 1}
    effect: >-
      How many pixels an external solver bins into one first: ASTAP as `-z`, `solve-field` as `--downsample`, the web
      service as `downsample_factor` above 1. The internal solver does not use it.
    tradeoff: Higher is faster on a large image and loses small, faint stars.
    persistence: Stored as `platesolve/downsample`.
  - name: "Timeout:"
    kind: spinbox
    default: "120"
    widget: {min: 10, max: 600, step: 1}
    effect: >-
      How long one solver may work, in seconds. The internal solver then stops with "Internal Gaia solver timed out
      after 120 seconds.", and ASTAP or `solve-field` is stopped.
    range: >-
      For the web service it bounds each request, at 30 seconds or more. Its waits for a job and for the job's solution
      ask every 5 seconds up to 120 times, about ten minutes each.
    tradeoff: A short timeout stops a search that would have succeeded. A long one delays the next solver.
    persistence: Stored as `platesolve/timeout`.
  - name: "Executable:"
    kind: field
    default: ""
    effect: >-
      The ASTAP program, under **ASTAP**. Set, ASTAP joins the solvers. The line under it reads "✓ ASTAP detected" or
      "✗ Executable not found".
    range: >-
      Its **Detect** looks on the `PATH`, then in `C:\Program Files\astap`, `C:\Program Files (x86)\astap` and `astap`
      in your home folder on Windows, or in `/usr/local/bin`, `/usr/bin`, `/opt/astap` and `~/astap` on Linux.
    tradeoff: >-
      The page checks only that the file exists and can run, not its version and not the star database ASTAP reads.
      The note under it says ASTAP ships with its own database: check that the database ASTAP needs for your field is
      installed.
    scope: ASTAP solves on your computer, from temporary copies of the image, so the image stays offline.
    persistence: Stored as `tools/astapPath`, empty after **Reset Page**.
  - name: "Local solve-field:"
    kind: field
    default: ""
    effect: >-
      The `solve-field` program of Astrometry.net, under **Astrometry.net**, on Linux. Set, the local solver joins the
      solvers. The line under it reads "✓ Astrometry.net solve-field detected" or "✗ Executable not found".
    range: Its **Detect** looks on the `PATH`, then in `/usr/bin`, `/usr/local/bin` and `/opt/astrometry/bin`.
    enabled: Hidden on Windows, where the note says local solving is available only on Linux.
    tradeoff: The page checks neither its version nor its index files. Local solving keeps the image offline.
    persistence: Stored as `tools/astrometryLocalPath`.
  - name: "Local index files:"
    kind: field
    default: ""
    effect: >-
      The folder of Astrometry.net index files that `solve-field` reads, such as the 4200 series. A solve passes it in a
      configuration file of its own, with `add_path` and `autoindex`.
    enabled: Hidden on Windows.
    tradeoff: >-
      Any folder is accepted, and missing or wrong index files make the solve fail while the solver counts as set up.
      Index files take much disk space.
    scope: The local solver only. Left empty, the configuration names no folder.
    persistence: Stored as `platesolve/indexPath`.
  - name: "Web API key:"
    kind: field
    default: ""
    effect: >-
      The key of your account at nova.astrometry.net, which sets the web service up. A solve logs in with it, uploads
      the image, sends the hints, waits for the solution and downloads it.
    range: Shown as dots. Any text counts as a key, and a wrong one fails only when a solve logs in.
    tradeoff: >-
      A web solve sends your image and its position to the service, and needs the network. The internal solver, ASTAP
      and the local solver keep the image on your computer. The key is stored in the
      settings database and written in plain text by **Export Settings...**. An Image Integration run keeps only a hash
      of it in its records.
    persistence: Stored as `tools/astrometryApiKey`, empty after **Reset Page**.
```

The Gaia catalog of the internal solver is set on **Catalogs & Data**, as the note "Internal Gaia catalog data is
managed in Settings -> Catalogs & Data." says. A pack saved there makes **Internal Gaia** available the next time this
page opens.

## When it goes wrong

A change seems to make no difference
:   **Cause.** The setting takes effect later: the thread and memory limits and the dock lock at the next start, the
    raw settings at the next file you open, the export settings at the next save.
:   **Fix.** Restart AfterNight, or open the file again. [Settings Reference](settings-dialog-reference.md) gives each
    setting's moment.

"Validation Error" with "The temporary files directory is not writable: ..." or "The log directory is not writable: ..."
:   **Cause.** **OK** could not create the folder you named, or write a test file in it.
:   **Fix.** Choose a folder you can write to, on a local disk with room, and choose **OK** again.

A red line under **StarNet2:** or **DeepSNR:**, such as "Could not detect StarNet2 version. StarNet2 2.5.3 or newer is required."
:   **Cause.** The program did not answer with a version in time, or its version is too old. The path is saved all the
    same, and the process stays unavailable.
:   **Fix.** Install the current version with all its files, then choose **Detect** or select the program again.

"Gaia catalog not installed. Use Download Catalog... to install a pack."
:   **Cause.** No Gaia pack is ready where **Catalog root:** and **Catalog pack:** point.
:   **Fix.** Choose **Download Catalog...**, or point **Catalog root:** at the folder that holds your pack, then choose
    **OK**.

**Default solver:** reads "No configured solver"
:   **Cause.** No solver is set up: no ready Gaia pack is saved, no ASTAP program is set and the web key is empty.
:   **Fix.** Set one up, choose **Apply**, and open the Plate Solving page again.

Imported settings are not the ones the file holds
:   **Cause.** A page you opened in the same dialog wrote its own values over the imported ones.
:   **Fix.** Open Settings anew, import on **General** and choose **OK** at once, before you open another page.

## Related

- [Settings Reference](settings-dialog-reference.md), for when each change takes effect, every message of the dialog,
  the settings file and where AfterNight keeps its data.
- [Plate Solving](../processes/registration-alignment/plate-solving.md), for how each solver uses its settings, and
  [Spectrophotometric Color Calibration](../processes/color-calibration/spectrophotometric-cc.md) and
  [Photometric Color Calibration](../processes/color-calibration/photometric-cc.md), for the catalogs they read.
- [Starnet++](../processes/star-operations/starnet.md) and [DeepSNR](../processes/noise-reduction/deepsnr.md), the
  processes External Tools sets up.
- [Supported Formats](../getting-started/supported-formats.md), [Debayer](../processes/calibration/debayer.md) and
  [Saving And Exporting](saving-and-exporting.md), for the files the Raw Images and Export pages concern.
- [Session Console](log-viewer.md), for the messages the Logging page writes to a file.
- [Image Viewer](image-viewer.md), [Docking Panels](docking-panels.md) and [Process Windows](process-windows.md), for
  the zoom, the panels and the window sizes the Interface page sets.

## Credits

- [StarNet command-line tools](https://starnetastro.com/cli-tools/), the StarNet2 and DeepSNR programs and their
  versions, which External Tools sets up.
- [Gaia DR3 credit and citation instructions](https://gea.esac.esa.int/archive/documentation/GDR3/Miscellaneous/sec_credit_and_citation_instructions/),
  the credit the Gaia packs and the catalog of SPCC keep, from the Gaia mission of the European Space Agency.
- [ASTAP](https://www.hnsky.org/astap.htm), [Astrometry.net](https://astrometry.net/use.html) and the
  [nova.astrometry.net API](https://astrometry.net/doc/net/api.html), the external solvers the Plate Solving page sets
  up.
