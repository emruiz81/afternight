---
kind: process
stage: noise
menu: Process > Noise Reduction > Total Variation Denoise Filter
summary: >-
  Reduces noise while keeping strong edges, by finding a piecewise-smooth image close to the source.
data_in: either
---
# Total Variation Denoise

## Purpose and expected result

Total Variation Denoise seeks a piecewise-smooth image while penalizing departure
from the source. Unlike a blur, it can preserve strong boundaries while reducing
small gradients. AfterNight offers two solvers for the same Rudin-Osher-Fatemi
(ROF) objective: split Bregman and Chambolle's dual-projection method.

The expected result is a same-size, same-type still or sequence with smoother
backgrounds and retained prominent edges. Total variation does not understand
stars, filaments, or texture. Strong settings can turn gradual structure into
flat terraces, round small features, and create a cartoon-like or stair-stepped
appearance. Compare at 100% with one display stretch and inspect both background
and faint structure.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Noise Reduction > Total Variation Denoise Filter** |
| Enabled when | An image or sequence is active, the same process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One grayscale or RGB still or sequence of any sample type, solved in its stored units at single precision - an integer image's weight and tolerance are restated in its own codes, so it denoises as its normalized copy would; an optional active mask must match target geometry |
| Outputs | Destructively replaces a still or, after successful temporary processing, a sequence; **Take snapshot** creates an auxiliary still; dimensions and source pixel type are retained; component modes reconstruct RGB; no file is written directly |
| Recommended image state | Usually linear or early nonlinear, after calibration/registration/stacking and before a strong stretch; also valid later when judged in that state |
| Preview support | Asynchronous RT preview using the same solver and mask path as Apply; a selectable preview area and display-only autostretch are available |
| Sequence support | Supported by the generic temporary replacement-sequence path with per-frame processing, progress, and cooperative cancellation at every solver iteration; configured CFA auto-debayer can produce RGB frames |
| Progress and cancellation | Both methods report progress as their iterations run and check Cancel once per iteration, so a preview, still, or sequence frame stops promptly |
| Mask support | Supported for preview, still Apply, and sequence Apply by post-solver blending; white applies, black protects, gray blends, and **Invert Mask** reverses weights |
| External dependencies | None; AfterNight's engine solves both methods natively. No Python interpreter, no scikit-image, and no global lock: Total Variation runs alongside other processes |

## Before you start

- Finish calibration, registration, stacking, gradient correction, and color
  calibration first. TV regularization can mistake weak real gradients for noise.
- Work on a duplicate or verified history state. Apply replaces the target after
  successful completion; sequence Apply writes a temporary sequence and commits
  only after all frames succeed.
- Choose the solver before tuning convergence controls. Changing **Algorithm**
  overwrites **Epsilon** and **Max Iterations** with that solver's runtime defaults.
