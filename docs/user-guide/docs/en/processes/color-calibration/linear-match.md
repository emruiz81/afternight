# Linear Match

## Purpose and expected result

Linear Match multiplies and offsets each channel of a target image so that its
level and spread match those of a reference image. Per channel it takes the
ratio of the two images' median absolute deviations as the scale, and the offset
that moves the target's median onto the reference's. When the two images have
the same width and height, the statistics are measured over the pixel positions
both images keep; when they differ, each image is measured on its own.

On success the target still, or every frame of the target sequence, is
replaced, one **Linear Match** Image History step is recorded, and the window
closes. Use it to bring aligned channels, panels or session masters to a common
level before combination. It is not colour calibration, registration, gradient
removal or histogram matching: one straight-line map per channel is all it
applies.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Calibration > Linear Match** |
| Enabled when | An image or sequence view is active. The process is shared (it does not take exclusive access to the view). **OK** is enabled only once a **Reference Image** is chosen. |
| Inputs | One target still or sequence and one other open view with the same number of channels (one to four); width, height and sample representation may differ. Two limits, as fractions of full scale, choose the samples measured. |
| Outputs | Replaces the target as one Image History step; a sequence is written whole into a new sequence the view then shows. No new view, coefficient table or report is produced; the scale and offset are written into the image's lineage. |
| Recommended image state | Linear data from comparable preprocessing; registered and cropped alike when the two images have one size, since positions are paired. |
| Preview support | None. **OK** runs the full operation. |
| Sequence support | Yes: every frame, matched to the same reference image, behind a progress window with **Cancel**. |
| Mask support | No. An enabled view mask is not applied. |
| External dependencies | None. |

## Before you start

1. Decide which image sets the level and spread. Linear Match changes
   **Target Image** to resemble **Reference Image**, never the reverse, and
   never modifies the reference.
2. Use linear data. The process does not check linearity or processing state.
3. For two images of one size, register and crop them alike. Pixel `(x, y)` of
   the target is measured beside pixel `(x, y)` of the reference; unrelated
   framing pairs unrelated samples.
4. For two images of different sizes, only each channel's median and median
   absolute deviation are matched; positions play no part. Changing the size of
   either image by one pixel changes which of the two measurements runs.
5. A sample that is exactly zero means nothing in either image and is never
   measured. Black borders therefore need no limit, but samples just above zero
   do.
6. Both images must have the same number of channels. Their representations may
   differ: every sample is measured as a fraction of its own representation's
   full scale.
7. The result is recorded in Image History and can be undone. Save a copy if you
   need the original outside the session.

## Procedure

1. Activate the intended target and open **Process > Color Calibration > Linear
   Match**. Confirm **Target Image**.
2. Choose **Reference Image**. The list holds every other open view whose image
   has the target's number of channels, of any size or representation; a
   sequence view offers the frame it shows. Until one is chosen the selector
   reads `<--NO IMAGE-->` and **OK** is disabled.
3. Start with **Reject low** `0.00` and **Reject high** `0.92`. Both limits are
   inclusive.
4. For one-size images, lower **Reject high** to leave bright stars and clipped
   highlights out of the measurement, and raise **Reject low** to leave dark
   artefacts out. Only the reference's samples are held to the limits; a
   position is left out of both images when the reference's sample there is.
5. For different-size images, the same limits are applied to each image on its
   own. Make sure both images keep a broad, varying set of samples inside them.
6. Choose **OK**. The reference, its image and both limits are read at that
   moment; changing a control, choosing another reference or closing the
   reference view afterwards does not reach the run. A still runs without a
   progress window. A sequence shows **Linear Match** progress with **Cancel**.
7. On success the window closes. Inspect the target's statistics, check for
   values below zero or above full scale, and review the **Linear Match** step
   in Image History.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Reference Image** | Open-view selector | `<--NO IMAGE-->` | None | Any other open view whose image has the target's channel count; size and representation may differ | Chooses the image whose level and spread the target is matched to. The image it shows when **OK** is pressed is the one measured. | The target is never listed. **OK** is disabled while `<--NO IMAGE-->` is selected. The list is rebuilt when views are created, changed, renamed or closed; a closed reference returns the selector to `<--NO IMAGE-->`, and choosing another target clears the choice. | A poor reference passes its background and spread on; one-size images that are not registered pair unrelated samples. | Apply; every channel; no mask; still or sequence (one reference for every frame) | Window lifetime |
