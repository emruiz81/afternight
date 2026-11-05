# Color Calibration

## Purpose and expected result

Color Calibration balances the three channels of an RGB image, or of every
frame of an RGB sequence, in one of two ways:

- **Automatic** (the default): it measures a white reference and a background
  reference, and gives each channel the gain that makes its white stand as far
  above its background as the strongest channel's does.
- **Manual White Balance**: it uses the three values you enter as the gains.

In both modes each channel is scaled about its own background level - the
median of the background reference - so the background stays where it was and
everything above it is stretched or compressed. It is intended for a linear RGB
master whose gradients have already been corrected.

A successful Apply replaces the target, keeps its dimensions, channel layout,
sample representation and metadata, and adds one `ColorCalibration` Image
History step. Two optional grey mask views show which reference pixels lie
within the limits; they are for inspection and never limit the calibration.

The automatic mode is a relative channel balance, not a catalog-based colour
solution. It does not identify stars, filters, spectra or a physical white
point, and it does not bring the three background levels to a common value.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Calibration > Color Calibration** |
| Enabled when | A three-channel RGB still or sequence is active and no other process holds it; Color Calibration needs the view to itself. The target selector lists the open RGB views. |
| Inputs | One RGB target and a background reference; in automatic mode also a white reference. Each reference is an open RGB view, or the target itself (`<--TARGET IMAGE-->`), whole or a rectangle of it. Any of the eight sample representations. |
| Outputs | The target replaced as one Image History step; a sequence is written whole into a new sequence the view then shows. Optionally the views `<target> - WHITE MASK` and `<target> - BACKGROUND MASK`, opened after the result is shown. No file is written. |
| Recommended image state | Gradient-corrected, stacked, cropped, linear RGB. The process does not check linearity. |
| Preview support | None. **From Selection** draws the ROI on the chosen reference view; it is not a result preview. |
| Sequence support | Yes: every frame, whatever the player has selected, with one measurement. A reference that is the target is the frame the view shows when Apply is pressed. Progress and Cancel are shown. |
| Mask support | An enabled view mask weighs the result against the source; inversion is honoured. The two reference masks are outputs only and never limit the calibration. |
| External dependencies | None |

## Before you start

1. Finish calibration, registration, integration, gradient removal and border
   cropping. A gradient or a black border biases both references.
2. Use linear RGB data. Nonlinear data is accepted, but its means and medians
   no longer have the intended radiometric meaning.
3. For automatic mode, choose a white reference whose channels stand clearly
   above the background - an unsaturated white or star-rich region - and a
   background reference that is clean sky, free of stars, nebulosity, galaxy
   halos and borders. With the defaults both references are the whole target,
   which on a mostly dark image makes each gap only the difference between the
   channel's mean and its median; see **Warnings and limitations**.
4. Duplicate the target or rely on Image History. Apply replaces the open
   target and there is no preview.
5. An enabled view mask controls only where the result replaces the source. It
   does not change what the references measure.
6. For a sequence, show the frame you want measured before pressing Apply when
   a reference is the target; the same gains are applied to every frame.

## Procedure

1. Open **Process > Color Calibration > Color Calibration** and check **Target
   Image**.
2. For automatic calibration, leave **Manual White Balance** off and choose the
   white-reference **Reference image** and the background-reference
   **Reference image**. Both start at `<--TARGET IMAGE-->`.
3. To measure a rectangle rather than the whole reference, turn on that
   reference's **Region of interest** and enter **Left**, **Top**, **Width** and
   **Height**, or click **From Selection** and draw or move the rectangle on the
   reference view.
4. Keep the white limits `0.0` to `0.9`, or narrow them to leave out dark or
   saturated samples. A sample equal to a limit is kept.
5. The background **Lower Limit** and **Upper Limit** (`0.0` to `0.1`) decide
   only which pixels the background mask shows; the background level is the
   median of every sample of the background reference's rectangle.
6. Optionally select **Output White Reference Mask** and **Output Background
   Reference Mask** to see the selected pixels after Apply.
7. Alternatively, enable **Manual White Balance** and set **Red**, **Green** and
   **Blue** between `0.000001` and `1.0`. This disables the white reference,
   its limits, its ROI and the white mask; the background reference is still
   required, because each channel is scaled about its median.
