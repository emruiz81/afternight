---
kind: process
stage: any
menu: Process > Utilities > Image Compare
summary: >-
  Shows two open images of the same size at the same pixel positions, one at a time or on either side of a divider
  you drag, each with its own histogram, and changes neither.
data_in: either
data_out: either
destructive: false
preview: real-time
sequences: false
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Image Compare window is open on it yet, and no window that needs the
    image for itself is open on it.
  inputs: >-
    The active image as Image 1, and another open image of exactly its width and height as Image 2. Their pixel types,
    channels, states and headers may differ. A sequence takes part through the frame its view shows.
  outputs: >-
    Nothing. Both images, their histories and their files stay as they were. Take snapshot can open a copy of Image 1.
  state: >-
    Two versions of one image on the same pixel grid: before and after a process, two processing choices, or two
    registered frames.
derived: false
---
# Image Compare

Image Compare shows two open images of the same size at the same pixel positions, so you can see what a process
changed or whether two frames line up. One mode shows one image at a time and switches to the other. Two split modes
show Image 1 on one side of a divider and Image 2 on the other, and you drag the divider across the preview. Each
image has its own histogram, counted from its samples. The window computes no difference and no score, and aligns or
matches nothing.

The window has no **Apply**. It changes neither image, adds no step to a
[history](../../reference/glossary.md#history) and writes no file.

```figure
image: ../../assets/images/processes/analysis/image-compare.webp
alt: >-
  The Image Compare window. On the left, two histograms one above the other, captioned Image 1 and Image 2: the upper
  one a peak a little way in from its left edge, the lower one a spike at the edge. Under them the histogram toolbar,
  the Mode row of three buttons with the second one pressed, and the Image 1 and Image 2 boxes, each naming an image.
  On the right, an upright line with a round handle cuts the preview in two: the left half, labelled IMAGE 1, shows a
  red nebula, and the right half, labelled IMAGE 2, is nearly black. Close sits at the bottom right.
caption: >-
  Image Compare with the Carina master after Histogram Transformation as Image 1 and the linear master as Image 2,
  side by side in the Linear display mode.
pins:
  - {n: 2, x: 35.0, y: 87.5}
  - {n: 4, x: 17.1, y: 54.1}
  - {n: 5, x: 75.0, y: 55.3}
  - {n: 7, x: 35.0, y: 24.0}
  - {n: 8, x: 89.3, y: 96.0}
```

## Before you start

- Bring the two images onto one pixel grid first: register them and crop them alike. The window offers only images
  of the same width and height, and checks nothing else: not the registration, the
  [WCS](../../reference/glossary.md#wcs), the crop, the channels, the exposure or the background.
- Compare like with like: the same [processing state](../../reference/glossary.md#processing-state), the same
  display channel, zoom and display mode. A difference at an edge or in brightness can come from the pairing rather
  than from a process.
- To compare brightness, choose **Linear** in **Display Mode**, which shows the samples as they are. In
  [Autostretch](../../reference/glossary.md#autostretch) each image is stretched by its own statistics, which shows
  structure and hides a difference of scale.
- Neither image is changed, and a [mask](../../reference/glossary.md#mask) on either view plays no part.
- A [sequence](../../reference/glossary.md#sequence) takes part through the frame its view shows. Two sequences are
  not paired frame by frame.

## Steps

1. **Open the window.** Make the first image the [active image](../../reference/glossary.md#active-image) and
   choose **Process > Utilities > Image Compare**. **Image 1** names it, **Image 2** reads `<--NO IMAGE-->`, and the
   preview shows the image labelled IMAGE 1.
2. **Choose the second image.** Pick it in **Image 2**. The list holds only the open images of exactly Image 1's
   width and height.
3. **Blink the two.** With **Toggle mode (click to switch between images)** pressed, the first button of the
   **Mode** row, choose **Switch image** in the preview's toolbar, or hold ++space++ over the preview. The preview
   and the histogram turn to IMAGE 2, and back.
4. **Split the preview.** Press **Horizontal split (side by side with draggable divider)** to put Image 1 on the left
   and Image 2 on the right, or **Vertical split (top/bottom with draggable divider)** to put Image 1 on top. A second
   histogram opens under the first.
5. **Move the divider.** Drag the line across stars, background and nebula, and double-click it to center it again.
   Right-click the preview to swap the two sides.
6. **Look at 100 %.** Choose **Zoom 100%** and hold the left button over either side: the lens magnifies the image of
   that side.
7. **Read the histograms.** Each counts the samples of its own image as they are stored, whatever the preview's
   display does with them.
8. **Close.** Choose **Close**. Both images, their histories and their files are as they were.

## Controls

The histogram toolbar is the one every window shares, without **Show histogram**: see
[Process Windows](../../interface/process-windows.md). Here each of its buttons, and **Plot resolution**, acts on
both histograms at once. The window has no **Preview** check box: its preview is always shown.

```controls
window: The histograms
controls:
  - name: Histogram of Image 1
    kind: readout
    default: ""
    effect: Shows how the samples of Image 1 are spread between 0 and 1, with Clipping Low and Clipping High in its corner. In Toggle mode it counts the image the preview shows, which is Image 2 while you have switched to it.
    range: Counted from the stored samples, never from the screen. The display mode, the stretch and the balance do not change it, and the clipping shares count stored samples at or below 0 and at or above 1, not what the screen clips.
    enabled: >-
      Always shown. In a split with an image in **Image 2**, a caption over it names the image, as `Image 1: M42`.
    scope: The window only. Its lines follow the **View Channel** of the preview's toolbar, as in every window.
  - name: Histogram of Image 2
    kind: readout
    default: ""
    effect: >-
      Shows how the samples of Image 2 are spread, counted as the first one is, under a caption that names the image,
      as `Image 2: M42 stretched`.
    enabled: Shown only in a split, and only while **Image 2** names an image.
    scope: The window only.
```

```controls
window: The split
controls:
  - name: Divider
    kind: slider
    default: "0.5"
    widget: {min: 0, max: 1}
    effect: Dragging the line moves the border between the two images across the picture. In a horizontal split, Image 2 is right of the line and Image 1 left of it. In a vertical split, Image 2 is below it.
    range: The value is the share of the picture's width, or height, on the first side, held between 0 and 1. The line is picked up within 6 pixels of the screen on either side of it, where the pointer turns into a double arrow.
    enabled: Drawn only in a split, and only once Image 2 has been presented beside Image 1.
    persistence: Until the window closes. A double-click on the line returns it to the middle.
    note: A round handle with two arrows sits on the line, in the middle of the pane.
  - name: IMAGE 1 and IMAGE 2
    kind: readout
    default: ""
    effect: Name the image on each side. In a horizontal split IMAGE 1 is at the top left and IMAGE 2 at the top right. In a vertical split IMAGE 2 is at the bottom left.
    enabled: In Toggle mode one label, in the preview's top left corner, names the image shown.
  - name: Swap
    kind: action
    default: ""
    effect: A right click on the preview, **Switch image** or ++space++ swaps the two sides of a split. The labels move with their images.
    range: A right click and **Switch image** swap until you swap again. ++space++ swaps while you hold it.
    persistence: Until the window closes, or until Toggle mode, which puts the sides back in their order.
  - name: Lens
    kind: readout
    default: ""
    effect: Holding the left button over a side of a split opens the lens on the image of that side, and the lens follows the pointer across the line.
    note: On Image 2's side the lens magnifies the picture made of Image 2 for the screen. Near the line the lens closes, and a press on the line drags it instead of opening the lens.
```

```controls
window: Image Compare
controls:
  - name: Toggle mode (click to switch between images)
    kind: toolbutton
    default: "On"
    effect: Shows one image at a time. **Switch image**, or ++space++ or the right button held over the preview, turns it from Image 1 to Image 2 and back.
    enabled: The three **Mode** buttons are exclusive, so one of them is always pressed.
    tradeoff: The quickest way to see a small shift or a small local change, because what moves blinks.
    scope: The preview and the histogram, which counts the image shown. The second image is shown in the channel **View Channel** names.
    persistence: Until the window closes.
    note: With **Image 2** at `<--NO IMAGE-->`, the switch shows Image 1 again under the label IMAGE 2.
  - name: Horizontal split (side by side with draggable divider)
    kind: toolbutton
    default: "Off"
    effect: Shows Image 1 left of an upright divider and Image 2 right of it, and adds the histogram of Image 2.
    enabled: Needs an image in **Image 2**. Without one the preview shows Image 1 alone, with no divider and no label.
    tradeoff: Image 2's side is a picture made for the screen, in 8-bit gray or color, at the image's full size, in the background, and shown when it is ready. It is made again when the display mode or Image 2 changes, and in **Autostretch** that measures Image 2 again. A change of the Autostretch settings alone does not make it again. It shows what the display shows, not the precision of the samples.
    scope: The preview and the histograms. Image 2's side is presented with the preview's display mode, stretch settings and balance, and shows the whole image whatever **View Channel** shows of Image 1.
    persistence: Until the window closes.
  - name: Vertical split (top/bottom with draggable divider)
    kind: toolbutton
    default: "Off"
    effect: Shows Image 1 above a level divider and Image 2 below it, and adds the histogram of Image 2.
    note: In every other way it is the horizontal split turned on its side.
  - name: Image 1
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the first image, which the preview shows, the first histogram counts and **Take snapshot** copies.
    range: Any size, channels, pixel type or state.
    persistence: Until the window closes. Choosing another fills **Image 2** again with the images of the new size and returns it to `<--NO IMAGE-->`.
  - name: Image 2
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the second image, which Toggle mode switches to, a split shows beside Image 1 and the second histogram counts.
    range: After `<--NO IMAGE-->` the list holds every other open view of exactly Image 1's width and height, whatever its channels, pixel type, state or header. An image of another size is left out, never resampled. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: The list follows the views as they open, close, change or are renamed. When the image chosen closes, or changes to another size, the box returns to `<--NO IMAGE-->` without a message.
    tradeoff: Nothing else is checked, neither the registration nor the channels, the pixel type, the world coordinates or the brightness. A pair off by a pixel shows as a doubled edge at the divider, or as a jump when you switch.
    scope: The image its view shows, which is the current frame of a sequence. Its mask plays no part.
    persistence: Until the window closes or **Image 1** changes.
```

```controls
window: The preview pane
controls:
  - name: Display Mode
    kind: combo
    default: The mode of the view of Image 1
    options: [The mode of the view of Image 1, Linear, Autostretch]
    effect: "**Linear** shows both images as their samples are. **Autostretch** stretches each one by its own median and deviation, with the same settings."
    tradeoff: In **Autostretch** two images of different levels can look alike, because each gets the stretch of its own statistics. Judge brightness in **Linear**.
    scope: The display only. The histograms and the images do not change.
  - name: Preview area
    kind: check
    default: "Off"
    effect: Changes nothing you see here. The preview is a copy of Image 1, and an area of it is still Image 1.
    range: Ticked, it is 600 by 600 pixels in the middle of the image, or the image's size when that is smaller.
    scope: The preview only. The split and the histograms keep showing the whole images, and there is no comparison of an area alone.
  - name: Take snapshot
    kind: toolbutton
    default: ""
    effect: Opens a new image named `<Image 1> - Image Compare - SNAPSHOT`, a copy of the samples, channels, pixel type and header of Image 1.
    range: It holds neither the split nor Image 2 nor the display stretch. A mask on Image 1 changes nothing in it.
    scope: The images compared stay as they were. The new image's history starts with an Image Compare step that has no description.
    persistence: Each press opens another image. Closing the window leaves them open.
```

## How it works

1. The window opens on the active image as Image 1, and lists for Image 2 every other open view of exactly its width
   and height.
2. The preview shows a copy of Image 1. In Toggle mode, Image 2 is the image the preview's switch turns to.
3. In a split, Image 2 is presented for the screen in the background, as the preview presents Image 1, and drawn on
   one side of the divider over the picture of Image 1. No arithmetic joins the two: there is no difference, ratio or
   score.
4. Each image is counted into its own histogram from its stored samples. In **Autostretch**, each is stretched by the
   median and deviation of its own samples, then given the saturation, brightness and contrast of the settings.
5. Neither image, nor its header, history or file, is changed. **Take snapshot** runs the window's process, which
   copies Image 1, and opens the copy.

### Boundaries

- An image of another size is left out of **Image 2**, never resampled or cropped. The window corrects no shift,
  rotation, distortion, background or color, so a seam or a registration error of a mosaic shows, and stays.
- The channels, the [pixel type](../../reference/glossary.md#pixel-type), the world coordinates, the crop origin and the scale of the samples are not checked.
  A grayscale Image 2 is shown gray beside a color Image 1.
- Image 2's side of a split is an 8-bit picture for the screen. Read values in the histograms, in Image Statistics or
  with the lens on Image 1, not from that side.
- When Image 2 closes or changes to another size, the box returns to `<--NO IMAGE-->` and a split shows Image 1
  alone. No message is shown.
- Nothing is applied, so nothing can be undone. Closing the window leaves the snapshots you took open.

### Worked example

Image 1 is the Carina [master](../../reference/glossary.md#master) after Histogram Transformation with **Shadows** and
**Midtones** at 0.02, and Image 2 the same master as it was, linear. With **Horizontal split (side by side with
draggable divider)** pressed and **Display Mode** at **Linear**, the left half shows the nebula and the right half is
nearly black. The upper histogram peaks near 0.28, where the [stretch](../../reference/glossary.md#stretch) put the
background, and the lower one is a spike at its left edge, where the median of 0.028 sits. With **Display Mode** at **Autostretch**, the two halves come much closer,
because each is stretched by its own statistics, and the histograms stay as they were.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Did a process change only what it should? | A duplicate made before the process as Image 2, **Horizontal split (side by side with draggable divider)**, **Linear** | Drag the divider across stars, background and the brightest structure at 100 %. |
| Do two frames line up? | Toggle mode at 100 %, with one channel in **View Channel** | Hold ++space++ to switch: a star that moves is a registration error. |
| Two stretches of one image | A split in **Autostretch**, for structure | Return to **Linear** before you judge brightness. |
| Peaks too narrow to compare | **Plot resolution** at its default | A finer resolution and the toolbar's zoom, for both histograms at once. |

## When it goes wrong

!!! warning "The same size is not the same pixel grid"
    **Image 2** lists every image of Image 1's width and height. Register and crop the two alike first, or what you
    see at the divider is the pairing and not the process.

The image you want is not in Image 2's list
:   **Cause.** It is Image 1 itself, or its width or height differs from Image 1's.
:   **Fix.** Register, crop or resize it to Image 1's grid, or choose it as **Image 1** instead.

A split shows Image 1 alone, with no divider
:   **Cause.** **Image 2** reads `<--NO IMAGE-->`, or the image it named closed or changed size.
:   **Fix.** Choose the second image again in **Image 2**.

Switching shows the same picture under the label IMAGE 2
:   **Cause.** No image is chosen in **Image 2**, so the switch shows Image 1 again.
:   **Fix.** Choose the second image in **Image 2**.

The two halves look alike, but the histograms do not
:   **Cause.** In **Autostretch** each image is stretched by its own statistics.
:   **Fix.** Choose **Linear** in **Display Mode** to compare the samples as they are.

Stars jump when you switch, or edges are doubled at the divider
:   **Cause.** The two images have one size but are not registered to the same pixels.
:   **Fix.** Register and crop the pair, and check its crop origin and distortion.

Image 2's side shows all channels while View Channel shows one, or keeps its old stretch
:   **Cause.** A split presents Image 2 whole, whatever **View Channel** shows of Image 1, and does not present it
    again when only the Autostretch settings change.
:   **Fix.** Use Toggle mode to compare one channel of each. After new Autostretch settings, switch **Display Mode**
    to **Linear** and back.

Nothing is left after you close the window
:   **Cause.** The window changes nothing and makes no comparison image.
:   **Fix.** Use **Take snapshot** for a copy of Image 1 before you close. For an image of the difference, use Image
    Blend's **Difference** mode or Pixel Math.

The snapshot holds neither the split nor Image 2
:   **Cause.** A snapshot is a copy of Image 1, not a capture of the screen.
:   **Fix.** Capture the screen for a picture of the split, or make an image of both with Image Blend or Pixel Math.

## Related

- [Image Registration](../registration-alignment/image-registration.md), to bring two images onto one pixel grid
  before you compare them.
- [Image Blend](image-blend.md), whose **Difference** mode makes an image of what differs.
- [Pixel Math](../pixel-math/pixel-math.md), for a difference, a ratio or any other image made from the two.
- [Image Statistics](../../interface/image-statistics.md), for numbers in place of a picture.
- [Image Viewer](../../interface/image-viewer.md), for the zoom, the display channel, the lens and the Autostretch
  the preview shares.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.
