# Background Neutralization

## Purpose and expected result

Background Neutralization brings the background of the three colour channels of
an RGB image, or of every frame of an RGB sequence, to one shared level. It
measures each channel's background level in a reference - the target itself,
another open colour image, or a rectangle of either - as the median of the
samples that lie within the dialog's limits, and moves every channel by the
constant that brings its level to the mean of the three levels.

It is intended for a linear image whose large-scale gradients have already been
corrected. A successful **OK** replaces the target still or sequence with one
**Background Neutralization** Image History step, keeps its dimensions, channel
layout, sample representation and metadata, and closes the window.

This operation aligns additive channel backgrounds; it does not estimate a
multiplicative white balance, solve catalog colours, remove spatial gradients,
or guarantee that an unrelated target becomes neutral from a separate reference.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Calibration > Background Neutralization** |
| Enabled when | A colour still or sequence view is active and no other process holds it; the process requires exclusive access. The **Target Image** selector lists the open three-channel RGB views. |
| Inputs | One three-channel RGB target and one three-channel RGB background reference. The reference may be the target or another open view, and may differ from the target in dimensions and sample representation. |
| Outputs | The target replaced as one Image History step; a sequence is written whole into a new sequence the view then shows. No auxiliary view or file is written. |
| Recommended image state | Gradient-corrected, stacked, cropped, linear RGB. The process does not test linearity. |
| Preview support | No processed preview. **From Selection** draws the ROI rectangle on the reference view only. |
| Sequence support | Yes: every frame, whatever frames the player has selected. The reference is measured once and the same offsets are applied to every frame; with `<--TARGET IMAGE-->` the reference is the frame the view shows when **OK** is pressed. Progress and **Cancel** are shown. |
| Mask support | An enabled view mask weighs the result: the source where the mask is empty, the result where it is full; inversion is honoured. The mask does not change the measurement. |
| External dependencies | None. |

## Before you start

1. Complete calibration, registration, integration, gradient correction, and
   border cropping. A spatial gradient cannot be repaired by one offset per
   channel.
2. Keep the target linear. An additive offset per channel has its intended
   radiometric meaning before stretching.
3. Identify a clean background area without stars, nebulae, galaxy halos,
   clipped borders, or dust signal. Use a separate reference only when its
   channel backgrounds are genuinely representative of the target.
4. Check that the limits contain background in every channel. The defaults keep
   samples from `0.0` to `0.1` of full scale; a channel with no sample in that
   range is refused, by name, and nothing changes.
5. A signed integer image (8, 16 or 32 bits) is read as a fraction counted from
   its smallest value, so a background stored near the zero code reads near
   `0.5`. Set the limits around the background as the image reads it.
6. Image History can undo the step. **OK** replaces the target rather than
   creating a new view.

## Procedure

1. Open **Process > Color Calibration > Background Neutralization** and confirm
   **Target Image**.
2. Choose the background **Reference image**. `<--TARGET IMAGE-->` is selected
   by default and means the image the run changes.
3. Leave **Region of interest** off to measure the whole reference, or turn it
   on and either enter **Left**, **Top**, **Width** and **Height** or press
   **From Selection** and drag a rectangle of clean sky on the reference view.
4. Start with **Lower Limit** `0.0` and **Upper Limit** `0.1`. Samples on
   either limit are kept. Raise **Lower Limit** above black borders and lower
   **Upper Limit** below stars and bright structure, keeping background in
   every channel.
5. Press **OK**. The dialog, the reference's image and the view mask are read
   at that moment; changing a field afterwards does not reach the run. A still
   is processed on a worker without a progress window; a sequence shows
   progress and **Cancel**.
6. Success closes the window and adds a **Background Neutralization** Image
   History step whose description is
   `Reference image '<name>' (ROI X:<x>, Y:<y>, W:<width>, H:<height>)`.