8. Click **Apply**. The dialog and the references are read at that moment;
   changing a control afterwards does not reach the run. A still is calibrated
   without a progress window; a sequence shows progress and **Cancel**.
9. Success closes the window, adds a `ColorCalibration` history step and opens
   any masks asked for beside the target. Compare channel statistics with the
   same display stretch before continuing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| White Reference — Reference image | Open-image selector | `<--TARGET IMAGE-->` | None | `<--TARGET IMAGE-->` and the other open RGB views, the target being offered once, as `<--TARGET IMAGE-->`; size and representation may differ from the target | Chooses the image whose per-channel means within the white limits are the white levels | Disabled by **Manual White Balance**. A view closed while selected returns the selector to `<--TARGET IMAGE-->`; a view that holds no image at Apply is refused | A reference whose channels barely stand above the background gives large, unstable gains | Automatic Apply; for a sequence the target is the frame shown at Apply | Window lifetime; Reset and reopening select the target |
| White Reference — Lower Limit | Fraction of full scale | `0.0` | None | `0.0`–`1.0`; step `0.05`; six decimals | Raising it leaves darker white-reference samples out of the mean; a sample equal to the limit is kept | Disabled by **Manual White Balance**. Not constrained relative to Upper Limit | Above the upper limit, or above every sample of a channel, the run is refused | Automatic white mean and white mask | Window lifetime; Reset restores `0.0` |
| White Reference — Upper Limit | Fraction of full scale | `0.9` | None | `0.0`–`1.0`; step `0.05`; six decimals | Lowering it leaves brighter or saturated samples out of the mean; a sample equal to the limit is kept | Disabled by **Manual White Balance**. Not constrained relative to Lower Limit | Too low can remove the white evidence; `1.0` keeps full-scale samples | Automatic white mean and white mask | Window lifetime; Reset restores `0.9` |
| White Reference — Region of interest | Checkable group | Off | None | Off/On | On measures only the rectangle; off measures the whole reference. Turning it on fills the fields with the whole reference | Disabled by **Manual White Balance** | A small clean rectangle avoids contamination but measures fewer samples | Automatic white mean and white mask | Window lifetime; turning it off or Reset clears the fields to `0` |
| White ROI — Left | Integer pixels | `0` | None | `0` to reference width minus 1; Width's maximum becomes width minus Left | Moves the rectangle right | Enabled with the white ROI on and manual mode off | Changes which samples set the gains | White reference rectangle | Window lifetime |
| White ROI — Top | Integer pixels | `0` | None | `0` to reference height minus 1; Height's maximum becomes height minus Top | Moves the rectangle down | Enabled with the white ROI on and manual mode off | Changes which samples set the gains | White reference rectangle | Window lifetime |
| White ROI — From Selection | Checkable button | Off | None | Draw or move one rectangle on the white-reference view | Clears the fields, then mirrors the drawn rectangle into them; turning it off keeps the rectangle, or restores the whole reference when Width or Height is `0` | Enabled with the white ROI on and manual mode off. Starting the background selection ends this one; switching to manual mode ends it | Placement only, not a preview | White reference view | Window lifetime |
| White ROI — Width | Integer pixels | Reference width when the ROI is turned on | None | `0` to width minus Left; Apply refuses `0` | Widens or narrows the rectangle | Enabled with the white ROI on and manual mode off | Fewer samples make the mean noisier | White reference rectangle | Window lifetime |
| White ROI — Height | Integer pixels | Reference height when the ROI is turned on | None | `0` to height minus Top; Apply refuses `0` | Heightens or shortens the rectangle | Enabled with the white ROI on and manual mode off | Fewer samples make the mean noisier | White reference rectangle | Window lifetime |
| Manual White Balance | Checkable group | Off | None | Off/On | On uses **Red**, **Green** and **Blue** as the gains instead of measuring a white reference | Disables the white selector, limits, ROI, selection and white mask. The background reference stays required: its medians are the levels the gains scale about | Simple and repeatable; the `1.0` maximum cannot boost a channel | Apply; every target pixel and channel | Window lifetime; Reset turns it off |
| Manual White Balance — Red | Gain | `1.0` | None | `0.000001`–`1.0`; step `0.05`; six decimals; the slider's left end is `0.000001` | Lower values compress red towards its background level; `1.0` leaves red unchanged | Used only in manual mode | A very small value makes red almost its background level everywhere | Red channel; view mask weighs the result | Window lifetime; Reset restores `1.0` |
| Manual White Balance — Green | Gain | `1.0` | None | `0.000001`–`1.0`; step `0.05`; six decimals | Lower values compress green towards its background level; `1.0` leaves green unchanged | Used only in manual mode | A very small value makes green almost its background level everywhere | Green channel; view mask weighs the result | Window lifetime; Reset restores `1.0` |
| Manual White Balance — Blue | Gain | `1.0` | None | `0.000001`–`1.0`; step `0.05`; six decimals | Lower values compress blue towards its background level; `1.0` leaves blue unchanged | Used only in manual mode | A very small value makes blue almost its background level everywhere | Blue channel; view mask weighs the result | Window lifetime; Reset restores `1.0` |
| Output White Reference Mask | Checkbox | Off | None | Off/On | Opens `<target> - WHITE MASK`: a grey image of the whole white reference, at the top of its range where every channel of a pixel inside the white rectangle is valid and within the white limits, at the bottom of its range everywhere else | Disabled, and ignored, in manual mode. Opened only once the result is shown and while the target is open | Adds one reference-size image | Output only; still and sequence Apply | Window lifetime; Reset turns it off |
| Background Reference — Reference image | Open-image selector | `<--TARGET IMAGE-->` | None | `<--TARGET IMAGE-->` and the other open RGB views, the target being offered once, as `<--TARGET IMAGE-->`; size and representation may differ from the target | Chooses the image whose per-channel medians are the background levels, in both modes | Always required. A view closed while selected returns the selector to `<--TARGET IMAGE-->`; a view that holds no image at Apply is refused | Stars or nebulosity in it raise the levels | Both modes; for a sequence the target is the frame shown at Apply | Window lifetime; Reset and reopening select the target |
| Background Reference — Lower Limit | Fraction of full scale | `0.0` | None | `0.0`–`1.0`; step `0.05`; six decimals | Raising it removes darker pixels from the background mask; a sample equal to the limit is selected | Not constrained relative to Upper Limit. It does not change the background level | Changes only the mask | Background mask only | Window lifetime; Reset restores `0.0` |
| Background Reference — Upper Limit | Fraction of full scale | `0.1` | None | `0.0`–`1.0`; step `0.05`; six decimals | Lowering it removes brighter pixels from the background mask; a sample equal to the limit is selected | Not constrained relative to Lower Limit. It does not change the background level | Changes only the mask | Background mask only | Window lifetime; Reset restores `0.1` |
| Background Reference — Region of interest | Checkable group | Off | None | Off/On | On takes the medians, and makes the mask, over the rectangle only; off over the whole reference. Turning it on fills the fields with the whole reference | Available in both modes | A clean sky rectangle keeps stars out of the levels | Background levels and mask | Window lifetime; turning it off or Reset clears the fields to `0` |
| Background ROI — Left | Integer pixels | `0` | None | `0` to reference width minus 1; Width's maximum becomes width minus Left | Moves the rectangle right | Enabled with the background ROI on | Changes the background levels and the mask | Background reference rectangle | Window lifetime |
| Background ROI — Top | Integer pixels | `0` | None | `0` to reference height minus 1; Height's maximum becomes height minus Top | Moves the rectangle down | Enabled with the background ROI on | Changes the background levels and the mask | Background reference rectangle | Window lifetime |
| Background ROI — From Selection | Checkable button | Off | None | Draw or move one rectangle on the background-reference view | Clears the fields, then mirrors the drawn rectangle into them; turning it off keeps the rectangle, or restores the whole reference when Width or Height is `0` | Enabled with the background ROI on. Starting the white selection ends this one | Placement only, not a preview | Background reference view | Window lifetime |
| Background ROI — Width | Integer pixels | Reference width when the ROI is turned on | None | `0` to width minus Left; Apply refuses `0` | Widens or narrows the rectangle | Enabled with the background ROI on | A narrow rectangle may not represent the sky | Background reference rectangle | Window lifetime |
| Background ROI — Height | Integer pixels | Reference height when the ROI is turned on | None | `0` to height minus Top; Apply refuses `0` | Heightens or shortens the rectangle | Enabled with the background ROI on | A short rectangle may not represent the sky | Background reference rectangle | Window lifetime |
| Output Background Reference Mask | Checkbox | Off | None | Off/On | Opens `<target> - BACKGROUND MASK`: a grey image of the whole background reference, at the top of its range where every channel of a pixel inside the background rectangle is valid and within the background limits, at the bottom of its range everywhere else | Available in both modes. Opened only once the result is shown and while the target is open | Adds one reference-size image | Output only; still and sequence Apply | Window lifetime; Reset turns it off |
| Target Image | RGB image/sequence selector | Launching view | None | The open RGB views | Chooses the still or sequence to calibrate | Color Calibration holds its target exclusively | A wrong target is replaced | Apply; all three channels; view mask; still/sequence | Target lifecycle |
| Apply | Button | Enabled while idle | None | Needs the background reference, and in automatic mode the white reference, to hold an image, and every ROI that is on to be non-empty and inside its reference | Reads the dialog, the references and the view's mask, then calibrates on a worker | Apply, Close and Reset are disabled while it runs; the other controls stay editable but no longer reach the run | Keeps the source for Image History; a sequence adds a temporary output sequence | Still or sequence | Success closes the window |
| Reset | Button | — | None | One action | Restores both selectors to `<--TARGET IMAGE-->`, the limits, the manual values, both ROI groups and their fields, both mask choices and manual mode to their first-open values, and ends any selection | Disabled while a run is in flight | No pixel change | Dialog only | No saved settings |
| Close | Button or title-bar close | — | None | One action | Closes without applying | The button is disabled while a run is in flight; a title-bar close then hides the window, which closes once the run has landed; it does not stop the run | Nothing waits on the GUI thread | Dialog only | No saved settings |

