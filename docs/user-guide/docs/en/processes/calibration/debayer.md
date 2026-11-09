---
kind: process
stage: calibrate
menu: Process > Color Spaces > Debayer
summary: >-
  Reconstructs full red, green and blue values from the Bayer mosaic of a one-shot-color camera, normally after
  calibration and before registration.
data_in: cfa
derived: true
---
# Debayer

## Purpose and expected result

Debayer reconstructs full red, green, and blue values from a single-channel
Bayer color-filter-array (CFA) mosaic. It is for one-shot-color data that is
still stored as CFA samples, normally after sensor calibration and before
ordinary RGB registration, stacking, or color calibration.

The source view is not changed. A still image produces a new RGB view, three
new monochrome channel views, or both. A sequence produces a new demosaiced RGB
sequence, three new channel sequences, or both, each written to AfterNight's
temporary directory and opened as a new view.

Method-selection advice on this page is guidance. Exact UI, data conversion,
metadata, output, and failure behavior comes from AfterNight's implementation.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > Debayer** |
| Enabled when | An image view is active. The target list holds every open view; it does not pre-filter RGB, monochrome, or CFA targets. A second Debayer window for the same target, or a conflicting exclusive process, is not launched. |
| Inputs | One open still or sequence view holding a single-channel mosaic. **From metadata** uses the filter array the image states, a Bayer or an X-Trans (Fujifilm) array; a named pattern (RGGB, BGGR, GBRG, GRBG) can demosaic a one-channel image that states none, once you confirm the question, and is refused over an image that states an X-Trans array. A colour image is refused. |
| Outputs | One RGB view, three monochrome R/G/B views, or all four, in the mosaic's own sample type. The source remains unchanged. |
| Recommended image state | Calibrated, linear, unstretched CFA data. Debayer after bias/dark/flat correction unless a CFA-aware downstream workflow deliberately defers reconstruction. |
| Preview support | None. **OK** runs the full still or sequence; there is no Apply or Reset action. |
| Sequence support | Yes. Every frame is demosaiced and written into a new sequence of the source's container; a progress window's **Cancel** stops the run and leaves nothing behind. |
| Mask support | Not supported. |
| External dependencies | None at run time: the demosaic methods are part of AfterNight's image engine. RAW decoding happens before this process. |

## Before you start

1. Calibrate the CFA data while it still has one sensor sample per pixel. A
   conventional order is bias/dark/flat correction, optional CFA-safe cosmetic
   correction, then Debayer.
2. Confirm that the image really contains an untouched Bayer mosaic. A file can
   lack CFA metadata even when its pixels are CFA, and a file can retain stale
   CFA metadata after some external conversion.
3. Decide where the pattern comes from. **From metadata** never guesses: it
   uses the filter array the image states, including its phase after a crop,
   and refuses an image that states none. If the file states nothing, choose the
   camera's exact layout instead.
4. Keep the original CFA source. Debayer creates new views, but the original is
   the reliable starting point for comparing patterns or methods.

## Procedure

1. Activate the CFA still or sequence and choose **Process > Color Spaces >
   Debayer**. Check that **Target Image** names the intended open view.
2. Set **Bayer/mosaic pattern**. Keep **From metadata** when the file states
   its filter array; otherwise choose the camera's exact layout.
3. Choose **Demosaicing method**. **RCD** is the default. For a quick wiring or
   pattern check, **Bilinear** runs the engine's fast estimate.
4. Choose **Output mode**. Start with **Combined RGB Color** for an ordinary
   color workflow; request separated channels only when they have a specific
   diagnostic or processing use.
5. Select **OK**. If AfterNight reports that the file has no CFA definition,
   choose **No** unless you have independently verified that the samples are a
   Bayer mosaic, and then pick a named pattern: **From metadata** refuses such an
   image. A sequence the view shows demosaiced is not asked about, because
   Debayer reads its recorded mosaic frames.
6. For a sequence, monitor **Executing debayer to sequence _name_** and use
   **Cancel** if required.
7. Confirm that the expected new view names appear. At 100% zoom, inspect star
   edges and high-contrast detail for zippering, false color, or a repeated
   checkerboard. Recheck the CFA pattern before changing algorithms.

