---
kind: process
stage: any
menu: Process > Utilities > Image Compare
summary: >-
  Shows two open images at the same pixel positions, side by side or overlaid, without changing either.
data_in: either
---
# Image Compare

## Purpose and expected result

Image Compare is a non-destructive inspection window for comparing two open
images at the same pixel coordinates. It is useful for checking registered
variants, alternate processing results, or before/after states. The observable
result is a toggle or movable split display plus one or two raw-data
histograms. It does not calculate a score, align or normalize the inputs, or
derive a comparison image. The shared **Take snapshot** action can create an
auxiliary still containing a copy of Image 1; it is not a capture of the split,
Image 2, or the display stretch.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Utilities > Image Compare** |
| Enabled when | At least one image view is open; the active view becomes **Image 1**. |
| Inputs | One active image and one other open image with exactly the same width and height. Pixel type, channel count, processing state, metadata, and display state need not match. |
| Outputs | Inspection does not change either input. **Take snapshot** optionally creates `<Image 1> - Image Compare - SNAPSHOT`, copying Image 1 pixels and metadata without the split/Image 2/display stretch. No file is written directly. |
| Recommended image state | Any state is accepted. Compare like with like: use the same processing state, registration, crop, display channel, zoom, and display mode when interpreting differences. |
| Preview support | Always-on private comparison view. Toggle and split modes operate in that view: in a split, Image 2 is presented by the engine on a worker exactly as the view presents Image 1 - the same display mode, stretch, balance and channel - and shown once it lands. There is no Apply action, but the shared toolbar can create an Image 1 snapshot. |
| Sequence support | A sequence view can be Image 1, but Image 2 is the other view's current image. There is no synchronized frame pairing or sequence output. |
| Mask support | None. Active masks are not applied to either comparison image. |
| External dependencies | None. |

## Before you start

Open both images and ensure they already represent the same field and pixel
grid. Image Compare filters **Image 2** by width and height only. It does not
check WCS, registration transforms, crop origin, channels, exposure, background,
or photometric scale. An apparent edge or brightness difference can therefore
be a coordinate or normalization mismatch rather than a processing result.

Choose **Linear** display when you need the same direct sample-to-screen
mapping. In **Autostretch**, each image is stretched from its own statistics;
that is convenient for structural inspection but not evidence that the two
images have the same intensity scale.

## Procedure

1. Activate the first image, then open **Process > Utilities > Image Compare**.
   Confirm that its name appears in **Image 1**.
2. Choose a same-size view in **Image 2**. If it is absent, its dimensions do
   not match Image 1. The private preview now has both images available.
3. Set the preview to the same display channel and zoom you intend to inspect.
   Use **Linear** for direct intensity comparison or **Autostretch** for a
   separately stretched structural comparison.
4. Start with **Toggle mode** and use the preview's switch-image action to blink
   between IMAGE 1 and IMAGE 2. Stationary features should remain at the same
   pixel location if the pair is registered.
5. Select **Horizontal split** or **Vertical split** for a seam comparison.
   Drag the divider through stars, background, and high-signal structure;
   double-click it to return to the center. Right-click the image, or use the
   preview switch-image action, to reverse the two sides.
6. Read the histograms as distributions of stored image samples, not of the
   screen stretch. Split modes show labelled histograms for both images; Toggle
   mode shows only the image currently displayed.
7. If you need a duplicate of Image 1, use **Take snapshot**. This creates a new
   still from Image 1's stored pixels and metadata; it does not capture the
   visible comparison, Image 2, or Linear/Autostretch presentation.
8. Close the window when inspection is complete. Closing alone creates no result
   and neither source receives a history entry.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Zoom in** | Histogram toolbar action | Available | None | Repeated clicks multiply histogram widget size by `1.30` | Magnifies bin geometry; does not recompute image data | Histogram panel active | Easier narrow-bin inspection; more scrolling | Both visible raw-data histograms; display only | Window lifetime |
