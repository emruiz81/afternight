# Starmask Recombination

## Purpose and expected result

Starmask Recombination adds a stars-only image to a matching starless base. An
optional image can then blend between the untouched starless base and the full
sum. It is intended for a nonlinear, geometrically identical starless/stars
decomposition, such as a target split by Starnet++ and edited in separate
branches.

The unmasked result is `starless + stars`: held at one when the images are read
as fractions of full scale, and not held when both are floating-point images
that state a physical quantity, as every floating-point file AfterNight opens
does (see **Model and equations**). **OK** opens the result as a new still named
`<starless> - <stars> - MERGED` beside the starless image and closes the window;
the selected Starless Image is left as it was. Use matching still images.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Star Processing > Starmask Recombination** |
| Enabled when | The process descriptor is available. This top-level action remains enabled without an active image; **OK** and **Preview** enable only after both required selectors resolve to open views |
| Inputs | One Starless Image, one Stars Image, and an optional Mask. Selectors list all open image/sequence views and do not prefilter dimensions, channels, format, type, alignment, or processing state |
| Outputs | One new still named `<starless> - <stars> - MERGED`, in the higher of the two images' representations, opened beside the starless image, which is left as it was; **Take snapshot** creates an auxiliary still |
| Recommended image state | Matching nonlinear stills produced from the same registered target and kept at identical geometry and processing state |
| Preview support | Full-frame asynchronous preview, display autostretch, zoom/channel/display controls, optional-mask result, and snapshots. **Preview area** crops the stars and the mask to the same area |
| Sequence support | Still images only: the suite holds still images, and a sequence is not an intended input |
| Mask support | Only the dialog's optional Mask input. Ordinary active target masks are disabled for this process. A one-channel mask weighs every channel and a mask with one channel per image channel weighs each channel by its own; a mask of another width, height, or channel count is refused |
| External dependencies | None |

## Before you start

1. **OK** leaves the selected Starless Image as it was and opens the result as
   a new still, so the starless base needs no duplicate.
2. Confirm that Starless Image and Stars Image have the same width, height, and
   number of channels. The selector does not enforce this; the run refuses a
   pair that differs.
3. Confirm pixel alignment and matching processing history. The process does no
   registration, resampling, linear matching, or state reconciliation.
4. Prefer floating-point stills when separate edits may contain samples above
   one. Two floating-point images read from files keep their sum above one; an
   integer image is read as a fraction of its full scale (a signed one counted
   from its smallest value), and a pair that includes one is added as fractions
   with the sum held at one.
5. If using a mask, give it the pair's width and height and one channel or the
   pair's channel count; another mask is refused. Its values are read as
   fractions of full scale and held to `[0,1]`.
6. **OK** reads the Stars Image, the Mask, and Invert when it is pressed; a later
   change reaches the next run only.

## Procedure

1. Open **Process > Star Processing > Starmask Recombination**. No active image
   is required to open the dialog.
2. Select the matching **Starless Image** and **Stars Image**. **Preview** and
   **OK** enable when both names resolve to open views; this is not a
   compatibility check.
3. Leave **Mask (optional)** at **No Image** to inspect the full sum.
   **Preview area** crops the stars and the mask to the same area as the
   starless image, so an area preview shows the recombination of that area.
4. If local blending is required, select a same-size grayscale mask. With
   **Invert** off, black preserves starless and white applies the sum. Turn
   **Invert** on to reverse those roles.
5. Inspect the full frame at 100% and check bright cores, halo transitions,
   color alignment, frame edges, and the mask boundary. **Switch image** toggles
   between the result and the starless image.
6. Click **OK**. The result opens as `<starless> - <stars> - MERGED` beside the
   starless image, which is left as it was, and the window closes. Inspect the
   new image's state and metadata.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/execution, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Starless Image | Image/sequence selector | **No Image** | None | Any open view; execution requires same width, height, and channel count as Stars Image | Supplies base `B`, the result's record and name, and preview autostretch statistics | Required for Preview/OK; no compatibility filtering | Wrong alignment/state produces doubled, displaced, or missing structure; the view itself is not changed | Preview/OK; all stored channels; still image | Dialog lifetime; successful OK does not clear it |