The dialog is read once, when **OK** is pressed; changing a selector while the
run works does not change that run.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Bayer/mosaic pattern** | Choice | **From metadata** | None | **From metadata**, **RGGB**, **BGGR**, **GBRG**, **GRBG** | **From metadata** demosaics under the filter array the image states, phase included, and refuses an image that states none. A named pattern places that layout at the image's top-left pixel, whatever Bayer pattern the image states; over an image that states an X-Trans array it is refused by name, before anything runs. | Always visible and enabled; its tooltip reads "From metadata demosaics under the filter array the image states, and refuses an image that states none." The item's stored text for **From metadata** is `Auto`, the text a saved pattern choice keeps for "the image's own filter array". | A wrong pattern commonly produces swapped colors, false-color edges, or a checkerboard. | Full still/sequence; all output channels; no mask | Dialog lifetime only; every new window starts at **From metadata**. |
| **Demosaicing method** | Choice | **RCD** | None | **Bilinear**, **VNG4**, **AHD**, **AMaZE**, **LMMSE**, **RCD** | Chooses one engine reconstruction. **Bilinear** is the engine's gradient-weighted fast estimate, not textbook bilinear interpolation. Over an X-Trans mosaic every method is written for a two-by-two block, so the chosen one runs as its X-Trans counterpart: **Bilinear** as the engine's fast X-Trans estimate (`XTransFast`), every other method as three-pass Markesteijn (`MarkesteijnThreePass`). | Always visible and enabled. DCB, HPHD and IGV are not offered. | Methods use different directional, color-difference, and border reaches. Compare artifacts on representative linear data; the heavier methods cost more time and working memory. | Full still/sequence; all output channels; no mask | Dialog lifetime only; every new window starts at **RCD**. |
| **Output mode** | Choice | **Combined RGB Color** | None | **Combined RGB Color**, **Separate RGB Channels**, **RGB Color + Separate RGB Channels** | Selects one 3-channel output, three 1-channel outputs, or both. | Always visible and enabled. | Separate and combined outputs are made from the same colour image. For a sequence each output is its own written sequence. | Full still/sequence; no mask | Dialog lifetime only; every new window starts at **Combined RGB Color**. |
| **Target Image** | Choice | The view bound when the process opens | None | All currently open image/sequence views; no CFA-only filter | Changes the input view. | Requires open views. The process window is tied to the selected view lifecycle. | Choosing an RGB target is refused when the run starts; a monochrome target needs a named pattern. | Full still/sequence | Dialog lifetime only. |
| **OK** | Action | Available | None | One execution at a time | Asks the CFA question when the target is not shown as a mosaic - except a sequence the view shows demosaiced - reads the dialog, then starts the run on a worker. | **OK** and **Close** are disabled during execution. They return after failure or cancellation; otherwise the window closes. | Still processing has no progress window and cannot be cancelled. A sequence run takes the view for the run and writes new sequence files. | Full still/sequence | Not persisted. |
| **Close** | Action | Available | None | Closes an idle process window | Closes without processing. | Disabled after **OK** while execution is active. A window closed during a run stays hidden until the run lands. | No output when closed before execution. | Dialog only | Not persisted. |
| **Yes** / **No** in **Debayer** warning | Conditional confirmation | **No** | None | Appears when the selected view reports no CFA definition | **Yes** runs anyway; **No** returns to the dialog. | Only on **OK** for a target that is not shown as a mosaic. A sequence the view shows demosaiced is not asked about: Debayer reads its recorded frames, and a recorded frame that states no filter array is still refused by name under **From metadata**. | Override only when independent acquisition information proves that the samples are CFA, and choose a named pattern. | Full still/sequence | Not persisted. |
| **Cancel** in sequence progress | Action | Not requested | None | While the sequence progress window is open | Stops the run; the engine observes the stop between tiles of a frame and the run between frames. | Sequence execution only. | A cancelled run opens nothing and removes the sequences it was writing. | Full sequence; no mask | Not persisted. |

## How the algorithm works

### Conceptual pipeline

1. Resolve the filter array. **From metadata** takes the image's own
   description, phase included; an image that states none is refused with
   `the image states no colour filter array to demosaic under`. A named pattern
   is placed at the image's origin; over an image that states an X-Trans array
   it is refused with `the image states an X-Trans filter array, which a
   two-by-two Bayer pattern does not describe; From metadata demosaics it under
   its own`.
2. Refuse an image of more than one channel: `the image has more than one
   channel; only a one-channel mosaic is debayered`.
3. Plan the chosen method over that filter array - over an X-Trans array, its
   X-Trans counterpart (see the table below). The white level, the sample
   value that means a fully exposed photosite, is the largest code of an
   integer representation and 1.0 for a floating one stated as a fraction of
   full scale. A floating mosaic in a physical or sensor-code domain - a
   floating FITS frame reads back as a physical quantity - has no full scale, so
   its own brightest finite valid sample stands for one, never below 1.0. Every
   highlight threshold is derived from the white level.
