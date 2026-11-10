---
kind: process
stage: stretch
menu: Process > Intensity Transformations > Histogram Transformation
summary: >-
  Maps the image through a shadows, midtones and highlights transfer, the controlled step from a linear master to a
  visible picture.
data_in: linear
data_out: nonlinear
---
# Histogram Transformation

```figure
image: ../../assets/images/processes/stretching/histogram-transformation.webp
alt: >-
  The Histogram Transformation window. On the left, the image's histogram above the transfer curve
  and its three handles, then the Channel box, the Shadows, Highlights and Midtones values, Target
  Image and Preview. On the right, the preview of the image.
caption: >-
  Histogram Transformation as it opens on a linear master. Nothing is stretched yet, so the
  histogram is a spike at the left and the preview is nearly black.
```

## Purpose and expected result

Histogram Transformation permanently maps normalized image samples through a
shadows/midtones/highlights transfer. It is intended for the controlled change
from a calibrated linear master to a visible nonlinear image, after operations
that require linear data are finished.

Manual Apply replaces the selected still or sequence, preserves its pixel type
and geometry, optionally post-blends an active view mask, records Image History,
and marks the output nonlinear with `histogram-mtf` provenance. The window also
shows source and predicted histograms and a real-time image preview.

The two **Auto-stretch** toolbar actions derive a linked or per-channel curve from
the exact statistics of the whole image a run changes: the target's for the
preview, a **Preview area**, and Apply, and each frame's own for a sequence. They do not copy the derived numeric values into
**Shadows**, **Midtones**, and **Highlights**, but the selected Auto-stretch mode
is retained in the typed execution snapshot: preview and a later Apply therefore
use the same mode. A manual transfer or **Channel** edit, **Reset**, or successful
Apply clears that retained Auto-stretch state.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Intensity Transformations > Histogram Transformation** |
| Enabled when | An image or sequence view is active. The launcher is nonexclusive, but each target must be idle when Apply requests its processing lock. |
| Inputs | One intended one-channel grayscale or three-channel RGB still/sequence; normalized Shadows/Midtones/Highlights; one shared RGB or selected-channel transform; optional enabled view mask. |
| Outputs | Destructive target replacement, preserved geometry/pixel type, nonlinear `histogram-mtf` processing state, and one history step. A sequence is rebuilt at a temporary path before replacement. No file, LUT, preset, or report is created. |
| Recommended image state | A calibrated linear mono/RGB master after background/color calibration and linear-only measurement, deconvolution, and denoising. Do not use on raw CFA or data that must remain photometrically linear. |
| Preview support | Full real-time image preview, which is the result Apply writes, plus lower source and upper predicted histograms. Manual settings are previewed asynchronously. An Auto-stretch **Preview area** uses the whole target's statistics, so it shows exactly what Apply writes there. The histogram prediction is quantized and does not include the active mask. |
| Sequence support | Supported through the generic replacement-sequence path. One manual or Auto-stretch mode snapshot is used for all frames; progress and cooperative cancellation are available. Auto-stretch derives each frame's curve from that frame's own statistics, as it would stretch the frame alone. The sequence reader may apply its configured CFA debayer request. |
| Mask support | Supported as a final original/processed blend. Inversion and grayscale/RGB mask-channel rules are honored. The mask does not alter the transfer curve or clipping readouts. |
| External dependencies | None. The transform is the bundled engine's screen transfer, run on its threads. |

## Before you start

1. Finish calibration, integration, gradient correction, background/color
   calibration, and any measurement or restoration step that requires linear
   samples. Apply changes the declared processing state to nonlinear.
2. Activate the intended mono or RGB target. A three-channel RGB master is the
   supported color use. A one-channel CFA mosaic is technically processed as
   grayscale, which destroys its linear sensor relationship and is not a valid
   debayer workflow.
3. Save a copy or confirm that Image History is available. Apply replaces the
   target; repeated stretches compound quantization and clipping.
4. Inspect the active view mask. It is applied only after the transform, while
   the two **Clipped** fields are calculated without it.
5. Leave **Plot resolution** at **8-bit (256)** for responsive adjustment. Raise
   it only when finer diagnostic bins matter; it never increases output pixel
   precision.
