---
kind: reference
summary: >-
  What Image Calibration needs of its masters, the files it writes, its messages and log lines, what too little and too
  much of each correction look like, what a run costs and what the window keeps.
---
# Image Calibration Reference

This page holds what [Image Calibration](image-calibration.md) leaves out to stay readable beside its window: what each
[master](../../reference/glossary.md#master) must hold, the files a run writes, the messages the window shows and the
lines it writes to the [Session Console](../../interface/log-viewer.md), what too little and too much of each
correction look like, what a run costs and what the window keeps. Each section can be read on its own.

## The masters

Every master
:   One file for the whole list, of the lights' width, height and channel count and, for a
    [CFA](../../reference/glossary.md#cfa) mosaic, of their filter pattern. Its
    [pixel type](../../reference/glossary.md#pixel-type) does not matter: every master
    is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), as the lights are. A
    floating-point master must hold such fractions, as the stacks of AfterNight write them, and not values in
    [ADU](../../reference/glossary.md#adu). The window prepares none of them: what each must hold, it holds before you
    choose it.

Bias master
:   A stack of [bias frames](../../reference/glossary.md#bias-frame) as they are. The window subtracts it from the
    lights only, never from the dark or the flat master.

Dark master
:   A stack of darks of the lights' exposure, gain, offset and temperature. Stacked from raw darks, it holds the bias
    as well, as Image Integration's master dark does: use it without the bias master, unscaled. To scale it, by
    **Dark scaling** or by [dark optimization](../../reference/glossary.md#dark-optimization), make it free of bias:
    calibrate the darks by the bias master in this window first, stack the results, and check the bias master beside
    it.

Flat master
:   A stack of flats, each calibrated before the stack by a [dark-flat](../../reference/glossary.md#dark-flat) or, without
    one, by the bias master, as Image Integration calibrates its flats. This window can do that pass: list the flats,
    check the one master they need, calibrate, and stack the results. A flat that still holds its bias corrects too
    little vignetting.

## The files a run writes

One file for each light that succeeds, in the output folder, named after the light, or `image_0000`, `image_0001` and
so on when **Use source filename** is off, followed by **Suffix**. For a light `light.fit` and the suffix `_cal`:

| Format | File | Pixel type | Refused |
| --- | --- | --- | --- |
| **FITS (Recommended)** | `light_cal.fit`: a `.fit`, `.fts` or `.fits` extension is kept, any other becomes `.fits` | 32-bit or 64-bit floating point, by **Precision** | An X-Trans mosaic |
| **TIFF** | `light_cal.tiff` | 32-bit or 64-bit floating point, by **Precision** | Nothing |
| **Original input format** | `light_cal.fit`, with the light's own extension, or `.fits` when it has none | The light's own, each value rounded to the nearest code | A mosaic in camera [RAW](../../reference/glossary.md#raw), PNG or JPEG |

- A 5544 by 3684 mosaic takes about 82 MB at 32-bit and 163 MB at 64-bit. An integer type of
  **Original input format** keeps no value below zero or above full scale, and is no master to process further: see
  [File Formats](../../reference/file-formats.md).
- The file keeps the light's [header cards](../../reference/glossary.md#header-card), its acquisition data and its
  filter array, and the codec writes the structural cards anew. No card names the masters, the dark's factor or the
  sigma values, and no `HISTORY` card is added. The Session Console names the masters, so keep its log, or note the
  settings, with the files.
- Only the pixel type is stated. The export settings of the application are not applied.
- Nothing else is written: no report, no [rejection](../../reference/glossary.md#rejection) map, no list. Nothing
  opens in a view, and no file is renamed to avoid another: a file of the same name is replaced.

## Messages

"Some files were passed over", with "Added N file(s); passed over M already listed or kept out by the filter."
:   An import added fewer files than it was given, because some were listed already or did not pass the options of
    **Add Folder**.

"Some files were skipped", with "could not be read. First error:" and "See the log for per-file details."
:   Some files could not be read. The others are in the list.

"You need to define an output directory before executing image calibration"
:   **Calibrate Images** was chosen without an output folder. Nothing started.

"Wait until the files being added and the cosmetic correction estimate have finished before calibrating."
:   Headers were still being read, or **Auto-estimate** was counting. Nothing started.

"Error calibrating images", with "An error occurred when calibrating images:" and "Some images in the list have different size"
:   The lights differ in width or height. Nothing was read or written. "different color depth" says the same of their
    pixel type or channel count.

"Error calibrating images", with "Could NOT load calibration dark frame" and the path
:   The master could not be read. The message names the bias, the dark or the flat. Nothing was written.

"Error calibrating images", with "Calibration flat file size is different than the target images"
:   The master's width or height is not the first light's. "file depth is different" says the same of its channel
    count, and the message names the master. Nothing was written.

"Error calibrating images", with "Calibration did not produce any output images. First error:" and a reason
:   Every light failed. The reasons a light fails include these.
    A master of another filter array:
    "bias and dark masters must match the light's channels and CFA; only an RGB flat may instead be Mono".
    A flat that cannot be normalized:
    "one flat channel or CFA site has a zero or non-finite central mean".
    A format that cannot hold the mosaic:
    "a colour filter array mosaic cannot be written as", or "an X-Trans mosaic cannot be written as FITS".

"Image Calibration: Calibration completed with warnings. N of M images succeeded. First error:" and a reason
:   Some lights failed and the others were written. The text shows over the controls and is logged.

"Image Calibration: Calibration completed with warnings. Cosmetic correction was on but neither the cold nor the hot sigma was checked, so no pixel was corrected."
:   The run wrote its files without any cosmetic correction. The same note ends the warning above when some lights
    failed too.

"Image Calibration: Calibration executed successfully!"
:   Every light was written. The text shows over the controls.

"Cosmetic Correction Estimation", with "Detected N cold pixels and M hot pixels"
:   The count of **Auto-estimate**. Zero of both means the thresholds as set mark nothing in this dark. The thresholds
    are not changed.

"Cosmetic Correction Estimation", with "a detection with neither polarity enabled is a request to find nothing"
:   **Auto-estimate** was chosen with both sigma boxes unchecked.

"Cosmetic Correction Estimation", with "Could NOT load dark frame for estimation"
:   The dark master could not be read.

"Delete selected file", with "This will delete the selected file from the disk permanently. Are you sure you want to proceed?"
:   The question of **Delete From Disk** in the row menu. "Could not delete" with the path follows when the file could
    not be deleted, and its row is gone all the same.

A stopped run, by **Cancel** or by closing the window, ends without a message.

## In the Session Console

| Line | When |
| --- | --- |
| `Image Calibration: using dark master '<path>'` | A run read a master, one line for each, with `bias` or `flat` for the others. |
| `Image Calibration: the dark marks <cold> cold and <hot> hot pixels` | A run with cosmetic correction mapped the dark. |
| `Image Calibration: '<light>' -> '<file>'` | A light was written. |
| `Image Calibration: '<light>' failed: <reason>` | A light failed, with its own reason. |
| `Image Calibration: the dark scale of '<light>' could not be optimized; the fixed scale <factor> was used` | Dark optimization could not serve an RGB or small light. |
| `Image Calibration: <calibrated> calibrated, <failed> failed in <time> seconds` | The run ended, with ` (stopped)` after a stop. |
| `Image Calibration: detected <cold> cold pixels and <hot> hot pixels` | **Auto-estimate** counted. |
| `Image Calibration: cosmetic correction is on but neither the cold nor the hot sigma is checked, so no pixel is corrected` | A run started with both sigma boxes unchecked. |
| `Image Calibration: could not read its settings:` | The kept values could not be read, and the window opened with its defaults. |

A warning or an error the window shows is logged too.

## Too little and too much

What each correction looks like when it does too little or too much, where to start and in what order to adjust.
These are places to start from, not results measured on an image.

| Correction | Start | Too little | Too much | Order |
| --- | --- | --- | --- | --- |
| Bias, dark and flat that match the lights | The masters you have, dark optimization off, **Dark scaling** and [flat normalization](../../reference/glossary.md#flat-normalization) as they open | Fixed pattern, thermal signal, dust or vignetting remains | Dark residuals turn negative, the background falls too low, or the flat overcorrects | How the masters were made and whether they fit, then the bias, the dark, the flat, and the scaling last |
| A dark of another exposure or temperature | A dark free of bias at the ratio of the exposures, then dark optimization for one-channel lights | Thermal pattern that follows the dark remains | The factor found misleads, and more values fall below zero | Exposure, gain and temperature, a fixed factor, the result, then optimization |
| [Hot pixels](../../reference/glossary.md#hot-pixel) and [cold pixels](../../reference/glossary.md#cold-pixel) | Cosmetic correction on with both thresholds as they open, and **Auto-estimate** as a count only | Single defects remain | Compact signal or many ordinary pixels are replaced | The dark, the count, one threshold at a time, then every channel at 100 % |
| The flat | Normalization on, with a calibrated flat of the lights' mosaic and channels | Vignetting or dust shadows remain | Corners grow too bright, gradients or color structure appear, or zeros turn into pixels that are not a number | The match of the optics, the central third of the flat, then normalized against direct division |
| Values below zero | [FITS](../../reference/glossary.md#fits) at 32-bit, the [pedestal](../../reference/glossary.md#pedestal) at 0, then a small one only if the next step needs one | Negative values the next tool or format cannot take | The level is raised for nothing, or high values pass full scale and are cut in an integer file | The bias and dark mismatch, floating point kept, the statistics, then the pedestal |
| A long list | One representative light, FITS at 32-bit, optimization and cosmetic correction off | A failure in a later file shows only at the end | Every costly option at once multiplies the time and hides the cause | One light, a few lights, the dark and flat options, then the whole list |

## What a run costs

- The lights are calibrated one after the other. The arithmetic of each, and the cosmetic replacement, are spread
  over the processor's threads.
- The work grows with width times height times channels times the number of lights. Reading, writing and the
  floating-point copy of each light add memory and disk.
- The masters are read once a run. The cosmetic correction scans the dark once to mark its defects, then copies each
  light once more to replace them. **Auto-estimate** is a scan of its own.
- Dark optimization scans the central 512 by 512 square of each light it serves.
- **64-bit double** about doubles the memory and the disk of the same run.
- Try one representative light before you turn the costly options on for the whole list.

## What the window keeps

- When the window closes, it keeps for the next window: every value, each master's check and path, the output folder
  when one is chosen, and the list in its order. Its size is kept as every window's is.
- A kept output folder that no longer exists is shown all the same, and created when a run starts. A kept file that
  can no longer be read is reported when the list is read again, and left out.
- The statuses are not kept, and every row opens **Pending**. The three advanced sections open closed, and the
  preview starts afresh.
- **Reset** returns the values to their defaults and leaves the list. The new values are kept when the window
  closes.