7. Compare background channel statistics under the same display stretch.
   Continue to white or colour calibration only if the background is stable and
   unclipped.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Reference image | Open-image selector | `<--TARGET IMAGE-->` | None | `<--TARGET IMAGE-->` and every other open three-channel RGB view, the target being offered once, as `<--TARGET IMAGE-->`; may differ from the target in dimensions and representation | Chooses the image whose channel medians set all three offsets | A closed reference returns the selector to `<--TARGET IMAGE-->`; a selected view that holds no image when **OK** is pressed is refused with **The selected background reference image is no longer available.** | An unrepresentative reference transfers the wrong offsets to the target | Measured once when **OK** runs; the same offsets reach a still or every sequence frame | Window lifetime; Reset and reopening select `<--TARGET IMAGE-->` |
| Lower Limit | Fraction of full scale | `0.0` | None | `0.0`–`1.0`; step `0.05`; six decimals; slider in thousandths | Raising it excludes darker samples; a sample equal to the limit is kept | Independent of Upper Limit; a Lower Limit above the Upper Limit keeps nothing | Too high, or above Upper Limit, leaves a channel with no sample, which is refused | Reference measurement, all three channels | Window lifetime; Reset restores `0.0` |
| Upper Limit | Fraction of full scale | `0.1` | None | `0.0`–`1.0`; step `0.05`; six decimals; slider in thousandths | Raising it admits brighter samples; a sample equal to the limit is kept | Independent of Lower Limit | Too high admits stars and structure; too low may leave a channel with no sample | Reference measurement, all three channels | Window lifetime; Reset restores `0.1` |
| Region of interest | Checkable group | Off | None | Off/On | On restricts the measurement to the rectangle; off measures the whole reference | Turning it on sets the rectangle to the whole selected reference; turning it off clears the fields to `0` and stops **From Selection** | A small clean region reduces contamination but holds fewer samples | Reference only; the target is never cropped | Window lifetime; Reset turns it off |
| Left | Integer pixels | `0` when ROI is turned on | None | `0` to reference width − 1; Width's maximum becomes reference width − Left | Moves the rectangle right | Enabled with ROI | Changes the measured background population | Reference ROI | Window lifetime; Reset/turning ROI off clears to `0` |
| Top | Integer pixels | `0` when ROI is turned on | None | `0` to reference height − 1; Height's maximum becomes reference height − Top | Moves the rectangle down | Enabled with ROI | Changes the measured background population | Reference ROI | Window lifetime; Reset/turning ROI off clears to `0` |
| Width | Integer pixels | Reference width when ROI is turned on | None | `0` to reference width − Left; **OK** requires more than `0` | Widens or narrows the measured area | Enabled with ROI | A narrow rectangle holds fewer samples | Reference ROI | Window lifetime; Reset/turning ROI off clears to `0` |
| Height | Integer pixels | Reference height when ROI is turned on | None | `0` to reference height − Top; **OK** requires more than `0` | Extends or shortens the measured area vertically | Enabled with ROI | A short rectangle holds fewer samples | Reference ROI | Window lifetime; Reset/turning ROI off clears to `0` |
| From Selection | Checkable button and image-view drag | Unchecked | None | Drag or move one rectangle on the selected reference view | Starts an area selection on the reference view and copies its geometry into Left, Top, Width and Height; turning it off keeps the rectangle, or restores the whole reference when Width or Height is `0` | Inside the ROI group; changing the reference stops an active selection | Places the rectangle; processes nothing | Reference view | Window lifetime; Reset stops it |
| Target Image | RGB image/sequence selector | The view the process was opened on | None | Open three-channel RGB views | Chooses the still or sequence to replace; changing it resets the dialog | Background Neutralization holds the chosen view exclusively | The wrong target will be changed | All three channels; view mask; still/sequence | Target lifecycle |
| OK | Dialog action | Enabled while idle | None | Needs an available reference; an enabled ROI must be non-empty and lie wholly inside the reference | Reads the dialog, the reference and the view mask, then runs on a worker | OK, Cancel and Reset are disabled while it runs | Keeps the source for Image History; a sequence also writes a temporary output sequence | Apply; all channels; view mask; still/sequence | Success closes the window; a failure leaves it open |
| Reset | Dialog action | Enabled while idle | None | One action | Restores the reference, both limits and the ROI to their defaults and stops **From Selection** | Disabled during a run | No pixel change | Setup only | No settings are saved |
| Cancel | Dialog action | Enabled while idle | None | Button or title-bar close | Closes without applying | The button is disabled during a run; a title-bar close during a run hides the window, which closes once the run has landed; it does not stop the run | Nothing waits on the GUI thread | Dialog only | No settings are saved |

