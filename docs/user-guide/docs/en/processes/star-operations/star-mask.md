---
kind: process
stage: noise
menu: Process > Mask Generation > Star Mask
summary: >-
  Detects the stars of an image and paints them into a new grayscale mask, as smooth profiles or solid shapes.
data_in: linear
data_out: nonlinear
derived: true
---
# Star Mask

## Purpose and expected result

Star Mask finds the stars of one image, or of every sequence frame, with
AfterNight's fitted star detector and paints them into a new single-channel
grayscale mask. Each accepted star becomes a smooth radial profile or a solid
ellipse.

The expected result is a same-size mask named `<target> - STARMASK`, with white
or bright areas over accepted stars and black elsewhere. The source is not
modified. This is a detector-driven mask generator; it does not read a saved
star list.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Mask Generation > Star Mask** |
| Enabled when | An image or sequence is active; a launch is refused with a warning when another Star Mask window is already attached to it or an attached process holds exclusive access |
| Inputs | One grayscale or RGB (with or without alpha) still or sequence in any integer or floating representation. Stars are found in an RGB image's green channel, or in its CIE L* with **Use Lightness**. A filter-array (mosaic) image that has not been demosaiced is refused; configured sequence auto-debayer can supply RGB frames |
| Outputs | One new same-size, single-channel grayscale mask still or sequence in the target's representation; **Take snapshot** creates an auxiliary still; source pixels are unchanged |
| Recommended image state | A calibrated linear or consistently prepared star field; clipping, strong gradients, hot pixels, blur, and prior nonlinear processing change detection and measured widths |
| Preview support | Asynchronous preview of the mask over the whole image or an initial centered `600×600` Preview area, original/result switching, snapshot, display-channel selection, and display-only Linear/Autostretch |
| Sequence support | Yes; one captured parameter set is applied frame by frame into a new temporary sequence, with frame progress and Cancel |
| Mask support | No. An active image mask is deliberately ignored because this process creates a mask rather than blending a correction through one |
| External dependencies | None; the detector and the mask painter are built into AfterNight |

## Before you start

- Use a calibrated image with representative focus and a controlled background.
  Hot pixels, noise peaks, saturated cores, gradients, and stretched halos can
  be accepted as stars.
- Match **Radius** to the stars: it is half the width of the box a star must be
  the brightest point of. The searched picture, or the Preview area, must be
  more than `4 x Radius + 8` pixels wide and tall, or the run is refused; at
  the default Radius `5` that is more than 28 pixels.
- Decide whether the mask needs graded profiles or hard selection. Leave
  **Shaped Areas** checked for radial falloffs; uncheck it for solid ellipses.
  Growth, size, and Star Shape controls do nothing without Shaped Areas.
- For an RGB target, stars are found in the green channel by default; enable
  **Use Lightness** to search CIE L* instead.
- Keep the preview Display Mode identical when comparing settings. Autostretch
  changes only presentation, not mask samples.

## Procedure

1. Select the target and open **Process > Mask Generation > Star Mask**.
2. Start from the defaults: Radius `5`, Threshold `1.00`, Roundness Threshold
   `0.50`, Gaussian, and the optional ranges off. If an error says the searched
   rectangle is too small, lower Radius or enlarge the Preview area.
3. Keep **Shaped Areas** checked. At **Zoom 100%**, inspect missed faint stars,
   false hot-pixel/noise detections, saturated cores, and blends. Fix detection
   settings before changing mask growth.
4. Tune Percent Increase and Pixel Increase, then restrict Min/Max Size if
   required. Percent Increase is a direct radius multiplier: default `0.50`
   halves the measured base radius before adding pixels; it does not add 50%.
5. Compare the six **Star Shape** profiles. Use **Switch image** under the same
   Display Mode. Uncheck **Preview area** before the final full-frame check,
   because the area is searched on its own.
6. Select **Apply**. A progress window with **Cancel** appears; on success
   AfterNight opens the new mask view, resets the settings, and closes the
   process. The original remains unchanged. Save/export the mask if it must
   survive the session.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Radius | Candidate-search half-width, pixels | `5` | None | `3..32`; integer | Larger widens the box a star must be the brightest point of and skips a wider border; smaller targets compact stars | **Detection** group; locked during Apply | The picture or Preview area must exceed `4 x Radius + 8` pixels each way or the run is refused; large values cost more and lose a fainter star within Radius of a brighter one; small values split or truncate broad stars | Detection; still/sequence | Captured per execution; Reset/success return `5` |