| Stars Image | Image/sequence selector | **No Image** | None | Any open view; addition requires same width, height, and channel count as Starless Image | Supplies additive layer `S` | Required for Preview/OK; no compatibility filtering | Non-stars residuals are added directly; a representation higher than the starless image's becomes the result's | Preview/OK; all stored channels; the image its view shows when the run is asked for | Dialog lifetime |
| Mask (optional) | Image/sequence selector | **No Image** | None | Any open view; the pair's width and height and one channel or the pair's channel count | Blends base and full sum by the mask read as a fraction of full scale and held to `[0,1]` | Optional; selecting a view enables Invert; a mask of another size or channel count is refused when the run starts | RGB masks can change channels differently | Preview/OK; internal blend only; ordinary active mask ignored | Dialog lifetime |
| Invert | Checkbox | Off; disabled | Checked/unchecked state is retained if Mask is later cleared | Off/On | Off computes `B(1-A) + (B+S)A`; On computes `BA + (B+S)(1-A)` | Enabled only while Mask resolves to a view | Reversing a protection mask can restore stars in the wrong region | Optional-mask preview/OK | Dialog lifetime; no Reset |
| Preview | Checkbox | On but disabled until both required images resolve | None | Off/On | Creates/destroys asynchronous preview | Requires Starless and Stars selections | Repeated full-frame addition/mask/type conversion allocates full images | Preview only | Dialog lifetime |
| Preview pane | Image viewer | Processed result | Execution autostretch supported | Pan/scroll and actions below | Displays current result without committing Starless Image | Exists with Preview | Holds source/result/alternate/display buffers | Preview only | Preview-window lifetime |
| Fit To Window | Preview action | Fitted on first display | None | Click or **Ctrl+Shift+F** | Fits whole image uniformly | Preview required | Resampling can hide one-pixel misregistration | Display only | Viewer lifetime |
| Zoom In | Preview action | Current scale | None | Click; `1.25x` relative zoom | Enlarges around viewport center | Preview required | Better local inspection, less context | Display only | Viewer lifetime |
| Zoom Out | Preview action | Current scale | None | Click; reciprocal relative zoom | Shrinks view | Preview required | More context, less pixel detail | Display only | Viewer lifetime |
| Zoom 100% | Preview action | Current scale | None | Click or **Ctrl+Shift+1** | Shows one image pixel per display pixel | Preview required | Best for alignment, cores, masks, and clipping | Display only | Viewer lifetime |
| View Channel | Preview selector | **RGB** for RGB; disabled **Grayscale** for mono | None | Stored/derived RGB, CIE, HSL/HSV/HSI, and YCbCr components | Changes displayed component only | RGB preview required for derived components | Reveals color misalignment without changing samples | Display only | Viewer lifetime |
| Display Mode | Preview selector | Inherits Starless Image Linear/Autostretch state | None | **Linear**, **Autostretch** | Changes display only | Autostretch enables settings | Display stretch can conceal clipping or exaggerate faint residuals | Display only | Preview view state |
| Autostretch Settings | Preview dialog action | Disabled in Linear mode; inherits base-view settings | Shared values load when opened | Opens display controls; Apply Stretch hidden | Tunes visualization | Autostretch mode only | Never changes recombined samples | Display only | Viewer-scoped |
| Switch image | Preview action | Masked/current result shown | None | Click, **Space**, right-click, or **Ctrl+Shift+O** | Toggles between the result and the starless image | Preview required | Compares the recombination with its base | Display only | Preview lifetime |
| Preview area | Preview checkbox/overlay | Off | None | Off/On; move/resize overlay | Crops the stars and the mask to the same area as the starless image and shows their recombination there | Preview required | A smaller area is less preview work | Preview and snapshot | Preview lifetime |
| Take snapshot | Preview action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes and opens `<starless> - Starmask Recombination - SNAPSHOT` | Preview required | Adds a full execution and auxiliary still | New still; all channels | Each click creates a view |
| OK | Dialog action | Disabled | None | Click | Recombines on a worker and opens the result as a new still `<starless> - <stars> - MERGED` beside the starless image, which is left as it was | Enabled when Starless and Stars resolve; the Stars Image, the Mask, and Invert are read when OK is pressed | Full-image buffers; non-destructive | Still image | A successful run closes the window |
| Close | Dialog action | Enabled | None | Click | Closes the window without starting a run | Remains enabled | Does not undo a prior OK or delete snapshots | Dialog | Discards dialog state |
| Progress / Cancel | Shared progress action | Created by the shared framework for a run | None | Cooperative Cancel | Reports the run's progress and requests cancellation | The engine's recombination honours the stop | A stopped run publishes nothing | Run | Per run |

