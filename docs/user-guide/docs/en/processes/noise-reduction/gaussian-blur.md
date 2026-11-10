---
kind: process
stage: any
menu:
  - Process > Details > Gaussian Blur
  - Process > Filters > Gaussian Blur
summary: >-
  Blurs an image with a Gaussian kernel of the standard deviation you choose, softening fine structure.
data_in: either
derived: true
---
# Gaussian Blur

## Purpose and expected result

Gaussian Blur applies a linear, isotropic low-pass filter. **Apply** convolves
the image with a sampled Gaussian kernel of the requested standard deviation,
one pass along the rows and one down the columns, at every Sigma the dialog
offers. While you drag **Sigma** the preview blurs what the preview view
shows - the image reduced to the view's size, by Sigma divided by the
reduction, or the part shown at 1:1 and closer with the blur's reach around
it - so that the preview keeps up with the slider; beyond Sigma `32` there,
the dragged preview blurs with three box passes that stay close to the
Gaussian and cost the same at any Sigma. The preview you rest on is computed
on the whole image and is what Apply writes.

The expected result is a same-size, same-type still or replacement sequence with
reduced high-frequency contrast. Noise, stars, edges, and fine texture all blur;
there is no edge-aware term. Use it deliberately for masks, support layers,
scale separation, or controlled smoothing—not as detail-preserving denoise.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Details > Gaussian Blur** or **Process > Filters > Gaussian Blur**; both entries are the same action |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence; a still CFA mosaic is treated as one grayscale sample plane unless debayered first; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions, channel count, and source pixel type are retained unless a selected component reconstructs RGB; no file is written directly |
| Recommended image state | Any finite image or mask whose fine structure should be softened; for a science master, prefer linear data and protect stars/edges with a mask when their flux profile matters |
| Preview | Asynchronous preview by default: over what the preview view shows while a slider is held - the image reduced, blurred by Sigma over the reduction, or the shown part at 1:1 and closer, by the exact Gaussian up to Sigma `32` there and by three box passes beyond - and the exact Gaussian over the whole image once it settles; optional movable Preview area, original/result switching, snapshot, display-channel selection, and display-only autostretch |
| Sequence support | Yes; one captured Sigma/channel/mask state is applied frame by frame to temporary storage before replacement |
| Mask support | Yes; the active mask blends the blurred result with the original |
| External dependency | None; the blur is the AfterNight engine's own, with no separate installation, model, or network access |

## Before you start

- Decide whether you need ordinary smoothing or edge-preserving denoise.
  Gaussian Blur averages across every structure; compare [Bilateral Filter](bilateral-filter.md),
  [Median Filter](median-filter.md), or a dedicated denoiser when edges or impulses
  must be treated differently.
- Use finite samples. The process does not repair NaN or infinity.
- Duplicate the target or verify Image History. Apply replaces the target after
  success, and successful Apply resets Sigma to `0` and Channel to **RGB**.
- Inspect at **Zoom 100%** under one display stretch. A fitted view can hide lost
  faint stars and broadened cores.
- For masks or auxiliary images, verify the mask is not itself unintentionally
  masked by another active image mask.

## Procedure

1. Select the target and open either **Process > Details > Gaussian Blur** or
   **Process > Filters > Gaussian Blur**. Confirm **Target Image** and Channel.
2. Increase **Sigma** from `0` until **Apply** becomes available. Every
   positive value blurs when applied; while the slider is held at a view that
   reduces the image, a very small Sigma shows little change, since the
   reduced preview blurs by Sigma divided by the reduction, and the preview
   you rest on shows the whole image's blur.
3. Start near Sigma `1` for fine-pixel smoothing or use the scale required by
   the mask/support-layer workflow. Leave Preview enabled and compare at 100%.
4. Toggle **Switch image** and inspect background noise, star cores, narrow
   structures, edges, and frame boundaries. Use **Preview area** to isolate a
   representative region when useful; it starts unchecked.
5. Verify the active mask and inversion state. The same linear blur is computed
   first, then the shared mask path blends it with the original.