| Threshold | Background-noise multiplier | `1.00` | None | `0.01..15.00`; step `0.05`; slider maps thousandths | Raises/lowers the candidate threshold `B + 5 x Threshold x n` | Detection group | Lower finds fainter/noisier candidates and costs more fits; higher misses faint stars | Detection plane | Captured; Reset/success return `1.00` |
| Amplitude Range | Checkable group | Off | None | Off/On | Enables a window on the fitted amplitude | Detection group; child bounds apply only when checked | Useful against weak/noisy or clipped fits; a narrow window can reject all stars | Post-fit rejection | Captured; Reset/success turn off |
| Amplitude Range > Minimum | Fitted amplitude, normalized (full scale `1`) | `0.00` | None | `0.00..0.99`; step `0.05` | Higher rejects fainter fits | Amplitude Range on | Can suppress weak noise but remove faint stars | Post-fit rejection | Captured; Reset/success return `0.00` |
| Amplitude Range > Maximum | Fitted amplitude, normalized (full scale `1`) | `1.00` | None | `0.01..1.00`; step `0.05`; a Maximum below Minimum is refused when the run starts | Lower rejects brighter fits | Amplitude Range on | Can remove clipped/bright stars or empty the list | Post-fit rejection | Captured; Reset/success return `1.00` |
| Roundness Threshold | Minimum fitted axis ratio `FWHMminor/FWHMmajor` | `0.50` | None | `0.01..0.95`; step `0.05` | Higher requires rounder stars | Detection group | High values reject elongated stars; low values admit trails/blends | Post-fit rejection | Captured; Reset/success return `0.50` |
| Roundness Range | Checkable group | Off | None | Off/On | Enables an upper axis-ratio limit | Detection group | A narrow lower/upper band can empty the list | Post-fit rejection | Captured; Reset/success turn off |
| Roundness Range > Maximum | Maximum fitted axis ratio | `1.00` | None | `0.01..1.00`; step `0.05`; a Maximum below Roundness Threshold is refused when the run starts | Lower rejects rounder fits above this bound | Roundness Range on; off means maximum `1.0` | Usually leave off unless deliberately selecting elongation | Post-fit rejection | Captured; Reset/success return `1.00` |
| Convergence | Solver iteration multiplier | `1` | None | `1..3`; integer | Allows each fit more solver iterations | Detection group | Can rescue difficult fits at CPU cost; a fit that does not converge is discarded | Per-candidate fit | Captured; Reset/success return `1` |
| Relax PSF Checks | Checkbox | Off | None | Off/On | Skips the scan's symmetry and local-amplitude tests and the fit's width-estimate and residual checks; amplitude, beta, geometry, magnitude, and roundness checks still apply | Detection group | Retains more difficult stars but also blends and poor fits | Candidate scan and post-fit validation | Captured; Reset/success turn off |
| Profile Type | Selector | **Gaussian** | None | **Gaussian**, **Moffat** | Chooses the fitted PSF model; Moffat also fits its exponent beta | Detection group; Moffat enables Minimum Beta | Moffat can represent broader wings but fits one more parameter | Per-candidate fit | Captured; Reset/success return Gaussian |
| Minimum Beta | Moffat exponent floor | `1.50` | None | `0.05..10.00`; step `0.05` | Higher rejects Moffat fits with heavier wings (lower beta) | Enabled only for Moffat | Too high rejects valid winged stars; too low admits unstable fits | Moffat post-fit rejection | Captured; Reset/success return `1.50` |
| Use Lightness | Checkbox | Off | None | Off/On | On searches an RGB image's CIE L*; off searches its green channel; a grayscale image is always searched in its one channel | Detection group; meaningful for RGB (on grayscale it only adds `, on the lightness` to the history text) | L* adds a conversion and combines all channels; green may have higher sampling and signal on a one-shot-color camera | Detection plane only | Captured; Reset/success turn off |
| Shaped Areas | Checkable group | On | None | Off/On | On paints radial profiles; off paints solid ellipses whose samples are `0` or `1` | Growth/size/shape children apply only when checked | Shaped profiles give graded, circular footprints; solid ellipses keep the fitted axis ratio and angle with hard edges | Mask painting only | Captured; Reset/success turn on |
| Percent Increase | Radius multiplier | `0.50` | Label suggests a percentage increase, but the value is used directly as a multiplier | `0.01..1.00`; step `0.05` with adaptive stepping | Scales the larger fitted FWHM to the painted radius; `0.5` halves it | Shaped Areas only | Small values under-cover stars | Shaped masks; all accepted stars | Captured; Reset/success return `0.50` |
| Pixel Increase | Additive radius, pixels | `0` | None | `0..99`; integer | Adds a fixed number of pixels after Percent Increase | Shaped Areas only | Dominates small stars and can merge neighboring masks | Shaped masks | Captured; Reset/success return `0` |
| Min Size | Smallest painted diameter, pixels | `2` | None | `0..99`; integer; a Min Size above Max Size is refused when the mask is painted | Raises the minimum accepted `2 x` larger FWHM | Shaped Areas only | Rejects small stars/hot pixels | Shaped-mask star selection | Captured; Reset/success return `2` |
| Max Size | Largest painted diameter, pixels | `25` | None | `0..99`; integer; must not be below Min Size | Raises/lowers the maximum accepted `2 x` larger FWHM | Shaped Areas only | High values retain large/saturated structures; low values reject large stars | Shaped-mask star selection | Captured; Reset/success return `25` |
| Star Shape | Selector | **Truncated Bell** | None | **Bell**, **Truncated Bell**, **Linear**, **Truncated Linear**, **Cubic**, **Quadratic** | Chooses the radial falloff function | Shaped Areas only | Changes core plateau, wings, cutoff, overlap, and transition width | Per-star shaped profile | Captured; Reset/success return Truncated Bell |
| Target Image | Image/sequence selector | Attached active target | None | Open eligible image or sequence | Changes the source, the Preview, and the image the new mask is made from and named after | Active target required; duplicate/exclusive launch gates apply; locked during Apply | Selecting the wrong target creates a mask for the wrong data | Detection source; active mask is ignored | Target lifetime; Reset unchanged |
| Preview | Checkbox | Checked | None | Checked/unchecked | Creates or removes the asynchronous mask preview | Remains interactive during Apply | Each run copies the target or the Preview area (plus a floating copy for integer images) and reruns detection and painting; a burst of changes collapses into one run with the latest values; Apply cancels a preview in progress; a refused preview reports its error | Preview only; still/current sequence frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | Mask result, with Preview area on at start | None | Pan/scroll and actions below | Shows the generated mask without modifying the source; with Preview area on, the area's mask over the rest of the image, an RGB image shown as its luminance | Exists only with Preview | Holds source, mask, and display buffers and detector workspaces | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits the whole image | Preview required | Broad context can hide small missed/false detections | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; relative `1.25x` | Enlarges the mask | Preview required | Improves edge/core inspection | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; relative `0.8x` | Reduces the mask | Preview required | Shows distribution but hides fine edges | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview required | Preferred for false detections, coverage, and edge profiles | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar selector | **Grayscale**, disabled for the one-channel result | None | Grayscale for current mask | Selects displayed component only | Output has one channel | Display-only | Preview display only | Viewer lifetime |
| Display Mode | Preview-toolbar selector | Inherits target **Linear** or **Autostretch** when created | None | **Linear**, **Autostretch** | Changes display only; Autostretch stretches the preview with the target image's statistics and stretch settings | Shown because this process supports a display stretch | Different stretches invalidate visual comparison | Preview display only | Preview-view state |
| Autostretch Settings | Preview-toolbar action | Starts from the target view's settings | None | Opens the preview's display-stretch settings | Tunes preview presentation only | Shown beside Display Mode | Can exaggerate weak mask wings/background; never changes output | Preview display only | Viewer-scoped, not a Star Mask parameter |
| Switch image | Preview-toolbar action | Mask shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles mask/original | Preview required | Comparison only | Preview display; current frame | Preview lifetime |
| Preview area | Preview-toolbar checkbox | Checked; centered `600x600` when possible, labeled **Preview: Star Mask** | None | Checked/unchecked; move/resize area | Searches and paints only the area and shows it over the rest of the image | Preview required; disabled during Apply | Faster tuning, but the area is searched with its own sky, noise, and border, so stars near its edges can differ from a full-frame Apply; an area not more than `4 x Radius + 8` pixels each way is refused | Preview and snapshot | Window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Runs the current settings again and opens `<target> - Star Mask - SNAPSHOT`; with Preview area on, the snapshot is the composite the Preview showed | Preview required; disabled during Apply | Adds full detector/mask work and one auxiliary still; display stretch is not baked in; no lineage entry is added | New still; source unchanged | Each action creates a new view |
| Apply | Button/action | Enabled after initialization/reset updates | None | One action | Captures all controls, locks the target, finds and paints on a worker, opens the new mask still or sequence beside the target, resets, and closes after success | Target lock required; during the run every Detection and Shaped Areas control, Target, Apply, Reset, Close, and the preview's snapshot and area actions are disabled whether or not Preview is on | Detector cost plus mask painting; output allocation, and sequence encoding adds memory/disk | New mask still/sequence; never blends through an active mask | Run snapshot; nothing is stored between openings |
| Reset | Button/action | Enabled while idle | None | One action | Restores every detection/mask default listed above | Leaves Target, Preview, Preview area, and preview-toolbar state unchanged; checked Preview schedules a run | Can trigger a complete preview recomputation | Configuration only | Immediate; also after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without creating the Apply output | Disabled during Apply; closing the window while a run is in progress stops it | A stopped run makes nothing | Window/execution | Settings discarded |
| Progress / Cancel | Progress window with a busy indicator for the current image and Cancel; a sequence run adds a second card showing `Executing process in sequence. Frame i/N` and the share of frames done | Created for every Apply | None | Cancel | Stops the run at the engine's next checkpoint: between scan bands, fitted candidates, painted rows, or sequence frames | Still or sequence Apply; the frame card appears only for a sequence | Nothing waits on the GUI thread | Still/sequence Apply | A stopped run opens no view and leaves no partial sequence; success opens the new mask and closes the process |