- Do not compare the numerical Weight passed to the two solvers. The UI remaps one
  `0..1` control in opposite mathematical conventions; see [Weight mapping](#weight-mapping).
- For RGB, the solver runs TV independently on R, G, and B. It does not use a
  vector-valued color gradient, so strong smoothing can move chromatic edges or
  produce channel-dependent terraces.
- Keep finite input values. NaN and infinity are not repaired; in an image that
  marks every sample valid they enter the solve and spread into their
  neighbours.

## Procedure

1. Select a calibrated image or sequence and open **Process > Noise Reduction >
   Total Variation Denoise Filter**. Confirm **Target Image**, select **RGB** or a
   deliberate component, and leave **Preview** selected.
2. Start with **Bregman**, **Weight: 0.5000**, **Epsilon: 0.0010**, **Max
   Iterations: 100**, and **Isotropic** selected. Use **Preview area** around smooth
   background, weak structure, and several small stars.
3. Raise Weight gradually for stronger Bregman smoothing. If the preview becomes
   terraced or waxy, lower Weight. Do not jump to exactly `1.0000` while judging
   the endpoint; its guard value is discontinuous with values immediately below it.
4. Lower Epsilon or raise Max Iterations only when the current cap stops before a
   stable result. Higher Epsilon may stop sooner; `0` normally forces the iteration
   cap and is not a useful default.
5. Compare **Isotropic** with it cleared. Isotropic treats diagonal and axial
   gradients jointly; anisotropic can retain axis-aligned features differently and
   may show more directional/block artifacts.
6. To compare **Chambolle**, switch Algorithm. The dialog changes Epsilon to
   `0.0002`, Max Iterations to `200`, and disables Isotropic. Retune Weight from its
   own default meaning; do not carry a perceived Bregman strength numerically.
7. Toggle **Switch image** using the same Linear/Autostretch display mode. Inspect
   faint gradients and star profiles at 100%, then restore the preferred solver.
8. Click **Apply**. Verify Image History and metadata before closing. For a
   sequence, keep the progress dialog open; Cancel takes effect at the solver's
   next iteration.

## Complete parameter and action reference

| UI label | Type and unit | Valid values | Runtime default | Persisted-default behavior | Effect of increasing or enabling | Dependencies and scope: preview/apply, channel, mask, still/sequence | Trade-off, failure boundary, and cost | Output and persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Algorithm | Choice | **Bregman**, **Chambolle** | Bregman | Dialog lifetime; reopening returns to Bregman | Selects split-Bregman or Chambolle dual-projection optimization | Changing to Bregman sets Max Iterations `100`, Epsilon `0.0010`, checks/enables Isotropic; changing to Chambolle sets `200`, `0.0002`, and disables Isotropic without clearing it; re-enabling controls after Apply preserves that dependency | Solvers use different Weight conventions, convergence tests, and boundaries; switching discards tuned Epsilon/iteration values | Recorded by label in still Image History; applies to every sequence frame |
| Weight | Double, normalized UI strength | `0.0000..1.0000`; spin step `0.005` with adaptive decimal stepping; slider has 1,001 positions | `0.5000` | Dialog lifetime; Reset returns to `0.5000` | Generally strengthens denoising, but through solver-specific mappings | Bregman passes `120(1-p)` except `p=1` passes `1`; Chambolle passes `0.1p` except `p=0` passes `0.0001` | The Bregman `p=1` endpoint jumps away from the limit below `1`; solver scales are not comparable; stronger TV can staircase and erase faint structure | UI value, not mapped solver weight, is recorded in still history |
| Epsilon | Double stopping tolerance | `0.0000..1.0000`; four decimals; spin step `0.001` with adaptive stepping; slider has 1,001 positions | Bregman `0.0010`; Chambolle `0.0002` after selection | Dialog lifetime; Algorithm changes overwrite it; Reset returns `0.0010` | A larger value usually stops earlier | Bregman compares per-iteration RMS pixel change; Chambolle compares absolute energy change with `epsilon * initial_energy` | Larger values can stop prematurely; smaller values can cost up to the cap; at `0`, Bregman stops only on exact zero change and Chambolle normally runs to the cap | Recorded in still history; one value is reused for all sequence frames |
| Max Iterations | Integer iteration cap | `1..500` | Bregman `100`; Chambolle `200` after selection | Dialog lifetime; Algorithm changes overwrite it | Raises the maximum solver work | Actual iterations can be fewer when Epsilon's criterion succeeds | Cost grows roughly linearly until convergence; a larger cap does not guarantee a visible difference | Recorded in still history; same cap per sequence frame |
| Isotropic | Check box | On/off | On and enabled for Bregman | Dialog lifetime; Bregman selection checks it; Chambolle disables but retains checked state | For Bregman, combines horizontal/vertical gradients with Euclidean norm | Only passed to Bregman; Chambolle always uses its isotropic gradient norm; remains disabled after Apply controls are re-enabled | Off uses axis-separable L1 shrinkage and can favor horizontal/vertical structure; disabled state under Chambolle has no effect | Not included in the current still-history description |
| Target Image | Image choice | Eligible open still or sequence | Image that launched the process | Live selector; not saved | Changes the source/replace destination | Disabled during execution; geometry must match any active mask | Selecting another image invalidates prior visual comparison; sequences can be disk intensive | Selected still/sequence is replaced on successful Apply |
| Channel | Choice | RGB; Red, Green, Blue; CIE L*a*b* L*/a*/b*; CIE L*c*h* L*/c*/h*; CIE XYZ X/Y/Z; HSL H/S/L; HSV H/S/V; YCbCr Y/Cb/Cr | RGB for RGB targets; mono has its stored channel | Dialog lifetime; Reset returns RGB only when target is RGB | Restricts TV to the chosen stored or derived component | RGB sends three independent planes; a derived component is extracted, denoised as mono, then converted back | Component round trips can clip or alter color; hue/chroma components are not linear intensity; reconstructed metadata can differ | Channel label is recorded for RGB still history; applied per frame |
| Preview | Check box | On/off | On | Dialog lifetime | Enables asynchronous recalculation after settings change | Uses selected preview area, channel, mask, and current parameters | A cancelled preview stops at the solver's next iteration; previews are not serialized with other processes | Display only; target is unchanged |
| Preview pane | Image panel | Current processed preview | Processed view | Dialog lifetime | Shows the current solver result; panning uses scroll bars/dragging and toolbar actions are listed separately below | Preview may represent full image or selected area | Display stretch changes appearance only; stale work is replaced asynchronously | No target mutation |
| Fit To Window | Preview-toolbar action | Click or **Ctrl+Shift+F** | Preview is fitted when its result is first displayed | Viewer lifetime | Sets one uniform scale that fits the whole image inside the viewport | Preview and displayed image required | Best overview; small details may be resampled or hidden | Display only; no target, Apply, or snapshot-pixel effect |
| Zoom In | Preview-toolbar action | Click; each activation requests `1.25×` relative zoom within viewer limits | Current fitted/display scale | Viewer lifetime | Enlarges the image around the viewport center | Preview and displayed image required | Improves local inspection but increases panning and can expose display interpolation | Display only |
| Zoom Out | Preview-toolbar action | Click; each activation requests `0.8×` relative zoom within viewer limits | Current fitted/display scale | Viewer lifetime | Reduces the image around the viewport center | Preview and displayed image required | Shows more context while hiding fine residuals | Display only |
| Zoom 100% | Preview-toolbar action | Click; sets absolute scale `1.0` | Current fitted/display scale | Viewer lifetime | Displays one image pixel at the viewer's 100% scale | Preview and displayed image required | Preferred for judging noise, stars, and artifacts; large images require panning | Display only |
| View Channel | Preview-toolbar combo box | **RGB**, **Red**, **Green**, **Blue**, **CIE L* (Lightness)**, **CIE a***, **CIE b***, **CIE a*=R / b*=G (Chrominance)**, **CIE c***, **CIE h***, **CIE X/Y/Z**, **HSL Hue/Saturation/Lightness**, **HSV Hue/Saturation/Value**, **HSI Hue/Saturation/Intensity**, **YCbCr Luma/Blue Chroma/Red Chroma**; disabled **Grayscale** for mono | **RGB** for RGB; **Grayscale** for mono | Viewer lifetime; preserved while the same channel list is rebuilt | Chooses only which stored or derived component the preview viewer displays | Distinct from process **Channel**; RGB target required for component choices | Derived display modes add conversion work and can exaggerate or hide artifacts without changing pixels | Display only; no execution, mask, Apply, snapshot, or history effect |
| Display Mode | Preview choice | Linear or Autostretch | Inherited standard preview setting | Dialog lifetime | Changes preview visualization | Preview pane only | Can make identical pixels look different; keep one mode for comparisons | Display only; not baked into Apply/snapshot pixels |
| Autostretch Settings | Preview action | Opens display-stretch controls | Current preview settings | Dialog lifetime | Tunes the preview's display transform | Relevant when Autostretch is selected; execution autostretch support is display-only here | Different stretches invalidate side-by-side noise judgments | Display only |
| Switch image | Preview-toolbar action | Source/processed preview | Processed preview | Dialog lifetime | Toggles before/after pixels | Requires a completed preview | A different display state can masquerade as a process difference | Display only |
| Preview area | Preview-toolbar action | Full frame or chosen rectangle | Full available preview | Dialog lifetime | Restricts later previews and snapshots to a representative area | Does not restrict Apply | Small areas run faster but can miss background gradients, boundaries, or other noise regimes | Preview/snapshot scope only |
| Take snapshot | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes current settings and opens `<target> - Total Variation Denoise - SNAPSHOT` | Exists only with Preview; selected area and mask are honored | Runs another full solve and allocates an auxiliary still; display autostretch is not baked in | New still; target unchanged |
| Apply | Dialog action | Enabled when controls are available | Enabled | Click or standard Apply action | Executes a captured parameter set and replaces the target after success | Uses current target, channel, mask, solver, and sequence path; locks controls while running | Solver errors leave the target uncommitted; successful Apply retains the current dialog controls | Adds a still-history step; sequence replacement commits only after all frames complete |
| Reset | Dialog action | Available while idle | Not invoked | Resets Weight `0.5`, Epsilon `0.001`, Max Iterations `100`, Isotropic on, Algorithm to Bregman, and RGB on RGB targets | Restores most controls to their constructor defaults | From Chambolle, switching to Bregman sets the same `100`/`0.001` values, so Epsilon ends at `0.001` from either Algorithm | Preview reruns if enabled | No target change until later Apply |
| Close | Dialog action | Available while idle | Open | Click, **Esc**, or window close | Closes without applying pending preview | Disabled during processing | Closing discards dialog settings, not an earlier Apply/snapshot | No target mutation |
| Sequence progress / Cancel | Modal progress action | Solver iterations plus sequence frame progress; Cancel | Not running | Created for sequence Apply | Requests cooperative cancellation | The solver checks for Cancel once per iteration | Cancel stops the current frame promptly; temporary output is removed and original sequence retained | Successful completion swaps in the temporary sequence; cancellation does not commit it |

## How the algorithm works

### ROF objective and TV models

Both choices seek an image `u` near source `f` while reducing its total variation:

```text
min_u  sum_i [ TV_gradient(u_i) + (lambda / 2) * (f_i - u_i)^2 ]
```

The first term favors smaller spatial gradients; the second preserves fidelity.
For Bregman **Isotropic**, each 2-D gradient contributes
`sqrt(dx^2 + dy^2)`. With Isotropic off, it contributes `abs(dx) + abs(dy)`.
Chambolle uses the isotropic norm. Lower `lambda` gives TV more influence, but the
two APIs expose different functions of lambda.

### Weight mapping

Let `p` be the Weight displayed by AfterNight:

```text
Bregman:  solver_weight = 120 * (1 - p), if p < 1
          solver_weight = 1,             if p = 1
          solver_weight = lambda / 2

Chambolle: solver_weight = 0.1 * p, if p > 0
           solver_weight = 0.0001,  if p = 0
           solver_weight = 1 / lambda
```

Thus increasing `p` generally strengthens smoothing in both UI modes, even though
it decreases the Bregman weight and increases the Chambolle weight. The Bregman
guard at exactly `1` is discontinuous: `p=0.999` passes `0.12`, while `p=1` passes
`1`. Exact `1` therefore denoises less than some immediately lower values. The
Chambolle zero guard avoids division by zero by passing `0.0001`.

### Bregman solver, convergence, and boundary

AfterNight's engine runs Goldstein and Osher's split-Bregman method as
published. Each iteration relaxes the image by one red and one black
Gauss-Seidel half-sweep, shrinks the horizontal/vertical derivative variables by
the published soft threshold (on the gradient's length when **Isotropic** is on,
on each component when it is off), and updates the Bregman variables. It stops
when the RMS pixel change between iterations is below Epsilon or when Max
Iterations is reached. The image edge is replicated (a no-flux boundary).

This route reaches the same ROF minimum as scikit-image's split-Bregman
implementation, but its intermediate states differ: the red-black sweep gives
the same answer at any thread count, which a row-by-row sweep does not, and the
soft threshold is the published one rather than the library's smooth
approximation of it. A run that stops at the iteration cap or an early Epsilon
can therefore differ slightly from that library's, while converged results
agree closely. Over a noisy edge, the Bregman and Chambolle answers agree to
`0.02` per sample after 8,000 iterations, and their objective values agree
within one part in a hundred.

### Chambolle solver, convergence, and boundary

AfterNight's engine runs Chambolle's 2004 dual-projection algorithm, written to
the discretization scikit-image uses, and reproduces that library's answer bit
for bit. It is not the later Chambolle-Pock primal-dual method implied by the
current UI tooltip. It iteratively
updates a dual field and reconstructs `u` from its divergence. It stops when
`abs(E_previous - E) < Epsilon * E_initial` or at Max Iterations. Forward
differences are zero on the terminal row/column, with the matching discrete
divergence acting like a no-flux boundary.

### Engine, color, masks, and sequences

1. The shared process path extracts RGB or the selected component and the active
   mask/preview region.
2. The engine solves the plane in its stored units, in single precision with
   double-precision sums. For an integer image the mapped weight and Epsilon are
   restated in the image's own codes, so the solve is the one its normalized copy
   would get; float values retain their numeric scale; double inputs lose
   precision. A sample that the image's validity rule or mask rejects enters the
   solve as zero, which can pull nearby valid samples, and is left as it was in
   the result.
3. For RGB, each channel is solved independently, one after another. It is not
   vectorial color TV. A selected derived component is solved as mono and
   converted back to RGB by the shared channel path. A derived component's
   reconstruction saturates an integer image at its type's range and keeps a
   floating image's values outside `[0, 1]`.
4. The result is converted back to the source pixel type. Integer conversion
   rounds and holds the type's range; float output is not clamped to `[0,1]`.
5. The standard mask blend combines source and denoised pixels. Apply replaces a
   still, or processes each sequence frame into a temporary replacement before
   committing the sequence.

Both methods report progress as their iterations run and check for Cancel once
per iteration, so a cancelled preview or Apply stops promptly and commits
nothing.

## Parameter interactions and starting points

| Goal | Starting point | What to inspect |
| --- | --- | --- |
| Conservative edge-aware smoothing | Bregman, Weight `0.50`, Epsilon `0.001`, Max `100`, Isotropic on | Smooth background at 100%; faint filaments and small-star profiles |
| Stronger Bregman result | Raise Weight in small steps, but keep below exact `1`; lower it at the first terrace/halo | Gradient banding, star rounding, loss of dim structure, border changes |
| Directional comparison | Bregman, toggle Isotropic with all other values fixed | Horizontal/vertical bias and block-like texture versus diagonal smoothness |
| Chambolle comparison | Switch Algorithm, begin at Weight `0.50`, Epsilon `0.0002`, Max `200` | Judge from the image, not Bregman's numeric scale; Isotropic is fixed |
| Faster preview | Representative Preview area; slightly larger Epsilon; lower Max Iterations | Whether the solver stops too early and Apply on the full frame changes character |
| Chroma-heavy noise | Compare RGB with a suitable lightness/chroma component on a duplicate | Color edges, hue changes, component-round-trip clipping, metadata |

Algorithm changes are destructive to convergence tuning: selecting Bregman always
sets `100/0.001` and checks Isotropic; selecting Chambolle sets `200/0.0002` and
disables Isotropic. Weight is retained. Reset ends at the Bregman values
`100/0.001` from either Algorithm.

## Inputs, outputs, and metadata

- Input and output dimensions are identical. Output is converted to the source
  pixel type; integer results are rounded, and double input is processed at
  float32 precision.
- The result keeps the source image's header cards and metadata for every
  Channel choice.
- The process does not explicitly update the image linear/nonlinear processing
  state. Do not infer state from the fact that TV was applied.
- Still Image History records Algorithm, UI Weight, Epsilon, Max Iterations, and,
  for a colour image, ` in channel: <channel>`. It omits the Isotropic setting and mapped solver
  weight. Preserve those separately when reproducibility matters.
- A still CFA mosaic is treated as one stored sample plane unless debayered first.
  For a sequence, shared preprocessing may auto-debayer configured CFA frames and
  the replacement sequence can therefore be RGB.
- A sequence is decoded frame by frame, processed with one captured parameter set,
  written to temporary storage with frame metadata, and swapped into the view only
  after success. Cancel/error removes the temporary output.
- **Take snapshot** creates a new still from the current preview scope. It does not
  change the target or preserve the preview's display autostretch in pixel values.

## Performance

Both solvers keep six full-frame single-precision working planes, taken once and
reused for every channel, and repeat full-image finite-difference passes on the
threads the operation is granted. Work is approximately proportional to pixel
count, channels, and actual iterations. Bregman keeps the answer, the previous
answer, and two each of derivative and Bregman fields; Chambolle keeps the
answer, the divergence, and two each of the dual and gradient fields. RGB is
solved channel by channel, and the answer is the same at any thread count.

No global lock serializes Total Variation with other processes. Preview-area
selection is the safest way to shorten tuning. Sequence processing adds decode,
temporary encoding, and disk space. Max Iterations is a cap, not a promise of
work or quality; Epsilon may terminate sooner. No approved astronomical image
was benchmarked, so no production timing is claimed.

## Worked example

This calculator example explains parameter mapping; it is not a visual-quality
measurement.

Suppose Bregman starts at Weight `p=0.50`:

```text
solver_weight = 120 * (1 - 0.50) = 60
lambda = 2 * solver_weight = 120
```

At `p=0.95`, the solver receives `6` and lambda is `12`, so TV has more influence.
At `p=0.999`, it receives `0.12`; at exactly `p=1`, the guard instead sends `1`.
The exact endpoint is therefore weaker than `0.999` under the solver's convention.

For Chambolle at `p=0.50`:

```text
solver_weight = 0.1 * 0.50 = 0.05
lambda = 1 / solver_weight = 20
```

At `p=1`, it receives `0.1` and lambda becomes `10`, increasing TV influence.
These values cannot be compared numerically with Bregman's `60`; compare actual
preview pixels with the same crop and display stretch. No approved mono/color
master was executed for this example.

## Warnings and limitations

- Total variation tends toward piecewise-constant regions. Strong settings can
  create stair steps, cartoon texture, flat patches, rounded stars, and lost faint
  gradients even when prominent edges remain.
- Bregman Weight is discontinuous at exactly `1.0000`; use a nearby lower value
  only with visual comparison and record the exact setting.
- The Chambolle UI description says “primal-dual style,” but the engine runs
  Chambolle's 2004 dual-projection iteration. Isotropic is disabled and ignored
  in this mode.
- Epsilon accepts `0`, although the upstream API specifies a positive tolerance.
  Zero usually causes work to continue to Max Iterations.
- RGB channels are independent, not vector-coupled. Component conversions can
  change color or metadata; compare on a duplicate.
- Both solvers treat the image edge as a no-flux boundary, which affects edge
  pixels. Crop away suspect borders or compare another solver if edge fidelity
  is critical.
- Integer data is solved in its stored codes and rounded; double data is solved
  at float32 precision. Float output is not range-clamped.
- Still history omits Isotropic and mapped solver weight, and processing-state
  provenance is not updated.

## Troubleshooting

| Symptom | Likely cause | Check or correction | Evidence |
| --- | --- | --- | --- |
| Weight behaves differently after changing Algorithm | Opposite upstream weight conventions and UI remapping | Return to the solver's starting point and retune; do not transfer numerical expectations | Process mapping plus scikit-image API |
| Exact Bregman Weight `1` is weaker than `0.999` | Endpoint guard sends `1` instead of the limiting near-zero value | Step back below `1` and compare; record the exact UI value | `TotalVariationDenoiseProcess.cpp` |
| Isotropic cannot be changed | Chambolle is selected | Switch to Bregman to compare isotropic/anisotropic TV; Chambolle ignores this control | Process |
| A large frame takes long at a high Max Iterations | Each iteration is a full-image pass per channel | Watch the progress; Cancel stops at the next iteration; tune on a smaller Preview area or raise Epsilon | Engine solver |
| Edge pixels differ more than the center | Solver boundary condition | Compare Bregman/Chambolle and avoid judging on a crop boundary | Engine solver boundary |
| Preview differs from Apply | Preview area, mask, target/channel, or display stretch changed | Match all controls and use one Display Mode; Apply always processes the full target | Shared preview/process path |
| Colors shift or terrace differently | Independent RGB solves or derived-component round trip | Reduce Weight; compare RGB and a deliberate component on a duplicate | Per-channel solve |

## Related workflow steps

- Complete [Image Stacking](../stacking-integration/image-stacking.md), gradient
  correction, and color calibration before denoising most deep-sky masters.
- Compare [Wavelet Denoise](wavelet-denoise.md) when scale-selective thresholding
  is preferable to a gradient penalty.
- Compare [NL-Bayes](nl-bayes.md) when repeated non-local structures provide
  useful patch groups and higher memory/runtime are acceptable.
- See [Broadband Imaging](../../workflows/broadband-imaging.md) for the usual
  placement in a larger workflow.
- Use [Process Windows](../../interface/process-windows.md) for target, preview,
  mask, history, and snapshot behavior shared by process dialogs.
- See the [Image Viewer](../../interface/image-viewer.md) for display autostretch
  and active-mask controls.
- Terms used here are defined in the [Glossary](../../reference/glossary.md).

## References and provenance

- AfterNight process, engine solver, shared RT-preview/channel/mask/sequence
  framework, and focused tests.
- [scikit-image 0.26.0 restoration source](https://github.com/scikit-image/scikit-image/blob/v0.26.0/src/skimage/restoration/_denoise.py)
  and [compiled TV-Bregman source](https://github.com/scikit-image/scikit-image/blob/v0.26.0/src/skimage/restoration/_denoise_cy.pyx),
  checked 2026-07-13 for objectives, mappings, solver loops, channel handling,
  convergence, and boundaries. The engine's Chambolle iteration reproduces it,
  and its split-Bregman route reaches the same minimum.
- [scikit-image restoration API](https://scikit-image.org/docs/stable/api/skimage.restoration.html),
  checked 2026-07-13 for public parameter semantics and algorithm references.
- Tom Goldstein and Stanley Osher,
  [The Split Bregman Method for L1-Regularized Problems](https://ww3.math.ucla.edu/camreport/cam08-29.pdf),
  checked 2026-07-13.
- Antonin Chambolle,
  [An Algorithm for Total Variation Minimization and Applications](https://www.math.ucla.edu/~lvese/285j.1.09f/Chambolle.pdf),
  checked 2026-07-13.