6. Select **Apply** only after checking the full frame. The applied blur is the
   exact Gaussian the settled preview shows. For a sequence, monitor progress;
   Cancel stops the blur between rows and preserves the original.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sigma | Double value plus slider, requested standard deviation in pixels per axis | `0.00` | None | `0.00..100.00`; two decimals; adaptive decimal spin step based on nominal `0.50`; slider resolution `0.10`; used at full precision | Apply and the settled preview: the exact Gaussian of that standard deviation. Higher values blur broader scales | Apply disabled exactly at `0`; a preview held at a reduced view blurs by Sigma divided by the reduction, so a small value shows little change until the slider is released | Larger values remove broader structure; Apply cost grows with Sigma, and above `32` the blur is split into several exact passes | Preview/Apply; selected channel/component; still/sequence; before mask | Captured per execution; Reset and successful Apply return `0.00` |
| Target Image | Image selector | Attached/active target | None | Open eligible image or sequence | Changes source, Channel list, Preview, active mask, and Apply destination | Active image required; duplicate/exclusive gates apply | Wrong target is destructively replaced after success | Preview/Apply source and active mask | Target lifecycle; Reset does not change it |
| Channel | Combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | RGB blurs each stored channel independently with the same kernel; a component blurs one plane and reconstructs/copies the others | Component choices require RGB; mono selector disabled | Independent RGB smoothing can broaden color fringes; component conversions add buffers and can alter color | Preview/Apply; active mask post-blend; still/sequence | Captured by execution; Reset and successful Apply restore RGB for RGB targets |
| Preview | Checkbox | Checked | None | Checked/unchecked | Enables/disables asynchronous full-image or selected-area execution | Process idle | Runs the blur over what the preview view shows while a slider is held - exact up to Sigma `32` there, box passes beyond - and the exact blur over the whole image once it settles; repeated full-frame updates allocate working buffers | Preview only; mask-aware; current still/current displayed sequence frame | Window lifetime; Reset does not change it |
| Preview pane | Image viewer | Current result | None | Pan with scroll bars/dragging; toolbar actions below | Shows the mask-aware result without modifying the target | Exists only with Preview | Holds source/result and display buffers in addition to blur buffers | Preview only | Window lifetime |
| Fit To Window | Preview-toolbar action | Preview fitted when first displayed | None | Click or **Ctrl+Shift+F** | Fits the whole image uniformly in the viewport | Preview image required | Good overview; resampling hides pixel-scale blur | Preview display only | Viewer lifetime |
| Zoom In | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom within viewer limits | Enlarges around viewport center | Preview image required | Improves local inspection; more panning | Preview display only | Viewer lifetime |
| Zoom Out | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom within viewer limits | Reduces around viewport center | Preview image required | More context, less fine detail | Preview display only | Viewer lifetime |
| Zoom 100% | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows the viewer's one-pixel scale | Preview image required | Preferred for judging broadened stars, lost fine structure, rounding, and boundary differences | Preview display only | Viewer lifetime |
| View Channel | Preview-toolbar combo box | **RGB** for RGB; disabled **Grayscale** for mono | None | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X**, **CIE Y (Luminance)**, **CIE Z**, **HSL Hue**, **HSL Saturation**, **HSL Lightness**, **HSV Hue**, **HSV Saturation**, **HSV Value**, **HSI Hue**, **HSI Saturation**, **HSI Intensity**, **YCbCr Luma**, **YCbCr Blue Chroma**, **YCbCr Red Chroma** | Selects only the displayed stored/derived component | Separate from process Channel; RGB target required for components | Reveals channel blur without changing pixels | Preview display only | Viewer lifetime; not in process history |
| Display Mode | Preview-toolbar combo box | Inherits target **Linear** or **Autostretch** when preview is created | None | **Linear**, **Autostretch** | Changes display only; Autostretch uses cached full-target source statistics | Autostretch enables settings action | Aggressive display stretch can exaggerate residual noise or hide broad gradients | Preview display only | Preview-view state; not in process history |
| Autostretch Settings | Preview-toolbar dialog action | Disabled in Linear mode; inherits target-view settings | Shared-view values load when opened | Opens Target Background, Shadows Clipping, linked channels, neutralization/color calibration, saturation/background, brightness, contrast, banner, Reset, and Close; **Apply Stretch** is hidden | Tunes preview display only | Autostretch mode only; disabled while dialog open | Can change visual judgment without changing blur | Preview display only | Viewer-scoped; not in Gaussian parameters |
| Switch image | Preview-toolbar action | Blurred preview shown | None | Click; **Space**; right-click; **Ctrl+Shift+O** | Toggles result/original | Preview required | Comparison only; no recomputation | Preview display; still/current frame | Preview-window lifetime |
| Preview area | Preview-toolbar checkbox | Unchecked | None | Checked/unchecked; move/resize overlay | When checked, processes a movable area and composites it over the original preview | Preview required | Reduces tuning work; full Apply remains full-frame | Preview and Take snapshot; mask cropped consistently | Preview-window lifetime; Reset unchanged |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes settings with the exact blur Apply uses and opens `<target> - Gaussian Blur - SNAPSHOT` | Preview required | Adds a complete execution and auxiliary image; display stretch is not baked in | New still; target unchanged; area/mask honored | Each action creates a separate image view |
| Apply | Button/action | Disabled while Sigma is exactly `0`; enabled for any positive UI value | None | One action | Captures Sigma and Channel, then processes the full still or every sequence frame | Target must lock; all controls disabled during commit | Two separable exact passes (more above Sigma `32`), component conversion, mask, sequence encode, and temporary storage add cost | Destructive still/sequence replacement after success | Records history, then resets Sigma and RGB Channel |
| Reset | Button/action | Enabled while idle | None | One action | Restores Sigma `0`; restores **RGB** for RGB targets | Does not reset Target, Preview, mask, or preview toolbar | Disables Apply; checked Preview schedules an identity execution | Configuration only | Immediate; also invoked after successful Apply |
| Close | Button/action | Enabled while idle | None | One action | Closes without applying current settings | Disabled during Apply | Active preview cancellation is requested and reaches the blur between rows | Window/execution | No process settings persist after destruction |
| Sequence progress / Cancel | Shared per-process/frame progress and Cancel | Created for sequence Apply | None | Cooperative Cancel; frame/task text updates around temporary processing; a blur composed of several passes also reports each of them | Sequence target only | The blur checks for Cancel between rows and tiles, so a frame stops promptly | Sequence Apply; captured Sigma, Channel, and mask apply to every frame | Cancel removes temporary output; success replaces sequence, records one history step, and resets Sigma/Channel |