## How the algorithm works

### Conceptual pipeline

1. The search plane is a grayscale image's only channel, an RGB image's green
   channel, or its CIE L* when Use Lightness is checked. Samples are read in
   normalized units with full scale `1`: an unsigned integer divided by its
   largest code, a signed integer counted from its smallest code, and a
   floating sample as it is. The plane is searched as linear data whatever
   state the image records.
2. The detector measures the plane's sky level `B` and noise `n`, smooths the
   plane with a Gaussian of about 1.83 pixels, and lists candidates: points of
   the smoothed plane above the threshold that are the brightest point of
   their `2 Radius + 1` pixel box, are backed by enough bright neighbors, and
   pass the scan's symmetry checks.
3. Candidates are fitted with an elliptical, rotated Gaussian or Moffat PSF,
   brightest first. Fits outside the roundness or amplitude window, below
   Minimum Beta, wider than the scan's own width estimate, or with too large a
   residual are rejected, and a fit that does not converge is discarded. At
   most 20,000 stars are kept.
4. With Shaped Areas on, each accepted star passes the size filter, has its base
   radius multiplied and enlarged by pixels, then receives the selected radial
   profile. Overlaps keep the maximum mask value.
5. With Shaped Areas off, each accepted star becomes a solid ellipse from its
   fitted widths and position angle, painted at full value.