| **Zoom out** | Histogram toolbar action | Available | None | Repeated clicks multiply histogram widget size by `0.70` | Fits more of the distribution | Histogram panel active | Less detail; no processing cost | Both visible raw-data histograms; display only | Window lifetime |
| **Zoom reset** | Histogram toolbar action | Available | None | One click | Fits each histogram widget to its parent | Histogram panel active | Restores overview | Both visible raw-data histograms; display only | Window lifetime |
| **Show grid** | Checkable histogram action | Checked | None | Off / on | Adds or removes plot grid lines | Histogram panel active | Display-only aid | Both visible histograms | Window lifetime; reset does not establish a saved process preset |
| **Show clipping info** | Checkable histogram action | Checked | None | Off / on | Shows low/high percentages counted at normalized values `<= 0` and `>= 1`, summed over all channels | Histogram panel active | Adds a small overlay; values describe stored samples, not screen clipping | Each visible raw-data histogram | Window lifetime |
| **Absolute scales** | Checkable histogram action | Unchecked | None | Off / on | Despite the label, checked sets the implementation's local-channel-normalization flag: every displayed channel uses its own peak. Unchecked uses one shared peak across displayed channels. | Histogram panel active | Checked makes channel shapes easier to see but hides relative peak-count differences | Both visible raw-data histograms | Window lifetime |
| **Draw histogram areas** | Checkable histogram action | Unchecked | None | Off / on | Switches from lines to additive filled channel areas | Histogram panel active | Filled overlaps are easier to see but can obscure fine bins | Both visible raw-data histograms | Window lifetime |
| **Plot resolution** | Histogram bin-count list | **8-bit (256)** | None | 6-bit (64), 7-bit (128), 8-bit (256), 9-bit (512), 10-bit (1024), 12-bit (4096), 14-bit (16K), 16-bit (64K), 100, 1000, or 10000 levels | Higher counts preserve narrower distribution detail; lower counts aggregate more samples per bin | Histogram panel active | More bins increase histogram computation and storage and can exceed the on-screen width | Both raw-data histograms; source pixel type is unchanged | Window lifetime |
| **Toggle mode (click to switch between images)** | Exclusive mode action | Selected | None | Toggle mode | Shows one image at a time; the preview switch action alternates IMAGE 1/IMAGE 2 | Requires a valid Image 2 for a meaningful toggle | Best for blinking registration and small local differences | Private preview only | Window lifetime; switching back from split clears split order |
| **Horizontal split (side by side with draggable divider)** | Exclusive mode action | Unselected | None | Horizontal split | Uses a vertical divider: Image 1 and Image 2 occupy left/right regions | A valid Image 2 supplies the overlay | Direct seam inspection; display conversion uses an 8-bit QImage overlay | Private preview only | Window lifetime |
| **Vertical split (top/bottom with draggable divider)** | Exclusive mode action | Unselected | None | Vertical split | Uses a horizontal divider: Image 1 and Image 2 occupy top/bottom regions | A valid Image 2 supplies the overlay | Direct seam inspection; display conversion uses an 8-bit QImage overlay | Private preview only | Window lifetime |
| **Image 1** | Open-image selector | Active image | None | Any currently open target supported by the launcher | Replaces the primary comparison image and rebuilds Image 2 choices | Changing it clears Image 2 to **No image** | No resampling or copying into a result | Still or current sequence image; all channels/types accepted | Window lifetime |
| **Image 2** | Same-size open-image selector | **No image** | None | Any other view with width and height exactly equal to Image 1 | Supplies the alternate or split overlay and second split histogram | Excludes Image 1 and every dimension mismatch; updates on view create, close, rename, or image change | No registration, channel, type, WCS, or intensity compatibility check | Still or another view's current image; no mask | Window lifetime |
| **Fit To Window** | Preview-toolbar action | Preview fitted initially | None | Click or **Ctrl+Shift+F** | Fits the whole comparison image in the preview | Private preview active | Broad context; resampling can hide pixel-scale differences | Display only | Preview lifetime |
| **Zoom In** | Preview-toolbar action | Current scale | None | Click; `1.25×` relative zoom | Enlarges around the viewport center | Private preview active | Better local seam/blink inspection | Display only | Preview lifetime |
| **Zoom Out** | Preview-toolbar action | Current scale | None | Click; `0.8×` relative zoom | Reduces the displayed scale | Private preview active | More context, less pixel detail | Display only | Preview lifetime |
| **Zoom 100%** | Preview-toolbar action | Current scale | None | Click; absolute scale `1.0` | Shows one-pixel viewer scale | Private preview active | Preferred for registration and interpolation inspection | Display only | Preview lifetime |
| **View Channel** | Preview-toolbar combo box | Follows the current Image 1 channel layout | None | RGB, grayscale, stored channels, and supported derived CIE/HSL/HSV/HSI/YCbCr display components | Changes only the displayed component | Choices follow Image 1/preview channel layout; does not alter histogram source data | Helps compare one component consistently; derived views add display work | Display only | Preview lifetime |
| **Display Mode** | Preview-toolbar combo box | **Linear** | None | Linear or Autostretch | Linear maps stored samples directly; Autostretch derives presentation statistics independently for the displayed comparison image | Private preview active | Independent stretches can conceal absolute intensity differences | Display only; histograms remain raw | Preview lifetime |
| **Autostretch Settings** | Preview-toolbar action | Initialized from the target view | None | Opens shadow clipping, target background, saturation, brightness, and contrast settings | Changes Autostretch presentation and its statistics/rendering path | Relevant when Display Mode is Autostretch | Display computation only; does not modify sources or snapshot pixels | Display only | Preview lifetime |
| Drag split divider | Pointer gesture | Center (`0.5`) | None | Left-drag within 6 display pixels of divider; position clamps to `[0,1]` | Changes how much of each image is visible | Horizontal or Vertical split with a valid overlay | Display-only | Private preview | Window lifetime |
| Double-click split divider | Pointer gesture | Not invoked | None | Left double-click within divider hit zone | Restores split position to `0.5` | Split mode with valid overlay | Display-only | Private preview | Window lifetime |
| Right-click split image | Pointer gesture | Image 1 on base side; Image 2 on overlay side | None | Right-click inside the image widget | Reverses the sides without changing selectors | Split mode | Display-only | Private preview | Window lifetime; returning to Toggle resets swapped state |
| Preview switch-image action | Preview toolbar action | Alternate rendering off initially | None | Click | Toggle mode blinks the two images; split mode reverses their sides | Valid Image 2 | Display-only | Private preview | Window lifetime |
| **Preview area** | Preview-toolbar checkbox | Unchecked; checking normally creates a centered `600×600` area when possible | None | Off/on with a movable/resizable selection | Limits the shared identity-copy preview execution to the selection, then composites it into the full Image 1 presentation | Private preview active; comparison overlay and raw histograms remain full-image features | Does not provide an area-only comparison result or snapshot | Preview/snapshot source handling | Preview lifetime |
| **Take snapshot** | Preview-toolbar action | No snapshot | None | Click or **Ctrl+Shift+K** | Re-executes the identity Image Compare operation and opens `<Image 1> - Image Compare - SNAPSHOT` | Private preview active | Allocates a full still; copies Image 1 stored pixels and metadata, not Image 2, the split, or the display stretch | Auxiliary still; sources unchanged | One new view per action |
| Histogram left-drag | Pointer gesture | Idle | None | Press/drag over a histogram | Displays bin coordinates/count information | Histogram visible | Inspection only | One histogram at a time | Not persisted |
| Histogram middle-drag | Pointer gesture | Idle | None | Press/drag over a zoomed histogram | Pans its scroll area | Useful after histogram zoom | Inspection only | One histogram at a time | Not persisted |
| **Close** | Dialog action | Available | None | One click | Closes the comparison window and removes preview bindings | Always available | Does not create a result or delete snapshots already created | Entire process | No process settings are saved |