## How the algorithm works

### Conceptual pipeline

1. On **Apply**, on the GUI thread, read the dialog, check each reference
   (a view holding an image, a rectangle inside it), take each reference's
   image as it is shown at that moment, and take the view's mask if one is
   enabled. A reference that is the target is the image the run changes for a
   still, and the frame the view shows for a sequence. The worker reads none of
   the window.
2. On the worker, read every sample as a fraction of full scale - an unsigned
   integer of its largest value, a signed one counted from its smallest value -
   in a Float32 working copy, Float64 for Float64 and the two 32-bit integer
   representations.
3. Measure each background channel's exact median over every finite sample of
   the background rectangle that the image's validity admits. In automatic mode,
   measure each white channel's mean over the finite, valid samples of the white
   rectangle within the white limits.
4. Derive one gain and one offset per channel (below). Write
   `sample × gain + offset` for every valid target sample, hold it to `[0,1]`,
   and bring it back to the target's representation to the nearest value.
5. Weigh the result by the view's mask, if one was taken, and record the change.
   For a still the result is an edit of the view that Image History records
   before the view shows it. For a sequence, see **Inputs, outputs, and
   metadata**.
6. Make the asked-for masks with the measurements, once per run, and open them
   beside the target once its result is shown.

### Model and equations

For channel `c`, let `B_c` be the median of every finite, valid
background-reference sample in the background rectangle. In automatic mode let
`W_c` be the mean of the white-reference samples in the white rectangle with