6. The result is one grayscale channel with the target's dimensions and
   representation. Apply records it as nonlinear with the operation
   `star-mask`.

### Detection models

With UI Threshold `s`, the candidate threshold is

`T = B + 5 s n`.

**Gaussian** fits an elliptical Gaussian with its position angle. **Moffat**
fits an elliptical Moffat profile whose exponent beta is fitted with the rest;
a fit whose beta falls below Minimum Beta is rejected. Each accepted star has a
major and a minor full width at half maximum (`FWHMmajor`, `FWHMminor`) and a
position angle. Its roundness `FWHMminor / FWHMmajor` must lie between
Roundness Threshold and the Roundness Range maximum (`1` when the range is
off). With Amplitude Range on, the fitted amplitude, in the same normalized
units, must lie inside the range.

Shaped masks use the larger full width as the base radius,
`R_base = max(FWHMmajor, FWHMminor)`, and become circular even when the fitted
star is elliptical. Solid ellipses instead keep the fit: each ellipse spans
`4 x FWHMmajor` along the major axis, has the fitted axis ratio, and is turned
by the position angle. A pixel is painted with `1` when at least half of its
area, estimated on a 4 × 4 sub-pixel lattice, lies inside the ellipse.

### Growth, size, and profile equations

The size filter accepts a shaped star only when