## How the algorithm works

### Conceptual pipeline

1. On **OK**, on the GUI thread: read the reference selector, the limits and the
   ROI; refuse a missing reference or an ROI that is empty or not wholly inside
   the reference; take the reference's image as it is at that moment and the
   view's enabled mask. The worker reads only these.
2. Read the reference as fractions of full scale - an unsigned integer as a
   fraction of its largest value, a signed one counted from its smallest, in a
   Float32 working copy, Float64 for Float64 and the two 32-bit integers.
3. For each of red, green and blue, keep the samples inside the rectangle that
   are finite, that the image's validity rule admits, and that lie within the
   closed interval `[Lower, Upper]`, and take their exact median.
4. Average the three medians into one shared level, and give each channel the
   offset that moves its median onto it.
5. Add each channel's offset to every sample of the target the image's validity
   admits, hold the result to `[0, 1]`, and bring it back to the target's own
   representation to the nearest value. Weigh it by the view mask when one is
   enabled.
6. For a still image the result is an edit of the view: Image History records
   the step before the view shows it. For a sequence, every frame is processed
   with the same offsets; see Inputs, outputs, and metadata.

### Model and equations

Let `S_c` be the samples of reference channel `c` inside the rectangle that are
finite, admitted by the image's validity, and satisfy

\[
L \le x \le U.
\]

Let `m_c` be the exact median of `S_c` - the middle sample, or the mean of the
two middle samples for an even count. The shared level is

\[
G=\frac{m_R+m_G+m_B}{3},
\]

and every normalized target sample becomes

\[
O_c=\operatorname{clip}_{[0,1]}\left(I_c+G-m_c\right).
\]

When the target is its own reference and nothing clips, each channel's median
within the limits moves exactly onto `G`. With a separate reference its offsets
are applied unchanged to the target.

An enabled view mask `M`, normalized to `[0,1]`, weighs the result:

\[
O' = (1-M)\,I + M\,O,
\]

and an inverted mask swaps the weights.

### Boundary and failure behavior

- The limits are a closed interval: a sample equal to either limit is kept.
  The dialog does not prevent Lower Limit from exceeding Upper Limit; such a
  range keeps nothing.
- A channel with no kept sample - every sample outside the limits, non-finite,
  rejected by the image's validity, or outside the rectangle - is refused
  before anything changes. The run reports **Error running background
  neutralization** with the text `An error occurred when executing the
  background neutralization process:` followed by, for example, `No green
  sample of the background reference lies between the limits 0 and 0.1; widen
  the limits or choose another reference`, writes the same to the log, leaves
  the image and its history as they were, and keeps the window open.
- An enabled ROI must have a positive Width and Height and lie wholly inside
  the reference; otherwise the window shows **The background ROI must be fully
  inside the reference image and have a non-zero size.** and starts nothing.
- The transform is one constant per channel. It cannot correct spatial
  gradients, vignetting, local colour structure, or multiplicative response.
- Results are held to `[0, 1]`: a negative offset can clip shadows at `0`,
  and a positive offset can clip highlights at full scale.
- An engine refusal, such as memory the session cannot provide, is reported
  under the same title with its reason, and nothing changes.