## How the algorithm works

### Conceptual pipeline

1. The launcher requires an active image. The Image 2 list retains only other
   views whose width and height equal Image 1.
2. Toggle mode assigns Image 2 as the preview's alternate image. Split mode
   converts Image 2 to an 8-bit grayscale or RGB `QImage`, scales it to the
   current displayed image rectangle, and clips it to one side of a movable
   divider. There is no pixel arithmetic between images.
3. In Autostretch display mode, each image is copied and stretched from its own
   median/MAD statistics before display. Preview saturation and
   brightness/contrast adjustments are then applied. Histograms continue to use
   the unstretched stored images.
4. A separate histogram is built for every visible input. Inspection never
   modifies either source image, its metadata/history, or a file.
5. **Take snapshot** runs the process's identity operation on Image 1 and opens
   the result as a new still. The new view copies Image 1's stored pixels and
   metadata; it does not rasterize the comparison widget or include Image 2.

### Model and equations

Image Compare has no comparison metric or derived-comparison equation. Its
snapshot operation is the identity `S=A`, where `A` is Image 1. In split mode,
for a horizontal split fraction `s` in `[0,1]`, the overlay is drawn for display
coordinates `x >= sW` (or `x <= sW` after swapping), where `W` is the displayed
image width. Vertical mode applies the equivalent rule to `y` and height `H`.
The default is `s = 0.5`.

