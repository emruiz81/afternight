---
kind: process
stage: finish
menu: Process > Details > Multiscale Alpha Sharpen
summary: >-
  Sharpens at several scales by raising the opacity of detail layers and compositing them back over the base image,
  with G'MIC.
derived: true
---
# Sharpen Alpha

## Purpose and expected result

Multiscale Sharpen Alpha decomposes the selected image or component into
opacity-weighted detail layers at successively broader scales, increases the
opacity of those layers, and alpha-composites them back over the residual base.
It is not the same model as Unsharp Mask: there is no single radius and no
additive high-pass equation exposed to the user.

The expected result is a same-size still or replacement sequence with stronger
detail over several scales. Channel count normally matches the input, but
configured CFA sequence frames can be auto-debayered to RGB before filtering
and temporary replacement. **Amplitude** controls the layer boost, **Number of
Scales** controls how many octave-like supports are available, **Anisotropy**
changes how the bases are smoothed, and **Amount** blends the complete sharpened
result back with the source. Strong settings can amplify noise, form star rims
or broad halos, exaggerate color edges, and flatten highlights or shadows
through the final `[0,1]` clamp.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Details > Multiscale Alpha Sharpen**; window title **Multiscale Sharpen Alpha** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; sequence auto-debayer can convert configured CFA frames to RGB before filtering; an optional active mask must match target geometry |
| Outputs | Destructively replaces the still or, after successful temporary processing, the sequence; **Take snapshot** creates an auxiliary still; no file is written directly |
| Recommended image state | Usually finite, normalized data after calibration, registration, stacking, gradient correction, color calibration, and noise reduction; test final-scale sharpening on a duplicate |
| Preview support | Asynchronous full-image or movable-area preview, original/result switching, snapshot, and display-channel selection; this process currently exposes no Display Mode or Autostretch Settings controls |
| Sequence support | Yes; numerical parameters and Channel are captured once when Apply starts and used for every frame; temporary output replaces the sequence only after success |
| Mask support | Yes; the active mask blends the Amount-modulated result with the original after filtering |
| External dependency | Bundled G'MIC 3.5.4/CImg runtime, run through AfterNight's optional G'MIC adapter; no separate installation or network access. In a build without G'MIC, Preview and Apply are disabled and their tooltip says why |
| Non-finite samples | An image holding a NaN or infinite sample is refused before G'MIC sees it, and left unchanged |

## Before you start

- Reduce objectionable noise first. Alpha-detail layers do not distinguish real
  small structure from shot noise, ringing, hot-pixel residue, or compression
  edges.
- Debayer CFA data before color sharpening. A CFA still is exposed as one
  grayscale plane and can sharpen the sensor mosaic itself.
- Use finite, normalized samples. The G'MIC result is clamped to `[0,1]` even
  for float or double input. An image holding a NaN or infinite sample is
  refused with "the image holds a sample that is not a finite number, which
  G'MIC cannot process" and left unchanged.
- This process needs G'MIC, which AfterNight normally includes. In a build
  without it, **Preview** and **Apply** are disabled and their tooltip reads
  "This build of AfterNight carries no G'MIC library, so this process cannot
  run."
- Duplicate the target or verify Image History. Apply replaces the target only
  after successful processing.
- Decide between RGB and a luminance/lightness component. The RGB path sends all
  stored channels through the same alpha-layer command; a derived component is
  filtered as one plane and reconstructed afterward.
- Inspect a smooth background, fine stars, a bright high-contrast edge, and
  broad structure at **Zoom 100%**. The embedded preview is linear-only for this
  process, so compare it with a linear target view or account for the difference.

## Procedure

1. Select the target and open **Process > Details > Multiscale Alpha Sharpen**.
   Confirm **Target Image**, choose **RGB** or one deliberate component, and
   leave **Preview** checked.
2. Start with **Amplitude 1.0**, **Number of Scales 4**, **Anisotropy 0.5**, and
   **Amount 1.0**, the values the window opens with and Reset restores.
3. Use **Preview area** around representative fine detail and background. Change
   Number of Scales first: more scales add progressively broader supports.
4. Adjust Amplitude for the alpha-layer boost, then reduce Amount to mix the
   complete result with the source. Amount is the safer final strength control.
5. Compare Anisotropy values while watching real edges and background texture.
   Its bundled branch changes the scale bases; it is not a halo-removal switch.
6. Toggle **Switch image**, inspect **Zoom 100%**, and check the full frame for
   broad halos before Apply. Confirm target, Channel, mask, and inversion.