| **Reject low** | Fraction of full scale; slider and spin box | `0.00` | None | `0.00`–`1.00`; spin step `0.01`, three decimals, so the box holds every slider position; slider `0`–`1000`; inclusive | Raising it leaves darker samples out of the measurement. A zero is left out whatever the limit. | Editable at any time; a change after **OK** does not reach the run. Nothing stops it exceeding **Reject high**; limits that keep no sample are refused when the run measures. | Too high leaves too few samples; it never limits which samples are changed. | Measurement only; every channel; still or sequence | Window lifetime; no Reset or preset |
| **Reject high** | Fraction of full scale; slider and spin box | `0.92` | None | `0.00`–`1.00`; spin step `0.01`, three decimals, so the box holds every slider position; slider `0`–`1000`; inclusive | Lowering it leaves brighter samples out of the measurement; `1.00` keeps full-scale samples. | As **Reject low**. | Lower keeps stars and clipping out; too low leaves too few or flat samples. | Measurement only; every channel; still or sequence | Window lifetime; no Reset or preset |
| **Target Image** | Open-view selector | The view the window was opened over | None | Open image and sequence views the process list offers | Chooses the image or sequence that is changed. | Changing it rebuilds the reference list for the new channel count and returns **Reference Image** to `<--NO IMAGE-->`; a view the process cannot run over closes the window. | The wrong target is changed. | Apply; every channel; still or sequence | Window lifetime |
| **OK** | Action | Disabled until a reference is chosen | None | One run at a time | Reads the dialog and the reference's image, then matches on a worker. | **OK** and **Close** are disabled while the run is in flight; the other controls stay editable but do not reach the run. | Keeps the replaced image for Image History; a sequence adds a temporary output sequence. | Apply; every channel; no mask; still or sequence | Result and history persist; settings do not. The window closes once the result is shown. |
| **Close** | Action | Enabled | None | Button or title-bar close | Closes without applying. | The button is disabled while a run is in flight; a title-bar close then hides the window, which closes once the run has landed. It does not stop the run. | Nothing waits on the GUI thread. | Dialog only | Not persisted |
| Sequence progress | Read-only progress window | Shown for a sequence only | None | Titled **Linear Match**; text `Executing linear match to sequence <name>` | Shows the run over the frames. | Belongs to the run, not to the Linear Match window. | The temporary output sequence needs disk space. | Sequence Apply | Not persisted |
| **Cancel** in sequence progress | Action | Enabled while a sequence runs | None | One request | Stops the run between frames. | Sequence only; a still has no Cancel. | A stopped run leaves the sequence as it was, writes nothing into Image History and leaves no temporary sequence behind; nothing is reported. The window stays open. | Sequence Apply | Per run |

## How the algorithm works

### Conceptual pipeline

1. On **OK**, read both limits and the reference view's current image on the
   GUI thread. The worker reads only those.
2. Read the target and the reference as working copies whose samples are
   fractions of full scale: an unsigned integer as a fraction of its largest
   value, a signed integer counted from its smallest, held in 32-bit floating
   point, or 64-bit for 64-bit floating point and the two 32-bit integer
   representations.
3. Refuse a reference with another number of channels, or an image of more than
   four channels.
4. Choose the samples measured. A sample counts when the image's own validity
   rule admits it, it is finite, and it is not exactly zero.
   - **Same width and height:** a position counts in a channel only when both
     images keep it there, and only the reference's sample is held to the
     inclusive range `[Reject low, Reject high]`.
   - **Different sizes:** each image's samples are held to the range on their
     own.
5. Measure each channel's median and median absolute deviation over the kept
   samples, and derive the scale and offset (below). Refuse the run when the
   limits keep no sample in some channel.
6. Write `scale * sample + offset` into every target sample the image's validity
   admits, whatever the limits measured. No clamp is applied; the working copy
   is brought back to the image's own representation to the nearest value,
   saturating an integer at the ends of its range.
7. For a still image the result is an edit of the view: Image History records
   the step before the view shows it. For a sequence, see Inputs, outputs, and
   metadata.

### Model and equations

For channel `c`, let `T` be the target's kept samples and `R` the reference's,
over the shared positions when the images have one size. With `med` the median
and `MAD(X) = med(|X - med(X)|)`:

\[
a_c=\frac{\mathrm{MAD}(R_c)}{\mathrm{MAD}(T_c)},\qquad
b_c=\mathrm{med}(R_c)-a_c\,\mathrm{med}(T_c),
\]

and every admitted target sample becomes

\[
O_c=a_cI_c+b_c .
\]

When either median absolute deviation is zero - more than half of the kept
samples share one value - the scale is left at `1` and only the medians are
matched.