6. Keep `Shadows < Highlights` and prefer an interior Midtones value. The spin
   boxes do not enforce point ordering even though dragging the range handles
   does.
7. Use either Auto-stretch action for a statistics-derived preview and Apply, or
   edit the manual controls. The derived Auto values stay hidden; record whether
   linked or unlinked mode was selected when reproducibility matters.
8. For sequences, choose deliberately: manual settings apply one curve to every
   frame, while Auto-stretch derives each frame's curve from that frame's own
   statistics.

## Procedure

1. Activate the linear target and open **Process > Intensity Transformations >
   Histogram Transformation**. Confirm **Target Image** and leave **Preview**
   selected.
2. Select **RGB** to apply one curve to all three channels, or select **Red**,
   **Green**, or **Blue** for a deliberate single-channel change. On mono data,
   the disabled selector reads **Grayscale**.
3. Choose a diagnostic **Plot resolution**. The lower histogram is the source;
   the upper histogram is the source counts remapped through the current
   transfer.
4. Raise **Shadows** cautiously. Check the source peak, the preview, and the
   approximate shadow **Clipped** count. Do not place the point inside useful
   background signal merely to make the display black.
5. Lower **Midtones** below `0.500000` to brighten the remaining interval, or
   raise it to darken. The middle range handle marks the input that maps to
   output `0.5` after Shadows/Highlights rescaling.
6. Leave **Highlights** at `1.000000` unless the source has a genuinely unused
   upper interval. Lowering it forces more samples to white; the highlight
   **Clipped** field is only a binned approximation.
7. Inspect the preview at 100% under fixed display conditions. If a mask is
   active, inspect the mask boundary and remember that the histogram clipping
   prediction is unmasked.
8. Choose **Apply** for the verified manual preview or the retained linked/
   unlinked Auto-stretch preview. A still runs in a worker and is replaced in
   place. A sequence shows progress and is committed only after its temporary
   replacement is complete.
9. Verify Image History and the nonlinear processing state. Continue with
   nonlinear contrast/color finishing; do not return the result to PCC/SPCC or
   another linear-only calibration stage.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Upper preview histogram | Read-only histogram | Current target at launch, 256 bins | None | One or three channel plots | Shows the source counts remapped through the current transfer - the manual values, or the retained Auto-stretch curve - recomputed after every transfer, Channel, Auto-stretch, target, or Plot resolution change. | Always visible. Displayed lines follow the preview viewer's display-channel mode, not necessarily the process **Channel** selection. | It is diagnostic only. The prediction excludes masks and uses binned input; it is never recounted from the processed preview. | Preview only; no output effect | Window lifetime. |