\[
L_W \le I_c \le U_W,
\]

and define each channel's gap

\[
D_c = |W_c-B_c|.
\]

The channel `r` with the largest gap is the strongest, and

\[
K_c = \frac{D_r}{D_c},
\]

so the strongest channel keeps a gain of `1` - as does a channel whose gap
equals it - and weaker channels are boosted to match its gap. In manual mode
`K_c` is the **Red**, **Green** or **Blue** value. In both modes the offset is

\[
O_c = B_c\,(1-K_c),
\]

and the result is

\[
P_c=\operatorname{clip}_{[0,1]}\left(I_c K_c + O_c\right)
   =\operatorname{clip}_{[0,1]}\left((I_c-B_c)K_c+B_c\right).
\]

Each channel's background level `B_c` stays where it was; the tool does not
neutralize `B_R`, `B_G` and `B_B`. Since the manual values are at most `1.0`,
manual mode can only move a channel towards its background level.

With an enabled view mask `M` (a fraction of full scale) the published sample is

\[
\text{out}=(1-M)\,I+M\,P,
\]

and an inverted mask swaps the two weights.

A mask pixel is selected when, for every one of the three channels, the
reference sample is valid, finite and within the closed limits, and the pixel
lies inside the rectangle. A selected pixel is the top of the representation's
range (`1.0` for a floating image, the largest code for an integer one); every
other pixel is the bottom (`0.0` for a floating image, the smallest code - zero
for an unsigned image, the most negative value for a signed one).

