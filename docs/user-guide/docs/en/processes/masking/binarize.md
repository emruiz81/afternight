# Binarize

## Purpose and expected result

Binarize applies an inclusive hard threshold to each stored channel. Samples at
or above the chosen threshold become the representation's largest value; every
other sample becomes its smallest. Floating images become `1` and `0`. On RGB
data, one shared threshold or three independent thresholds can be used. This is
useful for turning a soft mask into a strict black/white selection or for
classifying channels without a fuzzy transition.

**Apply is destructive:** a successful still run replaces the selected target
in place. It does not open a new mask image. Work on a duplicate or on the mask
view itself, not on the scientific target from which you still need continuous
pixel values.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Binarize** |
| Enabled when | An image or sequence is active, this process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale, RGB, or single-plane CFA still, or a sequence whose frames are read as its view shows them, in any integer or floating representation; UI thresholds are normalized `0..1`; optional active target mask |
| Outputs | Replaces the selected still in place with same-size, same-representation, same-channel samples holding only the representation's smallest or largest value; a successful sequence Apply writes a temporary replacement sequence; Take snapshot creates an auxiliary still |
| Recommended image state | A duplicate mask or classification image whose meaningful threshold is known; linearity does not change the transfer, but nonlinear/display-stretched data has different scientific meaning |
| Preview support | Asynchronous preview that runs the same threshold as Apply over the target, or over the Preview area; display mode follows the target view; original/result switch, snapshot, zoom, and display-channel selection |
| Sequence support | Every frame is processed as Apply processes a still image of it, into a temporary replacement sequence, with per-frame progress and Cancel; the target is replaced only after every frame succeeds |
| Mask support | Active target mask post-blends source and binary result for Preview, snapshot, still Apply, and sequence Apply; white applies, black protects, gray blends, and Invert Mask reverses weights |
| External dependencies | None; bundled AfterNight engine only |

## Before you start

Duplicate the image if it is not already a disposable mask. Binarize destroys
intermediate gray levels and cannot reconstruct them afterward. Measure the
desired cutoff in the same normalized representation shown by AfterNight; the
process does not estimate a background, noise level, or optimal threshold.

Disable/remove the target's active mask when you want the entire image to
become strictly binary. With a mask enabled, pixels outside or partly selected
by that mask retain or mix continuous source values, so the final image is not
guaranteed to contain only the representation's smallest and largest values.

## Procedure

1. Activate a duplicate mask or other disposable still and open **Process >
   Intensity Transformations > Binarize**. Confirm **Target Image** and keep
   **Preview** checked.
2. For grayscale or CFA data, set **RGB/K** (shown as the K threshold) and leave
   the disabled joint selection as-is. For RGB, keep **Joint RGB/K channels**
   when every channel should use the same numeric threshold.
3. To use different RGB cutoffs, select **Individual RGB/K channels**. The first
   label changes from **RGB/K** to **Red**, and **Green** and **Blue** become
   editable. Switching back to Joint copies Red into Green and Blue.
4. Inspect both sides of the boundary in **Linear** display mode. A sample at
   the threshold is white/maximum. Use **Zoom 100%** to check single-pixel
   classifications; Autostretch is unnecessary for a full-range binary result.
5. Optionally enable **Preview area** around a representative region. Inside
   the area the Preview is what Apply makes of that area; around it the image is
   shown as it was.
6. Select **Apply**. Wait for the target to update and verify the history entry
   records the intended K or R/G/B thresholds. Defaults reset to `0.5` and Joint
   after success; the dialog remains open.
7. If this image is intended to mask another target, activate that target and
   use **Mask > Select Mask**. Check the polarity and disable Binarize's own
   active mask before any later full-image re-thresholding.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **RGB/K** / **Red** | Double + slider, normalized | `0.500000` | None | `0..1`; 6 decimals; spin step `0.05`; slider `0..1000` | Raising classifies fewer samples as maximum | Label is RGB/K in Joint or grayscale; Red in Individual RGB; changing it in Joint also copies the value to Green/Blue; stays editable during Apply | Hard boundary; threshold `0` selects every sample in the normalized range, while `1` selects only samples at the representation's largest value; on integer images the threshold is a stored code (see the model below) | Preview/Apply; every channel in Joint, R in Individual, K for non-RGB | Captured per execution; Reset/success `0.5`; not kept between openings |