7. Select **Apply**. For a sequence, monitor progress; Cancel is cooperative
   inside G'MIC, and the original sequence remains until temporary work succeeds.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Amplitude | Double value plus slider, raw G'MIC alpha boost | `1.00` | G'MIC's GUI expresses the same raw value as `100%`; AfterNight displays and passes the raw `0..4` value | `0.00..4.00`; spin step `0.10` with adaptive stepping; slider has 4001 positions | `0` leaves layer opacity unboosted; increasing toward `4` raises opacity multipliers up to `1 + Amplitude` across the scale stack | Enabled while idle; disabled during Apply | Higher values emphasize detail/noise and increase clipping/halo risk; decomposition cost is largely unchanged | Preview/Apply; selected channel/component; still/sequence; before Amount and mask | Captured per execution; Reset and successful Apply return `1.00` |
| Number of Scales | Integer value plus slider, decomposition levels | `4` when a new process opens | None | `1..10`; step `1`; G'MIC limits the effective value to `floor(log2(min(width,height)))` | `1` yields no detail split and is neutral; higher values add bases at sigma `0.25 + 2^k` for `k=0..N-2` (subject to the image-size limit) | Enabled while idle; disabled during Apply | More scales add broader halos/support and more blur, alpha-layer, and composite work; `10` reaches very broad nominal support | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `4` |
| Anisotropy | Double value plus slider | `0.50` | None | `0.00..1.00`; spin step `0.05` with adaptive stepping; slider has 1001 positions | `0` builds each base with Gaussian blur; values above `0` use a bilateral blur with spatial sigma `0.25+2^k`, range sigma `(1-Anisotropy)×100`, then Gaussian blur with sigma `0.5+Anisotropy` | Enabled while idle; disabled during Apply | Changes decomposition shape and cost; high values can preserve/redirect strong edges but can also form directional or edge-local artifacts; normalized AfterNight input makes the fixed range constants an important limitation | Preview/Apply; selected channel/component; still/sequence | Captured per execution; Reset and successful Apply return `0.50` |
| Amount | Double value plus slider, final blend fraction | `1.00` | None | `0.00..1.00`; spin step `0.05` with adaptive stepping; slider has 1001 positions | Shared post-filter blend: `source × (1-Amount) + sharpened × Amount`; `0` is neutral and `1` selects the complete G'MIC result | Enabled while idle; disabled during Apply | Reduces all scale effects together but does not skip G'MIC work at `0`; active mask blends after Amount | Preview/Apply; selected channel/component; still/sequence; before mask | Captured per execution; Reset and successful Apply return `1.00` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive launch gates apply | A wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifetime; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB processes the multichannel G'MIC image; a component filters one converted plane and reconstructs/copies the others | Component choices require RGB; mono selector is disabled; disabled during Apply; the selection is captured when Apply starts | RGB alpha calculation can couple channel extrema and produce color changes; component round trips add buffers, quantization, and possible gamut clipping | Preview/Apply; active mask post-blend; still/sequence | Dialog-local; Reset and successful Apply retain the selected Channel |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables or disables asynchronous full-image or selected-area execution | Process idle | Runs the complete G'MIC decomposition; frequent changes allocate float/layer buffers | Preview only; mask-aware; still/current displayed sequence frame | Window lifetime; Reset unchanged |
| Preview pane | Image viewer | Sharpened result in Linear mode | No execution-autostretch support is enabled for this process | Pan with scroll bars or dragging; toolbar actions below | Shows the mask-aware result without modifying the target | Exists only with Preview | Holds source/result/display buffers plus G'MIC layer workspaces | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly | Preview image required | Useful for broad halos; resampling can hide fine noise/ringing | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around viewport center | Preview image required | Improves local inspection; increases panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces around viewport center | Preview image required | Shows broader context but hides fine artifacts | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for noise, star rims, and color edges | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored and derived RGB/CIE/HSL/HSV/HSI/YCbCr display components | Selects only the displayed component | Separate from process Channel; RGB target required for components | Helps diagnose color shifts without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Switch image | Preview-toolbar action | Sharpened result shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Checked with an initial `600×600` area | None | Checked/unchecked; move/resize overlay | Processes only the area and composites it over the original preview; uncheck for full-image preview | Preview required | Reduces tuning cost but can hide broad-scale halos; Apply is always full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings and opens `<target> - Multiscale Sharpen Alpha - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Enabled, including Amplitude/Amount `0` and Scales `1` | None | One action | Captures four numerical settings, locks the target, and processes the full still or every sequence frame | Target must lock; target, Channel, and process controls are disabled during Apply | G'MIC receives a cooperative abort flag; channel conversion, mask, sequence encode, and temporary storage add cost | Destructive still/sequence replacement after success | Records four settings and, for a colour image, a Channel label; successful Apply resets numbers, including Scales `4`, but retains Channel |
| Reset | Button/action | Enabled while idle | None | One action | Restores Amplitude `1`, Scales `4`, Anisotropy `0.5`, and Amount `1` | Does not change Target, Channel, Preview, mask, or preview toolbar; disabled during Apply | Checked Preview schedules another complete execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Requests active preview cancellation; G'MIC cancellation is cooperative | Window/execution | Process settings do not persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; task and frame text update around temporary work | Sequence target only | G'MIC polls the abort flag; cancellation after return discards the current result | Captured numerical settings and Channel are used for every frame | Cancel/error removes temporary output; success replaces sequence, records one history step, and resets numbers | Execution snapshot only |