- A still run cannot be stopped once it starts. If another run already holds a
  sequence view, the view's busy warning titled **Background Neutralization**
  is shown and nothing starts. If Image History cannot keep the image the step
  replaces, the history says so once, in a message titled **Image History**,
  and the image and its history are left as they were.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| ROI + limits | Clean-sky ROI; `0.0`–`0.1`; inspect channel statistics before and after | Residual background cast because bright structure dominates or the range misses part of the background | Shadows or highlights clip, or the whole image receives an offset from a tiny or coloured patch | Correct gradients, place the ROI, raise Lower above invalid borders, then adjust Upper |
| Separate reference + target | Prefer the target itself; use a separate reference only from the same acquisition and processing state | Target remains biased because the reference's backgrounds do not match it | A new cast from transferring unrelated offsets | Verify matching calibration, filter and gradient state before choosing a separate reference |
| View mask + neutralization | Run unmasked; enable a soft mask only for an intentional local blend | The mask is dark, so little correction appears | A hard mask edge produces a local colour discontinuity | Establish the reference settings first, then enable, soften or invert the view mask |
| Background Neutralization + Color Calibration | Gradient removal → neutralize additive offsets → calibrate channel gains → stretch | Background or white balance remains biased | Repeated passes clip data or chase one correction with another | Fix gradients first, neutralize once, apply gain or colour calibration, then recheck statistics |

## Inputs, outputs, and metadata

- The target and the reference are three-channel RGB images. The reference is
  never modified; while the run reads it, it is read under a lease of the
  run's own.
- **OK** replaces one target still or sequence. Dimensions, channel layout and
  sample representation (any of the eight) are kept; every sample is worked as
  a fraction of full scale, held to `[0, 1]`, and brought back to the nearest
  value of the representation. Samples the image's validity rejects are left
  as they were.
- The result keeps the image's description, validity, acquisition record, plate
  solution, header cards and meaning. No processing state is stated. The
  image's lineage records `an::applyColorCalibration` with the detail
  `background neutralization: gains 1.000000, 1.000000, 1.000000; offsets
  <red>, <green>, <blue>`.
- On a still image the result is one **Background Neutralization** Image
  History step described as
  `Reference image '<name>' (ROI X:<x>, Y:<y>, W:<width>, H:<height>)`. The
  name is the target's own name when the reference is the target, and the
  rectangle is the whole reference when **Region of interest** is off. The
  limits are not written into the description.
- On a sequence, Background Neutralization takes the view for the run, reads
  every frame - every one, whatever the player has selected - applies the same
  offsets to each, writes the frames into a new temporary sequence of the same
  container, publishes it whole, and shows it in the view as one Image History
  step. The progress window reads `Executing background neutralization to
  sequence <name>`. A run that fails or is cancelled leaves the sequence as it
  was and nothing in the temporary directory.

## Performance

The measurement reads the reference once into a floating working copy and marks
the samples it keeps; the apply makes a floating working copy of the target and
the result in the target's representation. A still run keeps the source image
for Image History beside the result. A sequence additionally needs the
temporary output sequence; work and disk use scale with frame count and
dimensions. The process has no result preview, so prototype on a duplicate or
a representative still.

## Worked example

- Evidence mode: Source-and-test-derived; not run on approved astronomical data.
- Input condition and settings: a linear RGB master after gradient correction,
  used as its own reference; ROI over clean sky; Lower `0.0`; Upper `0.1`; no
  view mask. Suppose the medians within the limits are red `0.050`, green
  `0.060` and blue `0.070`.
- Expected checkpoint: the shared level is `0.060`; the offsets are red
  `+0.010`, green `0.000` and blue `−0.010`. A pixel `(0.200, 0.300, 0.050)`
  becomes `(0.210, 0.300, 0.040)`; a blue sample of `0.005` becomes `0`. The
  history step names the target and the ROI.
- Inspect at 100%/statistics/history: compare the channel background medians,
  shadow clipping, and the history step.
- Limitations: focused synthetic tests prove the measurement, the offsets, the
  clamp and the step text; this is not evidence of real-data neutrality,
  timing, or memory.