| **Green** | Double + slider, normalized | `0.500000` | None | `0..1`; same precision/step | Raises/lowers green-channel cutoff | Enabled only for RGB + Individual; switching to Joint overwrites it with RGB/K | Channel imbalance can create colored binary edges | Preview/Apply; G only | Dialog lifetime until overwritten; Reset/success `0.5` |
| **Blue** | Double + slider, normalized | `0.500000` | None | `0..1`; same precision/step | Raises/lowers blue-channel cutoff | Enabled only for RGB + Individual; switching to Joint overwrites it with RGB/K | Channel imbalance can create colored binary edges | Preview/Apply; B only | Dialog lifetime until overwritten; Reset/success `0.5` |
| **Selection** | Selector | Joint RGB/K channels | None | Joint RGB/K channels; Individual RGB/K channels | Joint compares every stored channel with one threshold; Individual compares red, green, and blue with their own thresholds and an alpha channel, where present, with `0` | Selector disabled and forced Joint for non-colour targets; Individual is honoured only for a colour image; mode changes rerun the preview | Joint is not a luminance threshold or logical all-channel test | RGB preview/Apply; UI-only restriction for mono/CFA | Captured per execution; Reset/success Joint |
| **Target Image** | Image/sequence selector | Attached active target | None | Any open image or sequence | Changes the destructive destination, active mask, channels, and Selection availability | Disabled during Apply; updates rerun the colour check | Wrong target destroys the wrong image; verify name before Apply | Preview/Apply; still/sequence | Target lifetime; Reset unchanged |
| **Preview** | Checkbox | Checked | None | Off/on | Creates/destroys live asynchronous preview updates | Remains enabled during Apply | Each preview run works on a copy of the target or of the Preview area; previews drain to the latest settings, and Apply cancels a preview in progress | Preview only; active-mask-aware | Window lifetime; Reset unchanged |
| **Preview pane** | Image viewer | Full-frame threshold result | None | Standard pan/scroll and actions below | Displays the post-active-mask result Apply would write, without committing it | Exists while Preview is checked | Holds source/result/display buffers | Preview only | Window lifetime |
| **Fit To Window** | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits whole image | Preview image required | Broad context can hide isolated pixels | Display only | Viewer lifetime |
| **Zoom In** | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Better local inspection | Display only | Viewer lifetime |
| **Zoom Out** | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces scale | Preview image required | More context, less pixel detail | Display only | Viewer lifetime |
| **Zoom 100%** | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Preview image required | Best for threshold speckle/channel edges | Display only | Viewer lifetime |
| **View Channel** | Preview-toolbar selector | Follows result channel layout | None | Supported RGB/derived components or Grayscale | Changes only displayed component | RGB choices require RGB result | Helps compare per-channel classifications | Display only | Viewer lifetime |
| **Display Mode** | Preview-toolbar selector | Follows the target view (Linear or Autostretch) | None | Linear, Autostretch | Changes display transfer only | Preview active | Autostretch can obscure whether active-mask blending left intermediate values | Display only | Preview lifetime |
| **Autostretch Settings** | Preview-toolbar action | Initialized from target view | None | Opens display stretch settings | Adjusts only Autostretch presentation/statistics | Relevant when Display Mode is Autostretch | Adds display computation; no threshold effect | Display only | Preview lifetime |
| **Switch image** | Preview-toolbar action | Result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles threshold result and original | Preview required | Direct boundary comparison | Preview/current frame | Preview-window lifetime |
| **Preview area** | Preview-toolbar checkbox | Unchecked; centered `600×600` when geometry permits | None | Off/on and movable supported area | Runs the Preview over the selected area only and shows that result over the rest of the image as it was | Disabled during Apply; full Apply always uses full image | Reduces preview work; inside the area the result is what Apply makes of an image of that area | Preview/snapshot only | Window lifetime; Reset unchanged |
| **Take snapshot** | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Runs the current settings again and opens `<target> - Binarize - SNAPSHOT`; with Preview area on, the snapshot is the composite the Preview showed | Preview required; disabled during Apply; active mask is applied | Samples are what the Preview shows; the snapshot keeps the source's metadata and is not recorded as nonlinear | New still | One view per action |
| **Apply** | Dialog action | Enabled | None | One click | Captures thresholds/mode and replaces the selected still or sequence after success | A second Apply is ignored while one runs; the button itself is not disabled; Target Image, Preview area, and Take snapshot are disabled during the run | Destructive; works on a copy of the target, which is shown only when the run succeeds; target lock | Still/sequence; active-mask-aware | Success adds one history step, records the result nonlinear, resets defaults |
| **Close** | Dialog action | Enabled | None | One click | Closes the process window without starting a new Apply | Remains enabled during Apply; closing while a run is in progress hides the window at once, and it closes when the run has landed | Nothing waits on the GUI thread; a sequence Apply in progress is cancelled, while a still Apply already running completes | Dialog/still/sequence | Discards current UI values; nothing is stored between openings |
| **Reset** | Dialog action | Enabled | None | One click | Sets all thresholds `0.5` and Selection Joint; schedules preview if checked | Remains enabled during Apply; the running Apply uses its captured values | Extra preview work; can make displayed settings differ from in-flight run | UI/preview only | Immediate; also runs after success |
| **Sequence progress / Cancel** | Shared progress dialog | Created on sequence Apply | None | Per-frame progress; Cancel | Processes frames into a temporary sequence and replaces the target only after every frame succeeds | Sequence target only | Decode/encode and temporary disk cost; Cancel is observed between frames and inside the threshold pass | Destructive sequence replacement | Cancel discards temporary output |