`Min Size <= 2 R_base <= Max Size`.

The painted radius is then

`R = Percent Increase x R_base + Pixel Increase`.

Thus the default `0.5` is a half-radius multiplier, not a 50% addition. For
distance `d` from the star's center to each pixel's center, the six profiles
are:

- Bell: `exp(-d^2 / (2 R^2))`.
- Truncated Bell: `min(1, exp(0.5) x exp(-d^2 / (2 R^2)))`.
- Linear: `max(0, 1 - d / (3 R))`.
- Truncated Linear: `max(0, 1.5 - 1.5 d / (3 R))`.
- Cubic: `max(0, 1 - (d / (3 R))^3)`.
- Quadratic (implementation label): `max(0, 1 - (d / (3 R))^4)`.

Each star is painted over the square reaching `3R` from its center, and values
are clamped to `[0,1]`. The four polynomial profiles reach zero at `3R`; the
two bells are cut at the square's edge, which leaves a small step of at most
about `0.011` for Bell and `0.018` for Truncated Bell. Despite its UI name,
**Quadratic** uses a fourth power.

### Boundary and failure behavior

- The candidate scan skips a border of Radius pixels around the searched area,
  so a star peaking that close to the edge is not found. Painting is clipped to
  the image.
- A picture, or a Preview area, that is not more than `4 x Radius + 8` pixels
  wide and tall is refused with the reason, and no mask is made.
- An Amplitude Range Maximum below its Minimum, a Roundness Range Maximum below
  Roundness Threshold, and, with Shaped Areas on, a Min Size above Max Size are
  refused with the reason, and no mask is made.
- A filter-array (mosaic) image that has not been demosaiced is refused.
- No detections produce a black mask rather than an error.
- In the search plane, a pixel the image's own validity marks as not valid, or
  on an ordinary image a pixel holding NaN or an infinity, is replaced by the
  measured sky before smoothing, so it neither stops the run nor becomes a
  star. With Use Lightness, a pixel the image's validity rejects reads as black
  in the lightness plane instead.
- A run the session's memory budget cannot hold is refused with the reason and
  makes nothing.
- Detection and painting observe Cancel; a stopped run makes no mask and shows
  no error.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Radius + picture or Preview area size | Radius `5`, which needs more than 28 pixels each way | Broad stars truncated or split | Run refused on a small picture or area; a fainter star within Radius of a brighter one is lost | Match Radius to star width -> check the searched size -> enlarge the area or lower Radius |
| Radius + Threshold | Radius `5`, Threshold `1.00` | Broad/faint stars missing | Dense noise candidates, blends, slow fits | Match Radius to star width -> lower Threshold gradually |
| Roundness + Relax PSF Checks | `0.50`, Relax off | Useful elongated stars rejected | Trails/blends/poor fits retained | Tune Roundness -> inspect -> enable Relax only for diagnosed rejection |
| Gaussian + Moffat/Minimum Beta | Gaussian first | Broad wings fit poorly | Moffat becomes slow or rejects fits | Compare same field -> choose Moffat -> tune beta last |
| Detection + shaped growth | Fix detection first; then `0.50`, `0 px` | Accepted stars under-covered | Halos merge and protect background | Fix false/missed detections -> Percent Increase -> Pixel Increase -> size bounds |
| Shaped + solid output | Shaped Truncated Bell for graded protection | Wings too narrow | Hard edges or excessive merged coverage | Choose intended downstream use -> compare modes -> refine shape/growth |

## Inputs, outputs, and metadata