On data that already lie on one straight line the scaling match and a
least-squares line agree exactly. On real data a bright star or a gradient moves
a least-squares line but barely moves the medians and median absolute
deviations. The match reproduces only a level and a spread per channel, not a
histogram's shape, gradients or spatial content.

### Boundary and failure behavior

- **Different channel counts.** The reference list offers none, so this is
  normally seen only over a sequence whose frames differ from the view's image;
  the run
  reports `Reference and target images must have the same number of channels`.
- **Limits that keep nothing.** When no sample of some channel survives -
  **Reject low** above **Reject high**, a range the reference never enters, or
  an image that is zero there - the run is refused, changing nothing. For two
  images of one size the reason is `A channel of the image and its reference
  shares no position whose reference sample lies between the limits <low> and
  <high> and where neither sample is zero; widen the limits or choose another
  reference`; for two sizes it is `A channel of the image or of its reference
  holds no sample that lies between the limits <low> and <high> and is not zero;
  widen the limits or choose another reference`. The message does not say which
  channel.
- **A reference closed before OK.** If the chosen view is no longer there when
  **OK** is pressed, a message titled **Linear Match** says `The selected
  reference image is no longer available.` and nothing runs.
- **Any other failure** is reported under **Error running linear match** with
  the text `An error occurred when executing the linear match process:` followed
  by the reason, written to the log, and leaves the image and its history as
  they were; the window stays open.
- **Image History refuses.** If Image History cannot keep the image the match
  replaces, the history says so once, in a message titled **Image History**,
  and nothing changes.
- **Busy sequence.** If another run holds a sequence view, a busy warning titled
  **Linear Match** is shown and nothing starts.
- **Out-of-range output.** A floating-point result can hold values below zero
  or above one. An integer result saturates at the ends of its range.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| One size + registration | Registered, cropped images of comparable content; defaults `0.00`–`0.92` | Levels still differ because clipped or invalid areas dominate | Unrelated pixel pairs give an implausible scale | Register and crop first, choose the reference, then adjust each limit. |
| Reject low + dark artefacts | Start at `0.00`; zeros are already left out | Low, nonzero border artefacts pull the median down | Too few dark samples remain; the run is refused if none do | Raise in small steps only past the artefacts. |
| Reject high + stars and clipping | Start at `0.92`; lower only when bright sources dominate | Stars and saturated cores widen the spread | Too little dynamic range remains; the spread is no longer representative | Inspect the reference's histogram and lower in small steps. |
| Different sizes | Comparable linear images with broad distributions inside the limits | Medians align but the shape of the histograms still differs | One image keeps a nearly flat set of samples, and the scale falls back to `1` | Remove gradients and crop defects first, then tune one limit at a time. |
| RGB channels + colour | Same filter and colour state on target and reference | Per-channel levels differ | Independent channel matches import the reference's colour cast | Colour-calibrate consistently first; use Linear Match to normalize sessions, not as a colour standard. |

## Inputs, outputs, and metadata

- Target and reference may have one to four channels and must have the same
  number. Width, height and representation may differ. The reference is never
  modified.
- A still keeps its dimensions, channel layout and representation (any of the
  eight). Its description, validity rule, acquisition record, plate solution,
  header cards and meaning are kept; the image's lineage gains an entry
  `an::applyGlobalNormalization` whose detail reads `linear match:` followed by
  each channel's scale and offset (`red`, `green`, `blue` for a colour image,
  `channel 1` and so on otherwise). No processing-state card is added.
- The history step is named **Linear Match** and its description reads
  `Target image '<target>', Reference image '<reference>', Reject Low: <low>,
  Reject High: <high>`, for example `Target image 'target', Reference image
  'reference', Reject Low: 0, Reject High: 0.92`; over a sequence it begins
  `Target sequence`.