## How the algorithm works

### Conceptual pipeline

1. The window captures the selected normalized threshold(s) and Selection
   mode. Individual mode applies only to a colour image (red, green, and blue,
   with or without alpha); any other image uses the RGB/K threshold for every
   channel.
2. The run works on a copy of the target, or of the Preview area. Each stored
   channel is compared in place in that copy, through the engine's
   `an::binarizeImage`, with its threshold expressed as a stored value of the
   image's own representation. The comparison runs on the run's threads and
   observes a stop request.
3. The result retains geometry, representation, pixel format, and channel
   count. An enabled target mask is then applied by the shared framework.
4. A successful Apply records the result as nonlinear with `binarize`
   provenance, states every sample valid, and is recorded in the image history
   before the view shows it.

### Model and equations

Let `m` and `M` be the representation's smallest and largest values: `0` and
`255` for 8-bit unsigned, `-32768` and `32767` for 16-bit signed, and `0` and
`1` for floating images. For a dialog threshold `t`, the compared value is

- unsigned integer: `c_t = floor(M·t)`;
- signed integer: `c_t = round(t·(M - m) + m)`, halves rounded away from zero;
- 32-bit float: `t` rounded to single precision; 64-bit float: `t`.

For a stored sample `x`, Binarize uses the inclusive transfer

`B_t(x) = M` when `c_t <= x <= M`, and `B_t(x) = m` otherwise.

For ordinary floating data this is simply `1` when `x >= t` and `0` when
`x < t`. Equality belongs to the maximum side. On integer images the comparison
is made at the stored code: an 8-bit threshold of `0.5` compares at code `127`
(about `0.498`), so code `127` becomes `255` and code `126` becomes `0`. Joint
RGB/K means

`R'=B_t(R), G'=B_t(G), B'=B_t(B)`.

It does not compute luminance, require all channels to pass, or collapse RGB to
grayscale. Individual mode substitutes `t_R`, `t_G`, and `t_B` independently and
compares an alpha channel, where the image has one, with `t = 0`. A sample that
holds no number (NaN or an infinity) is never selected and becomes `m`; floating
values above `1` also fail the inclusive test and become `0`.

With an enabled, non-inverted active mask `a`, the framework applies

`Y = X(1-a) + B(X)a`,

where `X` is the original and `B(X)` the binary result. Inverted masking swaps
the two weights. Intermediate mask values therefore create intermediate output
even though the threshold itself is binary.

### Boundary and failure behavior

There is no spatial neighborhood, so image edges receive no special treatment
and no halos are introduced. Channels are independent. A CFA still is not
debayered; it is treated as one stored K/mosaic plane while its geometry and
pixel format remain in place. Sequence frames are read as the sequence view
shows them, so a mosaic sequence with automatic debayer on is binarized as RGB.
Binarize performs no smoothing, connectivity filter, small-object removal,
resampling, or threshold validation beyond the UI's `0..1` range.

The image's validity rule is not consulted: every sample, including one the
image marks as holding no data, is compared by its value, and the result states
that every sample is valid. A pixel holding no number does not stop the run.