| Lower source histogram | Read-only histogram with guide line | Current target at launch, 256 bins | None | One or three channel plots | Shows source sample distribution and hosts the three range handles. | Always visible. **Absolute scales**, area, grid, zoom, resolution, and preview display-channel state affect rendering only. | A narrow linear peak can require zoom or more bins. Plot appearance is not pixel precision. | Diagnostic only | Window lifetime. |
| Shadows/Midtones/Highlights range handles | Three draggable pointers | `0.0` / `0.5` / `1.0` | Reset restores these values | Shadows and Highlights are clamped to `0`–`1` and cannot cross while dragged; Midtones is stored relative to their interval. | Dragging left/right changes the corresponding transfer parameter and schedules preview on release. | Lower source histogram only. Overlapping handles can be difficult to select; equal endpoints make relative-midpoint dragging undefined. | Direct visual editing is fast, but exact work should use spin boxes. | Manual preview/Apply; selected process channels | Window lifetime; Reset restores. |
| Histogram mouse gestures | Pointer/wheel actions | Idle | None | Left click outside a handle shows coordinates; middle drag pans; right click resets zoom; wheel zooms when interactive zoom is enabled. | Changes inspection only. | Both plots support zoom/pan; only the lower plot has range handles. | No processing effect. Deep zoom can make the two scroll areas harder to compare. | Diagnostic only | Window lifetime; Reset also resets zoom. |
| **Zoom in** | Toolbar action | Fit-sized plots | None | Repeated clicks, each `1.30×` current widget size | Magnifies both plots. | Always enabled while the process is idle. | Increases scrollable plot size, not bins or sample precision. | Diagnostic only | Window lifetime; Reset/Zoom reset restores. |
| **Zoom out** | Toolbar action | Fit-sized plots | None | Repeated clicks, each `0.70×` current widget size | Shrinks both plots. | Always enabled while idle. | Can hide narrow peaks; no processing effect. | Diagnostic only | Window lifetime; Reset/Zoom reset restores. |
| **Zoom reset** | Toolbar action | Fit-sized plots | None | Click action | Fits both plot widgets to their scroll-area hosts. | Always enabled while idle. | No processing effect. | Diagnostic only | Window lifetime. |
| **Show grid** | Checkable toolbar action | Selected | Reset does not change it | Selected/unselected | Draws or hides the plot grid in both histograms. | Always available while idle. | Rendering only. | Diagnostic only | Window lifetime; not saved. |
| **Show transfer curve** | Checkable toolbar action | Selected | Reset does not change it | Selected/unselected | Draws or hides the white manual MTF curve on the lower source histogram. | The upper plot never draws a transfer curve. Auto-stretch does not replace the curve with derived values. | Rendering only; leaving it visible helps detect extreme/end-point settings. | Manual diagnostic only | Window lifetime; not saved. |
| **Absolute scales** | Checkable toolbar action | Unselected | Reset does not change it | Selected/unselected | Despite the tooltip, selected calls local channel normalization: each displayed channel uses its own peak height. Unselected uses one maximum across displayed channels. | Disabled for grayscale; affects both plots. | Local normalization makes weak channels visible but hides relative peak-height differences. | Diagnostic only | Window lifetime; not saved. |
| **Draw histogram areas** | Checkable toolbar action | Unselected | Reset does not change it | Selected/unselected | Switches both plots from line traces to translucent additive filled areas. | Always available while idle. | Rendering only; overlap colors can be easier to compare but dense peaks can obscure detail. | Diagnostic only | Window lifetime; not saved. |
| **Plot resolution** | Choice, bin count | **8-bit (256)** | Reset does not change it | **6-bit (64)**, **7-bit (128)**, **8-bit (256)**, **9-bit (512)**, **10-bit (1024)**, **12-bit (4096)**, **14-bit (16K)**, **16-bit (64K)**, **100 levels**, **1000 levels**, **10000 levels** | Higher choices quantize source/preview histograms more finely; lower choices update faster and aggregate more samples per bin. | Recomputes both histograms. It does not change the image transform, output type, or pixel precision. | Up to 65,536 counters per channel; high resolution adds computation/memory. Clipping counts use the current binned histogram and are recomputed with it after a resolution change. | Diagnostic and clipping prediction only | Window lifetime; not saved. |
| **Channel** | Choice | **RGB** for RGB; **Grayscale** for mono | Reset chooses **RGB** when available | **RGB**, **Red**, **Green**, **Blue**; mono has disabled **Grayscale** | RGB applies one shared curve to all channels. A single color applies it only to that channel and copies the others unchanged. | Color choices require a three-channel target. It does not itself hide other histogram lines; those follow preview display mode. | Single-channel stretching changes color balance and can produce colored clipping. | Manual preview/Apply and clipping numerator; mask blend follows | Window lifetime; Reset restores RGB. |
| **Auto-stretch linked channels** | Toolbar action | No request | None | One click; fixed `-2.80` clipping and `0.25` target background | Resets manual values and selects one statistics-derived MTF shared by all channels for preview and Apply. | Enabled only with Preview. Available for mono and RGB. Derived numeric values are not written to the controls, but the linked mode is retained in the typed execution snapshot. | Preserves one shared channel relationship. On sequences, each frame's curve is derived from that frame's own statistics. | Preview/Apply; linked mono/RGB; mask/readout mismatch applies | Retained until manual transfer/Channel edit, Reset, or successful Apply; not saved. |
| **Auto-stretch channels independently** | Toolbar action | No request | None | One click; fixed `-2.80` clipping and `0.25` target background | Resets manual values and selects one per-channel statistics-derived transform for preview and Apply. | Enabled only for RGB with Preview. Derived numeric values are hidden, but unlinked mode is retained in the typed execution snapshot. | Can neutralize channel medians but changes color relationships. On sequences, each frame's per-channel transform is derived from that frame's own statistics. | Preview/Apply; unlinked RGB; post-blend mask | Retained until manual transfer/Channel edit, Reset, or successful Apply; not saved. |
| **Shadows** | Normalized decimal spin box | `0.000000` | None | `0.000000`–`1.000000`; six decimals; step `0.01`; spin box does not enforce `< Highlights` | Higher maps more low samples to black and rescales the remaining interval; lower preserves more shadow range. | Shared by selected manual channels. Apply is disabled only when all manual values are identity and no auto request is pending. | Raising it can irreversibly erase faint signal. | Manual preview/Apply; selected channels; before mask blend | Window lifetime; reset after successful Apply. |
| Shadow **Clipped** | Read-only count and percent | `0 - 0.000000%` before prediction | None | Approximate integer count and six-decimal percentage | Reports selected-channel histogram bins strictly below the rounded Shadows bin. | Recalculated with the prediction whenever the transfer, Channel, retained Auto-stretch mode, target, or Plot resolution changes; never by the mask. | Exact-boundary samples mapped to black are not counted. For one selected RGB channel, count covers that channel but percent divides by all three channels, so it is diluted by three. | Unmasked diagnostic only | Not persisted. |
| **Highlights** | Normalized decimal spin box | `1.000000` | None | `0.000000`–`1.000000`; six decimals; step `0.01`; spin box does not enforce `> Shadows` | Lower maps more high samples to white and expands the remaining interval; higher preserves more highlight range. | Shared by selected manual channels. | Lowering can flatten star cores and saturated structure. | Manual preview/Apply; selected channels; before mask blend | Window lifetime; reset after successful Apply. |
| Highlight **Clipped** | Read-only count and percent | `0 - 0.000000%` before prediction | None | Approximate integer count and six-decimal percentage | Reports selected-channel histogram bins strictly above the rounded Highlights bin. | Same quantized/unmasked update path as shadow Clipped. | Exact-boundary samples mapped to white are not counted; a single-channel RGB percentage is divided by all RGB samples. | Unmasked diagnostic only | Not persisted. |
| **Midtones** | Relative normalized decimal spin box | `0.500000` | None | `0.000000`–`1.000000`; six decimals; step `0.01` | Below `0.5` brightens the unclipped interval; above `0.5` darkens it. It is the relative input point mapped to output `0.5`. | Shared by selected manual channels. | Values near `0` or `1` collapse most of the interval and magnify quantization. | Manual preview/Apply; selected channels; before mask blend | Window lifetime; reset after successful Apply. |
| **Target Image** | Open-image/sequence selector | Launching view | None | Supported open views | Changes the destructive target and refreshes statistics, supported channels, source histogram, and preview. | Target must remain open/supported. Processing lock is requested only at Apply. | Large images/sequences increase CPU/memory/disk. | Full still/sequence operation | Window lifetime. |
| **Preview** | Boolean | Selected | Reset does not change it | Selected/unselected | Creates/destroys the RT image preview and enables/disables both Auto-stretch actions. Manual histogram prediction remains available. | Unlinked auto also requires RGB. | Live full-image previews can be expensive; clearing Preview avoids repeated image transforms while editing. | Preview only | Window lifetime; not saved. |
| RT image preview pane | Interactive preview view | Visible because Preview is selected | None | Standard view zoom/pan/display-channel actions | Shows the asynchronously processed image, including the active mask blend. Its display-channel mode controls which lines both histogram widgets show. | Removed when Preview is cleared. | Full-image recomputation and copies dominate interactive cost on large data. | Preview only; mask-aware | Window lifetime. |
| **Apply** | Action | Disabled at identity defaults | None | One execution at a time | Applies the current manual values or retained Auto-stretch mode to the selected still or every sequence frame, then resets process settings. | Enabled when a manual value differs from identity or an Auto-stretch mode is retained. Target processing lock must succeed. | Destructive. Auto-derived numeric values are not visible in the controls or history; verify the preview and record the selected mode separately. | Full still/sequence; selected manual channels or linked/unlinked Auto; optional post-mask blend | Pixels, metadata, and history persist; controls/Auto mode reset. |
| **Close** | Action | Enabled while idle | None | Click action | Closes without applying pending manual values. | Disabled during full Apply; title-bar close follows shared process shutdown. | Unapplied settings are discarded. It is not sequence Cancel. | Dialog only | Not persisted. |
| **Reset** | Action | Enabled while idle | None | Click action | Restores Shadows/Midtones/Highlights, RGB channel, and both plot zooms, clears a retained Auto-stretch mode, and recomputes the prediction. | Does not restore Plot resolution, grid, transfer-curve, normalization, area, target, or Preview choices. | Returns the manual transform to identity and disables Apply. | Dialog/manual preview only | Not persisted. |
| Sequence progress/status | Read-only progress | Hidden for stills | None | 0–100 percent/frame status | Shows generic replacement-sequence preparation and frame progress. | Created only for a sequence Apply. | Requires a temporary sequence and disk space; no per-pixel stage diagnostics. | Sequence Apply | Not persisted. |
| **Cancel** in sequence progress | Action | Enabled during sequence processing | None | One cancellation request | Requests cooperative sequence cancellation. Temporary output is closed/removed and not committed. | Sequence only; observed between framework processing checkpoints rather than inside the TBB sample transform. | Original target sequence remains intact after cancellation. | Sequence Apply | Not persisted. |

