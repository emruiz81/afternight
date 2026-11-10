---
kind: process
stage: color
menu: Process > Color Spaces > NBRGB Combination
summary: >-
  Adds the line signal of up to three narrowband masters to the red, green and blue channels of a broadband RGB image,
  after removing their continuum.
data_in: linear
data_out: nonlinear
---
# NBRGB Combination

## Purpose and expected result

NBRGB Combination injects up to three registered monochrome narrowband masters
into the corresponding red, green, and blue planes of a broadband RGB still. It
uses the supplied effective filter bandwidths to estimate a continuum-corrected
line plane, boosts that structure around its median, matches it to the boosted
broadband channel when enough samples exist, and keeps the stronger value at
each pixel.

The expected result is a new RGB still named `<RGB target> - NBRGB`. The source
RGB and narrowband views remain unchanged. This is not channel assignment and
it is not a spectral response model: the process does not inspect central
wavelengths, transmission curves, exposure normalization, WCS, registration,
or point-spread-function differences.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > NBRGB Combination** |
| Enabled when | A three-channel RGB still is active, the same process is not already attached, and no attached process holds exclusive access |
| Inputs | One RGB still plus at least one open, same-size, grayscale still selected for Red, Green, or Blue; narrowband pixel types may differ from the RGB type |
| Outputs | A new same-size RGB still named `<target> - NBRGB` in the target's representation; **Take snapshot** creates another still; no file is written directly |
| Recommended image state | Registered, consistently calibrated linear masters with meaningful effective bandwidths and compatible intensity scales |
| Preview support | Always-on asynchronous preview with histogram and display autostretch; starts with a centered Preview area of up to `512 x 512` pixels |
| Sequence support | Not supported: the registered action needs a still image, and a sequence run is refused by name |
| Mask support | Not applied: the result is a new image, not an edit of the target |
| External dependencies | None; the bundled engine performs the calculation without a tool, model, catalog, or network service |

## Before you start

- Register and crop every narrowband master to the RGB target. The selectors
  check only grayscale format and exact width/height; they do not compare stars,
  WCS, pixel scale, orientation, crop origin, or PSF.
- Use linear, background-prepared data with consistent normalization. Continuum
  subtraction has a physical interpretation only when the RGB and narrowband
  samples retain a meaningful intensity relationship.
- Enter effective passband widths in nanometres. **RGB bandwidth** is one shared
  approximation for all three broadband planes; each selected narrowband width
  must be smaller than it. The process does not derive widths from FITS headers.
- Match gradients, exposure scaling, and other acquisition differences upstream.
  The internal linear fit handles a final per-channel affine relationship; it is
  not a substitute for registration, background correction, or photometric
  response calibration.
- Leave **Non Linear rescale** off for a physically interpretable linear start.
  Despite its label, it performs repeated full-plane min/max normalization; it
  does not linearize nonlinear data.
- Use a duplicate or Image History only if you need an extra safety copy of the
  inputs. Apply already creates a new image and does not replace the RGB target.

## Procedure

1. Activate the registered linear RGB still and open **Process > Color Spaces >
   NBRGB Combination**. Confirm the target under **RGB**.
2. Select at least one same-size mono master under **Red**, **Green**, or
   **Blue**. Leave unused channel selectors at **No image**; those RGB planes
   pass through unchanged.
3. Enter the broadband effective width in **RGB bandwidth** and each selected
   narrowband filter width in its **Bandwidth** field. Confirm every narrowband
   value is lower than the RGB value.
4. Start with each selected **Scale** at `1.20`, **Reject low** at `0.000`,
   **Reject high** at `0.920`, and **Non Linear rescale** off.
5. Keep **Preview** on **NBRGB** to judge the combination. Use **RGB**, **NBr**,
   **NBg**, and **NBb** to inspect the registered inputs. An unselected
   narrowband Preview mode falls back to the RGB source.
6. Move or resize the checked **Preview area** over background, bright emission,
   and star-rich structure. Outside the outlined area the preview remains the
   original RGB; uncheck the control to calculate a full-image preview.