## How the algorithm works

### Alpha-detail decomposition

AfterNight checks the selected image or component for non-finite samples and
passes it through its G'MIC adapter, one G'MIC session per run, as planar
float32 fractions of full scale to bundled G'MIC 3.5.4 with this command:

```text
sharpen_alpha Amplitude,NumberOfScales,Anisotropy
```

The unexposed G'MIC `minimize_alpha` argument therefore keeps its default `1`.
G'MIC separates any existing opacity, then calls `split_alpha` without spatial
subsampling. For `N > 1`, it repeatedly creates a smoother base and represents
the difference between the previous layer and that base as an RGBA alpha-detail
layer. The exact alpha difference is chosen so alpha-compositing the layer over
its base reconstructs the input.

For scale index `k=0..N-2`, the base support begins at

\[
\sigma_k=0.25+2^k\ \text{pixels}.
\]

At Anisotropy `0`, this is a recursive Gaussian blur using G'MIC's default
Neumann boundary. At values above `0`, the bundled script instead applies a
bilateral blur with spatial sigma `sigma_k`, range sigma
`(1-Anisotropy)×100`, and then a Gaussian blur with sigma
`0.5+Anisotropy`. The operation is recursive: each broader base is derived from
the preceding base, not directly from the original.

G'MIC then increases opacity across the detail stack from an unboosted factor
through a maximum of `1 + Amplitude`, clamps layer values/opacity to its
`0..255` alpha-compositing domain, and uses `merge_alpha` to reconstruct the
image. Amplitude `1` is the raw value corresponding to `100%` in G'MIC's own GUI;
AfterNight's maximum `4` corresponds to `400%`. Amplitude `0` leaves the opacity
multipliers at `1`, so reconstruction is neutral apart from floating-point
rounding and the `[0,1]` clamp. Number of Scales `1` performs no split and is
likewise neutral.

### Amount, channels, mask, and range

Let `I` be the source and `S` the clamped G'MIC result. The shared process layer
applies Amount `a` as

\[
M=(1-a)I+aS.
\]

Thus Amount `0` returns the original samples after G'MIC has still run, `0.5`
halves the complete source-to-sharpened difference, and `1` selects `S`. If the
effective normalized mask weight is `m`, the final result is

\[
O=(1-m)I+mM.
\]

Mask inversion replaces `m` by `1-m`. Mask blending therefore occurs after the
global Amount blend.

Integer types are converted to float32 `[0,1]`, a tightly packed float32 image
is handed over in place, and double is narrowed to float32. Every successful
G'MIC result is clamped to `[0,1]` before conversion back to the original pixel
type, rounded to nearest and held to the type's range. The bundled alpha
script contains fixed `0..255` and range-sigma constants even though AfterNight
normally supplies normalized data. This scale mismatch is part of the current
implementation: do not assume Anisotropy matches the visual behavior of the
standalone G'MIC plug-in on its usual sample scale.

RGB stays a multichannel planar G'MIC image. Alpha differences use vector
channel comparisons and a shared opacity for each detail layer, so a strong edge
in one channel can influence the layer opacity used to reconstruct all channels.
A selected derived Channel is converted to one plane, processed by the same
command, and reconstructed through the shared component path. A derived
component's reconstruction saturates an integer image at its type's range and
keeps a floating image's values outside `[0, 1]`.

### Boundaries, errors, and cancellation

- Gaussian scale bases use Neumann boundary conditions (edge samples extend
  with zero normal derivative). The bilateral branch is followed by a Neumann
  Gaussian. Edge support is therefore not the reflective model used by Unsharp
  Mask.