- Apply creates one new mask; it never replaces the still source. A sequence
  source creates a new temporary mask sequence and keeps the source sequence.
- Output width and height match the processed source frame, and the output has
  exactly one grayscale channel in the target's representation. A floating mask
  holds `0..1`; an unsigned integer mask runs from `0` to the largest code, and
  a signed integer mask from the smallest code (unpainted) to the largest
  (fully painted), rounded to the nearest code, ties to even. CFA sequence
  auto-debayer can change the decoded detection input to RGB, but the mask
  remains grayscale.
- The new mask is named `<target> - STARMASK`; snapshots use
  `<target> - Star Mask - SNAPSHOT`. No file is saved automatically.
- The mask keeps the target's acquisition record, astrometric solution, and
  lineage, but none of the target's FITS/XISF header cards: it carries only the
  processing-state cards, `ANLIN` nonlinear, `ANASRC` process, and `ANOP`
  `star-mask`. Every sample is marked valid, and no linear encoding is claimed.
  Apply adds a lineage entry naming `an::paintShapedStarMask`, or
  `an::paintEllipticalStarMask` without Shaped Areas, with the step
  description.
- The new view's history starts with the step description. At the defaults it
  reads:

  `Created in shaped areas mode. Radius: 5, Threshold: 1.00, Profile: Gaussian. Percent Increase: 0.50, Pixel Increase: 0, Min Size: 2, Max Size: 25, Star Shape: Truncated Bell`

  Without Shaped Areas it says `binary areas mode`; with Use Lightness the
  profile is followed by `, on the lightness`. The target's own history is
  unchanged.
- Snapshots hold what the Preview showed and do not gain the lineage entry that
  Apply records.
- An active source mask and inversion state are ignored. Assign the generated
  mask later through the [Masks](../../interface/masks.md) workflow.
- Nothing is stored between openings of the window.

## Performance

Detection scales with image area and candidate count. The candidate scan runs
in parallel bands, and every candidate is fitted with a nonlinear PSF, brightest
first, until 20,000 stars are accepted or the candidates run out. Moffat, larger
Radius, lower Threshold, higher Convergence, Relax PSF Checks, and dense fields
increase work. Painting runs in parallel over rows, each row checking every
accepted star, and each shaped star covers a square of `6R` pixels on a side;
solid ellipses sample every covered pixel on a sub-pixel lattice. A run copies
the target (plus a floating working copy for integer images, and a lightness
plane with Use Lightness), and holds the mask with, for an integer target, its
floating working copy. A run the session's memory budget cannot hold is refused
with the reason. Sequence processing adds decoding, encoding, and temporary disk
proportional to frame count. Tune on Preview area, then verify the full frame
before Apply. No production timing or peak memory was measured.

## Worked example

- Input and settings: A synthetic linear grayscale Float32 field of planted
  stars, wide enough for the search, at the dialog defaults: Radius `5`,
  Threshold `1.00`, Gaussian, Shaped Areas, `0.50` Percent Increase, `0` Pixel
  Increase, size `2..25`, and Truncated Bell.
- Expected checkpoint: The mask is one channel, the pixel under every planted
  star holds more than `0.5`, and the shaped falloffs leave samples between `0`
  and `1`. With Shaped Areas unchecked, every mask sample is exactly `0` or
  `1`. On an RGB copy whose green channel holds the field and whose red and
  blue channels hold it mirrored, every green star is painted. A 32 × 24 pixel
  picture is refused at Radius `5`, because it needs more than 28 pixels each
  way, and is searched at Radius `3`, which needs more than 20.
- Inspect: At 100%, check missed peaks, false noise/hot pixels, saturation,
  blends, profile edges, and whether mask coverage reaches the intended wings.
- Limitation: This review did not execute an approved astronomical star field or
  sequence, so detection completeness, production mask quality, timing, and
  memory are not claimed.

## Warnings and limitations

- Percent Increase is a multiplier, not an additive percent. Default `0.50`
  shrinks the measured base radius before additive pixels.
- Min/Max Size are diameter-like tests (`2 x` the larger FWHM), despite tooltips
  referring to star radius. They are ignored when Shaped Areas is unchecked.