For a histogram with `B` bins, a normalized sample `v` is clamped to `[0,1]`
and counted in bin

`b = floor(clamp(v, 0, 1) * (B - 1))`.

Clipping percentages count every channel sample at `v <= 0` or `v >= 1` and
divide by `width * height * channels`. They are endpoint counts, not a
difference map and not proof that a display stretch is clipped. With
**Absolute scales** unchecked, visible channels share one vertical maximum;
checked, each channel uses its own maximum despite the control's current label.

### Boundary and failure behavior

- Dimension mismatch removes a view from Image 2 rather than resampling it.
- A missing, closed, renamed, or newly incompatible Image 2 is cleared or the
  selector list is rebuilt. No error dialog is required.
- Channel count, pixel type, WCS, crop origin, and intensity scale are not
  validated. The split conversion renders one-channel data as grayscale and
  other data through the engine's 8-bit interleaved RGB conversion.
- Image Compare does not test or correct translation, rotation, distortion, or
  normalization. Misregistration appears directly at the seam or while
  toggling.
- Histogram construction clamps finite out-of-range values to endpoint bins.
  The reviewed histogram path has no explicit non-finite guard; sanitize NaN or
  infinite samples before relying on its plot or clipping readout.
- There is no Apply action or comparison-result pipeline. Snapshot uses the
  shared preview worker to create an auxiliary Image 1 copy; it writes no file
  and does not capture Image 2, the divider, or display presentation. Closing
  detaches the private preview interactions and leaves completed snapshots open.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Toggle + Linear display | Same channel, 100% zoom, matched registered pair | Small differences are hard to localize | Not applicable; no strength control | Match geometry and display channel, then blink |
| Split + Linear display | Horizontal split at `0.5` | Seam does not cross the feature of interest | Divider at an edge hides one image | Select orientation, drag through background/stars, then swap sides |
| Autostretch comparison | Same autostretch settings, interpreted as structure only | Faint structure remains hard to see | Independent stretches make intensity equality look better than it is | Inspect Linear first, then use Autostretch for morphology |
| Histogram resolution + scale mode | 256 bins, shared vertical maximum | Narrow peaks merge | Very high bin counts look sparse/noisy and cost more | Choose bins, then decide shared versus per-channel peak scaling |

## Inputs, outputs, and metadata