4. Demosaic into a three-channel image of the same width, height and sample
   type, on the operation's threads, tile by tile.
5. For separated output, extract the red, green and blue stored channels of
   that colour image as three grey images.

The methods the dialog offers are the engine's:

| UI method | Engine method and reach |
| --- | --- |
| **Bilinear** | The fast gradient-weighted estimate: four directional weights per green estimate, red and blue from their difference against green. Reach 5 pixels. Its result is the same on Windows and Linux. |
| **VNG4** | Variable number of gradients over four colours, treating the two green sites separately. Reach 4 pixels. |
| **AHD** | Adaptive homogeneity-directed interpolation that decides on a luminance homogeneity measure. Reach 5 pixels. |
| **AMaZE** | Aliasing-minimised, zipper-eliminated interpolation with a highlight-safe fallback above the white level's clip point. Reach 16 pixels. |
| **LMMSE** | Directional linear minimum mean-square-error interpolation, the strongest on noisy data. Reach 12 pixels. |
| **RCD** | Ratio-corrected demosaicing: horizontal or vertical green chosen by a low-pass discrimination measure, corrected by local colour ratios. Reach 9 pixels. |

Every method above is written for a two-by-two Bayer block. Over an X-Trans
mosaic (a six-by-six array) the chosen method runs as its X-Trans counterpart:
**Bilinear** as the engine's fast X-Trans estimate, `XTransFast`, and every
other method as three-pass Markesteijn, `MarkesteijnThreePass`, judged over
luma and colour differences in camera RGB. The view's name keeps the method
chosen in the dialog; the image's history (lineage) names the method that ran.

### Model and equations

For a Bayer pattern, the recorded mosaic value at pixel `(x,y)` is

\[
M(x,y)=C_{p(x,y)}(x,y),\qquad p(x,y)\in\{R,G,B\},
\]

where `M` is the single recorded sample, `C` is the unknown full-color image,
and `p` is the channel selected by the repeating 2×2 pattern. Debayer preserves
the known sample and estimates the two missing channels to form
`Ĉ_R(x,y)`, `Ĉ_G(x,y)`, and `Ĉ_B(x,y)`. VNG4 is the exception: at a green site
it averages the measured green with its own estimate.

Several methods exploit the fact that color differences often vary more
smoothly than brightness. One representative red estimate is

\[
\widehat{R}(x,y)=\widehat{G}(x,y)
-\mathcal{I}\!\left(G-R\right)(x,y),
\]

where `I` is a method-specific spatial/directional interpolation. This equation
explains the general color-difference idea; it is not a claim that all six
methods use the same estimator. AHD chooses between homogeneity candidates,
LMMSE minimizes a local estimation-error model, and AMaZE/RCD also use adaptive
ratios and directional discrimination.

### Boundary and failure behavior

- Every method fills the band at the picture's edge that its reach cannot
  cover with one shared rule, a three-by-three average, so a band 4 to 16 pixels
  wide (the reach in the table above) is the same rule whatever the method.
- Samples are demosaiced in their own representation; nothing is rescaled to a
  sixteen-bit working scale first.
- A mosaic that states which of its samples mean something - non-finite
  samples left out, zero as no data, a stated range, or a mask - is demosaiced
  under that statement. Every colour pixel whose estimate reads a sample that
  means nothing (the method's reach inside the picture, one pixel in the border
  band) is cleared in a mask the colour image carries, and separated channels
  keep it. Such a pixel still holds a number; the mask says it means nothing.
- A failure is reported in a message titled **Error running Debayer** that
  begins `An error occurred when executing the Debayer process:` followed by the
  reason, and in the log. No output view is opened.
- A sequence another run holds shows the busy warning; a still image whose edit
  is still being written shows the busy warning too.
- **Cancel** on a sequence's progress window stops the run quietly; a stopped
  run opens nothing and removes the files it was writing.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Pattern + method | Verify the camera pattern first; then compare **RCD** with one alternative on the same linear frame. | Wrong pattern can resemble severe algorithm artifacts or absent color. A fast method may leave more directional/zipper artifacts on difficult edges. | A more complex method can smooth or reshape tiny detail and consume more resources without fixing a wrong pattern. | 1. Pattern; 2. confirm CFA input; 3. method; 4. inspect at 100%. |
