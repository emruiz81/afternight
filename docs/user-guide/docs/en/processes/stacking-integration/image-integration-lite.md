---
kind: process
stage: stack
menu: Process > Preprocess > Image Integration Lite
summary: >-
  Calibrates, registers and stacks the frames of one target in one run, with most choices made for you, and writes
  one linear master for each filter: the simplified front end of Image Integration.
data_in: linear
data_out: linear
destructive: false
preview: none
sequences: false
masks: none
external: >-
  Optional: a plate solver set up in the settings, for Plate solve final masters
glance:
  enabled: >-
    Always. The window opens without an image and works on the files you add. Run Integration waits for the import to
    end and for the banner to show no error.
  inputs: >-
    Still image files of one target as the camera wrote them: lights, and any flats, darks, dark flats and bias
    frames. No open image, mask, sequence or video is used.
  outputs: >-
    Files in the output folder: calibration masters, registered lights, one linear 32-bit floating-point FITS master
    for each size, filter and object, and the run's report and log. The frames are never changed. The masters open in
    AfterNight.
  state: >-
    The raw frames of one target, grayscale or a color-filter mosaic, before any calibration.
derived: true
rewritten: true
---
# Image Integration Lite

Image Integration Lite calibrates, registers and stacks the frames of one target in one run, and writes one linear
[master](../../reference/glossary.md#master) for each frame size, filter and object. Lite makes most choices itself,
and **Processing Settings** holds the few you may change. It runs the workflow of
[Image Integration](image-integration.md), which offers every choice Lite makes for you. The run writes into a folder
you choose, never changes your frames, and opens the masters when it ends. The
[Quick Start](../../getting-started/quick-start.md) takes you through a first run step by step.

```figure
image: ../../assets/images/processes/stacking-integration/image-integration-lite.png
alt: >-
  The Image Integration Lite window. An amber banner at the top reads Ready with 4 warnings. The Dataset card counts
  64 light, 150 flat, 0 dark and 0 bias frames, with the filters Ha, Oiii and Sii and the object M 16. The Setup card
  holds the Output folder with its Browse button, two check boxes, the memory limit and CPU Threads. Processing
  Settings lists four closed lines: Calibration, Registration, Normalization and Stacking. The Frames card has the Add
  as box, the Add Files, Add Folder, Change Type and Clear All buttons, and a table of the files with their type,
  filter, object, exposure and calibration. Run Integration, Open Full Process and Close are at the bottom.
caption: >-
  Image Integration Lite after a run on Eagle Nebula narrowband lights and their flats, every setting at its default:
  one master was stacked for each of the three filters.
pins:
  - {n: 2, x: 8.9, y: 30.8}
  - {n: 3, x: 43.6, y: 71.6}
  - {n: 4, x: 29.4, y: 18.1}
  - {n: 5, x: 29.4, y: 51.4}
  - {n: 6, x: 21.4, y: 10.9}
  - {n: 7, x: 66.0, y: 97.3}
```

## Before you start

- Lite stacks one target at a time: the [light frames](../../reference/glossary.md#light-frame) must name one object in
  their headers, or none. Flats, darks and [bias frames](../../reference/glossary.md#bias-frame) may name any.
- Add the files as the camera wrote them, in a format [Supported Formats](../../getting-started/supported-formats.md)
  lists, and do not [debayer](../../reference/glossary.md#debayer) color frames. A video, a
  [SER](../../reference/glossary.md#ser) file or a [sequence](../../reference/glossary.md#sequence) is not taken:
  extract its frames with [Extract Sequence](../utility/extract-sequence.md).
- Give each set a new, empty output folder, on a disk with room for several times the size of the lights.
- Add [dark-flat](../../reference/glossary.md#dark-flat) frames with **Dark Flat** chosen in **Add as**: **Auto-detect**
  adds them as darks.
- **Plate solve final masters** needs a plate solver set up in [Settings](../../interface/settings-dialog.md).
- For several targets, calibration matched by hand, a chosen reference, a comet or a saved project, use
  [Image Integration](image-integration.md). **Open Full Process** takes your frames there.

## Steps

1. **Open the window.** Choose **Process > Preprocess > Image Integration Lite**. No image needs to be open. The red
   banner reads "No light frames available.", after "Output directory is required." while **Output** is empty.
2. **Choose the output folder.** Choose **Browse** beside **Output** and pick a new, empty folder, or type its path.
3. **Add the frames.** Leave **Add as** at **Auto-detect**, or choose **Dark Flat** for dark flats, then choose **Add
   Folder** or **Add Files**. The table lists one row for each file.
4. **Check what was found.** Read **Frames**, **Filters** and **Object** in the **Dataset** card, and the **Type** and
   **Calibration** columns. Correct a wrong type with **Change Type**.
5. **Change a setting only for a reason.** Click a line of **Processing Settings** to open it. Its title then names
   your choice.
6. **Read the banner.** Green or amber lets you run, and red names what to correct. Hover over amber to read its
   warnings.
7. **Run.** Choose **Run Integration**. A progress window names each stage, and a box titled **Complete** reads
   "Workflow completed successfully." at the end.
8. **Look at the masters.** Choose **Open Report** in the progress window for the report, then **Close**. Each master
   is open in a view of its own, nearly black until you choose
   [**Autostretch**](../../reference/glossary.md#autostretch) in its display mode box. Check stars, corners and dust
   shadows at 100 %.

## Controls

The window has no preview, target, **Apply** or **Reset**: **Run Integration** runs it, on files. Its header, its
message strip, ++esc++ and its kept size work as in [Process Windows](../../interface/process-windows.md). Only
**Output** is kept for the next window, and **Clear All** keeps every value.

### The banner and Dataset

```controls
window: Image Integration Lite
controls:
  - name: Banner
    kind: readout
    default: Output directory is required. No light frames available.
    effect: >-
      Says whether the run can start. Red is an error, disables **Run Integration**, and lists up to three errors.
      Amber is a warning, and its tooltip lists every warning. Green reads "Ready."
    range: >-
      Lights alone give the amber "No calibration frames provided. Lights will be integrated without bias, dark, or
      flat calibration." Otherwise warnings read "Ready with 4 warning(s).", with such lines in the tooltip as
      "Missing dark/bias calibration match", "Missing flat calibration match" or "Flat frames have no bias or Dark
      Flat calibration. Master flats will be uncalibrated and may over-correct lights."
    enabled: >-
      The errors: "Output directory is required.", "Output directory path is not a directory.", "Output directory is
      not writable.", "Output directory cannot be created (parent directory is not writable).", "No light frames
      available." and "Image Integration Lite expects one target object. Use the full Image Integration process for
      multi-object projects."
    tradeoff: It checks that the run can be planned, not that the matches are good or the frames sharp.
    persistence: >-
      Counted again after every change. "Importing frames...", "Preparing integration...", "Updated 3 frame type(s)
      to Dark Flat." and the like stand until the next change.
    note: >-
      When a run ends, the banner shows the readiness again, not the result. Read the result in the progress window.
      The message strip over the window reads "Importing: Reading frame metadata." during an import, and "Preparing:
      Preparing Image Integration Lite workflow." until a run ends.
  - name: Frames
    kind: readout
    default: 0 light, 0 flat, 0 dark, 0 bias
    effect: Counts the table's rows by type. Dark flats count among the darks.
  - name: Filters
    kind: readout
    default: "-"
    effect: >-
      Lists the filters the lights' headers name, sorted, with `-` for a light whose header names none. Each filter
      makes a master of its own.
  - name: Object
    kind: readout
    default: "-"
    effect: >-
      Lists the object names the lights' headers give, with `-` for a light that gives none. Two names other than
      `-` are an error.
    scope: The lights alone. The object name of a flat, a dark or a bias frame does not count.
```

### Setup

```controls
window: Setup
controls:
  - name: Output
    kind: field
    default: ""
    effect: >-
      The folder every file of the run is written to: the masters, the registered lights, the run's records and its
      report. The box reads `Output directory` while it is empty.
    range: A folder that does not exist yet is made when its parent folder can be written to.
    enabled: Disabled while frames are imported.
    tradeoff: >-
      A folder that holds the results of an earlier run of the same frames lets the run reuse them, unless
      **Reprocess existing outputs** is on. The masters of another set of the same target, filter and size are
      replaced, and only the copy in that run's folder under `history` remains.
    scope: The whole run.
    persistence: >-
      Kept when the window closes, and in the box when the next Lite window opens. Left empty, the next window opens
      with the last folder chosen with **Browse**, if it still exists. **Clear All** keeps it.
    note: >-
      Besides the masters and `registered_lights`, the folder receives `registration_metrics.db`, the folder
      `.afternight\image_integration` with `artifacts.db`, the normalization documents and, with drizzle, the
      drizzle documents, and `history\<run>` for each run, named by its date and time. That folder holds
      `workflow_report.html`, `workflow_report.md`, `workflow_log.txt`, `workflow_manifest.json` and, under
      `groups`, a copy of each stacked master with a preview. Calibrated lights are kept in memory and never
      written.
  - name: Browse
    kind: button
    default: ""
    effect: >-
      Opens a folder chooser titled "Output Directory", at the folder in **Output** or the last one chosen. The
      folder you pick replaces the box's text.
    enabled: Disabled while frames are imported.
  - name: Reprocess existing outputs
    kind: check
    default: "Off"
    effect: >-
      On computes and writes every file of the run again. Off reuses each file of an earlier run in the folder whose
      frames and settings still match, and computes only what changed or is missing.
    enabled: Disabled while frames are imported.
    tradeoff: On costs the whole run's time again. Turn it on when you want everything computed afresh, as after you edit a file the run wrote.
    persistence: Off in every new window. **Open Full Process** does not take it.
  - name: Open results when complete
    kind: check
    default: "On"
    effect: On opens each stacked master in a view of its own when the run succeeds. Off leaves them in the folder.
    enabled: Disabled while frames are imported.
    scope: The stacked masters, not the calibration masters. A failed or cancelled run opens none.
    persistence: On in every new window. **Open Full Process** does not take it.
  - name: Limit memory usage
    object: liteLimitMemoryCheck
    kind: check
    default: "Off"
    effect: On stacks in tiles that keep to the amount in the box beside it, and enables that box.
    enabled: Disabled while frames are imported.
    tradeoff: >-
      Smaller tiles take longer. It bounds the stacking alone: reading, calibration and registration use what they
      need.
    scope: Every stack of the run, calibration masters included.
  - name: Limit memory usage
    object: liteMaxMemorySpin
    kind: spinbox
    default: "2048"
    widget: {min: 512, max: 1048576, step: 1}
    effect: The most memory, in megabytes, one stack may use. Lower makes more, smaller tiles.
    enabled: With the **Limit memory usage** check box on, outside an import.
  - name: CPU Threads
    kind: spinbox
    default: The computer's thread count
    widget: {min: 1, step: 1}
    effect: The most threads the run's stages use at once. Lower leaves more of the computer to other work, and the run takes longer.
    range: Up to the number of threads the computer offers, where it opens.
    enabled: Disabled while frames are imported.
    persistence: At the computer's count in every new window. **Open Full Process** does not take it.
```

### Processing Settings

Each line opens with a click, and its title sums up its choices, as "Calibration: Automatic, flat auto-level".

```controls
window: Processing Settings
controls:
  - name: Matching
    kind: combo
    default: Automatic
    options: [Automatic, Strict metadata match, Adaptive]
    effect: >-
      Chooses how a dark is matched to the lights. **Automatic** takes a dark of the lights' exposure, or else the
      dark closest to it within 20 %. **Strict metadata match** takes a dark of the same exposure only. **Adaptive**
      matches as **Automatic** does and turns on **Optimize dark frames** for the run.
    range: >-
      Flats are matched by frame size and filter, bias frames by frame size, with every choice. Dates, temperature,
      gain and offset are never compared.
    tradeoff: >-
      **Strict metadata match** can leave lights without a dark, which the **Calibration** column shows. A dark
      matched within 20 % raises no warning.
    scope: The masters each light is calibrated with, and the **Calibration** column, which follows the choice at once.
    note: >-
      A light with a dark is not given the bias as well, since the dark master holds it. A light without a dark takes
      the bias. Cosmetic correction is always on: it finds hot and cold pixels at 3 deviations in the dark master and
      corrects them in the lights, and finds none without a dark. No pedestal is added. The choices' tooltips speak
      of gain and binning: gain is not compared, and binning only through the frame size.
  - name: Optimize dark frames
    kind: check
    default: "Off"
    effect: On scales the dark master for each light to match its thermal signal. Off subtracts it as it is.
    range: >-
      Only a one-channel light of at least 512 × 512 pixels, a mosaic among them, whose dark varies over its central
      square is optimized. Any other light gets the factor 1, and the run's warnings say so once for each dark.
    enabled: "**Adaptive** turns it on for the run whatever the box says."
    tradeoff: It helps when the darks are close to the lights but not equal. A wrong scale leaves thermal signal or adds noise.
  - name: Normalize master flat
    kind: check
    default: "On"
    effect: >-
      On divides each channel of the flat master by its mean over the central third of the frame, each color of a
      mosaic on its own, before the lights are divided by it. Off divides the lights by the master as it is.
    tradeoff: Off changes the lights' level by the flat's own level. Leave it on.
    note: It is the full process's **Enable flat auto-level**.
  - name: Transform
    kind: combo
    default: Homography
    options: [Similarity, Affine, Homography, Polynomial 2, Polynomial 3, Polynomial 4, Auto]
    effect: >-
      Chooses how a light may be warped onto the reference. **Similarity** shifts, turns and scales it, **Affine**
      also shears it, and **Homography** also corrects perspective. **Polynomial 2**, **3** and **4** refine a
      homography for optical distortion. **Auto** starts from a homography and refines it only where the stars fit
      clearly better.
    range: The polynomials and **Auto** fit in two passes, the other choices in one.
    tradeoff: A more flexible transform needs more matched stars and more time, and can bend to bad matches.
    note: >-
      The rest of the registration is fixed: star matching with at least 10 pairs and up to 2,000 stars a frame,
      stars found with a radius of 5, a threshold of 1.0, a roundness of 0.5 and a Gaussian profile, Auto
      interpolation, the mosaic's pattern read from each light's header and the default debayer method, no plate
      solve of the frames, no comet and no chromatic correction. Gaia enrichment is on. Without a Gaia catalog, a run
      whose lights carry a plate solution warns "Gaia enrichment is on, but no Gaia catalog is configured: ..." and
      names **Enrich with Gaia catalog**, a control of the full process. Lite reads no catalog figure, so the stack is
      the same. Install a catalog under **Settings > Plate Solving**, or turn enrichment off after **Open Full
      Process**.
  - name: Auto-select reference
    kind: check
    default: "On"
    effect: On lets the registration choose the reference frame from the lights' stars.
    range: Lite has no control to name a reference. To choose one, use **Open Full Process** and set it there.
    tradeoff: Off asks for a reference set by hand, which Lite cannot give. Leave it on in Lite.
  - name: Auto crop to common area
    kind: check
    default: "Off"
    effect: >-
      On writes the registered lights onto the area every light covers, so the masters have no empty border. Off
      keeps the reference's frame, where a shifted light leaves an empty border.
    range: When no area is common to every light, the run keeps the reference's frame and says so.
    tradeoff: It narrows the field.
  - name: Lights
    kind: combo
    default: Global Additive Scaling
    options: [Global Additive Scaling, Global Additive, Global Multiplicative Scaling, Global Multiplicative, Local Normalization, None]
    effect: >-
      Chooses how each light of a group is brought to the level of the group's reference before the stack, by
      equation (1). **Local Normalization** corrects the level region by region instead, and **None** stacks the
      lights as they are.
    range: The scale is measured with IKSS, and the reference is chosen among the group's best lights. Overlap normalization is off.
    tradeoff: >-
      **Local Normalization** is slower and can overcorrect, with the full process's settings, which Lite does not
      show. **None** suits only lights already at one level.
    scope: The light stacks.
  - name: Calibration
    kind: readout
    default: "Bias/Dark: none, Flat: multiplicative scaling"
    effect: >-
      States how the calibration frames are brought to one level before their masters are stacked. Bias and dark
      frames are stacked as they are. Each flat is matched by one factor, as **Global Multiplicative** does.
    note: The label reads "multiplicative scaling", and the flats are matched as **Global Multiplicative** matches lights.
  - name: Method
    kind: combo
    default: Average
    options: [Average, Sum, Maximum, Minimum, Median, Auto-Adaptive Weighted Average]
    effect: >-
      Chooses how the samples of one pixel are combined. **Average** takes the mean of those **Rejection** keeps.
      **Sum** adds them, **Maximum** and **Minimum** keep the extreme one and **Median** the middle one.
      **Auto-Adaptive Weighted Average** weighs each sample, in a few rounds, by its distance from the others' mean.
    tradeoff: >-
      **Average** leaves the least noise. Every other choice stacks without rejection, and **Rejection** is disabled.
      The extremes and **Sum** are for special masters.
    scope: Every stack of the run, the bias, dark and flat masters included.
  - name: Rejection
    kind: combo
    default: Auto
    options: [Auto, None, Sigma Clipping, MAD Clipping, Median Sigma, Winsorized Sigma Clipping, Percentile, GESDT, Linear Fit]
    effect: >-
      Chooses how the samples that disagree with the rest are left out: satellite trails, cosmic rays, hot pixels.
      **Auto** picks a method for each group by its type and its number of frames, as the table under Model shows.
      **None** keeps every sample. The other methods are those of the Image Stacking page.
    enabled: With **Method** at **Average**, outside an import.
    tradeoff: A method needs enough frames, and a low threshold also removes real signal.
    scope: Every stack of the run, the calibration masters included, with **Auto** decided for each group.
    note: >-
      Choosing a method sets both thresholds to its own defaults. Lite never runs the analysis of rejected clusters,
      nor the stack again without the frames it flags.
  - name: Sigma Low
    kind: spinbox
    default: "3.00"
    widget: {min: 0.01, max: 10, step: 0.1, decimals: 2}
    effect: The lower threshold of the method. Lower leaves more of the dark samples out.
    range: >-
      Its label and range follow **Rejection**: **Sigma Low** for **Sigma Clipping**, **MAD Clipping**, **Median
      Sigma** and **Winsorized Sigma Clipping**, **Linear Low** for **Linear Fit**, and, from 0.01 to 1 in steps of
      0.01, **Low Fraction of Median** for **Percentile** and **ESD Outliers** for **GESDT**.
    enabled: Shown only for a method with thresholds, not at **Auto** or **None**.
    note: >-
      The method's defaults: 3 for the four sigma methods, 5 for **Linear Fit**, 0.20 for **Percentile** and 0.30 for
      **GESDT**. **Percentile** leaves out a sample further below the median than this fraction of the median, a
      fraction and not a rank. For **GESDT** it is the largest share of samples that may be outliers.
  - name: Sigma High
    kind: spinbox
    default: "3.00"
    widget: {min: 0.01, max: 10, step: 0.1, decimals: 2}
    effect: The upper threshold of the method. Lower leaves more of the bright samples out.
    range: >-
      **Sigma High**, **Linear High**, **High Fraction of Median** or **ESD Significance**, with the ranges of
      **Sigma Low**.
    enabled: Shown with **Sigma Low**.
    note: >-
      The method's defaults: 3 for the four sigma methods, 4 for **Linear Fit**, 0.10 for **Percentile** and 0.05, the
      test's significance level, for **GESDT**.
  - name: Weighting
    kind: combo
    default: None
    options: [None, Number of stars, Weighted FWHM, Noise]
    effect: >-
      Chooses how much each light counts. **None** weighs them alike. **Number of stars** favors lights with more
      stars, **Weighted FWHM** lights with smaller stars and **Noise** lights with less noise.
    range: >-
      The figures are the registration's. The star count weighs by its square, no weight is below 0.01, and each
      light has one weight for all its pixels.
    tradeoff: A wrong measurement becomes a wrong weight.
    scope: The light stacks. Calibration masters are never weighted.
    note: A group where a light lacks the figure is stacked without weights, and the run warns.
  - name: Plate solve final masters
    kind: check
    default: "Off"
    effect: >-
      On solves each stacked master that carries no WCS, with the plate solver set up in the settings. A master
      carries the registration's WCS whenever the reference frame had a plate solution, on or off.
    range: Only the stacked masters are solved, never the frames.
    tradeoff: It adds the solver's time, and whatever catalog or network the solver needs.
    note: >-
      Off, the log says "Saved ... without WCS because no registration output-plane WCS was available and fallback
      plate solving is disabled."
  - name: Enable drizzle
    kind: check
    default: "Off"
    effect: On stacks the lights by drizzle onto a finer grid, with the scale and pixfrac below, instead of stacking the registered lights.
    range: The kernel is square, and the drizzle works on debayered lights, never on the mosaic.
    tradeoff: It needs dithered lights and a good registration, and takes far more memory, disk and time.
    scope: The light stacks.
    note: >-
      The stage writes one small descriptor for each light under `.afternight\image_integration\artifacts\drizzle`,
      and dense coordinate maps only when checkpoints are kept.
  - name: Drizzle scale
    kind: combo
    default: 2x
    options: [2x, 3x]
    effect: Multiplies the master's width and height.
    enabled: With **Enable drizzle** on.
    tradeoff: 3x has 2.25 times the pixels of 2x, with the memory and disk that go with them, and needs more dithered lights.
    note: The master keeps its lights' level at either scale, and the group's normalization applies at that level.
  - name: Drizzle pixfrac
    kind: combo
    default: "0.9"
    options: ["0.9", "0.8", "1.0"]
    effect: >-
      Sets the side of each input pixel's drop, as a fraction of a pixel. Smaller keeps more detail and needs better
      dither coverage to avoid holes. **1.0** is the smoothest.
    enabled: With **Enable drizzle** on.
```

### Frames

```controls
window: Frames
controls:
  - name: Add as
    kind: combo
    default: Auto-detect
    options: [Auto-detect, Light, Flat, Dark, Dark Flat, Bias]
    effect: >-
      Chooses the type the next files are added as. **Auto-detect** reads the `IMAGETYP` card, else `FRAME`, else an
      XISF file's image type. Failing those, the nearest folder on the file's path named `flat`, `flats`, `dark`,
      `darks`, `bias`, `biases`, `offset`, `offsets`, `light` or `lights`, in any case, decides. Failing that, the
      file is a light. Another choice adds every file as that type.
    range: >-
      A card names a type by the first of these words it holds anywhere, in any case: BIAS, ZERO or OFFSET, then
      DARK, FLAT, LIGHT or SCIENCE. `MasterOffset` names a bias. File names are never read.
    enabled: Disabled while frames are imported.
    note: "**Auto-detect** never gives **Dark Flat**: a dark flat's card reads DARK, so it is added as a dark."
  - name: Add Files
    kind: button
    default: ""
    effect: >-
      Opens a chooser titled "Add Frames" for one file or more, filtered to the image formats AfterNight reads, at
      the folder you last added files from. The headers are read in the background.
    range: FITS, XISF, TIFF, PNG, JPEG and camera raw files.
    enabled: Disabled while frames are imported. The other controls are disabled until the import ends.
    tradeoff: >-
      Many files, or a slow disk, take a while. A file in another format is skipped. A file whose header cannot be
      read is added without its size, filter or exposure. A file the list already holds as the same type is
      replaced, not listed twice.
  - name: Add Folder
    kind: button
    default: ""
    effect: >-
      Opens a folder chooser titled "Add Folder", at the folder you last added, and adds every file of a format
      AfterNight reads in it and in every folder under it.
    enabled: Disabled while frames are imported.
    tradeoff: It adds every other image the folders hold too. Review the table.
  - name: Change Type
    kind: button
    default: ""
    effect: Opens a menu of the five types for the selected rows.
    enabled: With a row selected, outside an import.
    note: >-
      Right-clicking a row offers the same, from **Set as Light** to **Set as Bias**, and selects the row first. A
      new type changes how the run uses the file, never the file.
  - name: Light
    kind: action
    default: ""
    effect: Makes the selected rows lights.
  - name: Flat
    kind: action
    default: ""
    effect: Makes them flats.
  - name: Dark
    kind: action
    default: ""
    effect: Makes them darks, which calibrate the lights.
  - name: Dark Flat
    kind: action
    default: ""
    effect: >-
      Makes them dark flats: darks kept apart from the others, which calibrate only the flats of their frame size,
      matched by exposure as **Matching** says.
    note: >-
      Their master is stacked with the darks, before the flats, and never calibrates a light. A flat with a dark
      flat takes no bias, since the dark flat's master holds it. A flat without one takes the bias. Ordinary darks
      never calibrate flats.
  - name: Bias
    kind: action
    default: ""
    effect: Makes them bias frames.
    note: >-
      After any change of type, the groups are formed again, a group of one calibration frame takes it as its
      master, and the banner reads, for example, "Updated 3 frame type(s) to Dark Flat."
  - name: Clear All
    kind: button
    default: ""
    effect: Asks "Remove all frames?" in a box titled **Clear All**, and on **Yes** empties the list.
    range: "**Output** and the processing settings stay. No file is deleted."
    enabled: Outside an import. With an empty list it does nothing.
  - name: Table
    kind: readout
    default: ""
    effect: >-
      Lists one row for each file, with its **Type**, **File**, **Filter**, **Object**, **Exposure** in seconds, and
      **Calibration**. Click a header to sort by that column, and hover over **File** for the whole path.
    range: A figure the header does not state reads `-`.
    scope: >-
      For a light, **Calibration** names the masters it will be calibrated with, as `Flat` or `Dark + Flat`, or
      `None`, and its tooltip names their files. For a calibration frame, it names what calibrates its own master,
      and the tooltip says what the master is used for, as "Used by lights as flat".
    persistence: The list lasts while the window is open. Closing the window discards it.
    note: Lite cannot leave a row out or remove one. **Clear All** empties the whole list.
```

### Run Integration and Open Full Process

```controls
window: Image Integration Lite
controls:
  - name: Run Integration
    kind: button
    default: ""
    effect: Starts the run with the list and the settings as they are, in a progress window.
    enabled: When the banner shows no error and no import is under way. Disabled while a run is prepared.
    tradeoff: >-
      A large set takes long and needs much memory and disk space. Try a few frames in a folder of their own first.
      Keep the frames and the folder where they are while the run works.
    scope: >-
      Every group of the list. The Lite window takes no input until the progress window closes, and the rest of
      AfterNight stays usable.
    note: >-
      A run is refused before it starts when the folder's `registration_metrics.db` is in a format this version
      does not read: a box titled **Registration Records** says so, and nothing is changed.
  - name: Open Full Process
    kind: button
    default: ""
    effect: >-
      Opens a new Image Integration window with the frames, their types, the output folder and the settings of each
      group, as a project named `Lite handoff`.
    enabled: Outside an import.
    range: "**Reprocess existing outputs**, **Open results when complete** and **CPU Threads** stay behind."
    scope: >-
      The Lite window stays open with its list, and its banner reads "Opened the current Lite setup in the full Image
      Integration process." A change in either window does not reach the other.
    note: >-
      Lite has none of these, which the full process is for: frames left out, groups and calibration chosen by hand,
      pre-calibrated groups, settings for each group, a chosen reference, comets, rejection and quality maps, runs of
      one group, a preview of calibrated frames, presets, undo, session notes, a plan editor, a history browser and a
      project you save.
```

### The progress window

```controls
window: Image Integration - Processing
controls:
  - name: Stages
    kind: readout
    default: Preparing
    effect: Names the run's stages in order, and marks each one done, current or failed.
  - name: Current operation
    kind: readout
    default: Initializing...
    effect: >-
      Says what the run does, with a progress bar and the **Current File** under it. It ends as "Workflow Completed
      Successfully", "Workflow Completed with Warnings", "Workflow Cancelled" or "Workflow Failed".
  - name: Process Output
    kind: readout
    default: ""
    effect: Shows the run's log as it is written, with every warning, and the elapsed time under it.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop at its next stop point, and reads "Cancelling...". Closing the window or ++esc++ does the same.
    persistence: What was finished is kept and reported.
  - name: Open Report
    kind: button
    default: ""
    effect: Opens the run's `workflow_report.html` in your web browser.
    enabled: Once the run has ended with a report.
  - name: Close
    kind: button
    default: ""
    effect: Closes the progress window.
    enabled: Once the run has ended. It replaces **Cancel**.
  - name: Complete
    kind: readout
    default: Workflow completed successfully.
    effect: A box that ends a successful run.
    note: >-
      A run whose groups partly failed ends with **Completed with Errors**, "Workflow completed, but 1 frame(s)/group(s)
      had errors:" and the list, a stopped one with **Cancelled**, "Workflow was cancelled.", and a failed one with
      **Failed**, "Workflow failed:" and the reason.
```

## How it works

1. **Add Files** and **Add Folder** read each file's header in the background and give it a type. The frames are
   sorted into groups: every type by frame size, the darks also by exposure with dark flats apart, the flats by
   filter, the lights by filter, object and exposure. A group of one
   [calibration frame](../../reference/glossary.md#calibration-frame) uses it as its master.
2. **Run Integration** plans the run as Image Integration would, with Lite's settings, and stacks the bias, dark and
   flat masters the groups need. Dark flats calibrate only the flats.
3. Each light is calibrated in memory, never written. The lights of one frame size and object are registered to one
   [reference frame](../../reference/glossary.md#reference-frame), whatever their filter, so the masters of a run line
   up. Each light is written aligned under `registered_lights`.
4. The lights of each [integration group](../../reference/glossary.md#integration-group), one frame size, filter and
   object, are brought to one level, combined with **Method** and **Rejection**, and written as
   `stacked_<size>_<filter>_<object>.fit` in 32-bit floating point. The calibration masters are
   `master_bias_<group>.fit`, `master_dark_<group>.fit` and `master_flat_<group>.fit`. A name used twice gets a
   short code.
5. `.afternight` keeps what a later run reuses, and `history` a folder for each run with its report, its log, its plan
   and a copy of each master. A later run reuses every file whose frames and settings still match.

### Model

With \(x\) a sample of light \(i\), \(m\) a median, \(s\) the IKSS scale and \(r\) the group's reference, the
**Lights** choices write

\[ x' = \begin{cases} x + (m_r - m_i) & \text{Global Additive} \\ x\,m_r / m_i & \text{Global Multiplicative} \\ \dfrac{s_r}{s_i}\,(x - m_i) + m_r & \text{Global Additive Scaling} \\ \dfrac{s_r}{s_i}\,\dfrac{m_r}{m_i}\,x & \text{Global Multiplicative Scaling} \end{cases} \tag{1} \]

for each channel. **Auto** in **Rejection** picks a [rejection](../../reference/glossary.md#rejection) method for each
group by its type and its number of frames \(N\):

| Group | Frames | Auto uses |
| --- | --- | --- |
| Lights | fewer than 8 | Percentile, 0.20 and 0.10 |
| Lights | 8 to 14 | [Sigma Clipping](../../reference/glossary.md#sigma-clipping) at \(k\) |
| Lights | 15 to 24 | Winsorized Sigma Clipping at \(k\) |
| Lights | 25 to 39 | Linear Fit, 5 and 4 |
| Lights | 40 or more | GESDT, 0.30 and 0.05 |
| Bias, darks | fewer than 8, 8 to 19, 20 or more | None, Sigma Clipping at \(k\), Winsorized Sigma Clipping at \(k\) |
| Flats | fewer than 8, 8 or more | None, Sigma Clipping at \(k\) |

With \(b\) at 3.8 for flats and 3.5 for the others, and \(\mathrm{round}_{0.1}\) rounding to one decimal, the
threshold \(k\) is

\[ k = \begin{cases} 3.0 & N < 10 \\ \min\Bigl(3.5,\ \max\bigl(2.0,\ \mathrm{round}_{0.1}(b - 0.5 \log_{10} N)\bigr)\Bigr) & N \ge 10 \end{cases} \tag{2} \]

### Boundaries

- Lights that name two objects are refused before the run. Lights of another frame size or filter make a master of
  their own, and lights of another exposure are calibrated apart and stacked with the rest.
- A light group no master matches is stacked without it, and the banner warns.
- **Cancel** stops the run at its next stop point. Groups finished by then keep their masters, and the report says
  what was done.
- Each image the run writes is renamed into place only when complete, so a stopped run leaves no half-written file.
  It carries the cards `ANARTID`, which a later run checks before it reuses the file, and `ANRUNID`, the run that
  wrote it.
- No rejection map, quality map, comet stack or HDR merge is made. The registered lights are always kept.

### Worked example

The data are narrowband lights of the Eagle Nebula, M 16, from a QHY533M camera through Ha, OIII and SII filters: 64
lights of 300 seconds, 3008 × 3004 pixels, in a folder `LIGHT`, and 150 flats, 50 for each filter, in a folder
`FLAT`. There are no darks and no bias frames.

Type a new folder in **Output**, then choose **Add Folder** for `LIGHT` and again for `FLAT`. The **Dataset** card
reads 64 light and 150 flat, the filters Ha, Oiii and Sii, and the object M 16. Each light's **Calibration** reads
`Flat`, and the banner is amber: no dark or bias matches the lights, and the flats have no bias.

Choose **Run Integration**, every setting at its default. The run took under a minute on 32 threads. The
registration chose one OIII light as the reference of all 64, so the three masters line up. For the flats, with
\(N = 50\), equation (2) gives \(3.8 - 0.8495 = 2.9505\), so \(k = 3.0\). The 20 OIII lights give
\(3.5 - 0.6505 = 2.8495\), so Winsorized Sigma Clipping at 2.8, and the 19 SII lights 2.9. The 25 Ha lights take
Linear Fit at 5 and 4. The report's rejection rows say the same.

The folder then holds three flat masters such as `master_flat_3008x3004_Ha.fit`, the aligned lights under
`registered_lights\reg_3008x3004_M_16`, and `stacked_3008x3004_Ha_M_16.fit` with its Oiii and Sii twins. The Ha
master's header reads `STACKCNT` 25 and `EXPTIME` 7500, the 25 exposures added. Its median is 0.038, as one light's
is: the stack keeps the level and lowers the noise.

```figure
owed: true
images:
  - image: ../../assets/images/processes/stacking-integration/image-integration-lite-before.webp
    alt: >-
      A 100 % crop of one registered Ha light: the pillars of the Eagle Nebula lying across the left half, a small
      star cluster beside them, all in gray under a coarse grain with scattered white and dark specks.
    label: One light
  - image: ../../assets/images/processes/stacking-integration/image-integration-lite-after.webp
    alt: >-
      The same crop of the Ha master, a little darker: the pillars and the nebula around them smooth, the specks gone,
      the same stars in the same places.
    label: Master of 25
caption: >-
  The middle of one registered Ha light and of the Ha master, at 100 % in the Linear display, both stretched with
  Histogram Transformation at Shadows 0.03 and Midtones 0.02.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A first run, or lights with flats only | Every setting at its default | Read the **Calibration** column and the banner's warnings. |
| Darks of a nearby exposure or temperature | **Automatic** with **Optimize dark frames** | **Strict metadata match** to require the lights' exposure. |
| Many dithered, undersampled lights | **Enable drizzle** at **2x** and **0.9** | **3x** or **0.8** only with many well-dithered lights. |
| A wide field whose corners do not line up | **Transform** at **Polynomial 2** | **Auto**, or a higher order, while the corners stay off. |

## When it goes wrong

!!! warning "The list is not saved"
    Closing the window discards the list, and Lite saves no project. To keep the work, choose **Open Full Process**
    and save it there.

"Image Integration Lite expects one target object. Use the full Image Integration process for multi-object projects."
:   **Cause.** The lights name more than one object, as when the capture program named part of the night otherwise.
:   **Fix.** Choose **Clear All** and add the lights of one object, or use **Open Full Process**.

"Flat frames have no bias or Dark Flat calibration. Master flats will be uncalibrated and may over-correct lights."
:   **Cause.** The list holds flats and neither bias frames nor dark flats.
:   **Fix.** Add bias frames, or dark flats with **Add as** at **Dark Flat**. Without them the flats keep the camera's
    offset.

"Missing dark/bias calibration match" in the banner's tooltip
:   **Cause.** No dark or bias frame matches a group of lights in frame size, or a dark in exposure within 20 %.
:   **Fix.** Add darks of the lights' exposure or bias frames, or set **Matching** to **Automatic**.

The dark flats are listed as **Dark**
:   **Cause.** **Auto-detect** adds a dark flat as a dark, and darks never calibrate flats.
:   **Fix.** Select their rows and choose **Change Type** > **Dark Flat**.

The log warns "Cosmetic correction is on for the lights calibrated with ..., which have no dark master to find defective pixels in, so none is corrected."
:   **Cause.** Lite always asks for cosmetic correction, which finds its pixels in the dark master, and no dark
    matched the lights.
:   **Fix.** Add matching darks, or use **Open Full Process** to turn it off. The stack is otherwise unaffected.

A box titled **Registration Records**, and the run does not start
:   **Cause.** The output folder's `registration_metrics.db` was written in a format this version does not read.
:   **Fix.** Choose another output folder.

The run is over in seconds and writes nothing new
:   **Cause.** The folder holds results of the same frames and settings, which the run reuses.
:   **Fix.** Turn on **Reprocess existing outputs**, or choose a new folder.

A master is missing, or there is one too many
:   **Cause.** Lights of another frame size, or whose filter is spelt otherwise, form groups of their own.
:   **Fix.** Read **Filters**, then choose **Clear All** and add only the lights you mean.

## Related

- [Quick Start](../../getting-started/quick-start.md), a first run with Lite, step by step.
- [Image Integration](image-integration.md), the full process, for every choice Lite makes for you.
- [Image Stacking](image-stacking.md), for the rejection methods and the stacking of registered lights.
- [Narrowband Imaging](../../workflows/narrowband-imaging.md), for what to do with Ha, OIII and SII masters.
- [Image Viewer](../../interface/image-viewer.md), to inspect a master with Autostretch.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

Lite runs the workflow of Image Integration, and owes the same credits:

- Color-filter frames are debayered by methods adapted from [RawTherapee](https://github.com/Beep6581/RawTherapee)'s
  demosaic code as [librtprocess](https://github.com/CarVac/librtprocess) packages it, under GPL-3.0-or-later.
- The star detection and its point-spread-function fit are adapted from the star finder and the profile fit of
  [Siril](https://gitlab.com/free-astro/siril), Copyright (C) 2005-2011 Francois Meyer and Copyright (C) 2012-2026
  team free-astro, under GPL-3.0-or-later.
- The [drizzle](../../reference/glossary.md#drizzle) corner interpolation is adapted from Siril's routine, which
  follows the [STScI drizzle library](https://github.com/spacetelescope/drizzle)'s, under the BSD 3-Clause notice of
  the Association of Universities for Research in Astronomy (AURA).
- The auto-adaptive weighted mean is adapted from [DeepSkyStacker](https://github.com/deepskystacker/DSS)'s routine,
  by David C. Partridge with Michael A. Covington and Simon C. Smith, under the BSD 3-Clause License of Luc Coiffier
  and the DeepSkyStacker contributors.
- The median selection networks the normalization analysis uses are RawTherapee's, by Flössie and Ingo Weyrich, under
  GPL-3.0-or-later.
- **About > About AfterNight** credits these works and reproduces the BSD notices.