## How the algorithm works

An ideal one-dimensional Gaussian with standard deviation `sigma` has weight

\[
G(x)=\frac{1}{\sqrt{2\pi}\sigma}
\exp\left(-\frac{x^2}{2\sigma^2}\right).
\]

A true two-dimensional isotropic Gaussian is separable: applying the same 1-D
kernel horizontally and vertically produces the 2-D result.

### Apply: the exact Gaussian

Apply samples the density at each pixel centre out to four standard deviations
(radius `ceil(4 × Sigma)`), divides every tap by their sum so a flat area stays
exactly flat, and convolves along the rows and then down the columns. The
engine's general convolution stops at a radius of `128` pixels, which four
standard deviations reach at Sigma `32`. A wider Sigma - the dialog offers up
to `100` - runs as one kernel of its own radius on the engine's compiled
single-precision path, which takes a kernel up to `4096` pixels wide over an
image more than twice its radius wide and high: one kernel of radius `400` at
Sigma `100` on a frame at least `802` pixels each way. On a smaller image the
blur is composed of several exact passes of equal width whose variances add up
to `Sigma²`, each reported as progress; because Gaussians compose exactly, the
result is still the Gaussian of the requested Sigma, apart from the small tail
each pass's four-sigma cut leaves out.

### Preview: the Gaussian over what the view shows

While you hold the slider, a frame runs over what the preview view shows
rather than the whole image: at a zoom that reduces the image by a factor `r`,
over the image reduced by `r` - each pixel the mean of its block - blurred by
`Sigma / r`, which is the reduction of the whole blur to within the averaging
of the reduction; at 1:1 and closer, over the part of the image the view
shows, read with the blur's reach around it so that the part's edges are
blurred as they are in the whole image. Up to a Sigma of `32` in the frame
(`Sigma / r` over a reduction) that blur is the exact Gaussian, reaching
`4 × Sigma` pixels. Beyond it, a dragged frame blurs with three running-sum box
passes along each axis, which stay within a few parts in a thousand of the
Gaussian and whose cost does not grow with Sigma, where the exact kernel's
does. Once you release the slider, or a typed value has rested for a moment,
the preview is computed on the whole image by the exact Gaussian and is what
Apply writes.

Each stored RGB channel is processed separately. The shared Channel dispatcher
can instead extract one stored or derived component, blur that one plane, and
reconstruct/copy the rest. There is no range weight, edge test, adaptive scale,
or cross-channel covariance.

### Boundaries and precision

The blur reflects the image at its edge without repeating the edge sample, so
a neighbour outside the frame is its mirror image and the result is the
Gaussian of the mirrored image. A preview over the part shown at 1:1 reads the
kernel's reach around that part, so its edges are the whole image's, not the
part's. Inspect frame edges when the boundary values matter.