| Method + output mode | Use **Combined RGB Color** while choosing a method. | Separate channels alone can hide cross-channel false-color relationships. | Requesting all outputs increases view count and, for sequences, disk use without changing reconstruction. | 1. Combined RGB; 2. method comparison; 3. request channels only if needed. |
| From metadata + missing/uncertain metadata | Select the known camera pattern manually. | The run is refused because the image states no filter array. | A named pattern applied to a cropped mosaic ignores the crop's phase; prefer **From metadata** when the image states it. | 1. Acquisition record; 2. **From metadata** or manual layout; 3. method. |

These are conservative diagnostics, not universal quality rankings. Sensor
noise, optics, sampling, and subject detail can change which reconstruction is
preferable.

## Inputs, outputs, and metadata

For a still image, dimensions are unchanged and every output keeps the mosaic's
sample type, any of the eight AfterNight stores. **Combined RGB Color** creates
`<source> - RGB - <method>` with the history step `Debayer: RGB image created
from RAW image '<source>'`. Separated outputs create `<source> - RED -
<method>`, `<source> - GREEN - <method>`, and `<source> - BLUE - <method>`,
whose histories begin `Debayer: Red channel extracted from RAW image
'<source>'` and likewise for Green and Blue. With both, the RGB view opens
first. Each new view opens beside the source.