### Boundary and failure behavior

Checked on the GUI thread, under the title **Color Calibration**, before
anything runs:

- `The selected white reference image is no longer available.` (automatic mode)
  or `The selected background reference image is no longer available.` - the
  chosen view holds no image.
- `The white reference ROI must be fully inside the reference image and have a
  non-zero size.` or the same for the background - an ROI that is on is empty
  or overhangs its reference.

Refused by the run, under **Error running color calibration** with the text
`An error occurred when executing the color calibration process:` followed by
the reason, also written to the log; the image, its history and the masks are
left as they were and the window stays open:

- `No red sample of the white reference lies between the limits 0 and 0.9;
  widen the limits or choose another reference` (with the channel and the
  limits that applied) - a white channel keeps no sample within its limits,
  including limits that cross.
- `No green sample of the background reference means something; choose another
  reference` - a background channel holds no finite, valid sample.
- `The blue white level of the white reference sits on its background level,
  so no gain balances it; choose another reference or other limits` - a
  channel's white mean equals its background median exactly.
- An engine refusal, such as a result the memory budget cannot hold.

Other boundaries:

- Samples the image's validity rule rejects are not measured and are copied
  unchanged into the result. Non-finite samples are never measured.
- If another run already holds a sequence view, Color Calibration shows the
  view's busy warning titled **Color Calibration** and starts nothing.
- If Image History cannot keep the image the calibration replaces, the history
  says so once, Color Calibration says nothing more, no mask is opened, and the
  image is left as it was.
- If the image changes while a still run works, the result is discarded without
  a message and the window stays open to run again.
- A still run has no Cancel. A sequence's **Cancel** stops between frames; a
  stopped run changes nothing, records nothing, opens no mask, says nothing,
  and leaves the window open.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| White ROI + white limits | A clean, unsaturated region that stands well above the sky; `0.0`–`0.9`; ask for the white mask | Residual cast because the white mean is dominated by background | Strong cast, or one channel clipped, because the region holds a coloured object, the gaps are tiny, or the limits keep only a narrow tail | Choose the region, inspect the white mask, then tighten the limits one at a time |
| Background ROI + gains | A clean sky rectangle; the background limits only shape the mask | Levels biased by stars or nebulosity in the rectangle | The scaling pivots about an unrepresentative patch; the background cast stays because this tool keeps each channel's level | Fix gradients, choose the rectangle, then run Background Neutralization if channel offsets remain |
| Manual Red/Green/Blue | Start at `1.0`; reduce only the channel that is too strong | A cast remains because the correction needs a boost | A reduced channel loses contrast towards its background level | Keep one channel at `1.0`; reduce the others gradually and compare statistics |
| View mask + calibration | Leave the mask off while choosing references; enable it only for an intended local blend | Mask too dark, so little calibration shows | Hard mask edges create local colour transitions | Settle the global result unmasked, then enable, soften or invert the mask |
| Color Calibration + Background Neutralization | Gradient removal, then Background Neutralization, then Color Calibration; recheck the background afterwards | Residual cast or a mismatched white reference | Repeated offset and gain passes clip shadows or highlights | Correct gradients, neutralize offsets, calibrate gains; avoid repeated passes |

## Inputs, outputs, and metadata

- The target is an RGB still or sequence in any of the eight sample
  representations. References are open RGB views or the target; they may differ
  from the target in size and representation, and they are never modified.