## How the algorithm works

### Conceptual pipeline

1. When OK or a preview asks for a run, take the Stars Image and the optional
   Mask the selectors name, each read under a lease of the run's own and cropped
   to the preview area when there is one; the Starless Image is the window's
   target and supplies the run's source.
2. Require equal width, height, and channel count (one or three) for the two
   additive inputs, and the pair's width and height and one or the pair's
   channel count for the mask.
3. Read both images in one domain: two floating-point images that state a
   physical quantity in their own units, and any other pair as fractions of full
   scale - an unsigned integer a fraction of its largest value, a signed integer
   counted from its smallest. The mask is always read as a fraction of full
   scale and held to `[0,1]`.
4. Add the stars to the starless image in double, hold the sum at one in a
   normalized domain, blend it over the starless image by the mask when one is
   given, and write every sample once in the higher of the two images'
   representations, rounded to nearest and saturating.
5. Keep the starless image's record - its acquisition record, header cards, and
   plate solution - with the recombination appended to its lineage, mark the
   result nonlinear with operation `starmask-recombination`, and show it in the
   preview, a snapshot, or a new still.

### Model and equations

Let `B(x,c)` be the starless base, `S(x,c)` the stars layer, and `A(x,c)` the
mask coefficient at pixel `x` and channel `c`. The unmasked sum is

`R(x,c) = min(B(x,c) + S(x,c), 1)` when the pair is read as fractions of full
scale, and `R(x,c) = B(x,c) + S(x,c)` when both images are floating-point
images stating a physical quantity, so a sum above one is kept. There is no
lower clamp, so a negative floating-point sum remains negative; an integer
result saturates when it is written.

With **Invert** off, the final sample is

`O = B(1-A) + RA = B + A(R-B)`.

With **Invert** on, it is

`O = BA + R(1-A)`.

For a conventional in-range decomposition where `R=B+S`, the non-inverted
formula simplifies to `O=B+AS`: mask zero restores no stars, mask one restores
the full stars layer. Inversion replaces `A` with `1-A`.

The result's representation is the higher of the two images' in the order
`sint8 < uint8 < sint16 < uint16 < sint32 < uint32 < float < double`, with or
without a mask; the mask does not change it. A pair that includes an integer
image is added as fractions of full scale, a signed integer counted from its
smallest value, so the sum is held at one and an integer result saturates at
its largest code. Two floating-point images read from files are added in their
own units and keep a sum above one; a floating-point image that states a
normalized domain is added as a fraction and held at one.

### Boundary and failure behavior

- A pair of different width, height, or channel count is refused with **the
  star image does not have the starless image's size and channels**.
- The UI enables OK before these checks. It also permits the same view in more
  than one selector.
- A mask of another width, height, or channel count is refused by the engine.
- A one-channel mask weighs every output channel. A mask with one channel per
  image channel weighs each channel by its own.
- Mask values are held to `[0,1]` before they are used, and inverted after that
  when **Invert** is on.
- A pixel that the starless image's, the stars' or the mask's validity rejects
  keeps the starless image's samples. A sample that is not a finite number in a
  pixel its image states valid makes the engine refuse the run.
- The recombination honours the run's stop; a stopped run publishes nothing.
- Sequences are not an intended input; the window is meant for still images.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Full reconstruction | Matching float stills, no Mask | Stars remain weak because the stars layer itself was reduced | Highlights clip or nonstellar residuals double | Verify decomposition/alignment, inspect sum, then adjust upstream stars layer |
| Local restoration | Same-size grayscale Mask, Invert off, values in `[0,1]` | Desired stars remain suppressed where mask is dark | Stars/halos return in protected regions or mask edges become visible | Inspect mask geometry/range, verify inversion, then tune the mask upstream |
| Type safety | Matching float types | Integer quantization/clipping masks subtle differences | Signed or mixed-type behavior changes the result unexpectedly | Convert both inputs and mask to float before recombination |