For a sequence, every frame is read as recorded, whatever frame the player
shows and whether or not the view displays it demosaiced, and demosaiced by the
dialog's method. The combined output is a new sequence named `<source> - RGB -
<method>` with the history step `Debayer: RGB sequence created from RAW
sequence '<source>'`; separated outputs are sequences named `<source> - RED`,
`<source> - GREEN`, and `<source> - BLUE`, with `Red channel extracted from
sequence '<source>'` and likewise. Each is written in the source's container in
AfterNight's temporary directory, converted as that container requires, and
published only when every frame is written. Save or export it when a durable
file is wanted.

The colour image keeps the mosaic's acquisition record, meaning, header cards,
plate solution and any decision already made about how it is written; it states no filter array, is marked
demosaiced, and its lineage names the engine's demosaic and the method. Each
separated channel is the stored-channel extraction of that colour image and
keeps its cards and meaning.

## Performance

Every method scales approximately with pixel count, `O(width × height)`, but
the work per pixel differs: Bilinear and VNG4 make about three passes, RCD four,
LMMSE five and AMaZE six. The engine works tile by tile on the operation's
threads, each tile with its own scratch, and the result does not depend on the
number of threads. Besides the source, a run holds the three-channel result in
the mosaic's representation; a source that is not 32-bit float is gathered into
a 32-bit float working plane first. Separated output adds three single-channel
images.

A sequence run writes one new sequence per output: the combined sequence holds
three times the mosaic's samples, and **RGB Color + Separate RGB Channels**
writes six times the mosaic's data in all. Prototype on one representative still
before committing to a long sequence. No production timing or peak-memory
benchmark was executed for this page.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: the window cases open a small
  16-bit RGGB mosaic in a view named `target` and run Debayer with RCD in each
  output mode; a 32-bit float grey image is run with **From metadata** and then
  with **RGGB**; a 32-bit float RGB image is run as well.
- Expected checkpoint: combined output is `target - RGB - RCD`, three channels,
  no filter array, marked demosaiced, with the mosaic's acquisition record.
  Both modes add `target - RED - RCD`, `target - GREEN - RCD` and
  `target - BLUE - RCD` with their history texts. The grey image is refused
  under **From metadata** with a message naming the missing filter array and is
  demosaiced under **RGGB**; the RGB image is refused as having more than one
  channel. Every one of the eight sample types comes out as it went in.
- Inspect at 100%/statistics/history: compare view names, histories, channel
  counts and the image's metadata. The behavior records fix the RCD,
  Bilinear, AHD and stated-BGGR answers within their tolerances.
- Limitations: no approved CFA camera frame or sequence was executed. Visual
  quality, method ranking and elapsed time are not established.

## Warnings and limitations

- Do not Debayer an image that is already RGB or is truly monochrome. A colour
  image is refused; a monochrome one can be run with a named pattern after the
  question, and the result is meaningless unless the samples really are a mosaic.
- **From metadata** is the image's statement, not pattern recognition. An image
  that states nothing is refused.
- A named pattern starts at the top-left pixel. For a mosaic cropped at an odd
  offset, **From metadata** keeps the right phase where a named pattern may not.
- Calibration should normally precede Debayer so bias/dark/flat masters still
  correspond to physical CFA sites. CFA-aware integration/drizzle workflows
  may intentionally defer it.
- The process has no preview, mask, per-method tuning, or persistence, and a
  still run cannot be cancelled.
- A floating mosaic in a physical or sensor-code domain takes its brightest
  sample as its full exposure, so AMaZE switches to its highlight-safe estimate
  only at that sample. A frame whose sensor saturated below its brightest
  sample - one hot pixel brighter than every saturated star - has its saturated
  stars estimated as unclipped. Integer mosaics take their largest code and
  floating mosaics stated as fractions of full scale take 1.0.
- Method border bands differ in width. Compare algorithms away from the frame
  edge as well as at the edge.
- An X-Trans mosaic is demosaiced only under **From metadata**, by the chosen
  method's X-Trans counterpart: the dialog offers no separate X-Trans method,
  and a named Bayer pattern does not describe an X-Trans sensor's samples, so it
  is refused over one.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Colors are swapped or a 2×2 checkerboard repeats | The selected named pattern does not match the mosaic, or does not account for a crop's phase. | Use **From metadata** when the image states its filter array; otherwise verify the acquisition record and choose the layout. | `resolveBayerPattern`; Debayer window |
| **This file doesn't have a CFA...** | The selected view is not shown as a mosaic, and is not a sequence the view shows demosaiced. | Choose **No** unless independent metadata proves the samples are CFA; correct the import/RAW settings or select the pattern manually. | `DebayerProcess::executeImageDebayer`; `GeometryProcessCase.DebayerAsksNothingOfAMosaicSequenceItsViewDemosaics` |
| `the image states no colour filter array to demosaic under` | **From metadata** on an image that states none. | Choose the camera's named pattern. | `resolveBayerPattern` |
| `the image has more than one channel; only a one-channel mosaic is debayered` | The target is a colour image. | Run Debayer only on the original single-channel CFA source. | `debayerImage` |
| **Error running Debayer** with `the image states an X-Trans filter array, which a two-by-two Bayer pattern does not describe; From metadata demosaics it under its own` | A named pattern (RGGB, BGGR, GBRG or GRBG) was chosen for an image that states an X-Trans array. | Choose **From metadata**; the chosen method then runs as its X-Trans counterpart. | `resolveBayerPattern`; `GeometryUnits.DebayerReadsAnXTransArrayFromTheImageAndRefusesANamedBayerPatternOverIt` |
| **Error running Debayer** with another reason | The engine refused the run, for example for memory. | Retry a representative still, check available memory, then try a lighter method or a shorter sequence. | `reportRunFailure` |
| The busy warning | Another run holds the sequence, or an edit of the still image is still being written. | Wait for the other run to land, then press **OK** again. | `runToNewViews` |
| Combined sequence has no saved file | The new sequence lives in the temporary directory. | Use Save or Sequence Export to create a durable file. | sequence run path |

## Related workflow steps

Debayer normally receives linear CFA output from [Image Calibration](image-calibration.md)
or [Cosmetic Correction](cosmetic-correction.md). [Split CFA](split-cfa.md) and
[Merge CFA](merge-cfa.md) are CFA-plane utilities, not alternative RGB
reconstruction methods. After Debayer, use [Image Registration](../registration-alignment/image-registration.md)
and [Image Stacking](../stacking-integration/image-stacking.md) for an ordinary
RGB workflow, then perform [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
while the result remains linear. A CFA-aware integration or drizzle workflow
can instead keep the mosaic through alignment/stacking and reconstruct later.

## References and provenance

- AfterNight's implementation is
  authoritative for the offered methods, their reach and the border rule.
- The methods descend from implementations distributed with
  [librtprocess](https://github.com/CarVac/librtprocess); the derived engine
  sources carry their own GPL-3.0-or-later and method-specific attribution
  notices.
- Keigo Hirakawa and Thomas W. Parks, [Adaptive Homogeneity-Directed
  Demosaicing Algorithm](https://doi.org/10.1109/TIP.2004.838691), IEEE
  Transactions on Image Processing 14(3), 2005, is the primary AHD reference.
- Lei Zhang and Xiaolin Wu, [Color Demosaicking Via Directional Linear Minimum
  Mean Square-Error Estimation](https://doi.org/10.1109/TIP.2005.857260), IEEE
  Transactions on Image Processing 14(12), 2005, is the primary LMMSE
  reference.
- RCD is attributed to the [RCD Demosaicing repository](https://github.com/LuisSR/RCD-Demosaicing).
