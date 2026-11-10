---
kind: process
stage: any
menu: Process > Mask Generation > Mask Drawing
summary: >-
  Paints a grayscale mask by hand over the preview of an image, with a brush, an eraser, a fill, a gradient and
  shapes you can still move, and opens it as a new image.
data_in: either
data_out: either
destructive: false
preview: real-time
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Mask Drawing window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    One image or sequence, whose width and height the mask takes, and optionally an image file of exactly that size
    to start from.
  outputs: >-
    One new grayscale image named after the target with " - MASK", beside it, in the target's pixel type, or a new
    sequence of that name. The target stays as it is. Export Mask writes a file only when you ask.
  state: >-
    Any image the mask is for, linear or stretched, shown under a display that lets you see the structures you
    outline.
derived: false
rewritten: true
---
# Mask Drawing

You paint a [mask](../../reference/glossary.md#mask) by hand in this window, over the preview of an image. A brush, an
eraser, a fill, a gradient and shapes you can still move and resize lay values from 0 to 1, which the preview shows
as a colored tint. Use it where no threshold or star detector finds the region you mean.

**OK** opens the drawing as a new grayscale image, named after the target with ` - MASK`, beside the target and in
its [pixel type](../../reference/glossary.md#pixel-type), and closes the window. The target is not changed, and the
new image is not attached to it as its mask.

```figure
image: ../../assets/images/processes/masking/mask-drawing.webp
alt: >-
  The Mask Drawing window. On the left, the Tool group with six buttons and Shape pressed, the Brush group grayed out
  with Tip Shape, Size, Hardness, Opacity, Flow, Spacing, Stabilization and Blend Mode, the Shapes group with Shape at
  Ellipse, Feather at 100.0 and Filled ticked, Mask View at Red, the Import Mask and Export Mask buttons, Undo, Redo
  and Clear, Target Image and a grayed Preview box. On the right, the preview of a red nebula with a red-tinted
  ellipse over the middle of the frame, outlined, with four square handles on its sides. Reset, OK and Cancel run along the
  bottom.
caption: >-
  Mask Drawing on the linear Carina master in the Autostretch display, with one ellipse drawn at Feather 100. The
  tint is the mask, and the handles show that the ellipse can still be edited.
pins:
  - {n: 2, x: 3.4, y: 29.6}
  - {n: 3, x: 3.4, y: 53.3}
  - {n: 4, x: 3.4, y: 19.7}
  - {n: 5, x: 34.7, y: 19.7}
  - {n: 6, x: 3.4, y: 80.1}
  - {n: 7, x: 3.4, y: 65.7}
  - {n: 8, x: 3.4, y: 72.8}
  - {n: 9, x: 78.3, y: 97.6}
```

## Before you start

- The mask takes the target's width and height. Open the image the mask is for, or one of exactly its size.
- **Size** and **Feather** are in pixels of the image. At a zoom that fits a large image in the pane, the brush is
  small: zoom in to paint detail, and use a shape or a fill for large areas.
- Choose the preview's **Display Mode** before you judge edges. The tint of **Mask View** changes nothing in the
  mask.
- The drawing lives in the window only. **Cancel**, the window's close button, **Reset** and a target of another
  size discard it with its undo steps. **Export Mask...** keeps a copy in a file.
- The drawing holds four planes of 32-bit floating point at the image's size, about 16 bytes a pixel: about 630 MB
  for an image of 7246 × 5429 pixels.
- A mask on the target's view plays no part here: the window neither reads it nor blends through it.

## Steps

1. **Open the window.** Make the image the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Mask Generation > Mask Drawing**. The preview shows the image with nothing drawn, and **Brush** is
   the tool.
2. **Paint.** Set **Size**, **Hardness** and **Opacity**, then drag over the preview with the left button. What you
   paint takes a red tint, stronger where the mask is higher.
3. **Draw a shape.** Choose **Shape**, pick a shape in the **Shape** box and set **Feather**. Drag from corner to
   corner, or from the center for a **Circle**. For **Polygon** and **Line**, click each point and double-click the
   last one.
4. **Edit the shapes.** Choose **Edit**. Drag a shape to move it, drag a handle to resize it, or drag a handle with
   the middle button to rotate it. ++delete++ removes the selected shape.
5. **Fill or fade.** Choose **Fill** and click a region to set it to **Opacity**, or **Gradient** and drag to fade
   from 0 to **Opacity**. Both turn the shapes into paint that can no longer be edited.
6. **Correct.** Choose **Eraser**, or press ++x++, and paint over what you want to remove. **Undo** and **Redo**
   step through your changes.
7. **Check the mask.** Zoom the preview in on the edges. Choose another **Mask View** when the tint is hard to see
   on the image.
8. **Keep a copy.** To use the drawing again later, choose **Export Mask...** and save it as
   [FITS](../../reference/glossary.md#fits), [XISF](../../reference/glossary.md#xisf) or TIFF.
9. **Create the mask.** Choose **OK**. A new image named after the target with ` - MASK` opens beside it, white
   where you painted, and the window closes.

## Controls

**Target Image**, **Preview**, the preview pane, **Cancel** and the progress window of a
[sequence](../../reference/glossary.md#sequence) work as in every process window: see
[Process Windows](../../interface/process-windows.md). The window has no histogram and no **Channel**.

```controls
window: Mask Drawing
controls:
  - name: Brush
    kind: button
    default: "On"
    effect: Paints with the tip, the values and the blend mode of the **Brush** group.
    enabled: Pressing it enables the **Brush** group and disables the **Shapes** group. ++x++ switches between **Brush** and **Eraser**.
    tradeoff: A large or soft tip touches more pixels with each dab, which costs more work per stroke.
    scope: The drawing, with the mouse or a tablet, whose pressure scales **Flow**. A pressure that is not a number lays nothing.
    persistence: Until the window closes. **Reset** chooses it again.
  - name: Eraser
    kind: button
    default: "Off"
    effect: Takes paint away with the same tip. Each pixel keeps its value times one minus what the stroke covers at **Opacity**.
    enabled: The **Brush** group stays enabled, except **Blend Mode**, which keeps the choice it shows and is not used.
    tradeoff: An eraser stroke over shapes first turns every shape into paint, which can no longer be edited. Its undo step brings the shapes back.
    scope: The drawing.
  - name: Shape
    object: toolShapeButton
    kind: button
    default: "Off"
    effect: Draws the shape chosen in the **Shapes** group. Each shape stays an object you can move, resize, rotate and delete with **Edit**.
    enabled: Pressing it enables the **Shapes** group and disables the **Brush** group. A new shape still takes its opacity from **Opacity**, and a **Line** its width from **Size**, as they were last set.
    tradeoff: Each shape keeps a layer of its own over the part of the image it can reach, drawn again only when the shape changes and counted against the memory the session allows.
    scope: The drawing.
  - name: Edit
    kind: button
    default: "Off"
    effect: Selects, moves, resizes and rotates the shapes already drawn.
    enabled: Pressing it disables the **Brush** and **Shapes** groups.
    tradeoff: A change lands when you release the button, as one undo step that holds the list of shapes and no paint.
    scope: The shapes only. Paint cannot be selected.
  - name: Fill
    kind: button
    default: "Off"
    effect: A click blends the value of **Opacity**, with **Blend Mode**, into the region connected to the pixel you click. The region holds the pixels whose mask value is within 0.05 of that pixel's, reached through their four side neighbours.
    enabled: The **Brush** group stays enabled, but only **Opacity** and **Blend Mode** act on a fill. ++f++ chooses it.
    tradeoff: A fill can visit every pixel of the image. One that changes something turns the shapes into paint first. Its undo step keeps the rectangle it changed.
    scope: The drawing as it is, shapes included. The tolerance cannot be changed.
  - name: Gradient
    kind: button
    default: "Off"
    effect: A drag lays a straight ramp across the whole image, from 0 at the line through the start to **Opacity** at the line through the end, blended with **Blend Mode**.
    enabled: The **Brush** group stays enabled, but only **Opacity** and **Blend Mode** act on a gradient. A drag of no length does nothing. ++g++ chooses it.
    tradeoff: In **Normal** the ramp replaces the whole mask, paint included. **Multiply** fades what is painted and **Screen** adds the ramp to it. It turns the shapes into paint, and its undo step keeps the whole paint.
    scope: Every pixel of the image.
  - name: Tip Shape
    kind: combo
    default: Round
    options: [Round, Square]
    effect: "**Round** measures a pixel's distance from the tip's center as a circle does, **Square** by the larger of its two offsets, which fills the corners."
    enabled: With **Brush** and **Eraser**. The cursor over the preview takes the same shape.
    tradeoff: At one **Size**, a square tip covers more pixels.
    persistence: Until the window closes. **Reset** returns it to **Round**.
  - name: Size
    kind: spinbox
    default: "24"
    widget: {min: 1, max: 200, step: 1}
    effect: Sets the tip's width in pixels of the image. Higher paints wider strokes.
    range: The tip is the size rounded up to a whole, odd number of pixels, and each dab sits on the nearest whole pixel. `[` and `]` lower and raise it by 1.
    enabled: With **Brush** and **Eraser**. A fill and a gradient do not use it. A **Line** shape is a quarter of it wide, and at least 1 pixel.
    tradeoff: A larger tip touches more pixels with each dab.
    scope: New strokes and new lines.
    persistence: Until the window closes. **Reset** returns it to 24.
  - name: Hardness
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets the share of the tip's radius that covers fully. Lower softens the edge of the stroke, which falls off to nothing at the radius.
    range: The slider moves in steps of 0.01.
    enabled: With **Brush** and **Eraser**. A fill and a gradient do not use it.
    tradeoff: A low value leaves a wide gray rim around each stroke.
    persistence: Until the window closes. **Reset** returns it to 1.
  - name: Opacity
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how far a stroke moves the mask toward what it paints. It is also the value a fill lays, the end of a gradient and the opacity of a new shape. Lower builds a weaker mask.
    range: It caps a whole stroke, however many of its dabs overlap. The slider moves in steps of 0.01.
    enabled: Disabled with **Shape**, which still takes its last value.
    tradeoff: At 0 a stroke, a fill or a shape changes nothing.
    scope: Every tool that draws.
    persistence: Until the window closes. **Reset** returns it to 1.
  - name: Flow
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how much of the tip each dab lays down. Lower builds a stroke up gradually where its dabs overlap.
    range: Tablet pressure multiplies it. The slider moves in steps of 0.01.
    enabled: With **Brush** and **Eraser**. A fill and a gradient do not use it.
    tradeoff: At a low **Flow**, a closer **Spacing** makes the stroke denser.
    persistence: Until the window closes. **Reset** returns it to 1.
  - name: Spacing
    kind: spinbox
    default: "0.25"
    widget: {min: 0.01, max: 1, step: 0.01, decimals: 2}
    effect: Sets the distance between dabs along a stroke, as a share of **Size**. Higher spreads them apart, lower packs them closer.
    range: The distance is never less than 1 pixel.
    enabled: With **Brush** and **Eraser**. A fill and a gradient do not use it.
    tradeoff: High values leave scalloped or separate marks. Low values cost more work and add up **Flow** faster.
    persistence: Until the window closes. **Reset** returns it to 0.25.
  - name: Stabilization
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Smooths the pointer's path. Higher steadies a shaking hand, and the stroke lags behind tight turns.
    range: Each point of the stroke moves from the last smoothed point toward the pointer by 1 minus 0.85 times this value. The first and the last point of a stroke are not smoothed.
    enabled: With **Brush** and **Eraser**.
    tradeoff: The last point jumps to the pointer when you release, which can leave a kink at the end of the stroke.
    persistence: Until the window closes. **Reset** returns it to 0.
  - name: Blend Mode
    kind: combo
    default: Normal
    options: [Normal, Multiply, Screen, Overlay]
    effect: Chooses how a stroke, a fill or a gradient combines with the mask. **Normal** moves the mask toward the new value, **Multiply** toward the product of the two, **Screen** toward one minus the product of their complements, and **Overlay** multiplies where the mask is below 0.5 and screens elsewhere.
    enabled: With **Brush**, **Fill** and **Gradient**. Disabled with **Eraser**, which always erases.
    tradeoff: "**Multiply** and **Overlay** can only darken where the mask is 0, so they change only what is already painted. **Screen** never darkens."
    scope: Strokes, fills and gradients. Shapes are always screened.
    persistence: Until the window closes. **Reset** returns it to **Normal**.
  - name: Shape
    object: shapeTypeCombo
    kind: combo
    default: Rectangle
    options: [Rectangle, Ellipse, Circle, Polygon, Line]
    effect: Chooses the shape **Shape** draws. **Rectangle** and **Ellipse** are dragged from corner to corner, **Circle** from its center. **Polygon** and **Line** are clicked point by point.
    range: A double-click adds the last point and ends the shape. A polygon needs three points and a line two.
    enabled: With **Shape**.
    scope: New shapes. A shape already drawn keeps its kind.
    persistence: Until the window closes. **Reset** returns it to **Rectangle**.
  - name: Feather
    kind: spinbox
    default: "0.0"
    widget: {min: 0, max: 100, step: 0.5, decimals: 1}
    effect: Softens a new shape's edge with a Gaussian blur whose standard deviation is half this value, in pixels of the image, and at least 0.5. Higher spreads the edge wider.
    range: The edge falls from nearly 1 to nearly 0 over about twice this value, centered on the outline.
    enabled: With **Shape**. It softens outlines and lines as well as filled shapes.
    tradeoff: A wider feather widens the layer the shape keeps, which costs memory and time when the shape is drawn or moved.
    scope: New shapes.
    persistence: Until the window closes. **Reset** returns it to 0.
    note: A wide feather may be blurred in several passes, which add up to the same blur.
  - name: Filled
    kind: check
    default: "On"
    effect: On fills a rectangle, an ellipse, a circle or a polygon. Off draws only its outline, 1 pixel wide and smoothed.
    enabled: With **Shape**. A **Line** is always an outline.
    tradeoff: An outline makes a very narrow mask. **Feather** widens it.
    persistence: Until the window closes. **Reset** turns it on.
  - name: Mask View
    kind: combo
    default: Red
    options: [Red, Green, Blue, Amber, Grayscale, Checkerboard]
    effect: Chooses how the mask is drawn over the preview. **Red**, **Green**, **Blue** and **Amber** tint it in that color, at most nearly two thirds opaque. **Grayscale** draws the mask's own gray. **Checkerboard** alternates two dark tints in squares of 8 pixels of the image.
    tradeoff: A change redraws the whole tint. It changes nothing in the mask or the result.
    scope: The preview only.
    persistence: Until the window closes. **Reset** returns it to **Red**.
  - name: Import Mask...
    kind: button
    default: ""
    effect: Reads an image file and replaces the paint with it, removing every shape. A grayscale file's samples are read as fractions of full scale, and a color file's luma, 0.299 red plus 0.587 green plus 0.114 blue. Every value is then held between 0 and 1, and a sample that is not a number reads as 0.
    range: "The file must have exactly the target's width and height: it is never resized or aligned. A file of other channels than one, three or three and alpha is refused. The dialog lists FIT, FITS, TIF, TIFF, PNG and XISF files, or all files."
    tradeoff: The file is read away from the window, which stays responsive, and the import lands after the edits made before it. Its undo step keeps the whole paint, twice.
    scope: The drawing.
    persistence: One undo step. The import and export dialogs open in the folder either one last used.
    note: Without a drawing it shows "Open a target image before importing a mask." A drawing replaced by **Reset** or another target before the file is read drops the import.
  - name: Export Mask...
    kind: button
    default: ""
    effect: Writes a copy of the drawing as it stands to a file, under the export settings of the application.
    range: The extension of the name you give chooses the format. FITS, XISF and TIFF keep the 32-bit floating-point values, PNG is written at 16 bits and JPEG at 8. Any other extension is refused.
    tradeoff: The file is written away from the window, which stays responsive. Each export takes one more copy of the drawing in memory while it writes.
    scope: A file only. The drawing, its undo steps and the window stay as they are, and nothing is attached as a mask.
    note: An empty drawing is written too. Without a drawing it shows "There is no mask to export yet."
  - name: Undo
    kind: button
    default: ""
    effect: Takes back the last stroke, shape change, fill, gradient, import or clear, after the edits made before it have landed.
    range: At most 100 steps are kept, the oldest dropped first.
    enabled: When there is a step to take back. ++ctrl+z++ does the same.
    tradeoff: A step keeps only the paint its change touched. A shape change keeps none, a gradient or an import the whole paint. Steps count against the memory the session allows for undo.
    scope: The drawing.
    persistence: Until the window closes. **Reset** deletes the steps and frees their memory.
  - name: Redo
    kind: button
    default: ""
    effect: Does again the last change **Undo** took back.
    enabled: After **Undo**, until you make a new change, which discards what could be redone. ++ctrl+shift+z++ does the same, and ++ctrl+y++ on Windows.
    scope: The drawing.
  - name: Clear
    kind: button
    default: ""
    effect: Removes all paint and every shape, as one step **Undo** can take back.
    range: An empty drawing keeps no step.
    tradeoff: Its undo step keeps the rectangle that held paint.
    scope: The drawing. The values of the groups stay as they are.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image under the drawing, which **OK** names the mask after and takes its size and pixel type from.
    range: An image of another size, or an edit that changes the target's size, replaces the drawing with a blank one and loses its undo steps. An image of the same size keeps the drawing.
    persistence: Until the window closes.
  - name: Preview
    kind: check
    default: "On"
    effect: Always on and disabled. The drawing is made on the preview pane, which shows the target with the tint over it.
    scope: "The pane's toolbar offers **Display Mode**. While the pane shows the original, with ++space++, the right button or **Switch image**, the tint is hidden and you cannot draw."
  - name: OK
    kind: button
    default: ""
    effect: Takes a copy of the mask as it stands, writes it into a new grayscale image of the target's size, origin, pixel aspect and pixel type, named after the target with ` - MASK`, opens the image beside the target and closes the window.
    range: An edit still waiting to land is not in the copy. A floating-point target receives the values as they are. An integer target receives each value as a share of its range, rounded to the nearest code, so soft edges fall into its steps.
    enabled: From the moment the window opens, also with nothing drawn, when the mask is black. While the run works, the tools, the drawing, **Reset** and **Cancel** are disabled.
    tradeoff: A single image shows no progress window and its run cannot be cancelled. The copy is kept until the drawing changes.
    scope: "On a sequence it writes a new sequence of that name, each frame the same drawing in that frame's pixel type. A frame of another size stops the run with \"the drawing is not the size of the image\"."
    persistence: The new image's history starts with **Mask Drawing** and the text `Create painted grayscale mask`. It keeps the target's acquisition data, plate solution and lineage, and none of its header cards. It is not recorded as linear or as nonlinear.
    note: It does not attach the new image as a mask. Do that on the target with **Image > Mask > Select Mask**.
  - name: Reset
    kind: button
    default: ""
    effect: Makes a new blank drawing at the target's size, deletes every undo and redo step, chooses **Brush**, and returns every value of the **Brush** and **Shapes** groups and **Mask View** to its default.
    range: Unlike **Clear**, it cannot be taken back.
    note: Its tooltip reads "Clear the drawing and restore the default brush settings."
```

```controls
window: The drawing
controls:
  - name: A stroke
    kind: action
    default: ""
    effect: With **Brush** or **Eraser**, a drag with the left button lays dabs of the tip along a smooth curve through the points of the pointer.
    range: A point outside the image is ignored. One inside is held to the first and last pixel.
    tradeoff: The work grows with the area the dabs cover and with their number.
  - name: A click or a drag for Fill and Gradient
    kind: action
    default: ""
    effect: A click with **Fill** fills the region under the pointer. A drag with **Gradient** lays the ramp, and a dashed line shows its direction while you drag.
    range: A fill never crosses the edge of the image.
  - name: A drag or clicks for a shape
    kind: action
    default: ""
    effect: With **Shape**, a drag outlines a rectangle, an ellipse or a circle, which is added when you release the button. Clicks add the points of a polygon or a line, and a dashed outline follows the pointer until a double-click ends the shape.
    range: A shape may reach past the edges of the image. Only the part inside counts.
    persistence: The new shape is selected, with its handles shown, and is one undo step.
  - name: Selecting, moving, resizing and rotating
    kind: action
    default: ""
    effect: "With **Edit**, a click selects the topmost shape under the pointer, drawn with a solid outline and square handles, and a click elsewhere selects none. A drag inside the shape moves it. A drag of a handle resizes it: any point of a rectangle's edge, the four side points of an ellipse or a circle, each point of a polygon or a line. A drag of a handle with the middle button rotates the shape about its center."
    range: A handle is caught within about 6 pixels of the screen. The cursor shows what a drag would do.
    note: ++delete++ removes the selected shape.
  - name: Brush cursor
    kind: readout
    default: ""
    effect: Shows the tip over the preview at its size at the present zoom, as a circle or a square, dashed for **Eraser**. A dotted inner outline marks where full coverage ends at a **Hardness** below 1.
  - name: Tint
    kind: readout
    default: ""
    effect: Shows the mask over the preview in the color of **Mask View**, stronger where the mask is higher and absent where it is 0. Only the areas a change touched are drawn again.
    note: Edits land in the order you made them. While the engine is busy with other work, they wait and land a moment later, so the tint can trail the pointer. None is dropped.
  - name: Keys
    kind: action
    default: ""
    effect: "`[` and `]` change **Size** by 1. ++x++ switches between **Brush** and **Eraser**, and chooses **Brush** from any other tool. ++f++ chooses **Fill** and ++g++ **Gradient**. ++delete++ removes the selected shape. ++ctrl+z++ undoes and ++ctrl+shift+z++ redoes, as does ++ctrl+y++ on Windows."
    enabled: While the window has the keyboard.
```

## How it works

1. The window makes a drawing of the target's width and height: the paint, the paint as it was before the stroke
   being drawn, that stroke, and the mask, which is the paint with every shape over it. Each is one channel of 32-bit
   floating point, counted against the memory the engine may use.
2. Every edit is queued in the order you made it, and lands when the engine has room for it. A busy engine delays it
   and drops nothing, and the window never waits.
3. A stroke's dabs add up in the stroke plane, which is blended once per pixel into the paint as it was before the
   stroke began. A fill and a gradient blend into the mask as it is. Each shape keeps a smoothed, feathered layer,
   screened over the paint at its opacity.
4. The tint redraws the areas each edit changed.
5. **OK** copies the mask, writes the copy into a new image of the target's pixel type away from the window, and
   opens it beside the target. Its [lineage](../../reference/glossary.md#lineage) records the drawing.

### Model

A dab of the tip covers a pixel at distance \(d\) from its center by \(q\). With \(r\) half of **Size** and \(h\)
the value of **Hardness**, \(q = 1\) for \(d \le h\,r\), \(q = 0\) beyond \(r\), and between the two, with
\(t = (d - h\,r)/(r - h\,r)\),

\[ q = \frac{e^{-4t^2} - e^{-4}}{1 - e^{-4}} \tag{1} \]

The dabs fall every \(\max(1, \text{Size} \times \text{Spacing})\) pixels. With \(f\) the value of **Flow** and \(p\)
the pressure, 1 for a mouse, each dab adds to the stroke's coverage \(v\) as

\[ v \leftarrow 1 - (1 - v)(1 - q\,f\,p) \tag{2} \]

A stroke, a fill or a gradient then moves each value \(b\) of the mask toward a target \(T\) by the value \(o\) of
**Opacity**. \(T\) is \(v\) in **Normal**, \(b\,v\) in **Multiply**, \(1 - (1 - b)(1 - v)\) in **Screen**, and in
**Overlay** \(2\,b\,v\) below \(b = 0.5\) and \(1 - 2(1 - b)(1 - v)\) above. The eraser scales \(b\) instead:

\[ R = b + (T - b)\,o, \qquad R_{\text{erase}} = b\,(1 - v\,o) \tag{3} \]

A pixel the stroke does not cover keeps \(b\). A fill uses \(v = o\) at full opacity, so **Normal** sets the region
to **Opacity**, and a gradient \(v = t\,o\), with \(t\) the position from 0 at the start line to 1 at the end line. A
shape of coverage \(s\), feathered with a blur of standard deviation \(\sigma = \max(0.5, \text{Feather}/2)\), is
screened at its opacity:

\[ R = 1 - (1 - b)(1 - o\,s) \tag{4} \]

So overlapping shapes add toward white and never take from each other. Every value is held between 0 and 1.

### Boundaries

- Pointer input outside the image is ignored. A shape's layer is cut to the image, and a fill does not cross its
  edge.
- **Fill**, **Gradient**, **Import Mask...** and an eraser stroke over shapes turn every shape into paint first. Their
  undo step brings the shapes back. An eraser stroke that lays nothing, or a fill that changes nothing, leaves them.
- An undo step is admitted against the memory the session allows for undo. Redo steps, then the oldest steps, are
  dropped to make room. An edit whose step still does not fit is not made, without a message, and a stroke is taken
  back.
- A drawing the memory budget cannot hold is refused when the window opens, in a message titled **Mask Drawing**,
  and the window then has nothing to draw on. Free memory, then choose **Reset** or open the window again.
- A run the engine refuses creates nothing and says why under "Error executing the process". With no drawing, the
  reason is "there is no drawing to create a mask from".

### Worked example

On the Carina [master](../../reference/glossary.md#master), shown in the
[Autostretch](../../reference/glossary.md#autostretch) display, choose **Shape**, set the **Shape** box to **Ellipse**
and **Feather** to 100, and drag an ellipse around the bright middle of the nebula. The feather blurs the ellipse with
\(\sigma = 50\) pixels, and equation (4) lays it on the empty mask unchanged: a pixel on the outline holds 0.5, one 50
pixels inside holds 0.84, and one 100 pixels outside holds 0.02. **OK** opens a new 32-bit floating-point grayscale
image of 7246 × 5429 pixels, white inside the ellipse and black outside it.

```figure
images:
  - image: ../../assets/images/processes/masking/mask-drawing-before.webp
    alt: The whole Carina master in the Autostretch display, red and white clouds of nebula across a field of stars.
    label: Image
  - image: ../../assets/images/processes/masking/mask-drawing-after.webp
    alt: The new mask image, a white ellipse with a soft edge over the middle of the frame on a black background.
    label: Mask
caption: >-
  The Carina master in the Autostretch display, and the mask that OK made from one ellipse at Feather 100, shown in
  the Linear display.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A broad region, such as a nebula's bright core | **Shape**, **Ellipse** or **Polygon**, **Feather** 50 to 100 | Move and resize with **Edit**, then check the edge at 100 %. |
| A hand-painted area | **Brush**, **Round**, **Hardness** 0.5, **Opacity** and **Flow** at 1 | **Size** first, then **Hardness**. Lower **Flow** to build up gradually. |
| A soft fade across the frame | **Gradient**, **Normal**, **Opacity** 1 | Drag the length of the fade. Paint after it, or use **Screen** to keep earlier paint. |
| Fixing a mask from a file | **Import Mask...** with a file of the image's size | Paint with **Brush** and **Eraser**, then **OK**. |

## When it goes wrong

!!! warning "Export the drawing before you leave the window"
    **Cancel**, the window's close button, **Reset** and a target of another size discard the drawing and its undo
    steps. A file you exported stays. Choose **Export Mask...** first when you may need the drawing again.

!!! warning "OK does not attach the mask"
    The new image is not the target's mask until you select it on the target with **Image > Mask > Select Mask**.
    Check which side is white before you run a process through it.

Dragging over the preview draws nothing
:   **Cause.** The pane shows the original, which hides the tint and stops drawing, or the tool is **Edit**.
:   **Fix.** Release ++space++ or press **Switch image** again, and choose a tool that draws.

Strokes appear a moment after you draw them
:   **Cause.** The engine is busy with other work, and your edits wait their turn.
:   **Fix.** Keep drawing. Every edit lands, in the order you made it.

The drawing is blank after choosing another image
:   **Cause.** The new target has another size, so a blank drawing replaced the old one.
:   **Fix.** Choose the first image again and import the drawing if you exported it. Export before you switch.

A new target shows the old drawing
:   **Cause.** The drawing follows the size of the target, and this image has the same size.
:   **Fix.** Choose **Clear**, or **Reset**, before you draw for the new image.

"The imported mask must match the target image size (W × H)."
:   **Cause.** The file has another width or height, and an import is never resized.
:   **Fix.** Resize or register the file to the target's exact size first, then import it again.

"Failed to import the selected mask." or "Failed to export the current mask." with a reason
:   **Cause.** The file cannot be read or written, or its extension names no format the export writes.
:   **Fix.** Check the file, the folder and the extension, and try again. The drawing is unchanged.

A shape can no longer be selected
:   **Cause.** A fill, a gradient, an import or an eraser stroke turned the shapes into paint.
:   **Fix.** **Undo** that step to get the shapes back. Edit the shapes first, and fill or fade last.

The soft edge of the mask shows steps
:   **Cause.** The target has an integer pixel type, and **OK** rounded the values to its codes.
:   **Fix.** Export the drawing as FITS, XISF or TIFF, or use **Convert to 32 bit float** on a copy of the target
    before **OK**.

## Related

- [Masks](../../interface/masks.md), to attach the new image to a target, enable it, invert it and show it.
- [Range Selection](../analysis/range-selection.md) and [Star Mask](../star-operations/star-mask.md), which make a
  mask from the image's brightness or from its stars.
- [Binarize](binarize.md), to turn an image into black and white at a threshold.
- [Morphological Transformation](morphological-transformation.md), to grow, shrink or smooth a mask.
- [Image Statistics](../../interface/image-statistics.md), to check the range and channels of the new mask.
- [Process Windows](../../interface/process-windows.md), for the preview pane and what every process window shares.