## How the algorithm works

### Conceptual pipeline

1. The lower plot builds one histogram per source channel at the selected bin
   resolution. Image statistics for Auto-stretch are computed separately from
   source pixels using the exact median and MAD of every valid sample, measured
   once for each change of the image.
2. Manual mode snapshots Shadows, Midtones, Highlights, and the process Channel.
   Auto mode snapshots linked/unlinked selection in the same typed parameters and
   derives the curve from the statistics of the whole image the run changes: the
   target's for a preview, a preview area, and Apply, and each frame's own for a
   sequence.
3. The engine normalizes every selected sample by its native pixel type, applies
   one rational midtones transfer, and converts it back to the same pixel type,
   rounding to the nearest code. Unselected RGB channels and an alpha channel
   are held aside while the transfer runs and copied back exactly as they were,
   including floating samples outside `[0,1]` and non-finite ones.
4. The shared process framework blends an enabled view mask after the transform.
   A still is swapped into the target; a sequence is processed frame by frame at
   a temporary path and replaces the source only on success.
5. The source's header cards, acquisition record, and solution are kept; the
   output states no transfer function, its processing history records
   the stretch, and processing state is marked nonlinear with `histogram-mtf`. Image History records the visible Channel and manual numeric
   values, not the hidden auto-derived parameters.

