# SCNR

## Purpose and expected result

SCNR (Subtractive Chromatic Noise Reduction) suppresses green in a three-channel
RGB image, or in every frame of an RGB sequence, with one of four per-pixel
protection methods. Two methods hold green at a ceiling taken from red and blue;
two multiply green by a protection mask taken from red and blue, mixed by
**Amount**. **Preserve Lightness** keeps each pixel's CIE L\* lightness while
taking the suppressed pixel's colour.

On **Apply** the process replaces the target's samples, weighed by the view's
image mask when one is enabled, and records one Image History step. The image's
size, sample representation and metadata are kept, and the result is recorded
as nonlinear; the change is recorded in the image's lineage. SCNR
always targets green; it cannot suppress red, blue, cyan, magenta or yellow.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Calibration > SCNR Filter**; window title **SCNR** followed by the view name |
| Enabled when | The active view shows a colour image. The **Target Image** list offers only views showing a three-channel RGB image. The process is nonexclusive. |
| Inputs | One three-channel RGB still or sequence of any of the eight sample representations, a protection method, Amount (mask methods only), Preserve Lightness, and the view's enabled image mask, if any |
| Outputs | Replaces the target as one Image History step; a sequence is written whole into a new sequence of the same container, which the view then shows. No preview, mask, report or new view is created. |
| Recommended image state | After gradient removal, background neutralization and colour calibration. SCNR records its result as nonlinear, so steps that need linear data refuse or warn about it afterwards. |
| Preview support | None. Apply runs the full image or sequence; the window stays open for another run. |
| Sequence support | Yes: every frame, whatever frames the player has selected, with the settings read at Apply; progress and Cancel are shown |
| Mask support | Yes: the result is weighed by the view's enabled mask, read when Apply is pressed; inversion is honoured |
| External dependencies | None |

## Before you start

1. Finish gradient correction, background neutralization and PCC, SPCC or
   manual colour calibration first. SCNR removes green; it does not estimate a
   neutral background or a calibrated colour response.
2. Decide whether the residual green is unwanted. Green can be real signal
   (OIII contribution, reflection nebulosity, stars), and the mask methods
   reduce green even where it is not the strongest channel.
3. Know how the image is described. With **Preserve Lightness**, an image whose
   description states linear RGB is read as linear light; any other colour image
   is read as sRGB-encoded. The lightness kept is computed in that reading.
4. Image History keeps the step and it can be undone. There is no preview or
   Reset, and a second Apply suppresses again on top of the first.
5. If you use an image mask, check that it is enabled, has the image's width and
   height, and is or is not inverted as you intend. It is read when Apply is
   pressed.