- Apply keeps the target's dimensions, channel layout, representation,
  validity, acquisition record, plate solution, header cards and meaning, and
  adds a lineage entry `an::applyColorCalibration` whose detail reads
  `color calibration: gains <r>, <g>, <b>; offsets <r>, <g>, <b>` to six
  decimals. The image is not marked as photometrically colour calibrated.
- On a still image the result is one Image History step named
  `ColorCalibration`. Its description is, in automatic mode,
  `White reference image '<name>' (ROI X:<x>, Y:<y>, W:<w>, H:<h>) (MIN:<lower>, MAX:<upper>) - Background reference image '<name>' (ROI X:<x>, Y:<y>, W:<w>, H:<h>) (MIN:<lower>, MAX:<upper>)`,
  and in manual mode
  `Using manual white balance values R:<r>, G:<g>, B:<b> - Background reference image '<name>' (ROI X:<x>, Y:<y>, W:<w>, H:<h>) (MIN:<lower>, MAX:<upper>)`,
  every number to six decimals. A reference that is the target is named by the
  target's view name; an ROI that is off reads as the whole reference.
- On a sequence, Color Calibration takes the view for the run, measures the
  references once - a reference that is the target is the frame the view showed
  when Apply was pressed - reads every frame of the sequence, whatever the
  player has selected, demosaiced when the view demosaics them by the method the
  sequence states from the one RAW
  [Demosaic Algorithm](../../interface/settings-dialog.md#raw-images-settings)
  setting, writes each calibrated frame into a new temporary sequence of the
  same container, publishes it whole and shows it in the view as one
  `ColorCalibration` step with the same description. The progress window is
  titled **Color Calibration** and reads
  `Executing color calibration to sequence <name>`. A run that fails or is
  cancelled leaves the sequence as it was.
- **Output White Reference Mask** and **Output Background Reference Mask** open
  views named `<target> - WHITE MASK` and `<target> - BACKGROUND MASK` beside
  the target, beginning with the history steps
  `ColorCalibration: White reference mask image created from '<target>'` and
  `ColorCalibration: Background reference mask image created from '<target>'`.
  Each has its reference's full size and representation, every sample written
  as described above, and keeps the reference's acquisition record and plate
  solution with no header cards. The white mask exists only in automatic mode.
  The masks open only once the calibrated result is shown and only while the
  target is open, even if the window has closed by then.

## Performance

Measuring a reference makes a floating working copy of it and one bit per
sample of the samples kept. Applying makes a floating working copy of the
target and the result in its representation; a view mask adds a blend copy, and
Image History keeps the source beside the result. Each mask adds one
reference-size image. A sequence is measured once and then costs one frame's
working copies at a time plus the temporary output sequence. Because there is
no preview, settle the references on a duplicate or one representative still
before processing a long sequence.

## Worked example

- Evidence mode: Source-and-test-derived; not run on approved astronomical data.
- Input condition and settings: an RGB image whose background rectangle reads
  `0.125`, `0.25`, `0.125` and whose white rectangle reads `0.625`, `0.75`,
  `0.375` in red, green and blue; automatic mode with those two rectangles,
  white limits `0.0`–`1.0`.
- Expected checkpoint: the gaps are `0.5`, `0.5` and `0.25`, so the gains are
  `1`, `1` and `2` and the offsets `0`, `0` and `-0.125`; a pixel of `0.3`,
  `0.4`, `0.2` becomes `0.3`, `0.4`, `0.275`. In manual mode with **Red**
  `0.9`, **Green** `1.0`, **Blue** `0.8` and the same background, the offsets
  are `0.0125`, `0` and `0.025`.
- Inspect at 100%/statistics/history: compare each channel's background median
  before and after (unchanged), the `ColorCalibration` history step, and the
  masks' coverage.
- Limitations: focused synthetic tests prove the gain rule, the offsets and the
  masks; this is not evidence of real-data colour quality, timing or memory.

## Warnings and limitations

- Apply replaces the open target. There is no preview and no Save As.
- Use linear RGB for meaningful statistics; nonlinear input is neither detected
  nor refused.
- The defaults measure the whole target as both references. On a mostly dark
  image each gap is then only a channel's mean minus its median, so the gains
  are large and sensitive; choose real white and sky references.
- Automatic mode matches white-above-background gaps; it does not solve catalog
  colour, camera or filter response, extinction or a physical white point. Use
  [Photometric Color Calibration](photometric-cc.md) or
  [Spectrophotometric Color Calibration](spectrophotometric-cc.md) when their
  prerequisites are available.
- The background limits shape only the background mask, not the background
  level.
- Manual values cannot exceed `1.0`, and manual mode still needs a clean
  background reference.
- The reference masks are outputs, not process masks; only the enabled view
  mask limits where the result replaces the source.
- A still run cannot be stopped; closing the window hides it until the run
  lands.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **The selected white reference image is no longer available.** / **The selected background reference image is no longer available.** | The chosen view holds no image at Apply | Select an open RGB view or `<--TARGET IMAGE-->` | `ColorCalibrationProcess::executeColorCalibration()` |
| **The white reference ROI must be fully inside the reference image and have a non-zero size.** (or background) | An ROI that is on is empty or overhangs its reference | Turn the ROI off, use **From Selection**, or correct Left/Top/Width/Height | `rectangleInside`, `ColorToolsUnitsTest.ARectangleIsTheWholeImageOrARegionInsideIt` |
| **Error running color calibration**: `No <channel> sample of the white reference lies between the limits ...` | No white sample of that channel lies within the limits, or the limits cross | Widen the limits or choose another reference | `ColorToolsUnitsTest.AChannelThatKeepsNothingIsRefusedByName` |
| **Error running color calibration**: `... sits on its background level, so no gain balances it ...` | The channel's white mean equals its background median, as on a flat reference | Choose a white reference that stands above the background, or other limits | `ColorToolsCase.AWhiteThatSitsOnItsBackgroundIsRefused` |
| **Error running color calibration** with another reason | The engine refused the run, for example for memory | Read the message and the log, free memory, and retry | `ColorToolsWindowTest.ARunTheEngineRefusesSaysWhyAndChangesNothing` |
| Result has a strong cast or one channel is boosted many times | The white reference barely stands above the background, so the gaps are tiny | Choose a distinct white region and a clean sky rectangle; inspect the white mask | `ColorToolsRecordTest.EveryRecordOfTheProcessIsReproducedThroughItsWindow` |
| Changing the background limits does not change the result | The background level is the median of every sample of the rectangle; the limits shape only the mask | Move or resize the background rectangle; use Background Neutralization for a limited-range offset | `ColorToolsUnitsTest.TheBackgroundLevelIsTheMedianOfEverySample` |
| Mask is empty or sparse | A pixel is selected only when all three channels lie within the limits and inside the rectangle | Widen the limits and check the rectangle | `ColorToolsCase.AnAutomaticCalibrationWritesEverySampleOfBothMasks` |
| No white mask opens | Manual mode measures no white reference | Expected; use automatic mode to see the white mask | `ColorToolsCase.AManualCalibrationWritesEverySampleOfTheBackgroundMaskAndNoWhiteMask` |
| An **Image History** error, nothing changes and no mask opens | Image History could not keep the image the calibration replaces | Free memory and run again | `ColorToolsCase.MasksOfACalibrationThatIsNotShownAreNotOpened` |
| Busy warning titled **Color Calibration** | Another run holds the sequence view | Wait for it to finish, then run again | `runInPlace` busy path |
| Nothing changes and the window is still open | The image changed while a still run worked, or a sequence run was cancelled | Run again | `ColorToolsWindowTest.AStoppedRunPublishesNothing` |

## Related workflow steps

Perform [Background Extraction](../background-extraction/background-extraction.md)
or [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md)
first so large-scale illumination does not bias the references. Use
[Background Neutralization](background-neutralization.md) when the RGB
background channels need offset alignment; then use this page for white or
manual balance. Catalog-backed alternatives are [Photometric Color
Calibration](photometric-cc.md) and [Spectrophotometric Color
Calibration](spectrophotometric-cc.md). Continue to [Histogram
Transformation](../stretching/histogram-transformation.md) only after the linear
colour workflow is complete.

## References and provenance

- AfterNight's Color Calibration window, the colour tools' shared support, the
  shared in-place run, the engine's colour calibration apply, and the
  focused tests are authoritative for this page.
- This is a project-specific mean/median channel-balancing method. No paper,
  standards document or external algorithm is used for the gain model.