- Shaped masks discard fitted ellipticity and angle by using the larger FWHM as
  a circular radius. Use solid ellipses when fitted ellipse geometry matters.
- Solid ellipses have hard edges and offer no soften control in this process;
  Percent Increase, Pixel Increase, Min/Max Size, and Star Shape do not apply
  to them.
- The **Quadratic** profile is fourth-power in the current implementation.
- A floating-point image is read as it is and full scale is taken as `1`. The
  detector judges saturation as a fraction of the range from the sky to `1`,
  so data scaled far above `1` is judged against the wrong range.
- Apply marks the mask nonlinear. Do not interpret its state as the source
  image's processing state.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Process is disabled or does not open | No active image/sequence, duplicate attached process, or exclusive attached process | Activate the intended view and close conflicting process windows |
| Error message includes `the searched rectangle is too small for the search radius`, at Preview or Apply | The picture, or the Preview area, is not more than `4 x Radius + 8` pixels wide and tall | Lower Radius, enlarge the Preview area, or uncheck it; nothing was made |
| Error message includes `the amplitude window's upper bound is at or above its lower bound` | Amplitude Range Maximum is below its Minimum | Raise Maximum or lower Minimum |
| Error message includes `the largest roundness lies within the unit interval and at or above the smallest` | Roundness Range Maximum is below Roundness Threshold | Raise Maximum or lower Roundness Threshold |
| Error message includes `the size window is finite, not negative and ordered` | Min Size is above Max Size with Shaped Areas on | Correct the size bounds |
| Error at Preview or Apply on a camera mosaic | The image is a filter-array mosaic that has not been demosaiced | Demosaic first, or enable sequence auto-debayer |
| No stars appear | Threshold too high, Radius unsuitable, optional ranges reject all fits, or the searched plane is weak | Reset; check green versus Use Lightness; tune detection before size/growth; disable optional ranges |
| Many hot pixels/noise peaks appear | Threshold/size rejection is too permissive or Relax PSF Checks admits poor candidates | Raise Threshold; use shaped Min Size; disable Relax; inspect on the same display stretch |
| Moffat detects far fewer stars | Fits fail or beta falls below Minimum Beta | Return to Gaussian; use Moffat only for diagnosed wings and tune Convergence/beta carefully |
| Mask is too small at Percent Increase `0.50` | `0.50` is a half-radius multiplier, not a 50% addition | Increase toward `1.0` and/or add Pixel Increase |
| Min/Max Size changes do nothing | Shaped Areas is unchecked | Re-enable Shaped Areas or accept that solid ellipses ignore shaped controls |
| Shaped mask loses elongation | Shaped masks use the larger FWHM as a circular radius | Use solid ellipses to retain fitted ratio/angle, or use a later morphology workflow |
| Preview differs from final output | Preview area is searched on its own, or the display mode differs from the full-frame comparison | Uncheck Preview area, match Linear/Autostretch settings, and recheck at 100% |
| Error names the memory budget | The session cannot hold the run's copies of the image and the mask | Close other images or work on a smaller crop; nothing was made |
| The mask has no OBJECT, filter, or exposure cards | A mask carries only the processing-state cards of its target's header | Expected; the acquisition record and astrometric solution are kept |

## Related workflow steps

Use [Star Profiler](../registration-alignment/star-profiler.md) when you need a
saved, inspectable, and manually curated detection list.
Assign the output through [Masks](../../interface/masks.md), refine it with
[Morphological Transformation](../masking/morphological-transformation.md), or
use it to guide [Star Reduction](star-reduction.md). For neural star separation
instead of profile detection, use [Starnet++](starnet.md).

## Credits

- The fitted-PSF detector is adapted from
  [Siril](https://gitlab.com/free-astro/siril)'s star finder and PSF fit, by
  Francois Meyer and team free-astro (GPL-3.0-or-later), and extended by
  AfterNight.
- The six Star Shape profiles, their reach of three radii and the largest value
  kept where stars overlap are adapted from
  [DeepSkyStacker](https://github.com/deepskystacker/DSS)'s star-mask shapes, under
  the BSD 3-Clause License of Luc Coiffier and the DeepSkyStacker contributors.
  **About > About AfterNight** credits both works and reproduces that licence's
  notice.