## Warnings and limitations

- **OK** replaces the target and there is no result preview.
- Use linear data. A nonlinear stretch changes the meaning of an additive
  offset per channel.
- Correct spatial gradients first. The process applies one constant per
  channel.
- Limits that leave any channel without a kept sample stop the run with an
  error naming that channel; the dialog does not prevent Lower Limit from
  exceeding Upper Limit.
- A signed integer image reads its zero code near `0.5`, so the default limits
  usually keep nothing in it.
- A separate reference transfers its offsets; it does not adapt them to the
  target.
- The view mask controls only how the result is blended, not what is measured.
- The limits are not written into the history description.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **The selected background reference image is no longer available.** | The selected reference view holds no image when **OK** is pressed; a closed reference instead returns the selector to `<--TARGET IMAGE-->` | Choose an open RGB reference | `BackgroundNeutralizationProcess::executeBackgroundNeutralization()` |
| **The background ROI must be fully inside the reference image and have a non-zero size.** | The enabled ROI is empty or overhangs the reference | Turn ROI off for the whole image, use **From Selection**, or correct the fields | `ColorToolsUnitsTest.ARectangleIsTheWholeImageOrARegionInsideIt` |
| **Error running background neutralization** with `No <channel> sample of the background reference lies between the limits <L> and <U>; widen the limits or choose another reference` | That channel of the reference, inside the ROI, has no finite valid sample with `L ≤ x ≤ U`, or Lower exceeds Upper; nothing changes | Widen the limits, restore Lower ≤ Upper, move the ROI, or choose a reference whose background lies within the limits | `ColorToolsCase.ANeutralizationWhoseLimitsKeepNoSampleIsRefusedByChannel` |
| **Error running background neutralization** with another reason | The engine refused the run, for example for memory | Read the message and the log, free memory, and retry | `ColorToolsWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| A signed integer image is refused with the default limits | Its background reads near `0.5` of full scale | Set the limits around the background as the image reads it | `ColorToolsWindowTest.EveryRepresentationIsKept` |
| Result is clipped | The offsets are large or the reference does not match the target | Undo; choose a clean, matching reference; inspect clipping | Constant-offset model |
| Background cast remains | A spatial gradient remains, the ROI or reference is contaminated, or the channel balance is multiplicative | Correct gradients; choose a clean target ROI; then use Color Calibration, PCC or SPCC for gains | Constant-offset model |
| An **Image History** error, and nothing is applied | Image History could not keep the image the step replaces; it says so once | Free memory and run again | `runInPlace` still-image path |
| Busy warning titled **Background Neutralization** | Another run holds the sequence view | Wait for it to finish, then run again | `runInPlace` busy path |
| Sequence does not change after Cancel | A stopped run publishes nothing | Run again | `ColorToolsWindowTest.AStoppedRunPublishesNothing` |
| Closing during a run does not stop it | A title-bar close hides the window until the run lands | Use the sequence progress **Cancel**, or wait for a still run | `ImageProcessControl::closeEvent()` |

## Related workflow steps

Run [Background Extraction](../background-extraction/background-extraction.md)
or [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md)
before neutralization. Follow with [Color Calibration](color-calibration.md),
[Photometric Color Calibration](photometric-cc.md), or [Spectrophotometric Color
Calibration](spectrophotometric-cc.md) for multiplicative channel balance.
Stretch only after the linear colour workflow, for example with
[Histogram Transformation](../stretching/histogram-transformation.md).

## References and provenance

- The AfterNight Background Neutralization process, the colour tools' shared
  support, the shared in-place run, the engine's statistics and colour
  calibration apply, and the focused tests are the
  implementation sources.
- This is a project-specific median offset model. No paper, external standard,
  or upstream algorithm claim is used.
- `ColorToolsRecordTest.EveryRecordOfTheProcessIsReproducedThroughItsWindow`
  holds the result to the stated model within `2e-6` of full scale and to the
  behavior records within one offset per channel.