## Inputs, outputs, and metadata

Starless and Stars Image must match in dimensions and channel count. The engine
does not align or resample. The output is written in the higher of the two
images' representations, with or without a Mask; this rule applies to previews,
snapshots, and OK.

**OK** opens the result as a new still `<starless> - <stars> - MERGED` beside
the starless image and closes the window after a successful run. The selected
Starless Image, its history, and the other inputs are left as they were.

The result keeps the Starless Image's record - its acquisition record, exposure
included, its header cards, and its plate solution - not the Stars Image's or
the Mask's. The recombination is appended to its lineage, and the
processing-state cards mark the output nonlinear with `starmask-recombination`.
No file is written directly.

## Performance

Addition and optional blending are parallel and linear in pixels times channels.
The main costs are the working copies of integer inputs and of the mask, the
destination, and preview display buffers. Integer inputs and masks therefore
add memory traffic and temporary allocations. **Preview area** limits the
work to the area. No production timing or peak-memory measurement was made.

## Worked example

- Evidence mode: Source-and-synthetic-test-derived; no approved astronomical
  pair was executed.
- Float checkpoint: `B=0.36` and `S=0.14` give unmasked `R=0.50`.
- Mask checkpoint: with `A=0.25`, non-inverted output is
  `0.36 + 0.25x0.14 = 0.395`; inverted output is
  `0.36 + 0.75x0.14 = 0.465`.
- Workflow checkpoint: select compatible stills and click **OK**. Verify the new
  MERGED image's dimensions/type/state, then inspect cores, halos, color
  registration, mask transitions, and clipping at 100%.
- Limitation: scalar operation tests verify addition and mask blending, not
  astronomical decomposition quality or the complete dialog OK path.

## Warnings and limitations

- **OK opens a new image.** The starless image is left as it was, and the
  window closes after a successful run.
- The Stars Image, the Mask, and Invert are read when OK is pressed; a later
  change reaches the next run only.
- Use still images only; a sequence is not an intended input.
- Preview area crops all three images to the same pixel area; it does not
  register them.
- **Switch image** toggles between the result and the starless image.
- Selectors do not validate alignment, dimensions, channel count, pixel format,
  type, mask range, or processing state before enabling OK; the run refuses a
  pair or a mask whose size or channel count does not fit.
- Addition is not an unscreen or screen operation. It is appropriate only when
  the stars layer is defined for additive reconstruction.
- A pair that includes an integer image is held at one and an integer result
  saturates at its largest code; two floating-point files keep their sum above
  one; a negative floating-point sum stays negative.
- Ordinary active target masks are ignored. Only Mask (optional) participates.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **the star image does not have the starless image's size and channels** | Width/height or channel count differs | Match geometry and channel count before reopening/previewing | `StarmaskRecombinationProcess::executeTypedProcessImpl` |
| The result appeared in a new view and the window closed | **OK** opens `<starless> - <stars> - MERGED` and closes the window after success | Inspect the new view; the starless image is unchanged | `StarmaskRecombinationProcess::getBatchExecutionParameters` |
| A run with a Mask is refused | The mask's width, height, or channel count does not fit the pair | Resize/crop the mask to the pair's geometry; use one channel or the pair's channel count | `an::recombineStarMask` |
| Bright cores are held at one | The pair includes an integer or normalized image, so both are read as fractions of full scale | Expected for such a pair; two floating-point images read from files keep their sum above one | `process_utils::recombinationReading` |

## Related workflow steps

Create a matched starless/stars pair with [Starnet++](starnet.md), or use
[Star Reduction](star-reduction.md) when the goal is to reduce rather than fully
separate and add stars. Use [Masks](../../interface/masks.md) to inspect mask
geometry and range, remembering that this dialog's Mask selector is separate
from the ordinary active-mask system. [Profiled Starmask Recombination](profiled-starmask-recomb.md)
builds a new analytical stars layer from saved profiles before applying the same
addition/blend stage.

## References and provenance

- The exact addition, optional-mask equations, type/geometry behavior,
  metadata/state, preview limitations, button wiring, and sequence boundary come
  from AfterNight's source and its focused tests.
- This process implements direct additive reconstruction. No external paper,
  model, catalog, service, or network operation is involved.
