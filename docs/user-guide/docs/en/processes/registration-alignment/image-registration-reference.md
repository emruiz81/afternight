---
kind: reference
summary: >-
  What Image Registration does in detail: how stars are matched and transforms fitted, how Auto chooses the reference,
  what the registration database holds and when it is reused, what Align writes, the comet run, and every message.
rewritten: true
---
# Image Registration Reference

This page holds what [Image Registration](image-registration.md) leaves out to stay readable beside its window. It
follows a run in order, from the detection of the stars to the files **Align** writes. Then come the chromatic
aberration correction, the comet run, [plate solutions](../../reference/glossary.md#plate-solution) and the catalog,
what the window keeps, how to adjust the settings, the messages, and more symptoms with their fixes. The equations go
on from the process page's numbering.

## The stars of a frame

- Every frame is read as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale) in 32-bit
  floating point. A [CFA](../../reference/glossary.md#cfa) mosaic is debayered with **CFA Pattern** and **Method** of
  **Advanced Registration Settings**, an X-Trans mosaic included. **Auto** reads the
  [Bayer pattern](../../reference/glossary.md#bayer-pattern) the file states, and a stated pattern overrides it.
- The stars are searched in the green channel of a color frame, and in the one channel of a grayscale frame. The
  search keeps at most **Max Stars per Frame** of them, the brightest first.
- Each star is fitted with a Gaussian or a [Moffat profile](../../reference/glossary.md#moffat-profile). The frame's
  figures are the medians over its stars: **FWHM** in pixels, the [SNR](../../reference/glossary.md#snr), and the
  roundness that gives **Ecc.** by equation (5). The database also keeps the frame's background noise, its
  [median absolute deviation](../../reference/glossary.md#median-absolute-deviation) and its robust scale.
- A sample that is not a number, which calibration writes where a flat is zero, is left out: the background is
  measured without it, and the search reads the background in its place.
- Star positions, transforms and comet positions are in pixel coordinates whose first pixel has its center at
  (0.5, 0.5).
- A detection setting the detector cannot take runs at the nearest value it takes: a radius above 32 at 32, a
  roundness threshold of 0 or below at 0.01, a Moffat exponent of 0 or below at 0.05. The
  [log file](../../reference/glossary.md#log-file) says "Star detection settings the detector refuses run at the
  nearest value it takes", with the values. The window shows nothing of it.

## How the stars are matched

Triangles of stars keep their shape when a frame shifts, turns or changes scale. **Star Matching** builds up to
30,000 triangles from each field, finds the triangles of the frame whose side ratios match triangles of the
[reference frame](../../reference/glossary.md#reference-frame), and lets them vote for pairs of stars.
[RANSAC](../../reference/glossary.md#ransac) then tries a transform on small sets of pairs, 512 times from a fixed
seed, and keeps the pairs that lie within 3 pixels of where the transform puts them. The same frames and settings give
the same answer every time.

A match is accepted by equation (3) of the process page. Nearly every star of the smaller field pairs with one of
the larger, so the pairs of a frame that shares only part of its field are that part of its stars: a match is judged
by its pairs, not by the share of the frame they cover.

**Through a frame registered near it.** A frame the reference does not match is tried again through the frames
already registered, nearest first:

- The frames are taken in the order of their file paths. Distance counts places in that order, and of two frames at
  one distance the earlier is tried first.
- A frame is tried through at most 24 frames, the reference not among them.
- Its stars are matched with that frame's by the same rule. That frame's matched stars are carried onto the
  reference through its transform, and the frame's transform is fitted to those pairs with **Transformation**.
- A frame registered this way can carry the next, and the tries repeat while one frame registers another. So a frame
  that overlaps its neighbors, but too little of the reference, is still registered.
- A frame no try registers is marked **Failed**.

**A frame's own distortion.** When a frame's header states a [SIP](../../reference/glossary.md#sip) distortion with
its forward polynomials, a star at \(\mathbf{p}\) is matched where the distortion corrects it to:

\[ \mathbf{p}_\text{matched} = \mathbf{p} + \bigl(A(u, v),\ B(u, v)\bigr) \tag{6} \]

where \(A\) and \(B\) are the distortion's polynomials and \((u, v)\) the star's offset from the distortion's
reference pixel. The transform then maps undistorted positions, and **Align** undoes the correction after the
transform's inverse. It uses the inverse polynomials when the header states them, and solves for the position step
by step otherwise. A header that states only the inverse polynomials, AP and BP, corrects no star. **Phase
Correlation** measures the frames as they are and applies no distortion, and a solution found by **Solve...** or by
**Auto-solve unsolved frames** brings none. The record of each frame states the distortion its transform was solved
over.

## The transform models

| Choice | Free numbers | What it follows | Pairs it needs |
| --- | --- | --- | --- |
| **Similarity** | 4 | A shift, a turn and one scale, without shear or mirror | **Min Star Pairs**, at least 5 |
| **Affine** | 6 | Also a different scale on each axis, and shear | **Min Star Pairs**, at least 5 |
| **Homography** | 8 | Also perspective | **Min Star Pairs**, at least 5 |
| **Polynomial 2** to **4** | 8, plus \(T_n\) on each axis | Also the optics' distortion | Also \(T_n\): 6, 10 or 15 |
| **Auto** | 8, or a polynomial | A polynomial only when it is clearly better | 10, 16 or 25 for orders 2, 3 and 4 |

- A polynomial refines a [homography](../../reference/glossary.md#homography). Its positions are measured from the
  frame's center in half-widths and half-heights, so that its high powers stay small.
- Its fit is solved up to four times. After each pass but the last it drops the pairs whose residual exceeds the
  larger of 0.25 pixels and three times the upper quartile of the residuals. It stops early when a pass drops nothing,
  or when dropping would leave too few pairs for the order. When every pass still drops a pair, the last fit is kept.
- **Auto** tries a polynomial only when the homography leaves an **RMS** above 0.5 pixels. It tries orders 2, 3 and
  4 in turn, each only with 10, 16 or 25 pairs, and keeps the first whose **RMS** is at most 0.8 times the
  homography's.
- A fixed order that has fewer pairs than its terms, or whose fit is refused, gives way to the linear transform the
  stars matched. The log file then says the frame "matched N star pairs, fewer than the T a polynomial of order n
  needs, and is registered with the linear transform", or that the polynomial "could not be fitted to them".
- A transform's inverse is the inverse of its whole matrix, and its scales are the lengths its axes are mapped to.
  The window shows neither.

## The reference Auto chooses

**Auto** scores each candidate frame by its figures and takes the highest score. With \(N\) the frame's stars,
\(\mathrm{SNR}\) its median signal-to-noise ratio, \(F\) its **FWHM** in pixels and \(e\) its **Ecc.**, the score is

\[ Q = 10 \ln(1 + N) + 2 \ln(1 + \mathrm{SNR}) - 5F - 10e \tag{7} \]

When the frame has catalog figures, \(Q\) also gains 4 times the zero point and 2 times \(\ln(1 + N_\text{cat})\), with
\(N_\text{cat}\) the stars the catalog matched, and 0.5 times the limiting magnitude. It loses 20 times the zero
point's scatter and 8 times the arcsecond **RMS**.

- Excluded frames, frames over a quality threshold and frames whose detection failed are not candidates.
- When the frames name two filters or more, each frame's figures are compared with those of its own filter, and a
  frame without a filter name is not a candidate.
- **Register** chooses from the figures its own detection measured, before any frame is matched, in one pass.
  Choosing **Auto** in the window chooses at once from the figures the table holds. **Best** in **Registration
  Settings** chooses the same way, and detects the frames that have no figures yet.
- When no frame can be chosen, **Register** keeps the reference the window had, and the log file says "auto-reference
  selection failed".
- A reference that is not among the listed frames is replaced by the first frame of the list. A reference that
  cannot be read holds no stars, and **Register** stops with "The reference frame holds too few stars to match".

## What Register records

**Register** writes `registration_metrics.db` in the output directory, makes the folder when it does not exist, then
reads the record back. A record that cannot be written or read back fails the run.

The database holds, for these frames:

- each frame's stars, with the position, **FWHM** and roundness of each, and the size, time and identity of the file
  they were read from;
- each frame's figures, whether it registered, and its transform with the distortion it was solved over;
- the frames' set, the reference, and fingerprints of the detection settings and the registration settings;
- for a comet run, the run, its transforms, its output plane, its files and whether they are ready.

The records are filed under a key. When every frame has the size and the object of the others, the key is the one
Image Integration gives that [registration group](../../reference/glossary.md#registration-group), so either window
reuses what the other recorded. Otherwise the key is the window's own, made from the frames' paths.

**When a registration is current.** The table and **Align** use a record only while it answers the window as it is:

| Changes the record | Does not change it |
| --- | --- |
| Adding or removing a frame, **Remove Failed** among them | The order of the rows |
| The reference file, and **Reference** between **Manual** and **Auto** | **Interpolation** |
| **Method**, **CFA Pattern** and the [debayer](../../reference/glossary.md#debayer) **Method** | **Standard** against **Intersection** |
| With **Star Matching**: **Transformation**, **Min Star Pairs**, **Max Stars per Frame** and the star detection settings | Exclusions and quality thresholds |
| A switch to or from **Mosaic** | **Skip already-registered frames** |
| The choices of **Auto-solve unsolved frames**, **Enrich with Gaia catalog** and **Chromatic aberration correction**, and the chromatic aberration's settings | The plate solver's and the catalog's own settings |
| With **Comet / Asteroid Tracking**: the comet's observations and options | With the other methods, the settings they do not read |

**What is reused.** With **Skip already-registered frames** on, **Register** first reads the record:

- When it answers the frames and settings, and no included frame failed, it is taken whole: no frame is read.
- When only the registration settings, the reference, the set of frames or a frame's figures differ, the stars of
  the frames that registered are reused, and only matching runs again.
- When the detection settings or a file changed, the stars are not reused.

**A database of another format.** A database this version of AfterNight does not read is refused and left as it
is, with "The registration records '<path>' were written in format N; this version of AfterNight reads format M." or
"... state no format version ...". Choose another output directory.

**Clear Registered Data** deletes every record under these frames' key, comet runs included, and no file. Records
Image Integration wrote to the same database under the same key go with them.

## What Align writes

| Canvas | Size | Rounded |
| --- | --- | --- |
| **Standard** | The place the reference covers on its own grid: the reference's frame | Up, to whole pixels |
| **Mosaic** | The smallest rectangle that holds every included frame | Up |
| **Intersection** | The rectangle every included frame covers | Down |

- **Align** takes the included, registered frames in the list's order and writes them one at a time. When the
  reference itself is excluded, **Standard** takes the place the first included frame covers.
- Every pixel of the canvas is mapped back into the frame through the transform's inverse, then through the inverse
  of the frame's distortion, and the frame is sampled there with **Interpolation**. **Auto** is **Lanczos4**.
- A canvas pixel whose position falls off the frame is 0, and is marked as holding no data while the frame is in
  memory. A position near the frame's edge is sampled from the frame's own edge pixels rather than from a black
  border, so **Bicubic** and **Lanczos4** do not [ring](../../reference/glossary.md#ringing) there.
- The file is named after its source without the last extension, with `_reg.fits` added: `light.fit` gives
  `light_reg.fits`, and `M31.L.fits` gives `M31.L_reg.fits`. A file of that name is replaced without asking, and two
  sources of one name in different folders write one file.
- The file is a [FITS](../../reference/glossary.md#fits) file in 32-bit floating point. A grayscale frame gives one
  channel, and a color frame or a debayered mosaic three.
- The file keeps the source's acquisition record and its observing context: optics, site, pointing, camera
  settings, filter, object, frame type and date. It keeps the `EXPTIME`, `TOTEXP`, `LIVETIME`, `STACKCNT` and
  `NCOMBINE` cards as the source states them, which a later stack groups and weighs the frames by. It drops the
  [WCS](../../reference/glossary.md#wcs) cards, which the warp would make wrong, and the source's other
  [header cards](../../reference/glossary.md#header-card).
- A stopped or failed alignment keeps the files written before it, and the frame being written leaves no file.

## Chromatic aberration correction

With **Correct chromatic aberration before registration** on, **Register** corrects each color frame before its stars
are detected:

1. It detects up to 4,096 stars in each of the red, green and blue channels.
2. It keeps the stars farther from the center than **Edge Fraction** times half the frame's diagonal. A channel, or
   the green, with fewer than three such stars is left as it is.
3. It matches the red stars, then the blue, with the green by the rule of equation (3), with **Min Matched Stars**,
   never fewer than five, in place of **Min Star Pairs**, and three fifths in place of four fifths. **CA Fit Type** is
   the model.
4. It resamples the channel onto the green with **CA Interpolation**. Where the moved channel does not reach, the
   pixel or two at the edge it moved off, the channel keeps its own samples.

The frame is corrected in memory only. No cache folder or file is written, every frame keeps its name, and the
reference stays the reference. The stars are then detected in the green channel, which the correction did not move,
and **Align** reads the original files. So the correction changes neither the transforms nor the aligned files: it
adds time to **Register**, and its settings make a recorded registration out of date. A channel whose correction
fails is logged as a warning, and the frame goes on uncorrected.

## Comet and asteroid tracking

**Comet / Asteroid Tracking** aligns the frames on a moving object, so that it stays still and the stars trail.

- It needs a fixed reference, a `DATE-OBS` card on every included frame, and two enabled observations marked in
  **Track Comet**. Without them **Register** stops with "Comet tracking requires accepted observations, finite
  timestamps, and an explicit reference frame.", and a frame without `DATE-OBS` with "Comet registration requires
  DATE-OBS on frame '<path>'."
- The object's track is fitted over the observations with **Track model**, leaving out marks beyond **Outlier sigma**
  while **Reject outlier observations** is on. In each frame the nucleus is then measured within **Search radius** of
  where the track places it. A nucleus below **Minimum SNR** is not taken, and its frame is aligned where the track
  places the object.
- Stars are not detected, and **Transformation**, **Min Star Pairs** and **Max Stars per Frame** are not read.
  Excluded frames are left out.
- **Register** records the run, its transforms and its output plane in the database, then writes each frame aligned
  on the object to `comet_aligned/<run>/<name>_comet_reg.<extension>` in the output directory. When every source is a
  FITS file the extension is the source's, otherwise `.fits`. Every comet file is 32-bit floating-point FITS, without
  WCS cards, since its grid follows the object and not the sky. It is debayered with **CFA Pattern** and **Method** and
  resampled with **Interpolation**.
- When every file is written, the run is made the one Image Stacking's comet stacking reads.
- **Comet only** is the one output offered. **Refine**, **Auto Track** and **Find Moving Object** have no work in this
  window, so the comet is marked by hand.
- The line under **Track Comet...** reads, for example, "Comet tracking: 3 observation(s), 3 enabled." and "Tracking:
  search radius 32 px, minimum SNR 3, 8 bootstrap frames; linear track, outliers beyond 3 sigma rejected."
- A value stored more finely than its control shows, in a settings file edited by hand, is kept while the control
  shows the value it runs as.

The progress window reads "Fitting comet track...", "Materializing comet-aligned outputs...", "Materializing
comet-aligned image 3 of 35" and "Comet registration complete".

## Plate solutions and the catalog

A frame's [plate solution](../../reference/glossary.md#plate-solution) comes from the first of these that has one:

1. the frame's own header;
2. **Solve...** in the frame list's menu, kept for that frame until the window closes;
3. **Auto-solve unsolved frames**, for an included frame, one frame at a time, before any star is detected.

A solve that fails is logged, and the run goes on. Only the solver you set up may use the network: nothing else of
the window does. A solution never places a frame and is never written to a file. It serves the catalog and the
arcsecond **RMS** only.

With **Enrich with Gaia catalog** on and a [Gaia](../../reference/glossary.md#gaia) catalog ready, each registered
frame that has a solution and whose stars this run detected is matched with the catalog through its solution. Its
stars are placed in the catalog at the frame's date of observation, counted in Julian years from J2000.0, 2000
January 1 at 12:00. The frame gains its zero point in the Gaia G band and the zero point's scatter, its limiting
magnitude, its count of catalog-matched stars, and its **RMS** in arcseconds by equation (4). When the catalog matches
nothing, the log file says "the Gaia catalog matched nothing", and the frame keeps its pixel **RMS**.

## What the window keeps

Kept for the next window
:   The frames and their order, the output directory, the reference file, every setting of the window and of its
    two settings dialogs, the comet's observations and options, and the three quality thresholds, which are applied
    again when it opens.

Kept until the window closes
:   Exclusions you made by hand or in the **Blink Comparator**, and the solutions found with **Solve...**.

Not kept
:   The selected row, the column a list was sorted by, the preview's zoom and display, and whether **Quality
    Thresholds** is open.

In the output directory
:   The registration itself. A new window reads the frames' headers again, then reads the record back when it still
    answers the frames and settings.

## Adjusting the settings

Places to start from, not results measured on an image.

| Settings | Start | Too little | Too much | Order |
| --- | --- | --- | --- | --- |
| Star detection | Gaussian, **Radius** 5, **Threshold** 1.00, both ranges off | Too few stars, and parts of the field without any | Noise and [hot pixels](../../reference/glossary.md#hot-pixel) taken for stars, merged stars, figures that jump | **CFA Pattern** and **Method**, then **Threshold**, **Radius**, the profile and its checks, then the ranges |
| **Min Star Pairs** and **Max Stars per Frame** | 10 and 2000 | Too few candidates, or a sparse pattern that matches wrongly | Detection and matching time with no better fit | Keep the minimum low enough for the sparsest good frame, and raise the maximum only when parts of the field have no stars |
| **Transformation** | **Homography**, or **Similarity** for a plain dither | Residuals that grow toward the edges, failed wide-field matches | A polynomial that bends the edges, or an unsteady fit on few stars | Detection and reference first, then a simpler linear model, then **Polynomial 2** before a higher order or **Auto** |
| Quality thresholds | All at 0 | Soft or trailed frames stay in | Fewer than two frames left, or one filter pruned more than another | Compare frames of one filter, and set one threshold at a time |
| Canvas and **Interpolation** | **Standard** and **Auto** | **Bilinear** or **Nearest Neighbor** soften or block fine detail | **Lanczos4** and **Bicubic** ring at bright stars, and **Mosaic** makes a very large canvas | **Mosaic** before **Register**, a few frames first, then the kernels compared at 100 % |
| Plate solving and the catalog | Solutions the frames already hold, and both options off until a solver and a catalog are ready | No zero point or catalog figures | Solver and catalog time that changes no transform | Check the solutions, then **Auto-solve unsolved frames**, then **Enrich with Gaia catalog** |

## Messages

| Message | What it means |
| --- | --- |
| "An error occurred when running image registration process:" and a reason, in "Error running image registration" | **Register** stopped for that reason, and nothing was recorded. |
| "Could not build registration persistence context: Registration output directory is empty." | No output directory is chosen. |
| "Some images in the list have different size or color depth" | The frames differ in width, height, [pixel type](../../reference/glossary.md#pixel-type) or channel count, at **Register** or **Align**. |
| "The reference frame holds too few stars to match" | The reference's stars cannot form the triangles the matching needs. |
| "The registration could not be recorded in '<path>': <reason>" | The database cannot be opened or written. |
| "The registration recorded in '<path>' could not be read back: <reason>" | The record was written but reads back as nothing. |
| "Registration needs to be updated before alignment can run." | **Align** was asked for with an out-of-date registration. |
| "You need to define an output directory before executing image alignment" | **Align** has no output directory. |
| "No included registered images are available for alignment" | Fewer than two frames are both registered and included. |
| "An error occurred when aligning images:" and a reason, in "Error aligning images" | **Align** stopped at that frame, and the files before it remain. |
| "the selected frames share no part of the plane" | **Intersection** of frames that do not overlap. |
| "the planned canvas exceeds this domain's extent ceiling" | A **Mosaic** canvas wider or taller than 1,000,000 pixels. |
| "N failed images have been removed from the table (not from the disk)" | **Remove Failed** took N rows out of the list. |
| "Added N file(s); passed over M already listed or kept out by the filter." | Some chosen files were already listed or did not pass **Add Folder Options**. |
| "...; K could not be read. First error: <reason> See the log for per-file details." | Some files could not be read as images. |
| "This will delete the selected file from the disk permanently. Are you sure you want to proceed?" | **Delete From Disk** asks before it deletes. |
| "Could not delete '<path>' from the disk." | The file could not be deleted. |

The log file gives more, each line starting with "Image Registration:". It reports how many frames registered and
failed and in how many seconds, which frames were reused, which were registered through a frame near them, why a
frame was not registered, and how many frames were aligned into which folder.

## Troubleshooting

The **Registration** card is disabled
:   **Cause.** The list holds fewer than two frames, or a run is working.
:   **Fix.** Add a second frame, or wait for the run to end.

**Phase Correlation** places frames wrongly
:   **Cause.** The frames also turn or change scale, or hold too little structure, and the method judges no answer.
:   **Fix.** Use **Star Matching** for frames that turn or change scale, or a reference with more structure.

**Auto** chose an unexpected reference
:   **Cause.** It chooses by the figures of equation (7), before any frame is matched.
:   **Fix.** Choose the reference yourself with **Manual**, and register again.

**Include selected frames** does not include a frame
:   **Cause.** A quality threshold excludes it, and thresholds are counted again at once.
:   **Fix.** Change the thresholds, or choose **Reset** under **Quality Thresholds**.

**Best** leaves **Reference Image** as it was
:   **Cause.** No frame has figures it can score, and the dialog does not say why.
:   **Fix.** Check the detection settings, then register once and use **Best** again.

**Apply to All Groups** changes nothing
:   **Cause.** It belongs to Image Integration's groups, and this window has none.
:   **Fix.** Choose **OK** to give the values to this window.

The aligned frames still show color fringes
:   **Cause.** The chromatic aberration correction does not reach the aligned files.
:   **Fix.** Correct the color fringes on the stack.

A registration is not reused though nothing seems changed
:   **Cause.** A file was written again, the star records of a frame are missing, or a frame that failed is still
    included.
:   **Fix.** Exclude or remove the failed frames, or let **Register** run once more.

## What costs time, memory and disk

- **Register** reads and measures every frame it does not reuse. A mosaic's debayer, a higher **Max Stars per
  Frame**, the Moffat profile, relaxed checks and the catalog add to it. The chromatic aberration correction detects
  stars in three channels and resamples two of them.
- **Auto-solve unsolved frames** solves one frame at a time and can take the most time of a run, above all with an
  online solver.
- **Align** holds, for the frame it writes, the frame as read, its debayered copy, the canvas, and the record of
  which canvas pixels the frame reaches. A **Mosaic** canvas can be far larger than any frame, and **Intersection** is
  smaller.
- Each aligned file takes its width times its height times 4 bytes for each channel. Leave room for the whole set in
  the output directory.
- Try a few frames first with **Standard**, and with the correction, the solver and the catalog off. Measure an
  aligned file with [Image Statistics](../../interface/image-statistics.md).