- On a sequence, Linear Match takes the view for the run, reads every frame -
  demosaiced when the view demosaics them, by the method the sequence states from
  the one RAW
  [Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
  setting - matches each to the same reference image, writes it into a new
  temporary sequence of the same container, publishes the sequence whole and
  shows it in the view as one Image History step. Each frame is measured against
  the reference on its own. A run that fails or is cancelled leaves the
  sequence as it was and nothing in the temporary directory.
- A view mask is not applied, and no mask, report or extra view is produced.

## Performance

Measurement and application are proportional to the samples of both images.
The run holds floating-point working copies of the target and the reference,
one bit per sample per channel for the samples kept, and the result; a still
also keeps the replaced image for Image History. A sequence measures the
reference again for every frame and needs the temporary output sequence on disk.
No timing is claimed on this page.

## Worked example

- Evidence mode: Source-and-test-derived (`ColorToolsUnitsTest.ALinearMatchMeasuresNoZero`);
  not run on approved astronomical data.
- Input condition and settings: a `32×2` floating-point RGB target whose first
  row holds `1/64` to `32/64` and whose second row is zero; a reference of the
  same size whose first row is `2 × target + 0.05` and whose second row is
  `0.5`; **Reject low** `0.00`, **Reject high** `1.00`.
- Expected checkpoint: the target's zero row is left out of the measurement, and
  so are the positions where the reference rises above `1.00`; the samples kept
  still lie on one line, so every channel's scale is `2` and its offset `0.05`.
  Every target sample is then written, so the zero row becomes `0.05`.
- Inspect at 100%/statistics/history: compare a channel's median before and
  after, the lineage's scale and offset, and the **Linear Match** step's
  description.
- Limitations: a synthetic, exactly linear relation proves the measurement and
  the apply, not robustness to gradients, registration errors or different
  fields, nor visual quality or timing.

## Warnings and limitations

- **OK** replaces the target; undo through Image History.
- One-size and different-size images are measured differently: paired by
  position, or image by image.
- One-size images must be registered and correspond pixel for pixel.
- The limits are inclusive and zeros are always left out. Limits that keep
  nothing are refused, but a nearly flat selection falls back to a scale of `1`
  without a message.
- No clamp: a floating-point result can leave the unit range, and an integer
  result saturates.
- View masks are ignored.
- The process does not display the scale and offset; they are recorded only in
  the image's lineage.
- Linear Match adjusts the level and spread of each channel. It does not correct
  colour response, nonlinear contrast, gradients, geometry or full histograms.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **OK** is disabled | **Reference Image** is `<--NO IMAGE-->`: none chosen, the chosen view was closed, or the target was changed | Choose a reference | `ColorToolsCase.ALinearMatchOffersTheViewsOfTheTargetsChannelCount` |
| The desired reference is not listed | It has another number of channels, or it is the target | Choose another target or reference; size and representation need not match | `ColorToolsCase.ALinearMatchOffersTheViewsOfTheTargetsChannelCount` |
| `A channel of the image ... between the limits <low> and <high> ...` | The limits keep no sample of some channel | Widen the limits, make **Reject low** no higher than **Reject high**, or choose another reference | `ColorToolsCase.ALinearMatchWhoseLimitsKeepNothingIsRefused` |
| `The selected reference image is no longer available.` | The reference view was closed before **OK** was pressed | Choose another reference and run again | `LinearMatchProcess::executeLinearMatch` |
| **Error running linear match** | The engine refused the run, for example a channel-count mismatch over a sequence or an allocation | Read the message and the log, then run again | `ColorToolsWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| The result differs after resizing or cropping one image | One size pairs positions; different sizes measure each image on its own | Choose the intended mode deliberately | `ColorToolsCase.ALinearMatchToAnotherSizeMeasuresEachImageOnItsOwn` |
| Values below zero or above full scale | The match is not clamped | Use a better-matched reference or limits; an integer result saturates | `matchLinearly` |
| The mask had no effect | Linear Match does not apply a view mask | Apply globally, or use a process that supports masks | `ColorToolsWindowTest.TheViewsMaskWeighsTheResult` |
| The sequence does not change after Cancel | A stopped run publishes nothing | Run again | `ColorToolsWindowTest.AStoppedRunPublishesNothing` |
| Busy warning titled **Linear Match** | Another run holds the sequence view | Wait for it to finish, then run again | `runInPlace` busy path |

## Related workflow steps

Use [Image Registration](../registration-alignment/image-registration.md) and
[Background Extraction](../background-extraction/background-extraction.md)
before a one-size, paired match when geometry or gradients differ. For
multi-session data, match comparable linear masters before
[Image Stacking](../stacking-integration/image-stacking.md) or
[Channel Composition](../color-composition/channel-composition.md); do not use
Linear Match to repair fundamentally different filters or exposures, or encoded
hue or chroma components.

[Color Calibration](color-calibration.md),
[Photometric Color Calibration](photometric-cc.md), and
[Spectrophotometric Color Calibration](spectrophotometric-cc.md) solve colour
balance rather than reference level and spread. [LRGB Combination](../color-composition/lrgb-combination.md)
and [NBRGB Combination](../color-composition/nbrgb-combination.md) have their own
combination-specific normalization; avoid normalizing twice without checking
their behavior.

## References and provenance

- The AfterNight Linear Match process, the colour tools' shared support, the
  engine's global normalization and the focused tests
  are the implementation sources.
- No external library is used by Linear Match.
