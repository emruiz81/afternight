---
kind: interface
summary: >-
  Measures the level, spread, clipping and noise of the active image, one channel at a time, from its stored values.
---
# Image Statistics

## Purpose, audience, prerequisites, and expected result

Image Statistics measures the active image or current sequence frame one channel
at a time. Use it to quantify level, spread, clipping, and background noise
before or after a processing step. It reads stored pixel values, not the
autostretched display.

You need an active view containing a valid mono or RGB image. The result is a
read-only table with one **Gray** column or separate **Red**, **Green**, and
**Blue** columns, and an **Alpha** column for a colour image with alpha. It does not change pixels, metadata, history, masks, or files.

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Path | **Image > Information > Statistics**, or **Statistics** on the active image-view toolbar. The toolbar advertises `Ctrl+Shift+T` while that view has focus. |
| Enabled/visible when | A view with an image is active. Opening the dialog disables that view's Statistics actions until the dialog closes; the focused-view shortcut also honors that disabled state. An image load or update in that view enables the actions again, so a second Statistics window can then be opened. |
| Supported inputs/state | Mono and colour images of every pixel type, signed integers among them. On a sequence view, the table describes the current decoded frame, not all frames. Display mode, autostretch, zoom, channel display, the view's mask, and selections do not limit the sample; samples the image itself marks as holding no data - a registration border, for one - are left out. |
| Outputs/side effects | A read-only per-channel table. No output image, file, clipboard data, metadata, or history entry is produced. |
| Persistence | None. Range and **Unclipped** return to their defaults in each new dialog. |
| External dependencies | None. Each measurement runs on a worker thread through the engine, in an operation the application's engine session admits; the table changes only when the latest measurement lands. |

## Before you start

- Select the view you intend to measure. With a sequence, navigate to the frame
  of interest first.
- Decide whether exact endpoint values are valid signal. With **Unclipped** off,
  normalized values at or below 0 and at or above 1 are excluded. Enable it to
  include finite black/white endpoints - and, for a floating-point image, values
  below 0 or above 1 - in the reported population.
- Disable autostretch only if you also want to compare appearance. It does not
  affect the numbers.

## Procedure

1. Activate the image or sequence frame and open **Image > Information >
   Statistics**. The initial computation runs asynchronously.
2. Confirm that the title names the intended view and that the table shows
   **Gray** or **Red/Green/Blue** columns (with **Alpha** for a colour image with
   alpha).
3. Leave **Normalized (0.0-1.0)** selected for the least ambiguous comparison
   between pixel types.
4. Compare **Count (px)** with the expected pixels per channel. The default
   excludes black/white endpoints. Enable **Unclipped** to include finite
   endpoints, then watch the count and percentage change. Non-finite values are
   excluded in either state.
5. Use **Median** and **MAD** for a robust background comparison; use **Min**,
   **Max**, and the count change to diagnose endpoints. Treat **BG Noise** as a
   row-difference noise estimate, not a full noise model.
6. Change the numeric range only when you need familiar 8-, 16-, or 32-bit-like
   units. This rescales the report; it does not convert the image.
7. Close the window when finished. **Refresh** measures the image again; image
   load/update notifications refresh the table automatically.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Numeric range selector | Selector; reported level units | **Normalized (0.0-1.0)** | **Normalized (0.0-1.0)**; **8-bit (0-255)**; **16-bit (0-65K)**; **32-bit (0-4000M)** | Rescales the already computed table. Normalized values use seven decimal places; other modes use three. | Always available after data exists. It does not change counts or recompute the source image. | A level or a spread is multiplied by the range's full scale and **Variance** and **BWMV** by its square, so a variance stays the square of the standard deviation shown beside it. No value is held to the range: a floating image's mean above one shows above full scale. | All reported channels in this dialog | Dialog lifetime only |