Sigma is used at full precision. A 32-bit float image runs the single-precision
kernels on its own samples where the processor offers them, which agree with a
double-precision pass to float rounding; a 64-bit float image accumulates every
tap in double precision. An integer image is worked on a single-precision copy
holding each sample as a fraction of full scale - exactly, for every integer
type up to 16 bits - blurred there, and converted back once, rounded to the
nearest code and held to the type's range; a blur above Sigma `32` works on
the same copy for every type. Every representation keeps its sample type.

If a mask is enabled, the shared path blends the fully blurred result with the
source after Channel reconstruction. The process has no separate Amount control.
A derived component's reconstruction saturates an integer image at its type's
range and keeps a floating image's values outside `[0, 1]`.

## Parameter interactions and starting points

- **Preview × Apply** agree once the preview settles. While a slider is held
  at a view that reduces the image, the preview is the blur of the reduced
  image, which is the reduction of the whole blur to within the reduction's
  own averaging; release the slider to see the whole image's result.
- **Sigma small against the reduction**: while a slider is held at a view
  that reduces the image by `r`, the preview blurs by `Sigma / r`, so a Sigma
  below about `r / 2` shows little change until the slider is released; Apply
  blurs by the requested amount.
- **Channel × mask** determines which samples actually change. RGB blurs all
  stored channels independently; one component reconstructs the others first;
  the active mask then blends the complete result.
- **Pixel type × rounding**: an integer image is blurred on a single-precision
  copy and rounded once when it is written back, so weak blurs are not eroded by
  repeated rounding.
- **Large Sigma × cost and boundaries**: Apply's cost grows with Sigma; above
  `32`, on an image too small for one wide kernel, each doubling of the
  variance adds a pass. The reflected-edge region also widens with Sigma.
- Gaussian smoothing is compositional: two applied runs at `sigma1` and
  `sigma2` approximate one run at `sqrt(sigma1²+sigma2²)`, apart from edge
  reflection and integer rounding between the runs.

## Inputs, outputs, and metadata

- Output dimensions, channel count, and source pixel type are retained. The
  filter writes a new result; it is not run in place on the target.
- The result keeps the source image's header cards and metadata.
- The process does not update linear/nonlinear processing-state provenance; a
  linear image stays recorded as it was.
- Still Image History records `Adjust sigma to <value>` and, for a colour
  image, ` in channel: <channel>`.
- A still CFA mosaic is treated as one plane, which mixes same-color and
  different-color filter positions spatially; debayer first unless blur of the
  mosaic itself is intentional. Sequence preprocessing may auto-debayer
  configured CFA frames before replacement.
- Sequence frames use one captured Sigma/Channel state, are processed to
  temporary storage, and replace the original sequence only after success.
  Cancel/error removes temporary output.
- **Take snapshot** creates a still from the current preview scope without
  changing the target or baking display autostretch into pixels.

## Performance

For width `W`, height `H`, and processed channels `C`, Apply's exact blur
costs two separable passes of `2 × ceil(4 × Sigma) + 1` taps each:
approximately `O(W × H × C × Sigma)`, still far below a direct
two-dimensional convolution. Above Sigma `32` the one wide kernel keeps that
cost; on an image too small for it, every pass costs as much as one blur at
Sigma `32`. A preview while a slider is held costs the blur of what the view
shows - the image reduced, or the part shown at 1:1 - and beyond Sigma `32`
there the box passes cost the same at any Sigma; the preview you rest on costs
what an Apply does.

The blur runs on the threads the operation is granted, one channel at a time.
A non-float or interleaved image is gathered into a working plane, and a blur
of several passes holds one more working copy of the plane. Preview, component
conversion, masks, destination, and sequence encoding add buffers; sequence
mode also needs temporary disk roughly comparable to an output sequence. No
approved astronomical workload has been timed.

## Worked example

These calculations come from the current kernel mapping. They were not executed
on an approved astronomical image and make no visual claim.

```text
Sigma   Apply: exact kernel radius / passes   Preview over the image reduced by 4: Sigma / 4, radius
 0.50   2 / 1                                 0.125, 1
 1.00   4 / 1                                 0.25, 1
 2.00   8 / 1                                 0.5, 2
 5.00   20 / 1                                1.25, 5
40.00   160 / 1 on a frame at least 322 px     10, 40
        each way; 114 / 2 (each pass Sigma
        28.28) on a smaller one
```

