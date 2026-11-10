---
kind: interface
summary: >-
  A mask is another open image linked to a view, which limits where a process changes the image: white takes the
  result and black keeps the original. Image > Mask links, shows, inverts and removes it.
menu: Image > Mask
rewritten: true
---
# Masks

A [mask](../reference/glossary.md#mask) is another open image that you link to a view, its target. A process applied
to the target then changes it only where the mask lets it: white takes the result, black keeps the original, and gray
mixes the two. You can draw the mask over the target in a color, invert it, or turn it off without unlinking it.
Everything here is in **Image > Mask**, which a right-click on the view opens too.

A mask is a link between two views, not a part of the image. It is not saved with the target, not copied by
**Duplicate**, not kept in the [history](../reference/glossary.md#history), and it ends when either view closes.

```figure
owed: true
image: ../assets/images/interface/masks.webp
alt: >-
  The AfterNight main window with two tabs, RGB and RGB - MASK. The view of RGB shows the Carina Nebula covered in
  solid red, except for its bright clouds, which show through in their own colors. The Image menu is open, and beside
  it its Mask submenu: Select Mask, Remove Mask, Invert Mask unticked, Enable Mask and Show Mask ticked, and Rendering
  Mode, whose own submenu lists Overlay Red, ticked, then Overlay Green, Overlay Blue, Overlay Yellow, Overlay
  Magenta, Overlay Cyan, Replace and Multiply.
caption: >-
  The stretched Carina master in the Linear display mode, with the mask Range Selection made of its bright clouds
  drawn in Overlay Red, and the Image > Mask menu open.
pins:
  - {n: 1, x: 14.4, y: 9.8}
  - {n: 2, x: 86.0, y: 50.0}
  - {n: 3, x: 6.0, y: 18.5}
  - {n: 4, x: 30.9, y: 19.7}
  - {n: 5, x: 30.9, y: 25.9}
  - {n: 6, x: 40.4, y: 40.0}
```

## What you see

1. **The target and the mask.** Two open images of the same size, each in its own view. Here the mask,
   `RGB - MASK`, was made from the target by Range Selection.
2. **The overlay.** The mask drawn over the target: red where the mask keeps the original, clear where it lets the
   process through.
3. **Image > Mask.** The mask commands of the [active image](../reference/glossary.md#active-image).
4. **Select Mask and Remove Mask.** Link another image as the mask, or unlink it.
5. **Invert Mask, Enable Mask and Show Mask.** Swap black and white, use the mask in processes, and draw it over the
   view. Each is on or off for this target alone.
6. **Rendering Mode.** How the mask is drawn: a tint in one of six colors, the mask itself, or the picture multiplied
   by the mask.

## What you can do

### Make a mask

Any image of the target's width and height can be its mask, with one channel or three. The windows of **Process >
Mask Generation** make one: [Range Selection](../processes/analysis/range-selection.md) from a band of brightness,
[Star Mask](../processes/star-operations/star-mask.md) and
[Starnet++ Star Mask](../processes/star-operations/starnet-star-mask.md) from the stars, and
[Mask Drawing](../processes/masking/mask-drawing.md) by hand. [Binarize](../processes/masking/binarize.md) and
[Morphological Transformation](../processes/masking/morphological-transformation.md) reshape a mask. Each new mask
opens as an image of its own and is not linked yet.

A grayscale mask weighs every channel of the target the same. A color mask weighs red, green and blue each by its own
channel, and fits only a color target.

### Link it to the target

1. Click the target's view, and choose **Image > Mask > Select Mask**.
2. Choose the mask in **New Mask**. The list holds only the open images that fit the target. Rest the pointer on a
   name to see its size, channels, [pixel type](../reference/glossary.md#pixel-type) and file.
3. Choose **OK**. **Enable Mask** and **Show Mask** are on, **Invert Mask** is off, and the overlay appears.

```figure
owed: true
image: ../assets/images/interface/masks-selector.webp
alt: >-
  The Mask Selector window for RGB. Under the heading Mask Selector and a line about choosing a compatible image, a
  panel says that compatible masks must match dimensions, avoid circular references and respect color format
  compatibility. Current Mask reads NO MASK and New Mask reads RGB - MASK. OK and Cancel stand at the bottom right.
caption: >-
  Mask Selector on the stretched Carina master, with the mask Range Selection made of it chosen in New Mask.
```

### Check what it protects

With **Overlay Red**, the red marks what stays as it is, and the clear part is what a process changes. Choose another
color in **Rendering Mode** when red is hard to see on the image, or **Replace** to see the mask itself. Hold the left
button over the picture: the lens shows the mask's value beside the image's.

When the wrong part is protected, choose **Invert Mask**. The overlay and what processes protect swap together.

### Apply a process through it

1. Open the process on the target. Its preview is blended through the mask, so check it at 100 % on both sides of
   the mask's edges.
2. Choose **Apply**. Where the mask is white the image takes the result, where it is black it keeps its samples, and
   where it is gray it takes a share of each.

For a sample \(A\) of the target, the result \(P\) of the process at that sample, and the mask's value \(M\) from 0 to
1, the image becomes

\[ O = (1 - M)\,A + M\,P \tag{1} \]

With **Invert Mask** on, \(M\) and \(1 - M\) swap places. With \(A = 0.2\) and \(P = 0.8\), a mask of 0, 0.5 and 1
gives 0.2, 0.5 and 0.8, and inverted 0.8, 0.5 and 0.2.

Not every process uses a mask. The **Masks** row of a process page's **At a glance** card reads **Blended after the
process** or **Not used**. Among those that do not are the windows that make a mask, the geometry and stacking
windows, [Debayer](../processes/calibration/debayer.md) and the [CFA](../reference/glossary.md#cfa) tools, Linear
Match, Starnet++, the recombination windows and Pixel Math.
[Process Windows Reference](process-windows-reference.md) lists the windows with a preview that leave it out.

### Show it, use it, or neither

**Show Mask** and **Enable Mask** are separate. A hidden mask still protects the image in every process that uses
a mask, and a shown mask protects nothing while **Enable Mask** is off. Turn **Enable Mask** off to see a process's
whole result, and on again before you apply.

### Remove it

Choose **Image > Mask > Remove Mask**. The link goes at once, without a question, and the history has no step for it.
The mask image stays open. Save it as an image of its own to link it again in a later session.

## Controls

### The Mask menu

```controls
window: Image > Mask
controls:
  - name: Select Mask
    kind: action
    default: ""
    effect: Opens Mask Selector for the active image, to link another open image as its mask.
    enabled: With an image or a sequence active. Without one the **Mask** menu is greyed, and a command that reaches it says "No selected image to process". There is no shortcut and no toolbar button.
    note: The window leaves the rest of the application usable, and each choice opens another. It closes with the target's view.
  - name: Remove Mask
    kind: action
    default: ""
    effect: Unlinks the mask at once, with no question. **Invert Mask**, **Enable Mask** and **Show Mask** turn off, and **Rendering Mode** returns to **Overlay Red**.
    enabled: When the target has a mask, as are the four commands below.
    scope: The link and the overlay. The mask image stays open, and neither image changes.
    persistence: It makes no history step, so Undo does not bring the link back.
  - name: Invert Mask
    kind: check
    default: "Off"
    effect: Swaps black and white of the mask, for processes and for the overlay alike. The mask image itself is not changed.
    scope: The previews, snapshots and every Apply of the processes that blend through the mask, and the overlay. A process an extension adds receives the mask as it is, not inverted.
    persistence: Off whenever a mask is linked, then kept while the view is open.
  - name: Enable Mask
    kind: check
    default: "On"
    effect: On, the processes that blend through a mask blend through this one. Off, they change the whole target, and the mask stays linked and shown.
    tradeoff: The blend adds a pass over the whole image after the process, and holds the mask and the original beside the result. Its cost grows with the pixels and the channels.
    scope: Previews, snapshots and every Apply, on a still image and on each frame of a sequence.
    persistence: On whenever a mask is linked.
  - name: Show Mask
    kind: check
    default: "On"
    effect: Draws the mask over the target's view, as **Rendering Mode** says. Off hides it. It changes nothing a process does.
    tradeoff: Drawing costs work for the view only, never for a process.
    scope: The target's view and its lens. While it is on, the lens shows the mask's value as **M**, or **M0** to **M2** for a color mask, not inverted. A saved or exported image never holds the overlay.
    persistence: On whenever a mask is linked.
  - name: Rendering Mode
    kind: radio
    default: Overlay Red
    options: [Overlay Red, Overlay Green, Overlay Blue, Overlay Yellow, Overlay Magenta, Overlay Cyan, Replace, Multiply]
    effect: "Chooses how the mask is drawn. The six **Overlay** choices lay that color over the target, opaque where the mask is 0 and clear where it is 1, so the tint marks what is protected. **Replace** shows the mask itself, opaque, in gray or in color. **Multiply** shows the picture as the view displays it, multiplied by the mask."
    tradeoff: "**Multiply** follows the view's stretch and channel, so it darkens what the mask protects and measures nothing. It is no preview of a masked process either. Judge the mask's edges and its sides with an overlay or **Replace**."
    scope: The display only. No process reads it.
    persistence: Kept when another mask is linked. **Remove Mask** returns it to **Overlay Red**.
```

### Mask Selector

```controls
window: Mask Selector
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the target, as `Mask Selector <RGB>`, and follows a rename. The heading reads **Mask Selector**, over "Select a compatible image to use as a mask for the active image.", and a panel under it reads "Compatible masks must match dimensions, avoid circular references, and respect color format compatibility."
  - name: Current Mask
    kind: field
    default: NO MASK
    effect: Names the image linked as the target's mask when the window opened, or reads `NO MASK`. It cannot be edited.
    tradeoff: It is read once, so a mask linked from another Mask Selector while this one is open is not shown here.
  - name: New Mask
    kind: combo
    default: The first image that fits
    options: [The first image that fits, Every other open image that fits, "<NO COMPATIBLE IMAGES>"]
    effect: Chooses the image to link. The tooltip of each entry gives its name, its file or `In-memory file`, its size, its channels and color, its pixel type and the memory its samples take.
    range: It lists the open images and sequences of the target's width and height with one channel, or three for a color target. It leaves out the target itself, an image of any other number of channels, a color image for a grayscale target, and an image whose own mask is the target, so two images never mask each other. A longer loop is not looked for. Convert an image with alpha to RGB or grayscale before you use it as a mask.
    enabled: Disabled, reading `<NO COMPATIBLE IMAGES>`, when no open image fits.
    tradeoff: The first entry is only the first that fits, not a recommendation. Nothing is aligned, cropped or resized, and no header or plate solution is compared, so an image of the right size can still be of another field or turned.
    persistence: The list follows the images opened, closed, changed and renamed while the window is open.
  - name: OK
    kind: button
    default: ""
    effect: Links the chosen image as the mask and closes the window. **Enable Mask** and **Show Mask** turn on, **Invert Mask** turns off, and **Rendering Mode** stays as it was.
    enabled: When the list holds an image.
    range: Choosing the mask already linked only closes the window. The image is looked for by its name when you press it, and the window closes without linking when it was closed meanwhile.
    scope: Linking changes neither image. No pixel, header card, file or history step comes of it.
    note: One image may be the mask of several targets.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window and changes nothing.
```

### The link, the blend and the overlay

```controls
window: How a mask works
controls:
  - name: The link
    kind: readout
    default: ""
    effect: The target holds a reference to the mask's view, not a copy. A change of the mask's image - a process, an undo, another frame of a mask sequence - redraws the overlay of every target it masks, and the next preview or Apply reads it.
    range: "A change of either image that makes the mask unfit - another size, other channels, a color mask over a grayscale target - removes the link, in a message titled **Mask Reference**: \"This update made the mask '<mask>' incompatible with the image '<target>'. Removing the mask reference.\" Undoing a crop or a resize of the target does this too."
    scope: A sequence used as a mask gives the frame its view shows, the same one to every frame of a target sequence. No frame of the mask is paired with a frame of the target, so a still mask is safer for a sequence.
    persistence: "The link lasts while both views are open, and survives a rename of either. Closing the target removes it. Closing the mask says \"This image is being used as a mask in other images. The mask references are being removed\", with OK alone, and the view closes all the same. Saving, duplicating, the history, reopening a file and restarting AfterNight keep no link."
    note: "Some windows link a mask themselves. Star Reduction links the protection mask it builds, enabled and hidden. Every mask an extension writes opens as an image, and the first becomes the mask of the image the extension ran on. A mask linked that way that does not fit is refused with \"'<mask>' cannot be the mask of '<target>': ...\" and the reason."
  - name: The blend
    kind: readout
    default: ""
    effect: After a process has computed its result, and after its **Amount** where it has one, each sample becomes the original plus the mask's share of the change, by equation (1).
    range: The mask may have another pixel type than the target. It is read in the result's pixel type, as a fraction of its full scale, so an integer mask is scaled from its codes. Over a floating-point result, a floating-point mask is read as it is and never clamped. A value below 0 or above 1 then pushes the result beyond the original or the process's result, and a value that is not a number reaches the result. Over an integer result the mask is held between 0 and 1, a value that is not a number reads as 0, and the result is cut at the ends of its type.
    tradeoff: Hold a floating-point mask between 0 and 1 before you link it. A mask rounded to the codes of an integer type when it was made keeps those steps in its soft edges.
    scope: A one-channel mask weighs every channel of the result, and a three-channel mask weighs each channel by its own. A **Preview area** uses the same part of the mask. Every frame of a sequence is blended with the same mask.
    note: A process an extension adds is not blended. When it asks for a mask, it receives the linked mask's image as it is, read only, whether the mask is enabled, shown or inverted. It can read those three settings, and decides itself what to do with the mask.
  - name: The overlay
    kind: readout
    default: ""
    effect: The layer the view draws over the target while **Show Mask** is on, in the mode **Rendering Mode** chooses.
    range: Every pixel type is read as a fraction of full scale and held between 0 and 1, so a code of a signed integer below zero reads as 0, as does a value that is not a number. A pixel the mask marks as invalid is left clear. Each value is rounded once to the 8 bits of the screen.
    scope: A color mask is drawn as one layer whose opacity is one minus the product of its three channels. While **View Channel** shows red, green or blue, only that channel of the mask is drawn, and **Multiply** of one channel uses the mean of the mask's three.
    tradeoff: The layer is computed in the background with the picture, over the same part of the image and at the same reduction, and the view keeps showing the old picture until both are ready. A large mask over several targets costs work and memory, never a pause of the window.
    persistence: It is drawn again when the mask's image, the target, its stretch or its channel changes, and when you invert the mask or choose another mode. Each time it is drawn from the mask's own samples, so repeated zooming does not blur it. While you paint a mask in Mask Drawing, that window's overlay shows every stroke that has landed since it was last drawn.
```

## When it goes wrong

`<NO COMPATIBLE IMAGES>` in New Mask, and OK greyed
:   **Cause.** No other open image has the target's width and height, with one channel or three for a color target.
:   **Fix.** Make the mask from the target itself, with Range Selection or Mask Drawing, or resize or convert the image
    you meant first.

A process changed the whole image
:   **Cause.** **Enable Mask** is off, or the process does not use a mask: its page's **Masks** row reads **Not used**.
:   **Fix.** Undo the step, turn **Enable Mask** on, and apply again. For a process that does not use a mask, write the
    blend in Pixel Math with `$M`.

The process changed the part you meant to protect
:   **Cause.** The mask is white where it should be black.
:   **Fix.** Undo the step, choose **Invert Mask**, check the overlay, and apply again.

The image is still protected after Show Mask was turned off
:   **Cause.** **Show Mask** hides the overlay only, and **Enable Mask** is still on.
:   **Fix.** Turn **Enable Mask** off, or choose **Remove Mask**.

"This update made the mask '...' incompatible with the image '...'. Removing the mask reference."
:   **Cause.** A crop, a resize, a conversion or an undo changed the size or the channels of the target or the mask.
:   **Fix.** Make the two images the same size again, or make a new mask from the target, and link it again.

The mask is gone after the image was saved and opened again
:   **Cause.** The link lasts only while both views are open, and no file keeps it.
:   **Fix.** Save the mask as an image of its own, open it beside the target, and link it again.

## Related

- [Range Selection](../processes/analysis/range-selection.md), [Star Mask](../processes/star-operations/star-mask.md)
  and [Mask Drawing](../processes/masking/mask-drawing.md), to make a mask.
- [Binarize](../processes/masking/binarize.md) and
  [Morphological Transformation](../processes/masking/morphological-transformation.md), to harden, grow or shrink one.
- [Process Windows](process-windows.md), for the preview and the Apply that blend through the mask.
- [Image Viewer](image-viewer.md), for the overlay, the lens and the channels a view shows.
- [Pixel Math](../processes/pixel-math/pixel-math.md), to write a blend of your own with the mask as `$M`.
- [Saving and Exporting](saving-and-exporting.md), to keep a mask as a file.