| **Unclipped** | Checkbox | Off | Off/on | Off retains only samples strictly inside normalized `(0,1)`. On includes every finite sample, including exact black/white endpoints and a floating-point value below 0 or above 1. Either change measures again on a worker. **Count** and **Count (%)** expose the retained population. | Applies to all channels; NaN/infinite samples, and samples the image marks as holding no data, are excluded in either mode. | Including endpoints reveals the complete finite population but can let black borders or saturated samples dominate extrema and distribution measures. | Whole current image/frame, all channels | Dialog lifetime only |
| **Count (px)** | Read-only integer row | Computed | 0 to width x height per channel | Number of finite samples used. | Changes with **Unclipped** when endpoints exist; unaffected by report range. | A lower count is evidence of excluded endpoints or non-finite values, not automatic proof of processing damage. | Per channel | None |
| **Count (%)** | Read-only percent row | Computed | 0-100%, seven decimals | `100 x retained count / (width x height)`. | Same filter as **Count (px)**. | Makes endpoint loss comparable across image sizes. | Per channel | None |
| **Mean** | Read-only level row | Computed | Selected report range | Arithmetic mean of retained normalized samples. | Recomputed by **Unclipped** and image updates. | Sensitive to stars, clipping, gradients, and outliers. | Per channel | None |
| **Sigma** | Read-only level row | Computed | Non-negative | The population standard deviation, as **StdDev**. | Recomputed with the retained population. | The same number as **StdDev**; the row keeps its place in the table. | Per channel | None |
| **Median** | Read-only level row | Computed | Selected report range | The exact central retained value; the mean of the two middle values of an even count. | Recomputed with the retained population. | More robust than the mean for sparse stars and outliers, but not a background-only sample. | Per channel | None |
| **Min** / **Max** | Read-only level rows | Computed | Selected report range | Lowest/highest retained finite sample. | Endpoint values disappear when **Unclipped** is off. | One extreme pixel can set either value. Pair with counts and histogram clipping information. | Per channel | None |
| **StdDev** | Read-only level row | Computed | Non-negative | Population standard deviation around the mean. | Uses the retained population. | Sensitive to gradients, stars, and signal structure as well as noise. | Per channel | None |
| **AvgDev** | Read-only level row | Computed | Non-negative | Mean absolute deviation from the arithmetic mean. | Uses the retained population. | Robustness is between mean-based spread and median-based MAD. | Per channel | None |
| **BWMV** | Read-only robust-spread row | Computed | Non-negative when defined | Biweight midvariance about the median, with a tuning constant of nine MADs; zero when MAD is zero. | Uses the retained population. | Useful against outliers. It is in squared units, scaled by the square of the selected range's full scale. | Per channel | None |
| **Variance** | Read-only squared-spread row | Computed | Non-negative | Mean squared deviation from the mean; `StdDev^2` apart from rounding. | Uses the retained population. | In squared units, scaled by the square of the selected range's full scale. | Per channel | None |
| **MAD** | Read-only robust-spread row | Computed | Non-negative | Median of `abs(x - median)`. | Uses the retained population. | Robust against sparse outliers; it is not multiplied by a normal-distribution consistency factor in this table. | Per channel | None |
| **BG Noise** | Read-only level row | Computed | Non-negative; zero when no row has two first differences | Median per-row population standard deviation of first horizontal differences after up to three strict 5-sigma rejection passes, divided by `sqrt(2)`. | Uses the retained population; a left-out sample is skipped and the next retained sample is differenced from the one before it. | Suppresses smooth background level but still reacts to structure and directionality. Compare similar data only. | Per channel | None |
| **Refresh** | Push button | Available | Click | Measures the current image or frame again. | Always available. | The table changes when the measurement lands. | This dialog | None |
| **Close** | Push button/default dialog action | Available | Click, Enter when default, or close the window | Closes the dialog and re-enables Statistics for its parent view. | Always available. | None. | This dialog | None |

## Behavior and state model

AfterNight reads each stored pixel component as a fraction `x` of its pixel
type's range before reporting it. An unsigned integer is read over its largest
code, a signed integer from its smallest value to its largest - so a signed 16-bit
image's -32768 reads as 0 and 32767 as 1 - and a floating-point value as it is.
Non-finite floating-point values are discarded, as are samples the image marks as
holding no data. With **Unclipped** off, only `0 < x < 1` contributes. With it on,
every remaining finite value contributes.

All rows are computed independently for each stored channel. The current display
composition does not merge or hide channels, and a mask does not restrict the
measurement. For a sequence, the view supplies the current frame as the image;
frame changes that emit an image update trigger a new calculation.