### Model and equations

Let normalized input be `x`, Shadows be `S`, Highlights be `H`, and relative
Midtones be `m`. Current source evaluates:

```text
x <= S  -> 0
x >= H  -> 1
t = (x - S) / (H - S)
f(x) = ((m - 1) * t) / (((2*m - 1) * t) - m)
```

For `S < H` and `0 < m < 1`, the curve is monotonic. The relative input `t=m`
maps to `0.5`, so the absolute middle handle is `S + (H-S)m`. `m=0.5` is a
linear rescale between the clipping points; smaller values brighten midtones and
larger values darken them. Clipping discards information, and this process does
not expose the engine's pseudoinverse operation.

Auto-stretch uses per-channel median `v_j` and normalized MAD
`q_j = 1.4826*MAD_j`, replacing zero `q_j` with `0.001`. If at least one channel
median is not inverted (`v_j <= 0.5`), linked mode computes:

```text
S = max(0, mean(v_j - 2.80*q_j))
H = 1
m = MTF(mean(v_j) - S, 0.25, 0, 1)
```

Unlinked mode computes the same `S` and `m` separately for each channel. If all
medians exceed `0.5`, it uses `S=0`, an upper point derived from
`v_j + 2.80*q_j`, and the complementary midtones value. These fixed constants
are implementation behavior, not editable controls. The calculation resembles
an adaptive STF, but AfterNight's current code and limitations—not an external
application—define the result.