7. Adjust one channel's **Scale** at a time. If the fit follows saturated or
   clipped structure poorly, narrow the Reject interval while retaining enough
   ordinary pixels for a fit.
8. Compare processed/original views with **Switch image**, keep one display
   stretch, then select **Apply**. Wait for `<target> - NBRGB` to appear; the
   run shows no progress dialog and has no Cancel button, and closing the
   window stops it.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Histogram plot | Graph, normalized intensity/count | Current preview; RGB channels overlaid | None | Binning comes from **Plot resolution** | Shows the mask-aware preview distribution and approximate endpoint-bin clipping | Always present | Recalculation adds work; endpoint counts depend on binning | Display only; preview RGB | Recomputed after preview or resolution changes |
| Left-drag in histogram | Graph gesture | No readout | None | Hold left button and move in graph | Shows normalized coordinate readout | Histogram always present | Diagnostic only | Display only | Ends on button release |
| Middle-drag in histogram | Graph gesture | Fitted/unpanned | None | Hold middle button and drag | Pans the graph | Useful after zooming | Display cost only | Display only | Until reset/window close |
| Ctrl+mouse wheel in histogram | Graph gesture | Fitted size | None | Up `1.20x`; down `0.8x`; wheel without Ctrl scrolls | Zooms around the cursor | Interactive graph enabled | Larger graph needs more rendering area | Display only | Window lifetime |
| Zoom in | Toolbar action | Fitted size | None | Each selection multiplies size by `1.30` | Enlarges plot | Available while idle | Plot rendering only | Display only | Window lifetime |
| Zoom out | Toolbar action | Fitted size | None | Each selection multiplies size by `0.70` | Shrinks plot | Available while idle | Plot rendering only | Display only | Window lifetime |
| Zoom reset | Toolbar action | — | None | One action | Fits graph and resets scrolling | Available while idle | Plot rendering only | Display only | Immediate |
| Show grid | Toggle | Checked | None | Checked/unchecked | Shows or hides graph grid | Available while idle | Display-only aid | Display only | Window lifetime; process Reset does not change it |
| Show clipping info | Toggle | Checked | None | Checked/unchecked | Shows or hides endpoint-bin counts and percentages | Available while idle | Counts are bin-based diagnostics | Display only | Window lifetime |
| Absolute scales | Toggle | Unchecked | None | Checked/unchecked | Checked normalizes each visible histogram channel to its own peak | Available while idle | Can conceal relative RGB strength; never changes pixels | Display only | Window lifetime |
| Draw histogram areas | Toggle | Unchecked | None | Checked/unchecked | Chooses filled-area or line rendering | Available while idle | Filled areas can obscure overlaps | Display only | Window lifetime |
| Show histogram | Toggle | Checked | None | Checked/unchecked | Shows or hides histogram curves | Available while idle | Display-only | Display only | Window lifetime |
| Plot resolution | Combo box, bins | **8-bit (256)** | None | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | More bins show finer distribution | Available while idle | Higher counts increase histogram memory/rendering; processing precision is unchanged | Display only | Window lifetime |
| RGB | Target image selector | Attached active RGB still | None | Open three-channel RGB still | Supplies all three broadband planes, output dimensions/type, metadata base, mask, and output-name stem | Registered action requires an active RGB still; changing target rebuilds narrowband lists | Wrong target invalidates registration; source remains unchanged on Apply | Preview/Apply source; all RGB channels; still only | Attachment lifetime; Reset does not change it |
| RGB bandwidth | Double spin box, nm | `100.00` | None | `0.10..1000.00`; step `1.00` | Larger values reduce the continuum fraction `NB/RGB`; smaller values subtract more RGB continuum | Must exceed every selected narrowband bandwidth by a nonzero margin | Incorrect effective width biases every selected channel | Preview/Apply; shared by R/G/B | Captured per run; Reset/success returns `100.00` |
| Red / Image | Image selector | **No image** | None | Open, non-sequence grayscale image with target width/height | Enables narrowband injection into red | Apply needs at least one selected channel; list rebuilds on image/target lifecycle changes | Misregistration creates red halos/double stars; adds full-plane buffers | Preview/Apply; red only | Selection survives Reset/success while valid; not saved after close |
| Red / Bandwidth | Double spin box, nm | `7.00` | None | `0.10..500.00`; step `0.50`; must be lower than RGB bandwidth | Higher values subtract a larger estimated continuum fraction | Ignored while Red / Image is **No image** | Near-RGB width amplifies noise through the small denominator | Preview/Apply; red only | Reset/success returns `7.00` |
| Red / Scale | Double spin box, multiplier | `1.20` | None | `0.00..10.00`; step `0.05` | Raises or lowers median-centered corrected red structure; `0` removes that boost but the matched/corrected maximum can still alter red | Ignored while Red / Image is **No image** | High values clip highlights and amplify noise/registration residuals | Preview/Apply; red only | Reset/success returns `1.20` |
| Green / Image | Image selector | **No image** | None | Open, non-sequence grayscale image with target width/height | Enables narrowband injection into green | Same lifecycle/readiness rules as Red | Misregistration creates green halos/double stars; adds full-plane buffers | Preview/Apply; green only | Selection survives Reset/success while valid; not saved after close |
| Green / Bandwidth | Double spin box, nm | `8.50` | None | `0.10..500.00`; step `0.50`; must be lower than RGB bandwidth | Higher values subtract a larger estimated continuum fraction | Ignored while Green / Image is **No image** | Near-RGB width amplifies noise through the small denominator | Preview/Apply; green only | Reset/success returns `8.50` |
| Green / Scale | Double spin box, multiplier | `1.20` | None | `0.00..10.00`; step `0.05` | Raises or lowers median-centered corrected green structure | Ignored while Green / Image is **No image** | High values clip highlights and shift color | Preview/Apply; green only | Reset/success returns `1.20` |
| Blue / Image | Image selector | **No image** | None | Open, non-sequence grayscale image with target width/height | Enables narrowband injection into blue | Same lifecycle/readiness rules as Red | Misregistration creates blue halos/double stars; adds full-plane buffers | Preview/Apply; blue only | Selection survives Reset/success while valid; not saved after close |
| Blue / Bandwidth | Double spin box, nm | `8.50` | None | `0.10..500.00`; step `0.50`; must be lower than RGB bandwidth | Higher values subtract a larger estimated continuum fraction | Ignored while Blue / Image is **No image** | Near-RGB width amplifies noise through the small denominator | Preview/Apply; blue only | Reset/success returns `8.50` |
| Blue / Scale | Double spin box, multiplier | `1.20` | None | `0.00..10.00`; step `0.05` | Raises or lowers median-centered corrected blue structure | Ignored while Blue / Image is **No image** | High values clip highlights and amplify blue noise | Preview/Apply; blue only | Reset/success returns `1.20` |
| Reject low | Double spin box, normalized value | `0.000` | None | `0.000..1.000`; step `0.010`; must be lower than Reject high | Raising it excludes darker boosted-channel samples from the affine fit | Fit also excludes zero/non-finite pairs and requires at least three usable samples | Too high can force the unannounced fit fallback | Preview/Apply; each selected channel fit | Reset/success returns `0.000` |
| Reject high | Double spin box, normalized value | `0.920` | None | `0.000..1.000`; step `0.010`; must exceed Reject low | Lowering it excludes brighter boosted-channel samples from the affine fit | Same fit/fallback rules as Reject low | Too low removes representative structure; too high admits clipping | Preview/Apply; each selected channel fit | Reset/success returns `0.920` |
| Non Linear rescale | Checkbox, stage-wise min/max normalization | Unchecked | None | Checked/unchecked | Checked rescales continuum, boosted, and final selected-channel planes independently to `[0,1]` before their respective clamps | Applies to every selected channel; does not change Preview modes showing raw inputs | Removes absolute level/scale relationships, expands outliers, and can change color balance; three extra scans per selected channel | Preview/Apply before mask; selected channels | Reset/success unchecks it |
| Preview | Combo box | **NBRGB** | None | **NBRGB**, **RGB**, **NBr**, **NBg**, **NBb** | Chooses calculated result or an input inspection view; mono inputs are displayed as RGB | Unselected NBr/NBg/NBb falls back to RGB; active mask still blends the chosen display against RGB | Input modes diagnose registration but are not outputs and can look mixed under a mask | Preview only | Reset/success returns **NBRGB** |
| Preview pane | Image viewer | Mask-aware Preview-area composite | Inherits target display-autostretch settings at creation | Standard viewer zoom/pan plus linear/autostretch display | Shows the current ROI result pasted over original RGB; display autostretch never changes output samples | Always present; no Preview checkbox | Holds source/result/statistics buffers; full preview costs more than ROI | Preview only; current still | Window lifetime |
| Switch image | Preview-toolbar action | Processed preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles processed preview and original RGB | Available in preview pane | Display comparison only | Preview display only | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox and movable ROI | Checked; centered up to `512 x 512` | None | Checked/unchecked; drag/resize within target bounds | Checked processes only the corresponding crop from RGB, selected narrowband images, and mask, then pastes it over original RGB | Apply always processes the full image; source-only Preview modes crop the selected input too | Reduces preview work; outside the rectangle is deliberately not processed | Preview and Take snapshot only | Window lifetime; Reset does not clear it |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes current settings and opens `<target> - NBRGB Combination - SNAPSHOT`; with Preview area checked, only that region is changed in the full-size snapshot | Snapshot uses current Preview mode, ROI, and active mask | Allocates a new still; source-only modes can create an inspection snapshot rather than an NBRGB result | New still; target unchanged | Each action creates a view |
| Apply | Button/action | Disabled until a valid RGB still, valid Reject interval, and at least one ready narrowband selection exist | None | One action | Captures current images/settings, runs in a background thread, and opens `<target> - NBRGB` | No progress dialog or Cancel; target selector and preview ROI/snapshot actions disable, but narrowband selectors, numeric options, Apply, Reset, and Close remain live; repeat Apply clicks are ignored while executing | Full-image vectors/intermediates can use substantial memory; wait for output before closing or interpreting later edits | Full still; active mask applies; never a sequence | Success resets numeric/options/Preview mode but retains valid selectors |
| Reset | Button/action | Enabled | None | One action | Restores bandwidths, scales, Reject limits, Non Linear rescale, and Preview mode | Does not clear RGB/narrowband selectors, histogram state, or Preview area; currently remains live during Apply | Schedules a new preview; does not alter the captured in-flight Apply | Configuration only | Immediate |
| Close | Button/action | Enabled | None | One action | Closes without applying current settings; during a background Apply it stops the run, which then opens nothing, and the window closes once the run has ended | Remains live during background Apply; wait for the output before closing if you want it | No output by itself | Window | Values are not persisted after destruction |