For Sigma `2`, Apply convolves with a 17-tap Gaussian along the rows and again
down the columns; a preview held over the image reduced by `4` convolves the
reduction with a 5-tap Gaussian. The focused tests hold Apply to a
double-precision Gaussian computed independently - within `2e-6` at Sigma `1.5`
and `6`, within `2e-6` at Sigma `40` on a `400×360` frame, which one kernel
blurs, and within `2e-4` at Sigma `40` on a `112×104` one, where two passes
compose it - and hold a frame over the image reduced by `2` to the Gaussian of
half the Sigma within `2e-6`. That is evidence for those fixtures, not a
universal error bound.

## Warnings and limitations

- The preview you rest on is the exact Gaussian and is what Apply writes; while
  a slider is held the preview is the blur of what the view shows, so judge
  fine detail after releasing the slider, at 100%, or with **Take snapshot**.
- Apply is enabled for any Sigma above zero and blurs at every such value; a
  preview held at a reduced view shows little change for a Sigma below about
  half the reduction.
- The filter is not edge preserving. It broadens stars, halos, dust lanes, mask
  edges, and noise together and can create visible soft halos when a blurred
  layer is recombined strongly.
- Mirror handling avoids a constant black border, but large scales pull
  reflected structure into the border region.
- A wide Sigma costs more: the kernel's radius is `4 × Sigma`, and on an image
  too small for one kernel that wide, above `32` each doubling of the variance
  adds a full pass.
- RGB channels are blurred independently. Spatial color fringes can broaden;
  derived-component round trips can alter color, and an integer image's
  reconstruction saturates at its type's range, while a floating image's keeps
  values outside `[0, 1]`.
- NaN and infinite samples are not repaired; the blur spreads them into their
  neighbours.
- Successful Apply resets Sigma to `0` and RGB targets to Channel **RGB**. Target,
  Preview, mask, and preview-toolbar state remain.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Gaussian Blur appears under two menus | The same QAction is intentionally added to Details and Filters | Use either route; both launch the same process |
| A small positive Sigma changes little in the preview while the slider is held | At a reduced view the held preview blurs by Sigma divided by the reduction | Release the slider, view at 100%, or use **Take snapshot** |
| Edges and stars soften too much | Gaussian has no range/edge protection | Reduce Sigma, use a mask, or compare Bilateral/TV/Wavelet denoise |
| Border differs from the interior | The image is mirrored at its edges, so border pixels average reflected structure | Inspect the full frame and reduce Sigma or pad/crop in a controlled external workflow |
| The preview changes when the slider is released | The held preview is the blur of the reduced image or of the shown part; the settled preview is the whole image's | Expected; the settled preview is what Apply writes |
| Channel returns to RGB after Apply/Reset | Process Reset explicitly restores RGB for RGB targets | Re-select the intended component for the next run |
| Preview differs from Apply by more than that | Preview area, mask, target/channel, or display stretch changed | Match all controls and one Display Mode; Apply always processes the full target |
| Apply is slow at a large Sigma | Above `32` the blur is composed of several exact passes | Watch the progress; reduce Sigma or work on a binned copy for very broad support layers |

## Credits

- [Kovesi, “Fast Almost-Gaussian Filtering,” DICTA 2010](https://www.peterkovesi.com/papers/FastGaussianSmoothing.pdf)
  is the primary reference for selecting repeated box widths to approximate a
  requested Gaussian at near-fixed per-pixel cost; the engine offers that
  approximation to callers whose result is only displayed, and this dialog does
  not use it.
- [Basile Fraboni's FastGaussianBlur implementation](https://github.com/bfraboni/FastGaussianBlur)
  is the MIT-licensed implementation whose fixed three passes and mirror policy
  the engine's approximation keeps.

## Related workflow steps

- [Bilateral Filter](bilateral-filter.md) adds a sample-difference weight to
  reduce mixing across sufficiently strong edges.
- [Median Filter](median-filter.md) replaces local samples by order statistics
  and is often a better first comparison for impulse noise.
- [Wavelet Denoise](wavelet-denoise.md) separates scales explicitly rather than
  smoothing every structure at one spatial scale.
- [Process Windows](../../interface/process-windows.md), [Image Viewer](../../interface/image-viewer.md),
  [Masks](../../interface/masks.md), and [Image History](../../interface/image-history.md)
  explain the shared controls and verification workflow.
