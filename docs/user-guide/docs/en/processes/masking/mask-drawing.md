# Mask Drawing

<!-- MEDIA_SLOT: UG-MEDIA-100 -->

## Purpose and expected result

Mask Drawing is an interactive editor for building a grayscale mask over the RT
preview of an open image. It supports pressure-aware painting, erasing,
connected fill, a linear gradient, editable geometric shapes, import/export,
and a 100-step undo history. It is intended for local structures that an
automatic threshold or star detector cannot isolate reliably.

Selecting **OK** creates a neighboring, same-size grayscale image named
`<target-name> - MASK` in the target's representation and then closes the
window; it does not alter the target or automatically assign the result as that
target's active mask. Assignment is a separate **Mask > Select Mask** step.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Mask Generation > Mask Drawing** |
| Enabled when | An image or sequence is active, this process is not already attached to it, and no attached process holds exclusive access |
| Inputs | One target view; optional same-size image file (the import filter offers FITS, TIFF, PNG, and XISF) imported into the editor |
| Outputs | On still-image OK, one new same-size, one-channel image named `<target> - MASK` in the target's representation; on a sequence, a new sequence of that name whose every frame is the drawing. Export writes a copy of the drawing to a selected file under the application's export settings |
| Recommended image state | Any displayable target whose geometry matches the intended mask; linearity does not change mask construction, but judge the overlay under a stable display stretch |
| Preview support | Required RT preview: the preview shows the target, and the editable drawing is painted over it as a separate overlay; the checked **Preview** control is disabled |
| Sequence support | OK writes a new sequence named `<target> - MASK` in which every frame is the same drawing in that frame's representation; no focused test runs this process over a sequence |
| Mask support | The process does not consume the target's active mask; the generated image is not assigned automatically |
| External dependencies | None; import/export uses AfterNight's installed image codecs |

## Before you start

Open the target whose width and height the mask must match. A still image is
the tested path; a sequence target produces a new sequence that repeats the one
drawing in every frame. Choose a stable Linear or Autostretch display before
judging edges, because **Mask View** changes only the overlay presentation, not
stored mask values.

Mask Drawing keeps one in-window drawing, made from the application's engine
session at the target's width and height. A drawing the session's memory budget
cannot hold is refused with a message, and the window then has no drawing to
edit. Changing **Target Image** to an image of another size, or editing the
target to another size while the window is open, makes a new blank drawing and
discards the current one with its undo history. Changing to another target with
the same size retains the drawing, so clear or reset it deliberately if that
reuse is not intended.

## Procedure

1. Activate a representative still and open **Process > Mask Generation > Mask
   Drawing**. Confirm the intended **Target Image** and the checked, disabled
   **Preview** indicator.
2. Choose **Brush**. Start with Round, Size `24`, Hardness `1`, Opacity `1`,
   Flow `1`, Spacing `0.25`, Stabilization `0`, and Normal blend. Paint over the
   RT preview; the overlay should appear without replacing the displayed image.
   While the engine is busy with other work, strokes wait and land a moment
   later, in the order you drew them.
3. Change **Mask View** if the overlay is hard to distinguish. Red, Green, Blue,
   and Amber are colored overlays; Grayscale follows mask intensity;
   Checkerboard alternates two overlay colors. This choice does not change the
   output mask.
4. Refine the mask with **Eraser**, **Fill**, **Gradient**, or **Shape**. Use
   **Edit** to move or resize a committed shape; middle-drag a shape handle to
   rotate it, and press Delete to remove the selected shape.
5. Use **Undo** and **Redo** after each meaningful change; they, like **Clear**,
   land after the strokes made before them. **Clear** removes all paint and
   shapes but is itself undoable. **Reset** is broader: it makes a new blank
   drawing, restores all defaults, and removes the prior undo stack.
6. Optionally choose **Export Mask...** to save a copy of the drawing before
   closing the editor; it is written in the background under the application's
   export settings. Reopen it with **Import Mask...** only against a target of
   exactly the same dimensions; the file is read in the background and replaces
   the drawing once read.