6. For a sequence, every frame must be three-channel RGB as SCNR reads it. If
   the sequence viewer demosaics the frames it shows, the frames are demosaiced
   before SCNR, by the method the sequence states from the one RAW
   [Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
   setting.

## Procedure

1. Activate the RGB target and open **Process > Color Calibration > SCNR
   Filter**. Confirm **Target Image**.
2. Start with **Maximum Neutral** for the gentler hard ceiling: it reduces green
   only where green exceeds both red and blue. **Average Neutral**, the default,
   holds green to the mean of red and blue and removes more.
3. For a graded correction choose **Maximum Mask** or **Additive Mask**.
   **Amount** becomes enabled; start below `1.00`.
4. Leave **Preserve Lightness** selected to keep each pixel's lightness; clear
   it to change green alone, with red and blue left as they were.
5. Check the view's image mask: disable it for a global run, or leave it enabled
   for a weighted one.
6. Choose **Apply**. The dialog and the mask are read at that moment; changing a
   control afterwards does not reach the run. A still runs on a worker without a
   progress window; a sequence shows an **SCNR** progress window with **Cancel**.
7. When the result is shown, an `SCNR` Image History step is added whose
   description is `Protection Method <method> preserve lightness true` or
   `... false`, and the window stays open with **Apply** and **Close** enabled
   again.
8. Compare at 100% under the same display conditions: green star cores, nebular
   colour, dark chroma noise and any clipping. Undo before trying other settings
   rather than stacking runs.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Protection Type** | Choice | **Average Neutral** | None | **Average Neutral**, **Maximum Neutral**, **Maximum Mask**, **Additive Mask** | Selects the green ceiling or protection mask: Average Neutral holds green at `(R+B)/2`; Maximum Neutral at `max(R,B)`; Maximum Mask uses `max(R,B)` and Additive Mask `min(1,R+B)` as the mask | Selecting a mask method enables Amount; a neutral method disables it | Average Neutral removes the most green; Maximum Neutral keeps green up to the stronger of red and blue; Additive Mask protects more than Maximum Mask at the same pixel and Amount | Apply; green, and all three channels with Preserve Lightness; still/sequence | Window lifetime; no Reset or preset |
| **Amount** | Slider and decimal spin box | `1.00`, disabled under the default method | None | `0.00`–`1.00`; spin step `0.05`; slider `0`–`1000` | Higher suppresses more green in the mask methods; `0` leaves green as it was and `1` applies the mask outright | Enabled only for **Maximum Mask** and **Additive Mask**; ignored by both neutral methods; keeps its value when the method changes | High values can desaturate legitimate green | Apply; green; still/sequence | Window lifetime |
| **Target Image** | Image/sequence selector | The launching view | None | Open views showing a three-channel RGB image | Chooses the image or sequence Apply changes | Reattaches the window to the chosen view | A wrong target will be changed | Apply; still/sequence | Window lifetime |
| **Preserve Lightness** | Checkbox | Selected | None | Selected/cleared | Selected: the result keeps the original pixel's CIE L\* and takes a\* and b\* from the suppressed pixel, so red and blue move too. Cleared: only green changes | Always enabled | Three colour conversions per pixel; the reconstructed colour can leave the unit interval and is then held to it | Apply; all three channels when selected; still/sequence | Window lifetime |
| **Apply** | Action | Enabled | None | One run at a time | Reads the dialog and the view's mask, then starts SCNR on a worker | Apply and Close disable while it runs and re-enable when the run lands | Destructive; no preview; Image History keeps the image it replaces | Apply; still/sequence | The window stays open; settings are not saved |
| **Close** | Action | Enabled | None | Button or title-bar close | Closes without starting a run | The button disables during a run; a title-bar close while a run is in flight hides the window, which closes once the run has landed; it does not stop the run | Unapplied settings are discarded | Dialog only | Not persisted |
| Sequence progress window | Read-only progress | Shown only for a sequence | None | Title **SCNR**; text `Executing SCNR to sequence <view name>` | Shows the run over the sequence's frames | Belongs to the run, not to the SCNR window | None | Apply; sequence only | Per run |
| **Cancel** in the progress window | Action | Available while a sequence runs | None | One request | Stops the run between frames | Sequence only; a still run has no Cancel | A stopped run leaves the sequence as it was, writes nothing into Image History and leaves no temporary sequence behind; nothing is reported | Apply; sequence only | Per run |

## How the algorithm works

### Conceptual pipeline

1. On **Apply**, read the protection method, Amount, Preserve Lightness and the
   view's enabled mask (and whether it is inverted) on the GUI thread. The worker
   reads only those values.
2. Refuse an image that is not three-channel RGB with `SCNR needs an RGB image`.
3. Make a working copy of the samples as fractions of full scale: an unsigned
   integer as a fraction of its largest value, a signed integer counted from its
   smallest, in Float32 - Float64 for a Float64 image and the two 32-bit integer
   representations.
4. Run the engine's chromatic-noise reduction (`reduceImageChromaticNoise`) with
   a white level of `1`: compute green from the selected method; with Preserve
   Lightness, rebuild the pixel in CIE L\*a\*b\*; hold all three channels to
   `[0,1]`. A pixel the image's validity rejects is left as it was in all three
   channels.
5. Bring the working copy back to the image's representation, to the nearest
   value and saturated, keeping the image's description, validity and metadata,
   with a lineage entry `an::reduceImageChromaticNoise`, detail
   `SCNR: <method>, amount <amount>, lightness preserved` (or
   `not preserved`), the method spelled without spaces (`AverageNeutral`) and
   Amount to six decimals.
6. If the view had an enabled mask, weigh the result by it against the source.
7. For a still image the result is an edit of the view: Image History records
   the step before the view shows it. For a sequence, see Inputs, outputs, and
   metadata.

### Model and equations

Let `(r,g,b)` be one pixel as fractions of full scale and `A` be **Amount**.

**Average Neutral** computes

\[
g'=\min\left(g,\frac{r+b}{2}\right).
\]

**Maximum Neutral** computes

\[
g'=\min\big(g,\max(r,b)\big).
\]

Both are hard ceilings and do not read Amount. Average Neutral is never gentler
than Maximum Neutral because `(r+b)/2 <= max(r,b)`.

**Maximum Mask** uses `m = max(r,b)`; **Additive Mask** uses
`m = min(1, r+b)`. Both evaluate

\[
g'=g\,\big[(1-A)(1-m)+m\big]=g\,\big[1-A(1-m)\big].
\]

At `A=0`, `g'=g`; at `A=1`, `g'=mg`. Additive Mask's `m` is never smaller than
Maximum Mask's, so it keeps at least as much green at the same pixel and Amount.
The mask methods do not first ask whether green exceeds red or blue.

Without Preserve Lightness the result is `(r, g', b)`, held to `[0,1]`.

With Preserve Lightness the engine converts the original pixel to CIE L\*a\*b\*
(D65 white, sRGB primaries) and keeps its `L*_0`, converts the suppressed pixel
`(r, g', b)` and keeps its `a*_1, b*_1`, converts `(L*_0, a*_1, b*_1)` back to
RGB, and holds each channel to `[0,1]`. The RGB samples are read through the
transfer the image states: linear when the image's description says its colour
is linear RGB, sRGB-encoded for any other colour image. Where the rebuilt colour
leaves the unit interval, the hold means the lightness is not kept exactly.

For a view mask value `alpha`, the result is

\[
O=(1-\alpha)\,I+\alpha\,P,
\]

where `I` is the source and `P` the processed image; an inverted mask swaps the
weights. A one-channel mask weighs all three channels; a three-channel mask
weighs each channel by its own.

### Boundary and failure behavior

- A view that does not show a three-channel RGB image is not offered as a
  target. A sequence frame that is not three-channel RGB stops the run with
  `SCNR needs an RGB image`.
- Only green is suppressed. There is no channel selector, cast detector,
  threshold or spatial model.
- Every result sample is held to `[0,1]` of full scale and then written in the
  image's representation. No count or map of held samples is produced.
- A failure - the engine refusing memory, a mask of another size, a frame that
  is not RGB - is reported in a message titled **Error running SCNR** whose text
  is `An error occurred when executing the SCNR process:` followed by the reason,
  and in the log; the image and its history are left as they were and the
  window stays open.
- If another run already holds a sequence view, SCNR shows the view's busy
  warning titled **SCNR** and starts nothing.
- If Image History cannot keep the image the run replaces, the history says so
  once, in a message titled **Image History**, SCNR says nothing more, and the
  image and its history are left as they were.
- A still run has no Cancel. Closing the window during a run hides it until the
  run lands; it does not stop the run.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Maximum Neutral + Average Neutral | Try Maximum Neutral once after colour calibration | Green remains above the red/blue mean but not above both channels | Average Neutral removes legitimate green or changes star or nebular colour | Compare Maximum Neutral first; undo before testing Average Neutral |
| Additive Mask + Amount | Additive Mask below `1.0` | Residual unwanted green or chroma noise | Broad green desaturation, especially in dark pixels | Choose the method, raise Amount gradually, inspect real green structures at 100% |
| Maximum Mask + Amount | Use when Additive Mask protects too much | Weak red and blue leave green under Additive Mask | Dark or weakly coloured areas lose green strongly | Switch method first, then retune Amount from a lower value |
| Preserve Lightness + clipping | Selected; inspect all channels | Cleared, the result is darker where green was removed | Selected, the rebuild is held at `0` or `1` or shifts red and blue | Test one method and Amount, compare Preserve on and off through undo, inspect histograms |
| View mask + SCNR | Settle the global settings unmasked, then enable a soft mask | A dark or disabled mask changes little | A hard or colour mask creates local, channel-dependent transitions | Check the mask and its inversion, then Apply once |

## Inputs, outputs, and metadata

- Inputs are three-channel RGB stills or RGB sequence frames of any of the eight
  sample representations. Width, height and representation are kept.
- The result keeps the image's description, validity rule and mask, acquisition
  record, plate solution and header cards. Its processing-state cards say it is
  nonlinear display RGB made by the operation `scnr`, and its meaning no longer
  claims a linear encoding. The lineage gains one entry,
  `an::reduceImageChromaticNoise`, naming the method, Amount and whether the
  lightness was preserved.
- An enabled view mask weighs the result against the source, honouring
  inversion; it must have the image's width and height and one channel or three.
- On a still image the result is one `SCNR` Image History step,
  `Protection Method <method> preserve lightness true|false`; it does not record
  Amount or the mask.
- On a sequence, SCNR takes the view for the run, reads every frame of the
  sequence - every one, whatever the player has selected, and demosaiced when
  the view demosaics them - writes each result frame into a new temporary
  sequence of the same container, publishes it whole, and shows it in the view
  as one Image History step. A run that fails or is cancelled leaves the
  sequence as it was and nothing in the temporary directory, and releases the
  view.
- Apply creates no preview, new view, mask, clipping map, report or saved file.

## Performance

Work is proportional to the number of pixels. A run holds two working copies of
the image in Float32 (or Float64), the result in the image's representation and,
for Image History, the image it replaces. **Preserve Lightness** adds three
colour conversions per pixel. An enabled mask adds a copy of the result and one
blend. A sequence additionally writes a temporary output sequence. No GPU,
network or preview cache is involved, and no timing is claimed here.

## Worked example

- Evidence mode: source-and-test-derived; not run on approved astronomical data.
- Input condition and settings: one pixel of a Float32 image,
  `(R,G,B) = (0.20, 0.60, 0.40)`, Preserve Lightness cleared, no view mask.
- Expected checkpoint: Average Neutral gives `G' = 0.30`; Maximum Neutral gives
  `G' = 0.40`; at Amount `1.0`, Maximum Mask gives `G' = 0.24` and Additive Mask
  `G' = 0.36`; at Amount `0.5`, Maximum Mask gives `G' = 0.42`. Red and blue stay
  `0.20` and `0.40`. With Maximum Neutral the history step reads
  `Protection Method Maximum Neutral preserve lightness false`.
- Inspect at 100%/statistics/history: compare green structures and star cores,
  clipping with Preserve Lightness, mask boundaries and the history step. Note
  Amount separately, because the history does not record it.
- Limitations: the arithmetic shows the equations only; it is not evidence of
  perceptual quality, removal of a real cast, colorimetric accuracy or runtime.

## Warnings and limitations

- SCNR always suppresses green. It has no colour selector and no test that the
  green is unwanted.
- SCNR is a nonlinear colour change, and the result is recorded as nonlinear:
  an image declared linear is declared nonlinear afterwards.
- Preserve Lightness reads a linear-RGB image as linear light and any other
  colour image as sRGB-encoded, under a D65 white. It is not a profile-aware
  conversion.
- Average and Maximum Neutral are full-strength ceilings; Amount is disabled and
  not read. The mask methods act everywhere, not only where green dominates.
- Apply is destructive, has no preview or Reset, and the window stays open for
  repeated runs. Undo between trials.
- The view mask weighs the result only; a three-channel mask weighs each channel
  differently.
- All result samples are held to `[0,1]` of full scale with no clipping count.
- History records the method and Preserve Lightness only.
- A sequence run can be cancelled only between frames; a still run cannot be
  cancelled.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| The target is not offered, or the menu action is disabled | The view does not show a three-channel RGB image | Select an open RGB still or sequence | `showsRgbImage`; `ColorToolsUnitsTest.AnImageATooDoesNotReadIsRefused` |
| Amount is disabled | **Average Neutral** or **Maximum Neutral** is selected; neither reads Amount | Choose **Maximum Mask** or **Additive Mask** | `ColorToolsCase.ScnrOffersTheAmountToTheMaskMethodsAndStaysOpen` |
| Amount `0` still changes something | The mask methods leave green as it was at `0`, but Preserve Lightness's round trip and the hold to `[0,1]` can move samples | Clear Preserve Lightness for an identity check | Engine equations |
| Legitimate green disappears | A ceiling or mask method suppressed real green; the mask methods do not require green to dominate | Undo; try Additive Mask with a lower Amount, or a view mask | SCNR equations |
| Red or blue changed | Preserve Lightness rebuilt the pixel from its original L\* and the suppressed colour | Undo; compare with Preserve Lightness cleared | Engine source |
| **Error running SCNR** | The engine refused memory, the mask did not fit, or a frame was not RGB | Read the message and the log, free memory or fix the mask, and run again | `ColorToolsWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| The mask had no or the wrong effect | The mask was disabled, inverted, or changed after Apply was pressed | Check the mask before Apply | `ColorToolsWindowTest.TheViewsMaskWeighsTheResult` |
| A setting changed during the run had no effect | The dialog is read when Apply is pressed | Run again with the new setting | `ColorToolsWindowTest.ARunReadsTheDialogAsItWasWhenApplyWasPressed` |
| History cannot reproduce the result | The step omits Amount and the mask | Note Amount and the mask before Apply | `ColorToolsCase.TheHistoryStepsDescribeWhatRan` |
| Busy warning titled **SCNR** | Another run holds the sequence view | Wait for it to finish, then run again | `runInPlace` busy path |
| Sequence does not change after Cancel | A stopped run publishes nothing | Run again | `ColorToolsWindowTest.AStoppedRunPublishesNothing` |

## Related workflow steps

Run [Background Extraction](../background-extraction/background-extraction.md),
[Background Neutralization](background-neutralization.md), and one of
[Color Calibration](color-calibration.md),
[Photometric Color Calibration](photometric-cc.md), or
[Spectrophotometric Color Calibration](spectrophotometric-cc.md) before SCNR.
Those steps estimate gradients, offsets or calibrated channel balance; SCNR is
a later selective green subtraction.

Use [Histogram Transformation](../stretching/histogram-transformation.md) to
establish the intended nonlinear display state, and
[Color Saturation](../color-adjustment/color-saturation.md) for broader chroma
adjustments. A view mask can localize SCNR, but SCNR itself does not generate a
cast mask.

## References and provenance

- The AfterNight SCNR window, the shared colour-tool support, the engine's
  chromatic-noise reduction and the focused tests
  are authoritative for the four methods, the fixed green channel, the hold to
  the unit interval, the metadata, the mask and the execution behavior.
- International Color Consortium, [How to interpret the sRGB color space](https://www.color.org/sRGB.pdf),
  checked 2026-07-13. Context for the sRGB transfer and D65 white used by
  Preserve Lightness; AfterNight does not perform an ICC profile conversion here.
- CIE, [CIE 1976 L*a*b* Colour Space](https://cie.co.at/publications/colorimetry-part-4-cie-1976-lab-colour-space-1),
  checked 2026-07-13. Context for `L*`, `a*`, `b*`; the SCNR protection methods
  are AfterNight implementation formulas, not CIE standard operations.