- G'MIC command failures are reported with G'MIC's message; a still is
  committed only after success.
- An image holding NaN or infinity is refused before the command runs and is
  left unchanged.
- G'MIC receives a live abort flag, so Cancel is cooperative within the
  command. AfterNight also checks cancellation after the call and discards the
  result.
- Sequence processing writes temporary output. Cancel/error removes it; success
  replaces the sequence and records one history step.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Scales × Amplitude | Scales `4`, Amplitude `1`; compare Scales `5` deliberately | Only the finest visible structures change | Broad star halos, crunchy multiscale contrast, clipped peaks | Choose largest useful scale first, then raise/lower Amplitude |
| Amplitude × Amount | Keep Amplitude near `1`, reduce Amount below `1` | Detail difference vanishes as Amount approaches `0` | All layer boosts arrive at full strength and clipping becomes obvious | Establish decomposition with Amplitude; use Amount as final global mix |
| Anisotropy × Scales | Compare `0` and `0.5` at modest Scales | Gaussian branch may soften edge localization | Bilateral/extra-blur branch can produce edge-local transitions or unexpected scale behavior | Fix Scales, compare Anisotropy endpoints, then retune Amplitude |
| RGB × luminance component | Compare RGB with **CIE Y (Luminance)** or **CIE L* (Lightness)** | Luminance-only can leave chromatic noise | Shared alpha opacity can exaggerate a dominant channel edge | Inspect View Channel, choose process Channel, then tune strength |
| Amount × mask | Tune at modest Amount before localizing | Protected regions change little | Hard mask transitions reveal inconsistent sharpening | Tune globally, verify mask/inversion, inspect the full frame |

These are source-derived starting points, not executed image-quality
measurements. Sampling, seeing, noise, prior deconvolution, and final resize all
change the useful scale range.

## Inputs, outputs, and metadata

- Output dimensions match the input. Channel count normally matches, except that
  sequence auto-debayer can convert configured CFA frames to RGB before the
  filtered temporary replacement is written. Source pixel type is kept;
  integer results are rounded and double data is processed at float32
  precision.
- The G'MIC leg is clamped to `[0,1]`. At Amount `1`, out-of-range float
  samples are lost; partial Amount can retain part of the original out-of-range
  contribution; Amount `0` restores the source after unnecessary filter work.
- The result keeps the source image's header cards and metadata. The process
  does not update linear/nonlinear processing-state provenance.
- Image History records Amplitude, Number of Scales, Anisotropy, and Amount, and,
  for a colour image, ` in channel: <channel>` with the selected Channel,
  derived components included.
- The Channel selector is not part of `SharpenAlphaParams`, but the run captures
  it when Apply starts, together with the four numerical settings, and uses it
  for every sequence frame.
- A still CFA mosaic follows the grayscale path. Sequence preprocessing may
  auto-debayer configured CFA frames before temporary replacement.
- Successful Apply resets the four numbers to the values the window opens with,
  while retaining Target, Channel, Preview, mask, and preview-toolbar state.

## Performance

- Work scales with pixel count, channel count, and the effective number of
  scales. Each added level creates a broader blur/bilateral base, an alpha-detail
  layer, and an additional alpha composite.
- Gaussian support doubles by scale (`1.25`, `2.25`, `4.25`, `8.25`, ... pixels),
  so high scale counts add much broader work than a linear-radius progression.
  G'MIC/CImg may use platform parallelism internally.
- The bilateral branch adds guide/range computation plus a second Gaussian per
  scale. Amount does not reduce decomposition work, even at `0`.
- Memory includes source/destination float images, multiple RGBA detail layers,
  smoother bases, component/mask buffers, and preview copies. Sequences also
  need temporary disk roughly comparable to an output sequence.
- Preview area reduces tuning work; Apply remains full-frame. No approved
  astronomical performance workload was run, and peak memory was not measured.

## Worked example

- **Input condition and settings:** Finite normalized data; Scales `4`,
  Amplitude `1`, Anisotropy `0`, Amount `0.5`.
- **Scale checkpoint:** G'MIC forms three recursive Gaussian bases with nominal
  sigmas `1.25`, `2.25`, and `4.25` pixels. Scales `5` adds an `8.25`-pixel
  base.
- **Blend checkpoint:** If a source sample is `0.20` and the clamped reconstructed
  sample is `0.32`, Amount `0.5` gives `0.20×0.5 + 0.32×0.5 = 0.26` before mask.