The dialog measures on a worker thread when it opens, on image load/update, when
**Unclipped** changes and when **Refresh** is pressed; a measurement asked for
while another is on its way replaces it, and the table changes only when the
latest one lands. A measurement that cannot be made - no engine session, or an
image the engine refuses - leaves the table empty. A rename updates the window
title only. Changing the numeric range only reformats the last result.

Use these relationships when reading the table:

- `mean = sum(x) / N`, where `N` is **Count (px)**.
- `variance = sum((x - mean)^2) / N` and `StdDev = sqrt(variance)`.
- `MAD = median(abs(x - median))`.
- **BG Noise** estimates high-frequency horizontal variation from adjacent
  differences; it is not the same as global standard deviation.

The table has no sum-of-squares row; the engine's sum and mean of squares reach
PixelMath, not this window.

## Inputs, outputs, and metadata

The only input is the active view's in-memory image. The tool reads all pixels
of the current still or decoded sequence frame and produces transient table
cells. It does not write an image, select pixels, alter display transfer
functions, append history, change metadata, save a report, or touch disk.

## Performance and responsiveness

Work scales with width x height x channels. The median, MAD and biweight rows
rank the retained samples exactly: an 8- or 16-bit image, which is what a camera
RAW opens as, is ranked from one read of each channel, and a floating-point image
reads each channel several times. Every measurement runs on a worker thread, so
large frames can take time to settle while the window stays responsive. A
measurement may use every thread of the Processing page's limit that is free.

For long sequences, pause playback on a representative frame before opening the
dialog. Close the tool when you no longer need continuously updated statistics.

## Worked example

- Starting state, actions, settings, checkpoints, result, and limitations:
  `StatisticsReportTest` measures a five-pixel 16-bit row holding 0, 1, 30000,
  65534 and 65535. With **Unclipped** on, **Count (px)** is 5; with it off the two
  end codes leave, **Count (px)** is 3, **Min** is above 0 and **Max** below 1. A
  floating-point row of -0.25, 0, 0.2, 0.4, 1 and 1.5 counts 6 with
  **Unclipped** on and 2 with it off. A signed 8-bit row of -128, -127, 0, 126
  and 127 with **Unclipped** off reports **Min** `1/255` and **Median**
  `128/255` in the normalized range. These are synthetic estimator checks, not a
  claim about a particular astrophotography exposure.

## Warnings and limitations

- Statistics describe stored pixel data, not autostretched appearance.
- **Unclipped** includes exact finite endpoints; with it off, strict endpoint
  exclusion is active. Neither state performs sigma clipping or highlight/shadow
  repair.
- The tool measures the full current frame. It has no ROI, mask, or selected-
  channel sampling mode.
- The bit-range selector is report scaling, not precision conversion.
- No table export or copy action exists. Image Summary has a separate clipboard
  action but reports metadata, not these statistics.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Statistics is disabled | No active image exists, or this view already owns an open Statistics dialog. | Activate a loaded view, or close the existing dialog for that view. |
| Count rises when **Unclipped** is enabled | Finite values `<= 0` and `>= 1` are restored to the population; non-finite samples remain excluded. | Compare **Count (%)**, **Min**, **Max**, and Histogram clipping; leave **Unclipped** off when endpoints should not influence the report. |
| Mean and median differ strongly | The distribution is asymmetric or contains bright/dark structure and outliers. | Inspect [Histogram Analysis](image-histogram.md) and compare channels before assuming a defect. |
| **BG Noise** is unexpectedly high | Horizontal structure, gradients, stars, or correlated noise contribute to adjacent-pixel differences. | Compare similar background regions with other analysis tools; do not interpret this full-frame number as a calibrated sensor-noise measurement. |
| A sequence value changes during playback | Statistics follows the current decoded frame and refreshes on image updates. | Pause on the frame you want to inspect. |

## Related workflow steps

- Inspect [Histogram Analysis](image-histogram.md) next to see where the counted
  samples lie and whether exact black or white endpoints are populated.
- Use [Image Summary](image-summary.md) for dimensions, pixel type, acquisition
  metadata, or sequence-level frame/FPS information.
- After saving, use [Saving And Exporting](saving-and-exporting.md) and reopen the
  file before comparing statistics when you need to verify codec precision.
- Continue to [Histogram Transformation](../processes/stretching/histogram-transformation.md)
  only after distinguishing measurement from display stretch.