The inspection path reads two open image objects and creates a private preview
plus temporary histogram/display buffers. It does not overwrite either input,
change source metadata/history, assign a mask, write a file, or produce a
derived comparison result. Split display is an 8-bit grayscale/RGB
visualization and must not be used to infer stored precision.

**Take snapshot** is a separate auxiliary-output path. It creates
`<Image 1> - Image Compare - SNAPSHOT` with Image 1's pixel type, channels,
stored samples, and copied metadata/history state. No new Image Compare history
description is supplied. The snapshot is unaffected by which image or split is
visible and by Linear/Autostretch display settings.

## Performance

Histogram construction scans every pixel and channel and stores one counter per
channel per selected bin. Raising plot resolution therefore increases histogram
memory and computation. Autostretch additionally copies and scans Image 2 for
statistics whenever its split overlay is rebuilt. Split rendering caches a
scaled pixmap for the current target size. No production timing or peak-memory
measurement was made.

## Worked example

- Input condition/provenance and settings: two open, already registered images
  with equal dimensions; Image 1 active; Image 2 selected; Linear display;
  Toggle followed by Horizontal split at `0.5`; 256-bin histograms.
- Expected checkpoint: Image 2 is selectable only when dimensions match; Toggle
  shows one labelled image at a time; Horizontal split shows two labelled
  distributions and a draggable centered divider; right-click reverses sides.
- Inspect at 100%/statistics/history: inspect stars and high-contrast edges at
  100%; use the raw histograms for distribution/clipping context. No history
  entry is created.
- Limitations: the example was not executed on approved astronomical data and
  makes no claim about visual quality, alignment accuracy, timing, or memory.

## Warnings and limitations

- Equal dimensions do not mean equal coordinates. Register and crop the images
  before interpreting pixel-local differences.
- There is no PSNR, SSIM, RMS error, correlation, difference image, or numeric
  threshold. Use [Image Statistics](../../interface/image-statistics.md) or
  [Pixel Math](../pixel-math/pixel-math.md) when a numerical or derived-image
  comparison is required.
- There is no background matching, exposure normalization, color matching, or
  channel conversion. Independent Autostretch displays can conceal these
  differences.
- An active Image 1 mask cannot change the identity snapshot: the shared mask
  post-blend combines Image 1 with an identical Image 1 copy.
- Sequence frames are not synchronized across two sequences.
- Split display is a screen visualization, not a precision-preserving output.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| The second image is absent | Image 2 must be another open view with exactly equal width and height | Resize/crop/register beforehand, or choose an equal-size view |
| Split mode shows only Image 1 | Image 2 is **No image**, closed, or became incompatible | Select a valid Image 2; reopen/recreate it if needed |
| Brightness agrees in Autostretch but histograms do not | Each image is stretched from its own statistics; histograms use raw stored data | Switch to Linear for absolute-intensity interpretation |
| Channel peaks look equally tall unexpectedly | Checked **Absolute scales** activates per-channel peak normalization | Uncheck it for one shared vertical count scale |
| Features jump while toggling | Images share dimensions but are not registered to the same coordinates | Register the pair; confirm crop/origin and distortion handling |
| No result appears after closing | **Close** creates nothing; only **Take snapshot** creates an auxiliary still | Use **Take snapshot** before closing for an Image 1 copy, or use Pixel Math/Image Blend for a derived comparison result |
| Snapshot does not contain the split or Image 2 | Snapshot executes the Image Compare identity operation on Image 1; it is not a screen capture | Use an external screen-capture workflow for the visual split, or Pixel Math for a derived image |

## Related workflow steps

Use [Image Registration](../registration-alignment/image-registration.md) before Image
Compare when coordinates do not already match. Use [Image Blend](image-blend.md)
to create a combined result, [Pixel Math](../pixel-math/pixel-math.md) to create
a difference or other expression, and [Image Viewer](../../interface/image-viewer.md)
to understand display channel, zoom, and stretch behavior. In a mosaic workflow,
comparison can reveal seam or registration differences but does not correct
them.