There is no Apply progress dialog or visible **Cancel** action. The run carries
a stop all the same: closing the window requests it, the engine's combination
honours it, and a stopped run opens nothing.

## How the algorithm works

### 1. Rectangular-bandwidth continuum estimate

The process handles Red, Green, and Blue independently. Let `R_c` be a
normalized broadband sample in selected channel `c`, `N_c` the corresponding
normalized mono narrowband sample, `B_rgb` the shared RGB bandwidth, and `B_c`
the selected narrowband bandwidth. It defines

\[
q_c=\frac{B_c}{B_{rgb}},\qquad
C_c=\frac{N_c-q_cR_c}{1-q_c}.
\]

`C_c` is the estimated continuum-corrected narrowband plane. The denominator
must be greater than `1e-6`, so each narrowband width must be meaningfully lower
than the RGB width. The model treats both filters as rectangular passbands and
uses the corresponding RGB channel as the continuum proxy. It does not use
central wavelength, measured transmission, sensor quantum efficiency, exposure,
or a spectrum; bandwidth values are therefore user-supplied approximations.

Without **Non Linear rescale**, `C_c` is immediately clipped to `[0,1]` in a
32-bit float intermediate. With the option checked, it is first min/max-scaled
over the complete current plane and then clipped.

### 2. Median-centered channel boost