- **Inspect at 100%/history:** Compare star rims, background noise, and separate
  display channels; after Apply, verify four numerical settings and the available
  Channel label in history.
- **Limitations:** No visual improvement, production runtime, sequence output,
  or memory claim is made without approved-data execution.

## Warnings and limitations

- This is alpha-layer boosting, not deconvolution. It cannot recover information
  absent from the input.
- Noise, hot-pixel residue, ringing, and compression edges are detail and can be
  boosted with real structure.
- More scales add broad supports and can create wide halos around stars or large
  transitions. Amplitude controls layer opacity, not a physical PSF or radius.
- Anisotropy switches to a different bilateral-plus-Gaussian construction for
  every nonzero value. It is not continuous with the exact `0` code path in all
  numerical details and is not a Reduce Halos control.
- The bundled command's fixed `0..255` alpha/range constants operate on
  AfterNight's normally normalized float input. Standalone G'MIC presets are not
  numerically interchangeable without validation.
- RGB detail layers share opacity across channels; strong single-channel edges
  can affect color. Derived component round trips can shift gamut and, in an integer image, clip
  at its type's range (a floating image keeps values outside `[0, 1]`).
- Every G'MIC result is clamped to `[0,1]`; highlights, shadows, and HDR float
  data are not safely preserved. Double input is narrowed. An image holding NaN
  or infinity is refused by name; without G'MIC in the build, the process cannot
  run and Preview and Apply stay disabled.
- The process preview is linear-only. It does not expose Display Mode or
  Autostretch Settings, so comparison with an autostretched target can mislead.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Amount `0` is unchanged but slow | The full G'MIC decomposition/clamp runs before the shared Amount blend restores the source | Disable Preview or close the process when no filter is wanted |
| Scales `1` is unchanged | `split_alpha` only decomposes when the effective scale count is greater than one | Raise Scales to at least `2` |
| Broad halos surround stars | High Scales and Amplitude boost broad alpha-detail layers | Reduce Scales first, then Amplitude or Amount; use a mask |
| Noise becomes crunchy | Fine alpha layers boost noise with signal | Denoise first; reduce Amplitude/Amount; protect background |
| Colors shift or rims appear | RGB layers use shared opacity derived from multichannel comparisons | Try CIE Y/L*; inspect View Channel; lower strength |
| Anisotropy behaves unexpectedly | Every nonzero value selects the bilateral-plus-extra-Gaussian branch and fixed range constants act on normalized data | Compare exact `0` with modest nonzero values; do not copy standalone plug-in presets blindly |
| Preview looks darker than target | This process preview has no execution autostretch controls | Compare in Linear mode or use a snapshot/duplicate under matched display settings |
| Cancel waits briefly | G'MIC abort handling is cooperative | Wait for its next polling point; use a smaller Preview area next time |
| Error: the image holds a sample that is not a finite number | The image has a NaN or infinite sample, which G'MIC cannot process | Repair or crop the non-finite samples, then retry; the image was left unchanged |
| Preview and Apply are disabled; tooltip says the build carries no G'MIC library | This build was made without G'MIC | Use a build that includes G'MIC, or another sharpening process |
| A G'MIC error is reported | G'MIC command failure, invalid data, or memory exhaustion | Verify finite normalized data and memory; retry on a duplicate |

## Related workflow steps

Complete calibration, registration, integration, gradient correction, color
calibration, and appropriate [noise reduction](../noise-reduction/wavelet-denoise.md)
before final sharpening. Compare [Unsharp Mask](unsharp-mask.md) for a single
Gaussian residual model or [High Pass Filter](high-pass-filter.md) when a neutral
detail layer is required. Use [Image History](../../interface/image-history.md)
to compare/revert Apply and [Saving and Exporting](../../interface/saving-and-exporting.md)
only after the final output scale has been checked.

## Credits

- [G'MIC `sharpen_alpha` command reference](https://gmic.eu/reference/sharpen_alpha.html) — official command purpose, arguments, and upstream defaults.
- [G'MIC `split_alpha` command reference](https://gmic.eu/reference/split_alpha.html) — official alpha-detail decomposition and reconstruction contract.
- [G'MIC `sub_alpha` command reference](https://gmic.eu/reference/sub_alpha.html) — official minimal-opacity difference definition used by the decomposition.
- [G'MIC `blur` command reference](https://gmic.eu/reference/blur.html) — official Gaussian/Deriche and boundary-condition defaults; the bundled script selects the default Gaussian and Neumann behavior.
