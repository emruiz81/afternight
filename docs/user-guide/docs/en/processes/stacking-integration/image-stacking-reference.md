---
kind: reference
summary: >-
  The rules behind Image Stacking: each rejection, normalization and weighting, how frames are grouped and files
  named, what a run writes, the memory it takes, comet stacking, settings to start from, and every message.
rewritten: true
---
# Image Stacking Reference

This page holds what [Image Stacking](image-stacking.md) leaves out to stay readable beside its window: the rule of
each method, [rejection](../../reference/glossary.md#rejection), normalization and weighting, how the frames are
grouped and the files named, what a run writes and the memory it takes, comet stacking, what too little and too much
of each setting look like, and every message. Each section can be read on its own.

## Methods

Every method combines, at each pixel and channel, the samples that mean something: an exact zero and a sample that is
not a number are left out of every frame. Nothing clips the result.

Average
:   The mean of the samples the rejection keeps, weighted by **Weighting**.

Sum
:   The sum of the samples, divided by the largest sum of the [master](../../reference/glossary.md#master), so that
    the brightest pixel is exactly 1 and the ratios between pixels are kept. When no sum is positive, the sums are
    written as they are, and the run ends with a warning. **Weighting** and the rejection are not read.

Maximum and Minimum
:   The largest or the smallest sample.

Median
:   The exact median of the samples.

Auto-Adaptive Weighted Average
:   A mean that weighs each sample by \(1/(1 + (r/\sigma)^2)\), with \(r\) its distance from the running mean and
    \(\sigma\) the root mean square of those distances, over five passes. The samples are first multiplied by
    65,535, the scale the method was written for, so it suits samples between 0 and 1. A pixel whose samples so
    multiplied spread by 5 or less, or that has fewer than three samples, takes their plain mean. A pixel whose
    largest sample is not positive is combined like any other.

## Rejection methods

Every method judges the samples of one pixel of one channel across a group's frames, after the normalization, and
needs three frames or more. Samples that mean nothing, an exact zero or a sample that is not a number, are never
judged and never counted in a rejection map. The sigma methods iterate until a pass leaves out nothing more. They
stop judging once the samples left, less every sample left out so far, are four or fewer, and a sample left out in an
earlier pass is counted again in that sum.

None
:   Every sample is kept. **Sigma Low**, **Sigma High** and the maps are disabled.

[Sigma Clipping](../../reference/glossary.md#sigma-clipping)
:   A sample further than **Sigma Low** standard deviations below the median of the samples kept, or **Sigma High**
    above it, is left out.

[MAD](../../reference/glossary.md#mad) Clipping
:   As **Sigma Clipping**, with the [median absolute deviation](../../reference/glossary.md#median-absolute-deviation)
    around the previous pass's median, unscaled, in place of the standard deviation. The median is taken again after
    it.

Median Sigma
:   As **Sigma Clipping**, but a sample judged out is replaced by the median rather than left out, and keeps counting.
    It has no limit of four samples.

Winsorized Sigma Clipping
:   As **Sigma Clipping**, with a standard deviation made robust first: the samples are held within 1.5 deviations
    of the median and their deviation multiplied by 1.134, over and over until it moves by no more than 0.0005 of
    itself. The thresholds then judge the samples themselves, not the held copy.

Linear Fit
:   The samples are sorted, and a line is fitted to them against their rank. A sample further below the line than
    **Linear Low** times the mean absolute residual, or above it than **Linear High** times it, is left out. Choosing
    the method sets the two to 5 and 4. It is made for large stacks.

Percentile
:   In one pass, a sample lower than the median by more than **Low Fraction of Median** of the median, or higher by
    more than **High Fraction of Median** of it, is left out. Choosing the method sets them to 0.2 and 0.1, which
    means 20 % and 10 % of the median. The name is the one the method is known by: it is no rank percentile. A
    median of zero leaves out nothing, and this method alone can leave out every sample of a pixel. Values above 1
    accept almost everything.

GESDT
:   The generalized extreme Studentized deviate test. Up to **ESD Outliers** of the samples, rounded up, are taken
    one extreme at a time as candidates, and every candidate up to the last one whose statistic exceeds its critical
    value at **ESD Significance** is left out, earlier ones included. The test stops when fewer than three samples
    remain. Choosing the method sets the two to 0.30 and 0.05, and both stay below 1. The statistics are worked in
    32-bit floating point. It is made for large stacks of nearly normal noise.

## Normalization

Each frame is brought to the group's first frame \(r\), channel by channel, before the rejection. With \(l\) the
median of a frame's samples and \(s\) its spread by **Scale Estimator**, a sample \(x\) of frame \(i\) becomes:

| Mode | The sample becomes | When a figure is zero |
| --- | --- | --- |
| **Additive** | \(x + l_r - l_i\) | Nothing changes. |
| **Multiplicative** | \(x \cdot l_r / l_i\) | A frame's median of zero leaves it as it is. A first frame's median of zero is refused. |
| **Additive With Scaling** | \((s_r/s_i)(x - l_i) + l_r\) | A spread of zero on either side leaves the ratio of the spreads at 1. |
| **Multiplicative With Scaling** | \(x \cdot (s_r/s_i) \cdot (l_r/l_i)\) | As both rows above. |

- **IKSS** measures the median and a robust biweight spread of the samples within six median absolute deviations of
  the median, a window chosen once. **MAD** measures the median and the median absolute deviation. Both are exact,
  over every sample that means something.
- A normalized sample may fall below 0 or rise above 1, and no method clips it.
- **Local** fits, for each frame, a background surface on a grid of **Grid Size** cells a side against the first
  frame, and with **Use local scale k(x,y)** a scale surface too, with the estimator, clipping and smoothing of
  **Local Normalization Settings**. It reads none of the reference choice, frame evaluation,
  [PSF](../../reference/glossary.md#psf) scale or filters that Image Integration's normalization adds.
- The first frame is the first of the group in the order of filter, session, exposure and file path. No control
  chooses another.

## Weighting

The weights apply to **Average** alone, with or without a rejection, and are the same in every channel. Each is
divided by their mean, so only their ratios count. When no frame of a group has the figure a weighting reads, the
group is stacked with equal weights and the window says why. When only some frames have it, the group is refused,
the window having named the first frame without it: no frame is given a weight of zero.

None (Equal)
:   Every frame weighs the same.

[FWHM](../../reference/glossary.md#fwhm)
:   Reads each frame's FWHM and star count from `registration_metrics.db` in **Directory**. A frame with fewer stars
    than the first frame has its width raised by twice the width times its share of missing stars, at least a tenth
    of itself. The weight is one over the square of the width, rescaled from 0 for the widest frame to 1 for the
    narrowest and raised to 0.01. Equal widths give equal weights.

[SNR](../../reference/glossary.md#snr)
:   Reads each frame's SNR from the database, and weighs each frame by it.

Stars
:   Reads each frame's star count from the database. The counts are rescaled from 0 for the fewest to 1 for the
    most, squared and raised to 0.01. Equal counts give equal weights.

Noise
:   Measures each frame's background noise when the stack starts, one more read of every frame: the first-difference
    estimate along the rows of each channel, of the samples as the file stores them, averaged over the channels. The
    weight is one over its square. Frames whose rows do not differ, such as a flat of one value, have no noise to
    measure.

Exposure
:   Reads each frame's exposure from its header, and weighs each frame by it.

**FWHM**, **SNR** and **Stars** find a frame's figures by its file path, as Image Registration recorded them, in the
newest record of each frame. The weighting warning is worked out again when the list, the weighting or
**Directory** changes.

## Groups and file names

- The frames are sorted by filter, session, exposure from the shortest, and file path, and each joins the first
  group it matches. The order of the list plays no part.
- **Filter** reads the filter the header states. An empty filter, `-` or `N/A` is `Broadband`.
- **Session** reads the date in UTC on which the frame was taken, as `2023-10-10`, else the date its file last
  changed, else the name of its folder.
- **Exposure** compares a frame with the mean exposure of the frames already in the group. It joins when the two
  differ by no more than **Tolerance** of the longer. A frame that states no exposure joins only a group of frames
  that state none.
- A group's name joins its filter, date and exposure, as `L  •  2023-10-10  •  180 s`, or reads `All Frames` with
  every key off.

The **Name Template** variables:

| Variable | Becomes | Example |
| --- | --- | --- |
| `$filter` | The group's filter, or `group` for `Broadband` | `L` |
| `$session` | The group's date | `2023-10-10` |
| `$exposure` | The group's mean exposure, with `s`, or `unknown` | `180s`, `4.35s` |
| `$group` | The group's name, its parts joined by `_` | `L_2023-10-10_180_s` |

Spaces and the characters a file name cannot hold become underscores, doubled underscores are made one, and
underscores at either end of the name are removed. The extension is kept in lower case when it names
[FITS](../../reference/glossary.md#fits) (`.fit`, `.fits`, `.fts`), TIFF (`.tif`, `.tiff`),
[XISF](../../reference/glossary.md#xisf), PNG or JPEG (`.jpg`, `.jpeg`, `.jpe`), and becomes `.fit` otherwise. When
two groups get one name, compared without regard to case, the later one becomes `<name>_<group>.<extension>`,
then `<name>_<group>_2.<extension>` and so on.

## What a run writes

Into **Directory**, for each group, in the order of the groups:

- The rejection maps, when asked: `<master>_rej_l+h.<extension>` merged, or `<master>_rej_l` and `<master>_rej_h`.
  Each is 32-bit floating point, of the master's size and channels, holding at
  each pixel the share of the group's frames left out there, at most 1. A sample replaced by **Median Sigma** counts
  once, on the side of its first replacement.
- Then the master, of the frames' size and channel count, or three channels when **Debayer CFA**
  [debayers](../../reference/glossary.md#debayer) it, in 32-bit or 64-bit floating point. A pixel no frame reached
  is 0 and marked invalid.
- Each file is written whole or not at all. When a map or the master cannot be written, the maps of that group
  already written are deleted, so a master is never left without its maps.
- A master of 5544 by 3684 pixels in one channel takes about 82 MB at 32 bits and 163 MB at 64 bits, and each map
  82 MB.

The master's header holds the first frame's acquisition data, its
[plate solution](../../reference/glossary.md#plate-solution) when it has one, and `EXPTIME` as **Sum exposure times**
says, written with six decimals and the comment `exposure time in seconds`. The frames' other
[header cards](../../reference/glossary.md#header-card) are not copied. In a view, its
[lineage](../../reference/glossary.md#lineage) names the combination, the rejection, the weighting, the
normalization and the number of frames.

Nothing else is written: no report, manifest, quality, coverage, variance or weight map,
[log file](../../reference/glossary.md#log-file), temporary calibrated frame or
[drizzle](../../reference/glossary.md#drizzle) image. The Session Console keeps these lines:

| Line | When |
| --- | --- |
| `Image Stacking: 1 group(s) into <folder>` | A run starts. |
| `Image Stacking: saved '<what>' to <path>` | A file is written. `<what>` reads `Stacked image created from 30 frame(s) using Average`, or names the map. |
| `Image Stacking: group '<group>': <warning>` | The weighting or the stack warned of a group. |
| `Image Stacking: finished in 12.34 seconds` | A run ends, with `(stopped)` after a stop. |

## Memory and time

A stack's time grows with the pixels, the frames and the channels. **Average**, **Sum**, **Maximum** and **Minimum**
without a rejection read one frame at a time into sums the size of the master, so the number of frames does not
enter the memory. A rejection, **Median** and **Auto-Adaptive Weighted Average** hold the samples of every frame for
a band of rows at once: the rows times the width, the channels, the frames and 4 bytes, or 8 for 64-bit frames.

With **Limit Memory Usage** on, the band is as many rows as **Max Memory** allows beside the master and one frame.
When it is smaller than the frame, every file is read whole once for each band, and the run ends with "Stacking
completed with warnings." and "Whole-frame reads are multiplied by the tile count", with the number of bands and
reads. The master is the same. For the NGC 253 frames of the process page, one pass needs about 2.6 GB for the 30
biases, 1.8 GB for the 20 darks and 3.4 GB for the 40 calibrated flats.

- A limit too small for the master and one row of every frame is refused before anything is read, with the sizes.
- The normalization's measures, **Local**'s grids, **Noise** weighting, **IKSS** and the rejection maps each add
  work, and the maps add memory and files. Several groups repeat everything.
- The limit covers the stack alone. Decoding a file, the open views and the rest of the application take memory
  besides.
- Start a new kind of stack with one group, 32-bit, equal weights and no maps, and add the costly options once it
  succeeds.

## Comet stacking

With **Comet Mode** at **Comet**, each listed frame is replaced by its copy aligned on the comet, which Image
Registration's comet registration wrote, as `registration_metrics.db` in **Directory** names it. List the frames the
registration registered, not its copies. The database must hold an active comet run whose state still matches the
registered frames, with a copy for every listed frame, or the group is refused rather than stacked from the frames
themselves. Every frame weighs the same.

## Too little and too much

What each choice looks like when it does too little or too much, and the order to adjust it in. These are places to
start from, not results measured on an image.

| Situation | Start | Too little | Too much | Order |
| --- | --- | --- | --- | --- |
| Deep-sky lights | **Average**, **Winsorized Sigma Clipping**, **Additive With Scaling**, **IKSS** | Trails and [hot pixels](../../reference/glossary.md#hot-pixel) remain, backgrounds differ | Faint signal missing from the master, few samples kept | Alignment, normalization, the maps, then **Sigma High** before **Sigma Low** |
| A small stack | **Median**, or **Average** with a cautious rejection | Passing objects remain | The rejection becomes unsteady and removes too much | More frames first, then **Median** before tighter thresholds |
| Uneven transparency or seeing | **Average** with a weighting the registration measured | Poor frames count as much as good ones | The stack is refused when some frames lack figures | The warning, equal weights, then the weighting, then the statistics |
| Uneven backgrounds | A global scaling mode, then **Local** at **Standard (Grid 4)** | Background structure differs from frame to frame | The grid prints noise or structure | Global, then **Local**, then **Grid Size**, then the advanced options |
| Mixed filters, nights or exposures | **Filter** on, **Session** and **Exposure** only for separate masters | One master mixes data that do not belong | Groups too small for a rejection | The headers, then one key at a time, then the frames of each group |
| Masters of a color camera | **Debayer CFA** off | The master stays a mosaic | A wrong pattern or method gives false colors | The [CFA](../../reference/glossary.md#cfa) column, the stack, then **Pattern** and **Method** if a color master is needed |
| A large set | 32-bit, **Limit Memory Usage** on, no maps | Many reads of every file | Not enough memory | One group, **Max Memory**, the maps, then **64-bit** |

## Messages

"Error stacking images", with "An error occurred while stacking images:" and the reason
:   The run stopped at a group, named in the reason as `Group 'Broadband':`. The files of the groups before it stay,
    and the groups after it are not stacked.

"frames with mismatched dimensions", "frames with mismatched color depth" or "frames with mismatched filter arrays"
:   The group mixes sizes, [pixel types](../../reference/glossary.md#pixel-type) or channel counts, or mosaics of
    different patterns or a mosaic beside a frame that states no filter array. Remove the odd frames, or give them a
    group of their own.

"does not have the size, channels, depth and filter array of the group's first frame"
:   A file changed between the moment its header was read and the stack. Add it again.

"The stack cannot be made: RejectionNeedsMoreFrames"
:   A group with a rejection holds fewer than three frames.

"measurements carry exposureSeconds on some frames and not on frame 3"
:   Only some frames have the figure the weighting reads: `exposureSeconds`, `signalToNoise`, `fullWidth`,
    `starCount` or `backgroundNoise`. The frame number counts from 0 in the group's order.

A message that names a file and why it cannot be read
:   A frame could not be read during the stack. Its group stops and is not stacked from the other frames.

"Failed to create output directory:" and the path
:   **Directory** could not be created. Choose a folder you can write to.

"Stacking completed with warnings."
:   The run wrote every file and has something to say of a group: equal weights used, a sum not normalized, or files
    read more than once. Each warning follows its group's name, eight at most, and " And 2 more - see the log." counts
    the rest.

"Registration metrics were not found in this output directory, so FWHM weighting is falling back to equal weights."
:   **Directory** holds no `registration_metrics.db`. **SNR** and **Stars** say the same with their own name.

"Registration metrics could not be loaded for FWHM weighting, so the stack is using equal weights."
:   The database could not be read.

"Registration metrics are missing or incomplete for FWHM weighting, so the stack is using equal weights."
:   The database holds no usable figure for any listed frame.

"Exposure weighting could not derive valid exposure values; using equal weights."
:   No frame states an exposure.

"Noise weighting could not estimate usable background-noise values; using equal weights."
:   No frame has a noise to measure. It is said when the run ends.

"2 of 30 frames are missing usable registration figures, so FWHM weighting cannot weigh the set; the first is" and a file name
:   Only some frames have the figure, `exposure`, `registration` or `noise`, and the group will be refused.

"Sum not normalized: no element's sum is positive, so there is no peak to normalize to and the sums are written as they are"
:   **Sum** found no positive pixel to divide by.

"Whole-frame reads are multiplied by the tile count"
:   The stack did not fit **Max Memory** and read each file once for each band of rows.

"Standalone comet stacking requires registration_metrics.db with an active role-qualified comet_aligned registration run."
:   **Comet** mode found no database in **Directory**. Other messages of the same start say that the database holds
    no registration, cannot be opened, or has no aligned copy for every frame, and "the registration names" says how
    many copies it names for how many frames.

"Missing output directory" or "No groups to stack"
:   **Stack Images** was pressed without a folder, or without frames. Its enabling keeps them from showing.

"Some files were skipped" or "Some files were passed over"
:   An import did not add every file: "Added 28 file(s); passed over 2 already listed or kept out by the filter",
    with how many could not be read and the first reason.

## What this window leaves to Image Integration

Image Stacking runs one ordinary stack for each group. [Image Integration](image-integration.md) adds what this
window does not offer.

- Calibration, registration, the choice of a reference, project files, workflow reports and the record of its
  products.
- The merge of exposures of different lengths into one high dynamic range.
- The normalization of overlapping panels. Here the frames must share one geometry, and no mosaic is stitched or
  feathered.
- Drizzle, after the [method](https://arxiv.org/abs/astro-ph/9808087) of Fruchter and Hook, with its scale, pixel
  fraction, kernels, filter-array mapping, canvas, [SIP](../../reference/glossary.md#sip) correction and trim offsets.
- The quality report, the coverage, variance, SNR gain and weight maps, the report of clustered rejections and a
  second stack without the frames it flags.
- Weights for **Sum**, and a debayer of each frame before the stack.
