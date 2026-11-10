---
kind: tutorial
stages: [calibrate, register, stack]
menu: Process > Preprocess > Image Integration Lite
summary: >-
  Calibrate, register and stack your first set of frames with Image Integration Lite.
needs: []
---
# Quick Start

You start with the frames of one night on one target: your [light frames](../reference/glossary.md#light-frame) and,
when you have them, darks, flats and [bias frames](../reference/glossary.md#bias-frame). You end with one stacked,
linear [master](../reference/glossary.md#master) open in AfterNight, and a report of what was done to get it. **Image Integration Lite** does the calibration, the registration and the stacking in one
run, with settings chosen for a first result.

## What you need

- [ ] The light frames of one target, taken with one camera setup, as the camera wrote them
- [ ] The matching dark, flat and bias frames, when you have them
- [ ] A new, empty folder for the results, on a disk with room for several times the size of your lights
- [ ] Folders that hold nothing but these frames, if you plan to add a whole folder

Lights alone are enough to run. The result is then stacked but not calibrated, and the window says so.

## Your camera

- **A color camera.** Use the raw or [FITS](../reference/glossary.md#fits) files as the camera wrote them, and do not
  [debayer](../reference/glossary.md#debayer) them first. The
  calibration works on the raw mosaic, and the registration turns it into color with the filter pattern the files
  state. You get one linear color master.
- **A mono camera.** Use the lights of one filter, with the flats taken through that filter. Each filter makes a
  group and a master of its own, so add one filter for this first run. You get one linear monochrome master.

## Steps

```figure
image: ../assets/images/getting-started/quick-start.png
alt: >-
  The Image Integration Lite window. A green banner at the top reads Ready. Under it, the Dataset card counts 18
  light, 40 flat, 20 dark and 30 bias frames of the object NGC253. The Setup card holds the Output folder with its
  Browse button and the options of the run. The Processing Settings card lists four collapsed lines: Calibration,
  Registration, Normalization and Stacking. The Frames card has the Add as box, the Add Files, Add Folder, Change
  Type and Clear All buttons, and a table of the files with their type, object, exposure and calibration. Run
  Integration, Open Full Process and Close are at the bottom.
caption: >-
  Image Integration Lite with a night on NGC 253 loaded and ready to run: a color camera's lights with their darks,
  flats and bias frames.
pins:
  - {n: 2, x: 10.2, y: 30.9}
  - {n: 3, x: 43.6, y: 71.6}
  - {n: 4, x: 97.0, y: 83.0}
  - {n: 5, x: 16.4, y: 48.4}
  - {n: 6, x: 8.2, y: 10.9}
  - {n: 7, x: 69.0, y: 97.3}
```

### Open Image Integration Lite

Choose **Process > Preprocess > Image Integration Lite**. The window needs no open image. The Dashboard's **Image
Integration** card opens the full process, which is another window.

!!! see "You should see"
    A red banner: "No light frames available." On a first run it also says "Output directory is required." Later,
    the window restores the last folder you used, even one that is gone, and its own size.

### Choose the output folder

Choose **Browse** beside **Output** and select the new, empty folder. Everything the run writes goes there. Your
frames are only read.

Use a new folder for each data set. A folder that already holds results of the same frames lets the run reuse them
instead of computing them again, unless **Reprocess existing outputs** is on. A run that reuses everything is over
in seconds and opens no master.

??? wrong "If it goes wrong: the banner says the output directory cannot be used"
    "Output directory cannot be created (parent directory is not writable).", "Output directory path is not a
    directory." or "Output directory is not writable.": choose a folder on a disk you can write to.

### Add your frames

Leave **Add as** at **Auto-detect** and choose **Add Files**, or **Add Folder** for a folder and everything under
it. **Auto-detect** reads each file's type from its header. Where the header does not say, it takes the type from a
folder on the file's path named for one: lights, darks, flats, bias. A file that says nothing either way is added as
a light. File names are not read.

When your files state no type and sit in one folder, add each kind on its own: choose its type in **Add as** first.
A file that is already listed is not added twice.

!!! see "You should see"
    The banner reads "Importing frames..." and the controls are disabled for a moment. Then **Frames**, **Filters**
    and **Object** in the **Dataset** card fill in, and the table lists every file with its type.

??? wrong "If it goes wrong: the import adds no rows"
    None of the files is a still image AfterNight can read, and the banner returns to "No light frames available."
    Use a format listed in [Supported Formats](supported-formats.md). Frames inside a video or a
    [sequence](../reference/glossary.md#sequence) file are
    extracted first.

### Check the table

Read the **Type** column. Where a type is wrong, select the rows and choose **Change Type**, or right-click a row
and choose **Set as**. This changes how the run uses the file, never the file.

Then read the **Calibration** column of the lights. It names what each light will be calibrated with, and its
tooltip names the files. **Object** in the **Dataset** card must show one name, and for a mono camera **Filters**
must show the one filter you meant to add.

!!! see "You should see"
    Every light row shows the calibration you expect, such as "Dark + Flat". The table has no column for the size of
    a frame: lights of another size are stacked as a group of their own.

??? wrong "If it goes wrong: a warning about a missing flat calibration match"
    The flats do not match the lights' filter or setup, or darks and bias frames were added without any flat.
    Correct the types, add the matching flats, or choose **Open Full Process** to assign the calibration by hand.

### Leave the processing settings

Leave the four lines of **Processing Settings** closed for this first run. They read **Calibration: Automatic, flat
auto-level**, **Registration: [Homography](../reference/glossary.md#homography), auto reference**, **Normalization:
Global Additive Scaling** and **Stacking: Average, Auto, final solve off, [drizzle](../reference/glossary.md#drizzle)
off**. With the final plate solve off, the run needs no solver
and no star catalog.

Keep **Open results when complete** on. **Limit memory usage** makes the stacking work in tiles. It does not bound
everything else the run allocates.

### Read the banner

The banner at the top says whether the run can start. Red is an error and must be resolved. Amber is a warning:
hover over the banner to read each one, and decide whether you accept it. Without
[calibration frames](../reference/glossary.md#calibration-frame) the amber
banner reads "No calibration frames provided. Lights will be integrated without bias, dark, or flat calibration."

The banner checks that the run can be built. It does not judge whether the calibration matches are good ones, or
whether the frames are sharp.

!!! see "You should see"
    A green "Ready." or an amber "Ready with 1 warning(s).", and **Run Integration** enabled.

??? wrong "If it goes wrong: Run Integration stays disabled"
    The import is still running, or the banner is red. It lists up to three errors: a problem with the output
    folder, no light frames, or "Image Integration Lite expects one target object." The last one means the lights
    carry more than one object name in their headers, as when the capture program named part of the night "Target".
    Choose **Clear All** and add only the lights of one object, or choose **Open Full Process**, which takes the
    frames with it.

### Run the integration

Choose **Run Integration**. Keep the frames and the output folder where they are while it runs. Changes you make in
the window after this point do not change the run.

**Cancel** in the progress window stops the run at the next point where it can stop. What was finished by then is
kept and reported.

!!! see "You should see"
    The banner changes to "Preparing integration...", the button is disabled and the **Image Integration -
    Processing** window names each stage in turn. At the end a box titled **Complete** reads "Workflow completed
    successfully." Choose **OK** and close the progress window.

??? wrong "If it goes wrong: the run stops with an error"
    Keep the output folder and read the message. A warning that lists frames before the run starts names the ones
    whose size or format does not fit: remove or correct them and run again. A "Registration Records" warning
    means the folder holds records written by a newer AfterNight: use a new folder. A run that fails later ends
    with a box titled **Failed** that reads "Workflow failed:" and the reason, and a cancelled one with "Workflow
    was cancelled."

### Look at the result

With **Open results when complete** on, the master opens in the workspace. It is
[linear](../reference/glossary.md#linear-image), so it looks nearly black: choose
[**Autostretch**](../reference/glossary.md#autostretch) in the display mode box of its toolbar to see it. That changes the
display only, not the image.

Zoom to 100 % and look at the stars and the background. Then open the report of the run, `workflow_report.html`
under `history` in the output folder, and check that the frames, the calibration and the warnings are what you
intended.

!!! see "You should see"
    The master open in its own view, named after its file, and the window's banner back at "Ready."

??? wrong "If it goes wrong: the master is gray, or its colors are wrong"
    The registration makes a color image only from files that state their filter pattern. Files that state none are
    stacked as monochrome, and a wrong pattern gives wrong colors. Choose **Open Full Process** and set the
    pattern in the registration settings. That corrects a wrong pattern. It cannot make color of files that state
    none.

## The result

You have one stacked master for each group of lights: `stacked_<group>.fit` in the output folder, linear, with your
frames untouched. Beside it are the masters the run built - `master_bias_<group>.fit`, `master_dark_<group>.fit`
and `master_flat_<group>.fit` - and the calibrated and registered frames it went through. A short code is added to
a name that would otherwise be used twice.

Each run leaves its record under `history`, in a folder of its own: the report as HTML and as Markdown, the log and
the settings it ran with. The folder `.afternight` holds what a later run needs to reuse this one. All of it takes
disk space, and a later run in the same folder reuses or replaces what matches.

```figure
image: ../assets/images/getting-started/quick-start-result.webp
alt: >-
  The AfterNight main window with one image open, in a tab named stacked_5544x3684_NGC253: a pale galaxy seen
  almost edge-on, in the middle of a field of stars on a gray background. The display mode box in the view's toolbar
  reads Autostretch.
caption: >-
  The stacked master of NGC 253 as the run opened it, shown with Autostretch. Its colors are those of the camera:
  color calibration comes later.
```

Lite keeps its list of frames only while the window is open, and does one target at a time. It does not take video
or sequence files, [masks](../reference/glossary.md#mask) or comets. For those, and to assign calibration by hand or save the work as a project,
choose **Open Full Process**: it opens Image Integration with the frames and the settings you have here.

## Next

- [Image Viewer](../interface/image-viewer.md) shows how to inspect the master and what Autostretch does.
- [Histogram Transformation](../processes/stretching/histogram-transformation.md)
  [stretches](../reference/glossary.md#stretch) the master into a picture.
- [Image Integration Lite](../processes/stacking-integration/image-integration-lite.md) describes every control of
  the window you just used.
- [Image Integration](../processes/stacking-integration/image-integration.md) is the full process, for several
  targets, projects and calibration by hand.
- [Broadband Imaging](../workflows/broadband-imaging.md) goes from the frames to the finished picture.
- [Supported Formats](supported-formats.md) says how to save and export the result.