7. Select **OK**. Confirm that a neighboring `<target> - MASK` image opens, the
   window closes, and the source remains unchanged. Activate the source and use
   **Mask > Select Mask** to assign the new image, then inspect polarity before
   enabling a mask-aware process.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Brush** | Exclusive tool button | Selected | None | Brush tool | Paints positive mask coverage with the current tip and blend mode | Enables Brush controls; disables Shapes controls | Dense strokes and soft tips increase touched pixels | RT drawing; mouse or tablet | Dialog lifetime; Reset selects |
| **Eraser** | Exclusive tool button | Off | None | Eraser tool | Multiplies existing mask values by inverse stroke coverage | Brush controls enabled; Blend Mode disabled and forced to Erase | Erasing over editable shapes flattens all shapes first; its undo restores them as shapes | RT drawing | Dialog lifetime |
| **Shape** | Exclusive tool button | Off | None | Shape-draw tool | Creates the selected geometric shape | Shapes controls enabled; Brush controls disabled, although the last Size and Opacity still determine line width and shape opacity | Each shape keeps one feathered layer over the area it can reach, drawn when the shape is made or changed | RT drawing | Dialog lifetime |
| **Edit** | Exclusive tool button | Off | None | Shape-selection tool | Selects, moves, resizes, or rotates committed shapes | Shapes and Brush control groups disabled | Editing is nondestructive until committed on release; each commit keeps an undo step holding the shape list and no paint | RT drawing | Dialog lifetime |
| **Fill** | Exclusive tool button | Off | None | Four-connected flood fill | Blends the Opacity value, with the selected Blend Mode, into the region connected to the clicked pixel whose mask values are within fixed normalized tolerance `0.05` of it | Brush group enabled, but only Opacity and Blend Mode affect Fill | Can traverse the full image; flattens editable shapes when it changes anything; its undo step keeps the changed rectangle | RT drawing | Dialog lifetime |
| **Gradient** | Exclusive tool button | Off | None | Click-drag linear gradient | Applies a full-canvas projection from `0` at the start point to Opacity at the end point | Brush group enabled, but only Opacity and Blend Mode affect Gradient; zero-length drag does nothing | Full-image operation; flattens editable shapes; its undo step keeps the whole paint | RT drawing | Dialog lifetime |
| **Tip Shape** | Selector | Round | None | Round, Square | Square uses maximum-axis distance and fills tip corners; Round uses radial distance | Brush/Eraser/Fill/Gradient group enabled, but used only by Brush/Eraser | Square affects more corner pixels at the same Size | Brush/Eraser strokes | Dialog lifetime; Reset Round |
| **Size** | Integer pixels + slider | `24` px | None | `1..200`, step `1` | Increases brush diameter; while Shape is active, the last value gives Line width `max(1, Size/4)` | Brush group enabled for Brush/Eraser/Fill/Gradient; ignored by Fill/Gradient; inherited but not editable for Shape | Larger tips touch more pixels per dab; larger line shapes are wider | RT drawing | Dialog lifetime; Reset `24` |
| **Hardness** | Normalized double + slider | `1.00` | None | `0..1`, step `0.05` | Moves the fully covered inner radius outward; values below 1 use a Gaussian-like falloff to zero at the tip radius | Used by Brush/Eraser only; ignored by Fill/Gradient | Low values produce wider soft transitions | RT drawing | Dialog lifetime; Reset `1` |
| **Opacity** | Normalized double + slider | `1.00` | None | `0..1`, step `0.05` | Scales the final stroke blend, supplies Fill value and Gradient end value, and becomes shape opacity | Brush group disabled for Shape, but its last value is inherited by new shapes | Lower values build softer/weaker masks; `0` can make an operation invisible | All drawing engines | Dialog lifetime; Reset `1` |
| **Flow** | Normalized double + slider | `1.00` | None | `0..1`, step `0.05` | Scales each brush dab before repeated dabs accumulate | Brush/Eraser only; ignored by Fill/Gradient | Lower flow builds gradually; tighter spacing then accumulates more strongly | Brush/Eraser strokes | Dialog lifetime; Reset `1` |
| **Spacing** | Tip-size fraction + slider | `0.25` | None | `0.01..1`, step `0.01`; actual spacing is at least 1 px | Raises or lowers distance between interpolated dabs | Brush/Eraser only; ignored by Fill/Gradient | High values can make separated/scalloped marks; low values increase work and flow buildup | Brush/Eraser strokes | Dialog lifetime; Reset `0.25` |
| **Stabilization** | Normalized double | `0.00` | None | `0..1`, step `0.05` | Raises cursor smoothing; each intermediate point follows a fraction `1-0.85s` of the new delta | Brush/Eraser mouse/tablet movement; first and release points are unsmoothed | High values reduce hand jitter but lag tight turns | Brush/Eraser strokes | Dialog lifetime; Reset `0` |
| **Blend Mode** | Selector | Normal | None | Normal, Multiply, Screen, Overlay | Chooses how Brush, Fill, and Gradient combine with the existing mask values | Disabled for Eraser; not used by Shape | Multiply can darken; Screen can only raise against nonzero input; Overlay is base-dependent | Brush/Fill/Gradient | Dialog lifetime; Reset Normal |
| **Shape** | Selector | Rectangle | None | Rectangle, Ellipse, Circle, Polygon, Line | Chooses drag geometry or a click-defined path | Enabled only for Shape tool | Polygon/Line need multiple vertices and a double-click to commit | Shape tool | Dialog lifetime; Reset Rectangle |
| **Feather** | Double pixels | `0.0` px | None | `0..100`, step `0.5` | Blurs the rasterized shape with Gaussian sigma `max(0.5, Feather/2)` | Enabled only for Shape; applies to filled and outline shapes | Larger values expand/soften edges and widen the area the shape's layer covers | Shape tool | Dialog lifetime; Reset `0` |
| **Filled** | Checkbox | Checked | None | Off/on | Checked fills Circle/Ellipse/Rectangle/Polygon; unchecked draws a 1-pixel antialiased outline | Enabled only for Shape; Line is always an outline | Outlines make narrow masks; feather can broaden them | Shape tool | Dialog lifetime; Reset checked |
| **Mask View** | Selector | Red | None | Red, Green, Blue, Amber, Grayscale, Checkerboard | Changes only cached overlay color/intensity presentation | Available while dialog is active | Full overlay cache rebuild on change; no output effect | Preview display only | Dialog lifetime; Reset Red |
| **Import Mask...** | File-open action | No file | Last browse location may be remembered by shared file context | Filter: FIT/FITS/TIF/TIFF/PNG/XISF or all files; exact target dimensions required | Reads the file in the background, then replaces the paint with its grey values (a color image's Rec. 601 luma, `0.299R+0.587G+0.114B` in floating point) clamped to `[0,1]`, a sample that is not a number read as no coverage, and removes editable shapes, after the edits before it | Requires a drawing; a file of another size is refused by name | Full-image read and conversion; its undo step keeps the whole paint | Editor document | Document change is undoable |
| **Export Mask...** | File-save action | No file | Last browse location may be remembered by shared file context | User-selected path; the extension names the container: FITS, XISF, TIFF, PNG, or JPEG | Writes a copy of the drawing as it stands, in the background, under the application's export settings | Requires a drawing; an empty drawing is still exportable | Full-image copy plus codec/disk cost; PNG is written at 16 bits and JPEG at 8 | File output only | Does not change document/history |
| **Undo** | Action button | Disabled | None | Up to 100 kept changes | Reverts the latest stroke, shape, import, fill, gradient, or clear change, after the edits before it | Enabled only when history exists | Steps keep only the paint a change touched (a shape edit keeps none) and are admitted against the session's undo memory allowance | Editor document | Dialog lifetime |
| **Redo** | Action button | Disabled | None | Available until a new change branches history | Reapplies the next reverted change, after the edits before it | Enabled only after Undo | Same memory characteristics as Undo | Editor document | Dialog lifetime |
| **Clear** | Action button | Enabled | None | One action | Clears paint and editable shapes, after the edits before it | No effect when already empty | Its undo step keeps the painted rectangle; undoable | Editor document | Dialog lifetime |
| **Target Image** | Image selector | Active launch target | Open-image selection can change while dialog remains open | Open compatible target; drawing geometry follows width/height | Changes the image under the overlay | A target of another size makes a new blank drawing; same size retains drawing | Size change discards current drawing/undo state | RT preview and OK | Selection lives for dialog |
| **Preview** | Disabled checkbox | Checked | None | Fixed on | Indicates the drawing canvas requires RT preview | Cannot be unchecked | Live overlay redraw/caching | Preview only | Fixed |
| **OK** | Dialog action | Enabled | None | One action | Takes a copy of the drawing as it stands, creates `<target> - MASK` from it on a worker in the target's representation, then closes | Controls/listener are disabled during execution; a sequence run shows a progress window whose Cancel stops it | Full-image copy and conversion; the source remains unchanged | Still image, or a new sequence | Successful close discards editor state |
| **Cancel** | Dialog action | Enabled | None | One action | Closes without creating the new image | Does not undo an already exported file | Unsaved editor document is lost | Dialog only | None |
| **Reset** | Dialog action | Enabled | None | One action | Makes a new blank drawing at the target's size and restores all toolbar defaults | Requires a current target to make a drawing | Clears drawing and all undo/redo history, unlike Clear | Dialog only | Defaults listed above |
| **[ / ]** | In-window shortcuts | Available | None | Left/right bracket | Decreases/increases Size by 1 within `1..200` | Widget-with-children scope | Same Size trade-offs | Dialog/tool state | Dialog lifetime |
| **X** | In-window shortcut | Available | None | Key X | Toggles Brush/Eraser | Widget-with-children scope | Eraser semantics apply immediately | Dialog/tool state | Dialog lifetime |
| **F / G** | In-window shortcuts | Available | None | F selects Fill; G selects Gradient | Activates the named tool and synchronizes its state | Widget-with-children scope | Same Fill/Gradient costs | Dialog/tool state | Dialog lifetime |
| **Delete** | In-window shortcut | Available | None | Delete key | Removes the selected editable shape | Effective only after Edit selects a shape | Undo step keeps the shape list, no paint | Shape document | Undoable |
| **Undo/Redo shortcut** | Standard platform shortcuts | Available | None | Usually Ctrl+Z / Ctrl+Y or platform equivalent | Same as Undo/Redo buttons, landing through the same queue | Widget-with-children scope and matching history state | Same history memory | Editor document | Dialog lifetime |

## How the algorithm works

### Conceptual pipeline

1. The window makes a drawing from the application's engine session at the
   target's width and height: the paint, a copy of the paint as it was before
   the active stroke, the stroke's own dabs, and the mask - the paint with the
   editable shapes screened over it. Each is a zeroed, one-channel 32-bit float
   image the engine counts against its memory budget.
2. Every edit - a stroke point, a fill, a gradient, a shape, an import, Undo,
   Redo, Clear - is queued in the order it was made and lands under an engine
   operation the window asks for without waiting. When the engine is busy the
   queue tries again a moment later, so no input is dropped and the window never
   stalls; the overlay redraws every area the landed edits changed.
3. Brush samples are joined by Catmull-Rom interpolation. Round or square tip
   coverage is stamped at `max(1, Size×Spacing)` pixel intervals; hardness
   controls the tip's full-strength core and soft falloff. Tablet pressure
   multiplies Flow.
4. A brush stroke lands against the paint as it was before the stroke began;
   Fill and Gradient blend into the current mask. Each shape is antialiased into
   one layer over the area it can reach, optionally Gaussian-feathered, and
   screened over the paint at its opacity.
5. **OK** takes a copy of the mask when it is selected. The run, on a worker,
   writes the copy's values into a new grey image in the target's
   representation, and the window opens it beside the target.

### Model and equations

For a brush dab, let `q` be tip coverage, `f` Flow, and `p` tablet pressure
(`p=1` for mouse input; a pressure that is not a number lays nothing). Repeated
overlapping dabs accumulate stroke coverage

`v_new = 1 - (1 - v_old)(1 - q f p)`.

The tip is Size rounded up to a whole, odd number of pixels, and each dab sits
on the nearest whole pixel. Hardness `h` makes the region inside radius `h r`
fully covered, where `r` is half Size. Between `h r` and `r`, coverage follows
the normalized falloff `(exp(-4t²) - exp(-4)) / (1 - exp(-4))`, with `t`
running from `0` at `h r` to `1` at `r`. Outside `r`, coverage is zero. A
square tip uses `max(|dx|,|dy|)` for distance; a round tip uses Euclidean
distance.

Let `b` be the existing value - for a brush stroke, the paint as it was before
the stroke began; for Fill and Gradient, the current mask - `v` the accumulated
stroke/fill/gradient value, and `o` Opacity. Normal uses `T=v`; Multiply uses
`T=bv`; Screen uses `T=1-(1-b)(1-v)`; Overlay uses `T=2bv` for `b<0.5` and
`T=1-2(1-b)(1-v)` otherwise. The stored result is

`R = clamp(b + (T-b)o, 0, 1)`.

A pixel the stroke does not cover keeps `b`. Eraser instead uses
`R=clamp(b(1-vo),0,1)`. Fill always passes an operation opacity of 1 and uses
the toolbar Opacity as `v`; it follows four-connected neighbors whose original
composite values are within `0.05` of the clicked seed. Gradient projects every
pixel onto the drag vector, clamps projection `t` to `[0,1]`, and uses
`v=t×Opacity`.

Each shape layer is antialiased and, when Feather is positive, blurred with the
engine's exact Gaussian of `sigma=max(0.5,Feather/2)`: one kernel on the
engine's compiled single-precision path, or, when Feather is above `64` and the
engine does not run that path, several exact passes. The layers are then
screened over the paint one after another, each at its inherited Opacity `o`:
`R=1-(1-b)(1-o s)`, where `s` is the layer's coverage. Overlapping positive
shapes therefore approach white rather than subtracting from one another.

### Boundary and failure behavior

Pointer input outside the scaled image is rejected; accepted coordinates are
clamped to the first/last image pixel. Brush rectangles are intersected with
the canvas. Shapes may extend beyond the target, but a shape's layer covers only
the part of the image its outline and feather can reach, so its raster is
clipped to the target bounds. Fill never crosses the image edge and uses
four-neighbor connectivity. Gradient is evaluated across the entire bounded
canvas.

Fill, Gradient, Import, and Eraser-over-shapes flatten the current composite
into paint and clear shape editability before applying their change. This
preserves visible coverage but means those shapes can no longer be selected as
objects afterward; undoing that change restores them as shapes. An eraser
stroke that lays nothing, or a fill that changes nothing, leaves the shapes as
they were.

Import reads the file in the background. A file that cannot be read is reported
with the reason, and a file of another size is refused with **The imported mask
must match the target image size (W × H).**; either way the drawing is
unchanged. If the drawing is replaced - by Reset or a target of another size -
before the file has been read, the import is dropped. Export writes a copy of
the drawing in the background and reports a failure with its reason. Both report
through the application's error notification once their work finishes, so the
window keeps responding while a file is read or written.

**OK** runs on a worker. A still-image run shows no progress window; a sequence
run shows one whose **Cancel** stops it. A stopped run creates nothing and
shows no error; a run the engine refuses creates nothing and reports why under
**Error executing the process**.

Undo keeps at most 100 steps. A step keeps only the paint its change touched,
before and after: a stroke the rectangle its dabs reached, a fill the rectangle
it changed, a clear the rectangle the paint covered, and a gradient or an import
the whole paint. A shape added, moved, or removed keeps no paint, only the shape
list with its layers; a change that flattened shapes also keeps the shapes'
rectangle and list. Steps are admitted against the session's memory allowance
for undo: redo steps and then the oldest steps are dropped to make room, and an
edit whose step still cannot be admitted is not made (a stroke is taken back),
without a message. A new edit after Undo discards the redo branch.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Opaque painted region | Round, Size `24`, Hardness `1`, Opacity `1`, Flow `1`, Spacing `0.25` | Gaps or incomplete coverage remain | Boundary covers adjacent structure | Size, then Hardness, then Opacity/Flow |
| Soft brush transition | Hardness `0.5`, Opacity `0.5`, Flow `0.5`, Spacing `0.25` | Edge remains abrupt or stroke builds too slowly | Broad gray skirt affects protected pixels | Size, Hardness, Flow, Opacity, then Spacing |
| Smooth hand-drawn contour | Stabilization `0`, raise gradually | Cursor jitter remains | Stroke lags corners and release endpoint can kink | Size first, then Stabilization |
| Editable geometric mask | Shape, Rectangle/Ellipse/Circle, Feather `0`, Filled on | Edge too hard or geometry misses structure | Feather bleeds beyond intended boundary | Geometry, edit handles, Feather, then inherited Opacity |
| Connected fill | Fill, Normal, Opacity `1` | Similar region remains partly unchanged because a >0.05 boundary blocks propagation | Connected background is filled unintentionally | Inspect seed region, Undo if needed, then choose a better seed/import separation |
| Directional gradient | Gradient, Normal, Opacity `1` | Transition ends too low | White coverage extends too far | Drag direction/length first, then Opacity |

## Inputs, outputs, and metadata

The in-window drawing and an exported file are one-channel masks clamped to
`[0,1]`; the drawing holds 32-bit float values. Import accepts any image the
application reads whose width and height match exactly: a grey image's samples
are read as fractions of full scale, a red, green, and blue image (with or
without alpha) is reduced to its Rec. 601 luma, and an image with other
channels is refused. A sample holding no number reads as zero, every value is
clamped to `[0,1]`, and editable shapes are removed. Import and Export do not
resample or register images.

**OK** writes the drawing into a new grey image of the target's size, origin,
and pixel aspect, in the target's representation: a floating target receives
the values as they are, and an integer target receives each value as a fraction
of its range - from zero to the largest code for an unsigned type, from the
smallest to the largest code for a signed one - rounded to the nearest code.
This quantizes soft values for integer targets. The copy the run uses is taken
when **OK** is selected, so an edit still waiting for a busy engine is not in
it. The new image is named `<target-name> - MASK`; the target is not modified.
It keeps the target's acquisition record, astrometric solution, and lineage,
carries none of the target's header cards, is not recorded as nonlinear, and
its lineage gains an entry naming the drawing document. The process adds the
history description **Create painted grayscale mask** to the new view.

Export writes a copy of the drawing as it stands, independent of **OK**, under
the application's export settings. The file's extension names the container:
FITS, XISF, and TIFF keep the 32-bit float values, PNG is written at 16 bits
and JPEG at 8, and the compression is the one the export settings give for that
container; any other extension is refused. A successful export does not mark an
undo step, close the dialog, or assign the file/image as a target mask.

Over a sequence, **OK** reads each frame and writes the same drawing into it in
that frame's representation, producing a new sequence named
`<target-name> - MASK` beside the target; a frame of another size than the
drawing stops the run. This path is derived from the source only.

## Performance

Brush updates scale with the stamped area and number of interpolated dabs.
Spacing below one brush diameter increases both stroke density and work. Fill
can visit every pixel. Gradient, import, export, output creation, and a
whole-overlay redraw are full-image operations; ordinary edits rebuild and
redraw only the areas they changed.

The drawing itself holds four full-size 32-bit float planes - the paint, the
paint before the active stroke, the stroke, and the mask - about 16 bytes per
pixel. One 6000×4000 plane is about 96 MB, so such a drawing is about 384 MB,
and the session's memory budget refuses a drawing it cannot hold before anything
is drawn. A run is handed another full float copy, kept until the drawing
changes, each Export takes one more, and the output image adds its own
representation. Undo steps hold only the patches their changes touched, but a
gradient or an import keeps two whole-paint copies per step. Each editable shape
keeps one feathered layer over the area it can reach, drawn once when the shape
is made or changed; a large Feather widens that area. Undo patches and shape
layers are counted against the session's memory allowances. Use fewer
full-image steps, clear history by Reset when appropriate, and prototype
geometry on a smaller same-aspect target. No production timing or peak-memory
measurement was made.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: finite same-size still, Brush Round,
  Size `24`, Hardness `1`, Opacity `1`, Flow `1`, Spacing `0.25`, Normal blend;
  then one filled Rectangle with Feather `0`; no approved astronomical data.
- Expected checkpoint: brush and rectangle centers reach positive/near-white
  mask coverage, untouched pixels remain zero, Undo removes the last committed
  object, Redo restores it, and **OK** creates a same-size one-channel
  `<target> - MASK` view in the target's representation, closes the window, and
  leaves the target unchanged.
- Inspect at 100%/statistics/history: check brush spacing and shape edges at
  100%, verify mask minimum/maximum and channel count in Image Statistics, and
  confirm **Create painted grayscale mask** belongs to the new view.
- Limitations: the numerical/undo checkpoints are covered by focused tests, but
  this workflow was not executed on approved astronomical data; visual quality,
  production timing, peak memory, and sequence output remain unclaimed.

## Warnings and limitations

- Prefer a still image. Over a sequence, **OK** repeats the one drawing in
  every frame of a new sequence; no focused test covers that path, so inspect
  the result.
- **OK** does not assign the result as a mask. Activate the intended target and
  use **Mask > Select Mask** afterward.
- **OK** copies the drawing as it stands when selected; an edit still waiting
  for a busy engine is not included.
- Changing targets across different dimensions, or editing the target to
  another size, silently starts a blank drawing; changing between equal-size
  targets retains the current drawing.
- Fill, Gradient, Import, and Eraser-over-shapes flatten editable shapes. Undo
  immediately if you need to recover object editability.
- Reset is not equivalent to Clear: Reset discards the drawing and history;
  Clear is undoable.
- Shape uses the last Brush Size and Opacity even though Brush controls are
  disabled while Shape is active. Set those values before selecting Shape.
- Fill and Gradient leave most Brush controls enabled even though only Opacity
  and Blend Mode affect them.
- Soft masks are quantized when **OK** writes them into an integer target type.
  Export the drawing as FITS, XISF, or TIFF when retaining float precision is
  important.
- No mask-aware post-blend, registration, resampling, automatic selection, or
  clipping warning is provided, and a still-image **OK** shows no progress
  window.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Preview cannot be unchecked | The canvas requires the RT preview and the control is intentionally checked/disabled | Leave it enabled; use Switch image/display controls to inspect the underlying target | UI builder and process setup |
| A new target shows a blank document | Width or height differs from the prior target, or the target was edited to another size | Return to the prior target if still available, or export before switching geometry | `syncDocumentToTargetImage()` |
| A same-size target unexpectedly shows the old drawing | Document reset is geometry-based, not target-identity-based | Select Reset or Clear before drawing for the new target | `syncDocumentToTargetImage()` |
| An error titled **Mask Drawing** appears and nothing can be drawn | The drawing could not be made from the engine session, for example because the memory budget cannot hold it | Free memory or use a smaller target, then select Reset or reopen the window | `replaceDocument()` |
| **Open a target image before importing a mask.** or **There is no mask to export yet.** | The window has no drawing | Activate a target, then select Reset or reopen the window | `importMask()`, `exportMask()` |
| **The imported mask must match the target image size (W × H).** | Imported width/height differs; no resampling occurs | Resize/register it separately to exact geometry, then import again | `importMaskFrom()` |
| **Failed to import the selected mask.** followed by the reason | The file could not be read: missing, unreadable, or not an image the application reads | Verify the file opens normally and use a supported FITS/TIFF/PNG/XISF mask | `importMaskFrom()` |
| **Failed to export the current mask.** followed by the reason | Path, container, permission, or disk failure; an extension other than FITS, XISF, TIFF, PNG, or JPEG is refused | Choose a writable path with a supported suffix and retry; the drawing remains open | `exportMaskTo()` |
| **Error executing the process** naming *there is no drawing to create a mask from* | **OK** was selected while the window had no drawing | Activate a target, select Reset or reopen the window, and draw again | `executeTypedProcessImpl()` |
| Strokes appear a moment after they are drawn | The engine is busy with a preview or another process; edits wait in order and land when it admits them | Keep drawing; nothing is dropped | `DrawingEditQueue` |
| Fill crosses more area than expected | Four-connected samples stayed within fixed `0.05` of the clicked seed | Undo and choose a more isolated seed or prepare a stronger boundary | `floodFill()` |
| Shape cannot be edited after Fill/Gradient/Eraser/Import | That operation flattened shape objects into paint | Undo restores them as shapes; edit shapes first, then flattening operations last | `DrawingDocument` state transitions |
| Soft output becomes banded | OK wrote the float mask into an integer target pixel type | Export the float mask as FITS, XISF, or TIFF, or use a float target before OK | output conversion path |

## Related workflow steps

Use [Range Selection](../analysis/range-selection.md), [Star Mask](../star-operations/star-mask.md),
or [Binarize](binarize.md) when the mask can be derived automatically. Refine a
binary result with [Morphological Transformation](morphological-transformation.md).
After drawing, follow [Masks](../../interface/masks.md) to select the generated
image, inspect its polarity, and enable it on the intended target.

## References and provenance

This page documents AfterNight's current drawing document, edit queue,
brush/shape engines, the engine's drawing primitives and layer blend, process
framework, and focused tests. The equations are
current implementation contracts; no external drawing standard or
executed-data benchmark is claimed.