Let `m_c` be the median of the clipped continuum-corrected plane and `s_c` the
channel's **Scale**. The boosted broadband plane is

\[
G_c=R_c+s_c(C_c-m_c).
\]

At `s_c=0`, `G_c` is the original RGB plane, but the later maximum can still
admit the corrected/matched narrowband plane. Increasing Scale strengthens both
positive and negative deviations around the narrowband median. The intermediate
is again optionally min/max-scaled and always clipped to `[0,1]`.

### 3. Guarded affine match

The process attempts an ordinary least-squares fit

\[
M_c=a_cC_c+b_c
\]

from continuum-corrected samples to the boosted plane. A pair is eligible only
when both values are finite and nonzero and `G_c` lies inside the inclusive
**Reject low**/**Reject high** interval. The NBRGB guard requires at least three
eligible pairs, variation in `C_c` above `1e-8`, and a valid rejection interval.
If those checks fail, `M_c=C_c`; the UI does not announce that fallback.

### 4. Maximum merge, rescale, and mask

The selected output plane is

\[
O_c=\operatorname{clip}_{[0,1]}\left(\max(G_c,M_c)\right).
\]

When **Non Linear rescale** is checked, the maximum plane is min/max-scaled once
more before the final clip. Unselected RGB channels are copied unchanged. There
is no neighborhood kernel or boundary extension: the continuum equation, boost,
and maximum are per-pixel, while the median, fit, and optional min/max operations
use global statistics over the current full image or preview crop.

The shared mask is applied last. For mask weight `W` after optional inversion,

\[
RGB_{final}=RGB_{source}(1-W)+RGB_{NBRGB}W.
\]

### Precision and invalid samples

Input samples are read through normalized double access. Corrected, boosted,
matched, and combined helper images are 32-bit float; the final image retains
the RGB source's pixel type, so integer outputs quantize on write. Narrowband
sources may have different pixel types because their normalized samples are
converted independently.

There is no general NaN/Infinity repair. The fit rejects non-finite pairs, but
continuum calculation, median selection, min/max rescaling, maximum, and final
write do not define an astronomical bad-pixel policy for them. Repair or reject
non-finite samples upstream, especially before enabling min/max rescaling.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong or defective looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| RGB/NB bandwidths | Use documented effective widths; keep NB well below RGB | Continuum correction resembles raw NB | Noise/gradients explode as widths approach equality | Verify units and widths before Scale |
| Scale + continuum median | `1.20`, one channel at a time | Emission enhancement is subtle | Clipped cores, amplified noise, colored star residuals | Fix registration/background, then Scale |
| Reject interval + fit | `0.000..0.920` | Fit still follows unwanted bright structure | Too few accepted samples silently return un-fitted `C_c` | Exclude clipping while retaining broad ordinary structure |
| Non Linear rescale + channel balance | Off for linear masters | — | Each stage expands outliers and destroys cross-channel amplitude relationships | Use only as a deliberate nonlinear display experiment |
| Preview mode + active mask | Temporarily disable mask when comparing raw RGB/NB inputs | Registration differences are hidden by black mask areas | Input mode appears to be a hybrid of RGB and NB | Inspect source modes unmasked, then restore mask for NBRGB |
| Preview area + Apply | Tune several representative ROIs, then full preview | Local setting misses other structures | Full-image background/highlights behave unlike one ROI | Sample background, emission, and stars before Apply |

## Inputs, outputs, and metadata

- The target selector accepts only RGB stills. Each narrowband selector accepts
  non-sequence grayscale views with exact target dimensions and excludes the
  target. It does not check registration, WCS, pixel type, processing state,
  filter identity, bandwidth metadata, exposure, or PSF.
- Apply creates one new RGB still and leaves every source unchanged. The output
  dimensions and pixel type match the RGB target. No mask, continuum map, fit
  coefficients, report, preset, or direct disk file is produced.
- The result's metadata is built from the RGB source and every selected
  narrowband source. Its exposure is the light they hold together, recorded
  once: the sum, over the distinct acquisitions, of each source's whole
  integration - its `TOTEXP` card, else its `LIVETIME` card, else its exposure
  times its frame count (`STACKCNT`, else `NCOMBINE`, else one). Sources that
  state the same exposure start are one acquisition - an image and copies made
  of it, such as its starless image, its stars or a channel taken out of it -
  and count once, with the largest integration and the largest frame count any
  of them states, since a copy such as a StarNet output may have lost its
  `TOTEXP` and `STACKCNT` cards; a source that states no exposure start always
  counts on its own. The observation start is the earliest any source states.
  The object, instrument, telescope, observer, filter, sensor temperature,
  gain, and binning are kept when every selected source states the same value
  and dropped otherwise, so a filter is kept only when every source shares it.
- The result keeps the header cards every selected source states identically,
  but no exposure, processing-state or identity card of a source; a card the
  sources state differently, or only some state, is dropped, except the RGB
  target's pointing cards (`RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`,
  `OBJDEC`, `TELRA`, `TELDEC`), kept because the result is on its grid, and the
  observation-start cards (`DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`,
  `MJD-BEG`, `TIME-OBS`), taken together from the source that began earliest.
  The result states its own `EXPTIME` and `TOTEXP` as the exposure above,
  `LIVETIME` the same when a source states one, and `STACKCNT` and `NCOMBINE`
  as the frame counts of the distinct acquisitions summed, each when a source
  states it. It keeps the RGB target's plate solution with its world coordinate
  (WCS) cards, so the viewer, the astrometry window and a new plate solve find
  it on the result. The solution describes the RGB target's grid; it does not
  prove that the narrowband sources were registered to it.
- If every input explicitly declares linear state, the output is marked linear
  RGB with `nbrgb-combination` provenance. If any input declares nonlinear, the
  output is marked nonlinear. Otherwise the combined state is unknown. The
  **Non Linear rescale** checkbox does not independently change that decision.
- The new view receives the execution description “Created NBRGB combination
  from RGB and selected narrowband channels.” Successful Apply then resets the
  numeric/options state while retaining valid source selections.
- **Take snapshot** follows the selected Preview mode. With **Preview area**
  checked, it creates a full-size still in which only the ROI is replaced; with
  **RGB** or a narrowband input mode selected, it is an inspection snapshot, not
  necessarily an NBRGB combination.

## Performance

Work is linear in pixel count for each of one to three selected channels. Each
channel performs continuum correction, median selection, boosted-plane
construction, fit eligibility/statistics, an optional least-squares fit,
maximum merge, and output write. **Non Linear rescale** adds three full-plane
min/max scans and normalization passes per selected channel.

The pixel loops use oneTBB, but selected channels are processed serially.
Several full-size `double` vectors plus float intermediates coexist, alongside
the RGB source/output, preview, mask, and histogram buffers. Memory therefore
scales with image size and selected-channel count even though the mathematical
pipeline is per-channel. Use the default Preview area to tune representative
regions, then perform one full preview before Apply. No approved astronomical
set was timed.

## Worked example

- **Input condition:** Red broadband samples are `(0.20, 0.40, 0.60)` and the
  registered mono red samples are `(0.326, 0.562, 0.798)`.
- **Settings:** RGB bandwidth `100`, Red bandwidth `10`, Red Scale `1.20`,
  Reject interval `0..1`, and Non Linear rescale off.
- **Continuum checkpoint:** `q=0.1`, so corrected values are
  `(0.34, 0.58, 0.82)` and their median is `0.58`.
- **Boost checkpoint:** Before clipping, `G=R+1.2(C-0.58)` gives
  `(-0.088, 0.40, 0.888)`; clipping gives `(0, 0.40, 0.888)`.
- **Fit fallback:** The zero boosted sample is ineligible, leaving only two
  nonzero pairs. NBRGB requires three, so the matched plane falls back to
  `(0.34, 0.58, 0.82)`.
- **Output checkpoint:** The per-pixel maximum is
  `(0.34, 0.58, 0.888)`. Green and blue are unchanged because they were not
  selected. These exact values reproduce the engine regression; they do not
  claim a visually suitable scale for real data.

## Warnings and limitations

!!! warning "Bandwidths are an approximation, not filter metadata"
    One shared RGB width and three user-entered NB widths drive a rectangular-
    passband equation. AfterNight does not read central wavelengths or measured
    response curves and cannot verify that a selected master belongs to the
    labelled output channel.

!!! warning "Non Linear rescale changes the data three times"
    The option independently min/max-normalizes the corrected, boosted, and
    final selected-channel planes. It is not a display STF and does not merely
    prevent final clipping.

!!! warning "Apply has no progress dialog or Cancel button"
    The run is asynchronous, and this process shows no progress dialog and no
    Cancel button; closing the window stops the run, which then opens nothing.
    Most source/parameter/dialog controls remain live after Apply captures its
    parameters, and a later edit reaches the next run only. Wait for the new
    image before closing.

!!! warning "Equal dimensions are not registration"
    There is no translation, rotation, scale, distortion, WCS, PSF, or intensity
    compatibility check. Misregistered narrowband structure becomes colored
    halos and doubled stars while the result's acquisition facts still describe
    the sources.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Menu action is disabled | Active view is grayscale or a sequence, no image is active, the same process is attached, or an exclusive process is attached | Activate an RGB still and close the conflicting process |
| Narrowband image is absent from a selector | It is RGB, a sequence, a different size, the target itself, closed, or not updated | Extract/convert to grayscale, register/crop to exact dimensions, and reopen/update it |
| Apply is disabled | No NB channel is selected, NB bandwidth is not below RGB bandwidth, Reject high is not above Reject low, or a selected view disappeared | Restore at least one valid source and correct the bandwidth/rejection values |
| Preview shows plain RGB | Parameters are not ready, or NBr/NBg/NBb was selected without that source | Select a ready narrowband channel and choose NBRGB |
| Narrowband input Preview looks mixed with RGB | The target's active mask is being applied even in source-inspection modes | Temporarily disable the mask while checking raw registration |
| Strong gradients or odd background offsets | Inputs were not background/intensity prepared, or the rectangular bandwidth approximation is poor | Correct gradients and scaling upstream; verify effective widths; reduce Scale |
| Fit seems to have no effect | Fewer than three finite, nonzero pairs survived the Reject interval, or the corrected plane is almost constant | Widen the interval and inspect clipping; understand that fallback uses corrected NB directly |
| Bright structures clip or color shifts strongly | Scale is high, bandwidth denominator is small, or Non Linear rescale expanded an outlier | Restore defaults, turn rescale off, verify widths, then raise one Scale gradually |
| Doubled stars or colored rims | Equal-size sources are not registered or have different PSFs | Register/crop upstream and consider matching star profiles before combination |
| Take snapshot is not an NBRGB result | Preview is on RGB/NBr/NBg/NBb, or Preview area is checked | Select NBRGB and uncheck Preview area for a full calculated snapshot |
| No progress window appears after Apply | NBRGB does not implement a progress dialog | Wait for `<target> - NBRGB`; use Preview area to estimate suitability before the full run |

## Related workflow steps

- [Linear Match](../color-calibration/linear-match.md) — prepare intensity
  relationships upstream when the NBRGB-specific internal fit is insufficient.
- [Image Registration](../registration-alignment/image-registration.md) — align
  RGB and narrowband geometry before composition.
- [NB Color Mapper](nb-color-mapper.md) — map and mix several narrowband layers
  directly rather than injecting them into a broadband target.
- [Narrowband Palette Composer](narrowband-palette-composer.md) — explore palette
  matrices and presets from narrowband sources.
- [Process Windows](../../interface/process-windows.md) — shared preview, masks,
  snapshots, Apply, Reset, and image-history behavior.
- [Image Viewer](../../interface/image-viewer.md) — display stretch, masks, and
  image-state inspection.