Preview area is exact because the threshold has no neighbourhood: the Preview
inside the area is what Apply makes of an image of that area. A refused run
(for example, one the session's memory budget cannot hold) leaves the target
and its history unchanged and is reported by the shared execution error path
with the reason. Sequence Apply writes a temporary replacement and shows it only
after every frame succeeds; Cancel or a failure removes the temporary output.
Cancel is observed between frames and inside the threshold pass.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Hard grayscale mask | Duplicate mask, no active mask, Joint, RGB/K `0.5` | Too much low-level gray structure becomes white | Desired faint structure becomes black | Measure samples, move threshold, inspect at 100% |
| RGB shared cutoff | Joint at `0.5` | Noise/signal passes similarly in every channel | One channel loses useful structure earlier than others | Tune shared value, then consider Individual |
| RGB channel cutoffs | Switch to Individual after shared preview | Color-specific contamination remains | Colored edges/speckle appear because channels disagree | Red, Green, Blue, then combined RGB view |
| Mask-limited binarization | Prefer active mask disabled | Continuous pixels remain outside selection | Not a strength issue: output is not globally binary | Remove/disable active mask, then reapply |
| CFA classification | Prefer debayer or extract intended plane first | Mosaic-pattern classification is hard to interpret | Bayer positions become a hard mosaic, not a luminance mask | Choose correct preprocessing before threshold |

## Inputs, outputs, and metadata

Binarize preserves width, height, representation, pixel format, channel count,
and image name. It does not create a grayscale mask from RGB: RGB remains
three-channel RGB in both Selection modes. Soft input values are irreversibly
replaced by the representation's smallest or largest value, except where an
active mask post-blends the original.

- Apply keeps every FITS card it does not own, the astrometric solution, and the
  acquisition record, and records the result as nonlinear on the
  processing-state cards: `ANLIN` nonlinear, `ANASRC` process, `ANOP`
  `binarize`, and the channel basis a nonlinear image of its kind carries
  (`ANBASIS` `display-rgb` for RGB, `monochrome` for a single plane). A linear
  encoding the source claimed is no longer claimed, every sample is stated
  valid, and the image's lineage gains an entry naming `an::binarizeImage` with
  the threshold (`threshold 0.5`) or the three thresholds
  (`thresholds 0.4, 0.5, 0.6`). Scientific metadata otherwise remains attached,
  but its meaning after destructive classification must be judged by the user.
- Image History records one step named **Binarize**, described as `K: 0.5 ` on
  a grayscale or CFA image, `R: 0.5, G: 0.5, B: 0.5 ` on a colour image in
  Joint, and `R: 0.4, G: 0.5, B: 0.6 ` for those Individual thresholds; undo
  restores the previous image. A sequence Apply records a step for the
  replacement sequence.
- Apply modifies the existing view and writes no file. A preview snapshot is a
  separate view holding what the Preview showed; it keeps the source's metadata
  and is not recorded as nonlinear, so it is not the normal Apply result.
- Nothing is stored between openings of the window.

## Performance

Work grows linearly with pixel count and channel count. Each channel is compared
on the threads the run is granted, with no neighborhood buffer and no floating
working copy. Preview and Apply each work on a copy of the target (a Preview
area works on a copy of the area); Apply copies the result once more when it
restates the processing-state cards, and the history keeps a copy of the state
it replaces. A mask adds a full-image blend; sequence Apply adds decode,
encode, and temporary-disk work. Preview reruns after every setting change, and
Preview area is the safest way to reduce interactive cost. A run the session's
memory budget cannot hold is refused with the reason and publishes nothing. No
production timing or peak-memory measurement was made.

## Worked example

- Evidence mode: Source-and-equation-derived.
- Input condition/provenance and settings: finite normalized floating RGB
  duplicate, no active mask, Individual thresholds Red `0.25`, Green `0.50`,
  Blue `0.75`.
- Expected checkpoint: `(R,G,B)=(0.25,0.49,0.80)` becomes `(1,0,1)` because
  Red equality is included, Green is below threshold, and Blue is above it.
  `(0.24,0.50,0.74)` becomes `(0,1,0)`. A NaN sample becomes `0`. Apply retains
  RGB geometry/representation, replaces the duplicate, marks it nonlinear,
  records `R: 0.25, G: 0.5, B: 0.75 `, and resets controls to Joint `0.5`.
- Integer checkpoint: an 8-bit grayscale image at Joint `0.5` compares at code
  `127`; codes `127..255` become `255`, codes `0..126` become `0`, and the
  history records `K: 0.5 `.
- Inspect at 100%/statistics/history: inspect colored channel disagreements at
  100%, verify each channel contains only the smallest and largest values in
  Image Statistics, and confirm the threshold history entry.
- Limitations: checkpoints follow AfterNight's source equations and were not
  executed on approved astronomical data; visual quality, sequence output,
  production timing, and peak memory are unclaimed.

## Warnings and limitations

- Apply is destructive and resets thresholds after success. Duplicate valuable
  data before opening the process.
- Joint RGB/K is a shared numeric threshold applied separately per channel; it
  is not a luminance threshold, RGB logical test, or grayscale conversion.
- Disable/remove an active target mask for a globally binary result. The shared
  mask blend can retain continuous original values.
- Threshold controls, Selection, Preview, Apply, Close, and Reset stay enabled
  during Apply; Target Image, Preview area, and Take snapshot are disabled. The
  run uses the values captured when Apply was selected, so later UI edits do not
  change it, and a second Apply is ignored until it lands. Closing hides the
  window at once; it closes when the run lands, and a sequence Apply in progress
  is cancelled.
- Selection is disabled for grayscale/CFA and forced Joint. CFA is thresholded
  as a mosaic plane without demosaicing.
- On integer images the threshold is a stored code; an unsigned threshold is
  truncated, so a sample a fraction of a code below the normalized threshold can
  become the maximum.
- In Individual mode an alpha channel, where the image has one, is compared with
  `0`, so every ordinary alpha sample becomes the maximum.
- The image's validity rule is not consulted, and the result claims every
  sample is valid.
- No fuzzy edge, automatic threshold, morphology, connectivity, antialiasing,
  smoothing, or new-mask naming/assignment is provided.
- NaN, infinities, and floating values above `1` become the smallest value,
  even though ordinary `x >= t` shorthand might suggest otherwise.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Binarize is absent under Sensor Calibration | Registry metadata files it under the sensor-calibration category, but the menu composition places it in Intensity Transformations | Use **Process > Intensity Transformations > Binarize** | app UI builder and registry wiring |
| Green and Blue cannot be edited | Joint mode is selected, or target is non-RGB | On RGB choose Individual RGB/K channels; non-RGB has only K | mode handler and `checkImageColorSpace()` |
| Switching to Joint lost custom Green/Blue values | Joint copies the current RGB/K/Red threshold into both channels | Record values before switching or re-enter them after returning to Individual | selection-mode handler |
| Result is still gray in some regions | An enabled active mask blended continuous original samples into the binary result | Disable/remove the target mask and Apply again from an unmodified duplicate/source | shared `blendProcessResult` |
| RGB result has colored edges | Individual channel thresholds classified channels differently | Inspect each channel, use closer thresholds, or return to Joint | per-channel dispatch in `BinarizeProcess::executeTypedProcessImpl` |
| An 8-bit or 16-bit sample just below the threshold became white | Unsigned thresholds are truncated to a stored code before comparing | Raise the threshold by one code step if the distinction matters | `thresholdCode` in `BinarizeProcess.cpp` |
| Pixels holding NaN became black | A sample holding no number is never selected | Repair or replace such pixels upstream if needed | `BinarizeProcess::executeTypedProcessImpl`; `an::binarizeImage` |
| Apply modified the source instead of opening a mask | Binarize uses the in-place Apply | Undo/reopen the source if possible; use a duplicate next time | `ImageRTPreviewControl::runApplyInPlace` |
| Displayed controls changed while Apply used older values | Parameters are captured when Apply is selected; Binarize does not lock its own controls | Wait for completion and read the committed history entry | run snapshot and control-lock path |
| Snapshot carries no nonlinear record | Snapshots are Preview results and are not recorded | Apply on a duplicate for an archival result | `BinarizeProcess::executeTypedProcessImpl`; `process_utils::recordNonlinearResult` |
| Sequence Cancel leaves the sequence as it was | Cancel stops the run between frames or inside the threshold pass, and the target is replaced only when every frame succeeds | Expected; Apply again when ready | `ImageRTPreviewControl::runOverSequence`; `an::binarizeImage` |
| **Error executing the process** names the memory budget | The session cannot hold the run's copy of the image or the history's copy of the state it replaces | Close other images or work on a smaller crop; nothing was changed | shared execution error path |

## Related workflow steps

Use [Range Selection](../analysis/range-selection.md) when you need two limits,
fuzzy ramps, Screening, smoothing, inversion, or a new mask image. Use
[Mask Drawing](mask-drawing.md) for manual regions and [Morphological
Transformation](morphological-transformation.md) to expand, erode, open, close,
or otherwise reshape a binary mask. Follow [Masks](../../interface/masks.md) to
assign the result to another target.

## References and provenance

This page documents AfterNight's threshold operation,
process window, metadata-state behavior, and focused tests. The hard transfer is an implementation contract; no external
automatic-threshold method or executed-data benchmark is claimed.