The predicted histogram remaps source-bin weights through the same MTF. It adds
weights below `round(S*(bins-1))` to shadow Clipped and above
`round(H*(bins-1))` to highlight Clipped. The percent denominator is the samples
the image's validity admits, summed over every channel -
`width * height * number_of_channels` when every sample is valid.

### Boundary and failure behavior

- The intended channel counts are one and three. The UI has no useful selection
  model for other channel counts.
- Spin boxes permit `S >= H`. At equality, the transform becomes an abrupt
  black/white threshold because the lower endpoint comparison wins at the exact
  point. With Shadows above Highlights the engine refuses the transfer: the
  preview and Apply report "midtones parameters must be finite unit values with
  ordered endpoints" and the prediction is cleared. Dragging the relative
  midpoint at zero interval can divide by zero.
- `m=0` and `m=1` collapse almost the whole interval. The UI permits both.
- A non-finite sample in a transformed channel of a pixel the image's validity
  admits makes the engine refuse the run ("a selected stretch sample is not
  finite"); a channel the transfer leaves alone is kept as it was, and pixels
  the validity rule rejects are left unchanged. Auto-stretch also refuses an
  image with no valid sample, a non-finite sample in any admitted channel, and
  a floating image whose channel median lies outside `[0,1]`. Use finite
  normalized data.
- A Float64 or 32-bit integer image is evaluated in a double working copy and
  every other representation in Float32. Integer formats are normalized,
  transformed, and quantized back to their original type. Repeated Apply
  increases rounding loss.
- The clipping readouts are binned estimates, omit exact endpoint bins, ignore
  masks, and use an all-channel denominator. They are not scientific clipping
  measurements.
- A Plot resolution change recounts the source and recomputes the prediction
  and both Clipped fields at the new bin count. A 12-bit or finer histogram is
  redrawn to fill its plot when the dialog is resized, and a whole code on a bin
  boundary lands in its own bin, by exact interval arithmetic.
- Both Auto-stretch actions reset the manual controls and retain a linked or
  unlinked execution mode. They do not publish derived S/M/H values back to the
  controls, so the visible identity values and history description do not reveal
  the applied curve. Manual transfer/Channel edits, Reset, or successful Apply
  clear the retained mode.
- A still has no progress dialog or user cancellation once full Apply starts.
  Sequence Cancel is cooperative; the temporary output is removed on the normal
  cancellation path.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Manual linked RGB | **RGB**, `S=0`, `m=0.5`, `H=1`; move Shadows only after locating the background peak | Faint signal remains compressed near the left and the image remains dim | Background/faint structures clip black or star cores clip white | Shadows, then Midtones, leave Highlights last |
| Midtones with fixed endpoints | Keep `m` near `0.5` and make small changes | Useful signal remains hard to see | Contrast becomes harsh; low values overbrighten, high values bury midtones | Establish endpoints first, then Midtones |
| Selected color channel | Prefer shared RGB after color calibration; select one channel only to correct a known imbalance | Intended channel mismatch remains | Color balance and colored star/background clipping shift | Compare channel histograms, select one channel, adjust gently, return to RGB |
| Plot resolution and zoom | Begin at 256 bins and use plot zoom before increasing bins | Narrow structures merge into a few bins | High-bin display becomes noisy/slow without changing the image result | Zoom, then raise resolution only as needed |
| Active mask | Verify the unmasked manual curve first, then enable the intended mask for final preview | Protected region changes too little | Mask seams or channel differences become visible | Manual curve, mask geometry/inversion, preview at 100%, Apply |
| Auto-stretch | Inspect the linked/unlinked preview under fixed display conditions | Statistics-derived curve remains hidden in the controls/history | Unlinked mode changes color ratios; each sequence frame is stretched from its own statistics | Choose linked/unlinked deliberately, verify at 100%, record the mode, then Apply |

## Inputs, outputs, and metadata

- A still is replaced in place. A sequence is rebuilt in its current sequence
  representation at a temporary path and then replaces the target; no extra view
  is requested by this process.
- Width, height, channel count, pixel format, and pixel type are preserved.
  Every selected sample is evaluated in normalized float precision, double for a
  Float64 or 32-bit integer image. Integer output is rounded to the nearest
  code.
- The source's header cards, acquisition record, and astrometric solution are
  kept. The output states no transfer function, the dialog writes nonlinear
  processing state with operation provenance `histogram-mtf`, and the lineage
  entry (`an::stretchImage`) carries the same Channel, Shadows, Midtones, and
  Highlights text as Image History; no other metadata block holds the S/M/H
  settings.
- Image History receives **Histogram Transformation** and the visible Channel,
  Shadows, Midtones, and Highlights. It does not record plot resolution, toolbar
  display choices, Preview, mask/inversion, clipping estimates, or hidden
  Auto-stretch results.
- An enabled view mask is sampled and blended after transformation. Its geometry
  and channel compatibility are the shared image-view framework's responsibility.
- The process has no QSettings persistence, presets, saved instances, LUT export,
  or report. Successful full Apply resets the three transfer values and channel;
  most histogram-display toggles remain until the window closes.

## Performance

The pixel transform is `O(width * height * channels)` and uses oneTBB across all
samples. It allocates a working copy - Float64 for a Float64 or 32-bit integer
image, Float32 otherwise - and a result image in the source representation,
beside the framework's destination copy of the source; a one-channel stretch
also holds the untouched channels aside, and RT preview and mask blending add
further full-image copies.

Each histogram recomputation also scans the image and allocates one counter array
per channel. At 16-bit plot resolution that is 65,536 counters per channel,
before render-preparation buffers. High bins, full-size RGB data, and continuous
Preview therefore cost more than the 256-bin default. Plot zoom changes widget
size only.

Sequence processing applies the same snapshot to each frame - measuring each
frame first in Auto-stretch mode - and writes a
temporary replacement sequence, so time scales with frames and pixels while disk
must hold the new sequence. Cancellation is checked by the sequence framework
before each frame and by the engine once per chunk of rows inside each frame's
transform.

## Worked example

- Input condition/provenance and settings: one normalized RGB pixel with
  `R=0.50`, `G=0.25`, `B=0.75`; **Channel=Red**, `S=0.10`, `m=0.40`, `H=0.90`.
- Expected checkpoint: only red is transformed. Its rescaled value is
  `t=(0.50-0.10)/(0.90-0.10)=0.50`, and the rational MTF gives `R'=0.60`.
  Green remains `0.25` and blue remains `0.75`. Input `0.10` maps to black,
  input `0.90` maps to white, and the absolute input
  `0.10 + 0.80*0.40 = 0.42` maps to output `0.50`.
- Inspect at 100%/statistics/history: for an actual run, compare protected and
  unprotected areas, check all-channel min/max and exact-zero/exact-one counts in
  Image Statistics, and confirm a **Histogram Transformation** history step plus
  nonlinear `histogram-mtf` state. Do not treat the UI's binned Clipped fields as
  the exact statistics check.
- Limitations: the values above follow the reviewed equation exactly but do not
  establish visual quality, mask appearance, sequence behavior, or production
  timing on an approved master.

## Warnings and limitations

- Apply is destructive and changes processing state to nonlinear. Finish
  linear-only calibration, measurement, deconvolution, and intended denoising
  first.
- Auto-stretch mode is durable through preview and Apply, but its derived numeric
  S/M/H values remain hidden from the controls, history, presets, and metadata.
- The process offers one shared manual RGB curve or one selected channel. It does
  not store independent manual S/M/H triplets for red, green, and blue.
- The histogram line visibility follows the RT preview display mode, independently
  of the process **Channel** selector. Confirm both before interpreting a plot.
- Clipped counts are quantized, unmasked, endpoint-exclusive estimates; selected-
  channel RGB percentages are divided by all three channels.
- **Absolute scales** has misleading wording: selected enables per-channel local
  normalization and hides relative peak heights.
- Never collapse Shadows and Highlights. The spin boxes permit it, and the
  result is a threshold rather than a usable stretch.
- Highlight clipping is rarely recoverable. Leave `H=1` unless an unused upper
  range has been verified with statistics and source inspection.
- Float64 images are evaluated in double precision. Preserve a high-precision
  linear master and avoid repeated permanent stretches.
- Masks modify final pixels but not the histogram prediction or clipping counts.
  Use exact post-Apply Image Statistics for verification.
- Auto-stretch on a sequence snapshots linked/unlinked mode and stretches every
  frame from its own statistics, as it would stretch that frame alone; use
  manual settings for one curve across the sequence.
- There is no permanent inverse action in this process. Even where a mathematical
  inverse exists, clipped or quantized samples cannot be recovered exactly.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Auto preview is stretched while S/M/H still show `0/0.5/1` | Auto-stretch retains linked/unlinked mode in the execution snapshot but does not publish derived numeric values to the manual controls | This is expected. Verify the preview, record linked/unlinked mode, then Apply; use manual values when exact numeric repeatability is required |
| Auto-stretch is no longer pending | A manual Shadows/Midtones/Highlights or Channel edit, Reset, or successful Apply clears the retained Auto mode | Click the intended Auto-stretch action again and verify the refreshed preview before Apply |
| Clipped percentage seems three times too small for Red/Green/Blue | Numerator covers the selected channel but denominator is width × height × all RGB channels | Use Image Statistics or multiply only for informal interpretation; do not use the UI field as an exact measurement |
| Clipped readout changed after Plot resolution | The Clipped fields count bins at the selected resolution, and a resolution change recounts the source and recomputes them | Compare readouts at one resolution; use Image Statistics for exact counts |
| Histogram still shows all RGB lines while Channel is Red | Plot display channels follow RT preview display mode, not the process Channel selector | Change preview display-channel mode for inspection; keep Channel set to the intended processing scope |
| Selecting Absolute scales hides relative channel strength | Selected state actually enables local per-channel normalization | Clear **Absolute scales** to use one maximum across displayed channels |
| Shadows/Highlights produce a nearly binary image, or the preview reports "midtones parameters must be finite unit values with ordered endpoints" | Endpoints are equal or Midtones is at `0`/`1`; with Shadows above Highlights the engine refuses the transfer. Spin boxes do not enforce a safe interval | Reset; restore `S < H` and an interior Midtones value |
| Preview is slow | Full-image asynchronous transform, histogram rebuild, high bin count, mask blend, and large plot buffers all add work | Clear Preview while editing, return to 256 bins, reset plot zoom, then re-enable for a final check |
| Color balance shifts | A single channel was processed or unlinked auto was used for inspection | Undo; use RGB/manual shared settings or a dedicated color-calibration process before stretching |
| Sequence cancellation takes time | Cancellation is cooperative: the framework checks before each frame and the engine once per chunk of rows, so the current chunk, frame read, or frame write finishes first | Wait for the current checkpoint. The normal cancel path removes the temporary output |

## Related workflow steps

Complete [Background Neutralization](../color-calibration/background-neutralization.md),
[Photometric Color Calibration](../color-calibration/photometric-cc.md), or
[Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md)
while the image is linear. Use [Image Statistics](../../interface/image-statistics.md)
and [Histogram Analysis](../../interface/image-histogram.md) for exact post-Apply
checks. [Generalized Hyperbolic Transformation](gh-transformation.md),
[ArcSinh Stretch](arcsinh-stretch.md), and
[Statistical Stretch](statistical-stretch.md) are alternative stretch models;
do not stack them by habit without checking clipping and processing state. Use
[Curves Transformation](curves-transformation.md) afterward when deliberate
point-controlled tone or color refinement is needed.

The broader order is summarized in [Broadband Imaging](../../workflows/broadband-imaging.md)
and [Narrowband Imaging](../../workflows/narrowband-imaging.md).

## Credits

- The official [XISF 1.0 specification](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html)
  documents display functions and an adaptive display-function algorithm. It is
  background for the MTF and STF terms; AfterNight does not implement the whole
  specification.
- A [PixInsight staff explanation of the midtones transfer function](https://pixinsight.com/forum/index.php?threads/after-doing-luminance-levels-transform-the-image-is-monochrome.741/)
  describes normalized endpoints, the `{m, 0.5}` control point, rational
  interpolation, and the identity at `m=0.5`.
