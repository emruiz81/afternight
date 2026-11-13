---
kind: process
stage: any
menu:
  - Image > Actions
  - Image > Extract Channel
  - Image > Convert Format
  - Image > Fast Geometry
  - Image > Mask
  - Image > Sequence
  - Process > Adjustments
summary: >-
  The Image menu's commands and three of Process > Adjustments that act at once, without a window: they duplicate,
  extract channels, convert the pixel type, mirror and turn, manage masks and sequences, and invert or change color.
data_in: either
data_out: either
destructive: true
preview: none
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active. A command that does not fit it is greyed: Extract Channel and Convert to
    Grayscale need an RGB image, Convert to RGB a grayscale one, and the Sequence submenu a sequence.
  inputs: >-
    The active image or sequence, of any pixel type. Negative Transformation also reads the mask on its view, and
    Select Mask another open image of the same size.
  outputs: >-
    A new view beside the source for Duplicate, Extract Channel, Convert Format and Extract Frame, the source
    unchanged. The same view, replaced, with one history step, for Fast Geometry, Negative Transformation, Convert to
    RGB, Convert to Grayscale and a sequence's Convert. A new sequence is a temporary one, and nothing is saved.
  state: >-
    Any image or sequence, linear or not. Channels, components and pixel types are data management and no substitute
    for calibration. A mirror or a turn belongs before the plate solve.
derived: false
---
# Non-Interactive Processes

These commands run as soon as you choose them: they open no process window and have no setting or preview. They
work on the [active image](../../reference/glossary.md#active-image) or
[sequence](../../reference/glossary.md#sequence), from six submenus of **Image** - **Actions**,
**Convert Format**, **Extract Channel**, **Fast Geometry**, **Mask** and **Sequence** - and the end of
**Process > Adjustments**.

Some open their result in a new view and leave the source as it was: a duplicate, a channel or a color component, a
copy in another [pixel type](../../reference/glossary.md#pixel-type), a frame of a sequence. Others replace the image
in its view and add one step to its [history](../../reference/glossary.md#history): a mirror or a turn, a negative, a
change to gray or to RGB, and a sequence written in another container.

```figure
image: ../../assets/images/processes/utility/non-interactive-processes.webp
alt: >-
  The top left of the main window over the Carina Nebula master. The Image menu is open under the menu bar and lists
  Actions, Convert Format, Extract Channel, Fast Geometry, Information, Mask, a greyed Sequence and View, then Show in
  File Browser below a line. Convert Format is highlighted, and its submenu beside it lists seven commands from
  Convert to 8 bit to Convert to 64 bit float, with Convert to unsigned 16 bit third and Convert to 32 bit float
  greyed.
caption: >-
  Image > Convert Format over the linear Carina master: the master's own type, 32-bit floating point, is greyed, and
  Convert to unsigned 16 bit is the command of the worked example.
pins:
  - {n: 2, x: 12.6, y: 14.9}
  - {n: 3, x: 63.8, y: 23.1}
```

## Before you start

- Click the view you mean just before you choose. Every command acts on the active view at that moment.
- A command that replaces the image keeps the state before in its history, and **Edit > Undo** returns to it. Choose
  **Duplicate** first when you want the two side by side.
- A greyed command does not fit the active image: its color, its pixel type, a still image for a sequence command, a
  view without a [mask](../../reference/glossary.md#mask). It is never a preview state.
- **Fast Geometry** drops the [plate solution](../../reference/glossary.md#plate-solution). Plan to plate-solve again
  after a mirror or a turn.
- On a sequence, the result is a [temporary sequence](../../reference/glossary.md#temporary-sequence) on disk, which
  needs the space of a full copy and is lost with the session unless you save or export it.
- [Debayer](../../reference/glossary.md#debayer) a raw [CFA](../../reference/glossary.md#cfa) still image before
  **Convert to RGB** or a channel extraction. A mosaic is one gray channel until then.

## Steps

1. **Make the image active.** Click its view. The status bar under the views names its size, its channels and its
   pixel type, which decide what is enabled.
2. **Open the submenu.** Choose **Image** and the submenu, or **Process > Adjustments**. A right click on the view
   offers the same submenus. Greyed entries do not fit the image.
3. **Choose the command.** It starts at once. A still image shows no progress window. A sequence shows one, and its
   **Cancel** stops the run.
4. **Find the result.** A new view opens beside the source, named after it with a suffix such as ` - RED` or
   ` - CONV_16_UINT`. Or the same view shows the changed image, and its history ends with the command's step.
5. **Check it.** Read the new size, channels and pixel type in the status bar, and the last step in
   **Edit > Image History**. After a mirror or a turn, plate-solve the image again.
6. **Keep what you need.** Save a new view, or export a new sequence. Until then a new view is unsaved, and a new
   sequence is a file in the session's temporary folder, which **Image > Show in File Browser** shows.

## Controls

Each block below is one submenu, in the order of the menu. A command has no value to set: its row says what it makes,
when it is enabled and what the result keeps. A new view takes the name its row gives, with `_1`, `_2` and so on
added when another view has that name. The messages of a refused run are under When it goes wrong.

### Image > Actions

[Toolbar & Menus](../../interface/toolbar-and-menus.md) describes the rest of the menus, and
[Saving and Exporting](../../interface/saving-and-exporting.md) what a save writes.

```controls
window: Image > Actions
controls:
  - name: Duplicate
    kind: action
    default: ""
    effect: >-
      Opens a copy of the active image or sequence in a new view beside it, under the same name with `_1` added. The
      new history starts with `Cloned from image <name>`, or `Cloned from sequence <name>`.
    enabled: With an image or a sequence active.
    tradeoff: >-
      A still image is copied whole in memory: every sample, the header cards and the plate solution. A sequence is not
      copied: the new view opens the same file again, with the source's automatic debayer setting.
    scope: The source is not changed.
    persistence: >-
      The copy of a still image is not tied to the source's file, so saving it cannot write over the original. It
      counts as unsaved when the source does. The second view of a sequence shares its file and is no copy on disk.
    note: The way to keep a version to compare with before a command that replaces the image.
  - name: Rename
    kind: action
    default: ""
    effect: >-
      Opens a small window titled `Rename <name>` with one field. Type the new name and choose **OK**: the view takes
      it, and its history gains a **Rename** step.
    range: >-
      An empty name is refused with "Image name is invalid", and the name of another open view with "Image with name
      '<name>' already exists". The window stays open after either.
  - name: Close
    kind: action
    default: ""
    effect: Closes the active view, as **File > Close** does.
    note: It asks first when the view has unsaved changes, or a process window open on it.
  - name: Save File...
    kind: action
    default: ""
    effect: Saves the active image, as **File > Save as...** does.
```

### Image > Convert Format

Each command opens a copy of the image in another pixel type, in a new view beside the source, named after it with
the suffix its row gives. The entry of the image's own type is greyed. Equations (1) and (2) give each new sample: a
type with fewer levels rounds the samples, and a wider one only gives later edits room. The copy keeps the size, the
channels, the [header cards](../../reference/glossary.md#header-card), the plate solution and the
[processing state](../../reference/glossary.md#processing-state). Its history starts with
`Converted to <type> from image '<name>'`.

```controls
window: Image > Convert Format
controls:
  - name: Convert to 8 bit
    kind: action
    default: ""
    effect: Opens a copy in unsigned 8-bit integers, named `<name> - CONV_8_UINT`.
    enabled: When the image is not 8-bit already, signed or unsigned.
    tradeoff: >-
      256 levels: faint gradients band, and later edits show the steps. Converting back to a wider type does not
      restore what was rounded away.
  - name: Convert to signed 16 bit
    kind: action
    default: ""
    effect: Opens a copy in signed 16-bit integers, named `<name> - CONV_16_INT`.
    enabled: When the image is not signed 16-bit already. Greyed for a SER or video sequence.
  - name: Convert to unsigned 16 bit
    kind: action
    default: ""
    effect: Opens a copy in unsigned 16-bit integers, named `<name> - CONV_16_UINT`.
    enabled: When the image is not unsigned 16-bit already.
    tradeoff: Half the memory of 32-bit floating point, with 65,536 levels.
    note: >-
      The widest type a SER sequence holds. A video sequence holds 8 bits only, and refuses it when the run starts.
  - name: Convert to signed 32 bit
    kind: action
    default: ""
    effect: Opens a copy in signed 32-bit integers, named `<name> - CONV_32_INT`.
    enabled: When the image is not signed 32-bit already. Greyed for a SER or video sequence.
  - name: Convert to unsigned 32 bit
    kind: action
    default: ""
    effect: Opens a copy in unsigned 32-bit integers, named `<name> - CONV_32_UINT`.
    enabled: When the image is not unsigned 32-bit already. Greyed for a SER or video sequence.
  - name: Convert to 32 bit float
    kind: action
    default: ""
    effect: >-
      Opens a copy in 32-bit floating point, named `<name> - CONV_32_FLOAT`. An integer image's samples become
      fractions from 0 to 1, and a floating-point image's samples are kept as they are.
    enabled: When the image is not 32-bit floating point already. Greyed for a SER or video sequence.
    tradeoff: Later edits are not rounded to codes, but no detail lost before is restored.
  - name: Convert to 64 bit float
    kind: action
    default: ""
    effect: Opens a copy in 64-bit floating point, named `<name> - CONV_64_FLOAT`.
    enabled: When the image is not 64-bit floating point already. Greyed for a SER or video sequence.
    tradeoff: >-
      Twice the memory and disk of 32-bit floating point and four times that of 16-bit integers, with no more detail
      than the source had.
```

On a sequence, the copy is a new sequence in the source's container, and its history starts with
`Converted to <type> from sequence '<name>'`.

### Image > Extract Channel

Every command needs an RGB image and opens each result in a new view beside the source, named after it with the
suffix in its row. The first four copy the stored channels. The others compute a component of another
[color model](../../reference/glossary.md#color-model) - [CIE L*a*b*](../../reference/glossary.md#cie-lab), XYZ,
[HSL](../../reference/glossary.md#hsl), [HSV](../../reference/glossary.md#hsv), [HSI](../../reference/glossary.md#hsi)
or [YCbCr](../../reference/glossary.md#ycbcr) - and store it by its
[component encoding](../../reference/glossary.md#component-encoding). Each new history starts with
`<component> extracted from image '<name>'`.

```controls
window: Image > Extract Channel
controls:
  - name: Extract Red Channel
    kind: action
    default: ""
    effect: Opens the red channel as a grayscale image named `<name> - RED`.
    enabled: For an RGB image or sequence. Greyed for a grayscale image and an undebayered mosaic.
    scope: >-
      The samples are copied as they are stored, in the image's own pixel type and size. The copy keeps the header
      cards, the plate solution and the image's meaning, and the part of an explicit validity mask that covers red.
  - name: Extract Green Channel
    kind: action
    default: ""
    effect: Opens the green channel as `<name> - GREEN`, as **Extract Red Channel** opens the red one.
  - name: Extract Blue Channel
    kind: action
    default: ""
    effect: Opens the blue channel as `<name> - BLUE`, as **Extract Red Channel** opens the red one.
  - name: Extract RGB Channels
    kind: action
    default: ""
    effect: Opens the three channels as three grayscale images, `<name> - RED`, `<name> - GREEN` and `<name> - BLUE`.
    tradeoff: Three full planes in memory, or three new sequences on disk.
  - name: Extract CIE L*
    kind: action
    default: ""
    effect: Opens the lightness of CIE L*a*b* as `<name> - CIE L*`, stored as L* divided by 100.
    range: >-
      A 32-bit floating-point image, or 64-bit for a 64-bit image, whatever the source's type: a 16-bit image's
      lightness is not rounded to 16-bit codes. Every component of a color inside the gamut lies between 0 and 1.
    scope: >-
      Red, green and blue are read as fractions of full scale, as linear when the image is described as linear RGB and
      as sRGB-encoded otherwise. The component keeps the acquisition record and the plate solution. Of the header
      cards it keeps only the frame's observing context and the light it holds, as every component does.
    tradeoff: A measurement of the image to process or inspect, not a picture. It stores no unit such as degrees.
    note: A pixel the image marks invalid has no color, and its component is not a number.
  - name: Extract CIE a*
    kind: action
    default: ""
    effect: Opens the green to red axis of CIE L*a*b* as `<name> - CIE a*`, stored as a* plus 128, divided by 255.
    note: A neutral gray sits near one half. Precision and kept data as **Extract CIE L***.
  - name: Extract CIE b*
    kind: action
    default: ""
    effect: Opens the blue to yellow axis as `<name> - CIE b*`, stored as b* plus 128, divided by 255.
  - name: Extract CIE c*
    kind: action
    default: ""
    effect: Opens the chroma of CIE L*c*h* as `<name> - CIE c*`, stored as the chroma divided by 200.
  - name: Extract CIE h*
    kind: action
    default: ""
    effect: >-
      Opens the hue angle of CIE L*c*h* as `<name> - CIE h*`. The angle, measured from -180 to 180 degrees, is stored
      as a fraction of a turn: the angle plus 180, divided by 360.
    tradeoff: The hue is a line from 0 to 1 here, not a circle, so 0 and 1 are the same color.
  - name: Extract CIE a*=R / b*=G
    kind: action
    default: ""
    effect: >-
      Opens one color image named `<name> - CIE a*=R / b*=G`, with a* in red, b* in green and a cleared blue, each
      stored as **Extract CIE a*** stores it.
    tradeoff: A way to see the two color axes together. It is not an RGB picture of the image.
  - name: Extract CIE L*a*b*
    kind: action
    default: ""
    effect: Opens L*, a* and b* as three grayscale images, `<name> - CIE L*`, `<name> - CIE a*` and `<name> - CIE b*`.
  - name: Extract CIE L*c*h*
    kind: action
    default: ""
    effect: Opens L*, c* and h* as three grayscale images, `<name> - CIE L*`, `<name> - CIE c*` and `<name> - CIE h*`.
  - name: Extract CIE X
    kind: action
    default: ""
    effect: Opens the X tristimulus value as `<name> - CIE X`, divided by the X of the white so that the white is 1.
  - name: Extract CIE Y
    kind: action
    default: ""
    effect: Opens the Y tristimulus value, the luminance, as `<name> - CIE Y`, divided by the Y of the white.
  - name: Extract CIE Z
    kind: action
    default: ""
    effect: Opens the Z tristimulus value as `<name> - CIE Z`, divided by the Z of the white.
  - name: Extract CIE XYZ
    kind: action
    default: ""
    effect: Opens X, Y and Z as three grayscale images, `<name> - CIE X`, `<name> - CIE Y` and `<name> - CIE Z`.
  - name: Extract HSL Hue
    kind: action
    default: ""
    effect: Opens the hue of HSL as `<name> - HSL Hue`, stored as a fraction of a turn.
  - name: Extract HSL Saturation
    kind: action
    default: ""
    effect: Opens the saturation of HSL as `<name> - HSL Saturation`.
  - name: Extract HSL Lightness
    kind: action
    default: ""
    effect: Opens the lightness of HSL as `<name> - HSL Lightness`. It is not CIE L*.
  - name: Extract HSL
    kind: action
    default: ""
    effect: Opens hue, saturation and lightness as three grayscale images, with the suffixes of the three rows above.
  - name: Extract HSV Hue
    kind: action
    default: ""
    effect: Opens the hue of HSV as `<name> - HSV Hue`, stored as a fraction of a turn.
  - name: Extract HSV Saturation
    kind: action
    default: ""
    effect: Opens the saturation of HSV as `<name> - HSV Saturation`.
  - name: Extract HSV Value
    kind: action
    default: ""
    effect: Opens the value of HSV, the largest of the three channels, as `<name> - HSV Value`.
  - name: Extract HSV
    kind: action
    default: ""
    effect: Opens hue, saturation and value as three grayscale images, with the suffixes of the three rows above.
  - name: Extract HSI Hue
    kind: action
    default: ""
    effect: Opens the hue of HSI as `<name> - HSI Hue`, stored as a fraction of a turn.
  - name: Extract HSI Saturation
    kind: action
    default: ""
    effect: Opens the saturation of HSI as `<name> - HSI Saturation`.
  - name: Extract HSI Intensity
    kind: action
    default: ""
    effect: Opens the intensity of HSI as `<name> - HSI Intensity`.
  - name: Extract HSI
    kind: action
    default: ""
    effect: Opens hue, saturation and intensity as three grayscale images, with the suffixes of the three rows above.
  - name: Extract YCbCr Luma
    kind: action
    default: ""
    effect: >-
      Opens the luma of YCbCr as `<name> - YCbCr Luma`, with the BT.601 weights at full range. It is not calibrated
      luminance.
  - name: Extract YCbCr Blue Chroma
    kind: action
    default: ""
    effect: Opens the difference toward blue as `<name> - YCbCr Blue Chroma`, with a neutral pixel at 0.5.
  - name: Extract YCbCr Red Chroma
    kind: action
    default: ""
    effect: Opens the difference toward red as `<name> - YCbCr Red Chroma`, with a neutral pixel at 0.5.
  - name: Extract YCbCr
    kind: action
    default: ""
    effect: Opens luma and both chromas as three grayscale images, with the suffixes of the three rows above.
```

On a sequence, each command writes one new sequence for each image it makes, in the source's container, and a mosaic
frame is demosaiced first, whatever the view's automatic debayer setting. A component keeps the same data in a frame
as in a still image. The container stores the types it can: a [SER](../../reference/glossary.md#ser) turns a
floating-point component into unsigned 16-bit codes, and a video into 8-bit ones. Each new history starts with
`<component> extracted from sequence '<name>'`.

### Image > Fast Geometry

Each command moves every sample to its new place and copies it exactly, in every pixel type, without interpolation
and without a filled edge. On a still image it is an edit of the view: it is computed in the background, recorded
as one history step before the view shows it, and undone by that step. Edits of one view run one at a time, in the
order you ask for them, so a turn chosen twice turns twice.

```controls
window: Image > Fast Geometry
controls:
  - name: Horizontal Mirror
    kind: action
    default: ""
    effect: Reverses each row, so the left edge becomes the right.
    enabled: With an image or a sequence active.
    scope: >-
      Every channel. The history step is **Image Horizontal Mirror**, or **Sequence Horizontal Mirror** on a sequence.
      A mask on the view is not used.
    persistence: >-
      The image keeps its acquisition record, its meaning, and the header cards of the frame's observing context and
      of the light it holds. The plate solution and every other header card are dropped, and a decision on how to
      write the image is withdrawn.
  - name: Vertical Mirror
    kind: action
    default: ""
    effect: Reverses the order of the rows, so the top edge becomes the bottom.
    scope: >-
      The history step is **Image Vertical Mirror**, or **Sequence Vertical Mirror**. Kept data as **Horizontal
      Mirror**.
  - name: Rotate 180º
    kind: action
    default: ""
    effect: Turns the image half a turn. Width and height stay.
    scope: >-
      The history step is **Image Rotation**, or **Sequence Rotation**, described as `180 degrees`. Kept data as
      **Horizontal Mirror**.
  - name: Rotate 90º Left
    kind: action
    default: ""
    effect: Turns the image a quarter turn counter-clockwise. Width and height swap.
    scope: >-
      The history step is **Image Rotation**, or **Sequence Rotation**, described as `90 degrees left`. Kept data as
      **Horizontal Mirror**.
    note: A pixel shape other than square is turned with the axes.
  - name: Rotate 90º Right
    kind: action
    default: ""
    effect: Turns the image a quarter turn clockwise. Width and height swap.
    scope: >-
      The history step is **Image Rotation**, or **Sequence Rotation**, described as `90 degrees right`. Kept data as
      **Horizontal Mirror**.
    note: A pixel shape other than square is turned with the axes.
```

The cards that stay are the ones every geometry change keeps, which [Crop](../geometry/crop.md) lists. An explicit
validity mask moves with its samples, and any other [validity rule](../../reference/glossary.md#validity-rule) is kept
as the image states it. A mosaic is turned without being demosaiced: its
[Bayer pattern](../../reference/glossary.md#bayer-pattern) is restated so that each sample stays under the filter it
was measured through. When the image had a solution, the Session Console says that it was removed and that a plate
solve restores it.

On a sequence, the command turns every frame as it was recorded and writes the frames into a new sequence of the same
container. The view shows it, with one history step, once the whole sequence is written, and demosaics its frames
for display as it did before. A run that cannot finish says "Could not turn the sequence '<name>': " and the reason.

### Image > Mask

These commands change which image is the view's mask and how it is shown. None changes a sample or a header card
of either image, and none adds a history step. [Masks](../../interface/masks.md) explains how a process uses the
mask.

```controls
window: Image > Mask
controls:
  - name: Select Mask
    kind: action
    default: ""
    effect: Opens the mask selector of the active image, where you choose another open image of its size as its mask.
    enabled: With an image or a sequence active.
  - name: Remove Mask
    kind: action
    default: ""
    effect: Unlinks the mask from the image. Both images stay open and unchanged.
    enabled: While the image has a mask. Chosen without one, it does nothing.
  - name: Invert Mask
    kind: action
    default: "Off"
    effect: Swaps the parts the mask protects and the parts it lets a process change, on the display and in processing.
    enabled: While the image has a mask.
    persistence: A state of the view, off when a mask is selected and lost when the view closes.
  - name: Enable Mask
    kind: action
    default: "On"
    effect: >-
      On, the processes that use a mask blend their result through it. Off, they ignore it, and the mask stays linked.
    enabled: While the image has a mask.
    persistence: A state of the view, on when a mask is selected.
  - name: Show Mask
    kind: action
    default: "On"
    effect: Shows or hides the mask drawn over the image. It changes the display only, and does not enable the mask.
    enabled: While the image has a mask.
    persistence: A state of the view, on when a mask is selected.
  - name: Overlay Red
    kind: action
    default: "On"
    effect: Draws the mask over the image as a red overlay.
    range: >-
      The eight entries of **Rendering Mode** form one choice: choosing one clears the others, and choosing the one in
      force leaves it checked.
    enabled: While the image has a mask.
    scope: The display only.
  - name: Overlay Green
    kind: action
    default: "Off"
    effect: Draws the mask as a green overlay.
  - name: Overlay Blue
    kind: action
    default: "Off"
    effect: Draws the mask as a blue overlay.
  - name: Overlay Yellow
    kind: action
    default: "Off"
    effect: Draws the mask as a yellow overlay.
  - name: Overlay Magenta
    kind: action
    default: "Off"
    effect: Draws the mask as a magenta overlay.
  - name: Overlay Cyan
    kind: action
    default: "Off"
    effect: Draws the mask as a cyan overlay.
  - name: Replace
    kind: action
    default: "Off"
    effect: Shows the mask itself in place of the image.
  - name: Multiply
    kind: action
    default: "Off"
    effect: Shows the image, as the view displays it, multiplied by the mask.
```

### Image > Sequence

The whole submenu is greyed for a still image. **Extract Sequence** and **Export...** open windows of their own:
[Extract Sequence](extract-sequence.md) and [Saving and Exporting](../../interface/saving-and-exporting.md) describe
them. [File Formats](../../reference/file-formats.md) says what each container stores.

```controls
window: Image > Sequence
controls:
  - name: Convert to SER
    kind: action
    default: ""
    effect: >-
      Writes every frame into a new SER sequence in the session's temporary folder and shows it in the view in place
      of the source. The history step is **Convert Sequence Type**, described as
      `Sequence converted from <container> to SER format`.
    enabled: >-
      For a sequence that is not a SER already. It sits in the **Convert** submenu. Chosen another way on a SER, it
      says "The selected sequence is already in SER format".
    tradeoff: >-
      SER holds 8 or 16 unsigned bits of gray, mosaic or RGB. Frames of 8 bits are written as unsigned 8-bit, every
      other type as unsigned 16-bit. A frame's exposure start is kept, and its other header cards are not.
    scope: Every frame as the file holds it. A mosaic stays a mosaic, whatever the view's automatic debayer setting.
    persistence: The source file is not changed, and its frame rate is passed to the new sequence.
  - name: Convert to Image List
    kind: action
    default: ""
    effect: >-
      Writes every frame as a FITS file of its own, with an `.anseq` list that names them, in the temporary folder,
      and shows the new sequence in the view. The history step is **Convert Sequence Type**.
    enabled: >-
      For a sequence that is not an image list already. Chosen another way on one, it says "The selected sequence is
      already in image list format".
    tradeoff: >-
      Each frame keeps its pixel type and its header cards, so this is the container for data you will process
      further. A frame of another type than the first is converted to the first frame's type.
    scope: Every frame as the file holds it. A mosaic stays a mosaic.
    persistence: The source file is not changed, and its frame rate is passed to the list.
  - name: Convert to FITS
    kind: action
    default: ""
    effect: >-
      Converts nothing. Chosen another way, it says "Converting to a FITS sequence is not available. Convert to Image
      List writes every frame of the sequence as a FITS file."
    enabled: Greyed for every sequence. Choose **Convert to Image List** for frames as FITS files.
  - name: Extract Frame
    kind: action
    default: ""
    effect: >-
      Opens the frame the view shows as a new still image, named `<name> - FRAME_<n>` with the number the player
      shows. Its history starts with `Frame <n> extracted from sequence '<name>'`.
    scope: >-
      The frame is copied as the view shows it - debayered when the view debayers its frames - with its pixel type and
      metadata. The sequence is not changed.
    note: A failure is reported in a message box titled **Extract Frame** and in the log.
  - name: Extract Sequence
    kind: action
    default: ""
    effect: >-
      Opens the Extract Sequence window, which copies a range of consecutive frames into a new sequence of the source's
      container: SER, an image list, AVI, MP4, or Matroska for any other video.
  - name: Export...
    kind: action
    default: ""
    effect: Opens the Export Sequence window, which writes the frames to a file you choose.
    tradeoff: The file's format decides what the frames keep of their precision and their header cards.
```

A container conversion locks the view while it runs, removes what it wrote when it fails or is stopped, and shows the
new sequence only once it is complete.

### Process > Adjustments

The other entries of this submenu open process windows, each described on its own page. These three run at once, on
the image or on every frame of the sequence. On a sequence, the frames are read with the view's automatic debayer
setting, so a mosaic is demosaiced first when that setting is on.

```controls
window: Process > Adjustments
controls:
  - name: Negative Transformation
    kind: action
    default: ""
    effect: >-
      Replaces every sample with its complement, equation (4): dark becomes bright and bright dark. The history step
      is **Negative Transform**.
    enabled: With an image or a sequence active.
    tradeoff: >-
      The result is recorded as nonlinear: a saved file states `ANLIN` as `nonlinear` and `ANOP` as
      `negative-transform`, and a linear encoding the image stated is no longer stated.
    scope: >-
      Every channel, through the mask on the view while **Enable Mask** is on, with **Invert Mask** as it is when you
      choose the command. A pixel the image marks invalid is left as it was.
    persistence: The pixel type, the size, the other header cards and the plate solution are kept.
  - name: Convert to RGB
    kind: action
    default: ""
    effect: >-
      Replaces a grayscale image with an RGB one whose three channels are copies of the gray. The history step is
      **Convert To RGB**.
    enabled: For a grayscale image or sequence. Greyed for an RGB one.
    tradeoff: Three times the memory, and no color.
    scope: An explicit validity mask is shared by the three channels.
    persistence: >-
      The pixel type, the size, the acquisition record, the header cards, the plate solution and the processing state
      are kept.
    note: >-
      On a mosaic it asks first, in a box titled **Convert CFA to RGB**, whether to run anyway. **No** stops it.
  - name: Convert to Grayscale
    kind: action
    default: ""
    effect: >-
      Replaces an RGB image with one channel, the weighted sum of equation (3), rounded to the nearest code of its own
      pixel type. The history step is **Convert To Grayscale**.
    enabled: For an RGB image or sequence. Greyed for a grayscale one.
    tradeoff: A third of the memory. The color cannot be brought back from the result.
    scope: >-
      A pixel is valid where all three channels were. An explicit mask's planes are merged into one, and a rule that
      depends on the values becomes such a mask.
    persistence: >-
      The pixel type, the size, the acquisition record, the header cards, the plate solution and the processing state
      are kept.
```

### Sequence progress

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: >-
      Says what the run does, as `Rotating sequence <name> 90 degrees right` or
      `Extracting red channel from sequence <name>`, and moves on after each frame.
    range: >-
      Its title names the command: **Extract sequence channel**, **Convert sequence format**, **Convert sequence
      type**, **Flip sequence**, **Sequence Rotation**, **Convert to grayscale**, **Convert to RGB** or **Negative
      Transformation**. A failure is reported under the same title.
    enabled: >-
      For every command that writes a sequence. A still image has no progress window. The bar of **Convert to SER**
      and **Convert to Image List** does not move until the run ends.
    tradeoff: >-
      The new sequence is written to the session's temporary folder, under a name that starts with `SEQ_SWAP`, and
      needs the disk space of the whole result before the view shows it.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The view stays as it was, and nothing is left in the temporary folder.
    range: A run that works frame by frame notices it before its next frame.
```

## How it works

1. The command reads the active view when you choose it. Without one, or with a view it cannot work on, it says so
   in a message and does nothing. The commands of **Convert Format**, **Extract Channel** and **Sequence**, and
   **Select Mask** and **Remove Mask**, also join **Process > Recent Processes**, which offers them whatever the
   active view is.
2. A still image is computed in the background. A new image opens in a new view beside the source, or the edit is
   recorded in the view's history before the view shows it.
3. A sequence's view is locked against other runs. Every frame is read, worked and written into a new temporary
   sequence, published whole as a new view or in the same view. The source's file is never written. The time grows
   with the frames times their pixels.
4. Each result keeps what its row under Controls says, and the [lineage](../../reference/glossary.md#lineage) of a
   computed result gains an entry with the operation.
5. A run that cannot finish says why, in a message titled after the command and in the Session Console, and leaves
   the image, its history and the temporary folder as they were. Only a **Cancel** is quiet.

### Model

A sample \(x\) of an integer type is first read as a
[fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) \(n\). With \(m_T\) and \(M_T\) the
smallest and largest codes of the source's type \(T\):

\[ n = \frac{x - m_T}{M_T - m_T} \tag{1} \]

For an unsigned type \(m_T = 0\), so \(n = x / M_T\). A floating-point sample is \(n\) as it is. With \(m_U\) and
\(M_U\) the codes of the target type \(U\), a conversion to an integer type writes

\[ y = \operatorname{round}\bigl(m_U + n\,(M_U - m_U)\bigr) \tag{2} \]

rounded in double precision to the nearest code, a tie to the even one, and cut at \(m_U\) and \(M_U\). A
floating-point target holds \(n\) itself.

With \(R\), \(G\) and \(B\) the three channels of a pixel, **Convert to Grayscale** writes

\[ Y = 0.2989\,R + 0.5870\,G + 0.1140\,B \tag{3} \]

and **Convert to RGB** writes \(R = G = B = Y\). **Negative Transformation** writes the complement of \(n\), which in
the image's own codes is

\[ y = 1 - x \ \text{(floating point)}, \qquad y = M_T - x \ \text{(unsigned)}, \qquad y = m_T + M_T - x \ \text{(signed)} \tag{4} \]

so a signed 16-bit sample becomes \(-1 - x\). The mask then blends \(y\) with \(x\), as
[Process Windows](../../interface/process-windows.md) describes. A mirror or a turn computes nothing: each new sample
is one old sample, copied bit for bit.

### Boundaries

- A conversion cuts what lies outside its target: a floating-point sample above 1 becomes the largest code and one
  below 0 the smallest. A sample that is not a number becomes 0 in an integer type. Nothing is dithered.
- A conversion keeps the image's validity rule, except one it would move, such as exact zero as no data next to a
  signed type: that rule becomes an explicit mask. A conversion to the image's own type, which the menu greys, is an
  exact copy.
- A sequence keeps its container. A **Convert Format** command to a type the container cannot hold is refused, and
  an image list holds every type.
- A still image shows no progress window and cannot be stopped once the command starts.
- **Image** has no command that applies an automatic [stretch](../../reference/glossary.md#stretch). The view's
  [Autostretch](../../reference/glossary.md#autostretch) changes the display only, unless you confirm
  **Apply Stretch** in its settings.

### Worked example

The linear Carina [master](../../reference/glossary.md#master) is a 32-bit floating-point RGB image of 7246 x 5429
pixels, with a median of 0.02766 in each channel and a plate solution. Choose
**Image > Convert Format > Convert to unsigned 16 bit**, as in the picture. A new view opens beside the master,
named after it with ` - CONV_16_UINT`, and its history starts with
`Converted to unsigned 16 bits from image '<name>'`. By equation (2), the median becomes
\(\operatorname{round}(0.02766 \times 65535) = 1813\). The cores of the stars above 1 become 65535, and the few
samples below 0 become 0. The status bar reads `[7246x5429 (RGB)] - [16 bit unsigned integer] - [225.10 MB]`, half
the master's 450.19 MB, and the master is unchanged.

Now make the master active and choose **Image > Fast Geometry > Rotate 90º Right**. The status bar reads
`[5429x7246 (RGB)]`, and the view shows the image turned so that its top left corner is at the top right. Its
history ends with **Image Rotation**, described as `90 degrees right`. The Session Console says "Astrometric solution
removed after 90-degree-right rotation of '<name>'. Plate solve again to restore coordinates."

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| The color channels must be worked apart | **Extract RGB Channels**, in the image's own type | Convert the pixel type only for the destination that needs it. |
| Lightness or hue alone | A CIE or HSL component, which is floating point already | Work it in floating point, and convert to an integer type at the end. |
| The frame is upside down or on its side | **Fast Geometry**, before any plate solve | Check the orientation, then plate-solve. |
| A sequence for another tool | **Convert to Image List**, which keeps each frame's type and cards | **Convert to SER** only for 8-bit or 16-bit data. Check the first and the last frame. |

## When it goes wrong

!!! warning "A mirror or a turn drops the plate solution"
    Coordinates, overlays and the color calibrations need it. Plate-solve the image again after **Fast Geometry**.
    A save writes only the header cards the turn kept.

!!! warning "A new sequence lives in temporary storage"
    Save or export a sequence you want to keep. Do not close AfterNight or remove the source's disk while a sequence
    command runs.

**Extract Channel** is greyed
:   **Cause.** The active image is not RGB: it is grayscale, or an undebayered mosaic.
:   **Fix.** Make an RGB image active, or debayer the mosaic first. A grayscale image is already its one channel:
    **Convert to RGB** would only copy it into three.

A **Convert Format** entry is greyed
:   **Cause.** The image already has that type, or a SER or video sequence cannot hold it.
:   **Fix.** Check the type in the status bar. For a sequence, choose **Convert to Image List** first.

"... sequence cannot hold unsigned 16 bits; convert it to an image list first", or the same of another type
:   **Cause.** A conversion of a sequence writes the source's container, which cannot store that type.
:   **Fix.** Choose **Image > Sequence > Convert > Convert to Image List**, then convert the new sequence.

**Convert to FITS** is greyed
:   **Cause.** AfterNight writes no [FITS](../../reference/glossary.md#fits) sequence container.
:   **Fix.** Choose **Convert to Image List**, which writes each frame as a FITS file.

A command on a still image did nothing, and a message titled **Image History** says "Could NOT apply ..."
:   **Cause.** The history could not keep the image the command would replace, often for lack of memory.
:   **Fix.** Close views you no longer need to free memory, and choose the command again.

A mirror or a turn did nothing, and a message titled **Image Rotation**, **Image Horizontal Mirror** or **Image Vertical Mirror** gives a reason
:   **Cause.** The turn could not be computed, or its result could not be shown. The history is as it was.
:   **Fix.** Read the reason in the message or the Session Console, free memory, and choose the command again.

"Another process is executing for this sequence, please cancel that job first before running this process"
:   **Cause.** Another run holds the sequence's view. The message is titled after the command, **Rotate Sequence**
    for a mirror or a turn.
:   **Fix.** Wait for that run to end, or cancel it in its progress window, then choose the command again.

"Could NOT extract frame from image <name> because it is not a sequence"
:   **Cause.** **Extract Frame** was chosen from **Process > Recent Processes** on a still image.
:   **Fix.** Make the sequence active, move the player to the frame, and choose **Image > Sequence > Extract Frame**.

"No selected image to process", "No selected sequence to process" or another "No selected ..." message
:   **Cause.** No view was active, or a sequence command from **Recent Processes** found a still image.
:   **Fix.** Click the view you mean, then choose the command again.

A **Rendering Mode** entry will not uncheck
:   **Cause.** One of the eight is always in force.
:   **Fix.** Choose another mode, or turn **Show Mask** off to hide the mask.

The negative changed the wrong part of the image, or none of it
:   **Cause.** The mask on the view was enabled, or inverted, other than you meant.
:   **Fix.** Undo the step, set **Enable Mask** and **Invert Mask**, check the overlay, and choose the command once
    again.

A sequence command ended with no new sequence and no message
:   **Cause.** The run was cancelled. A failure always gives a message, titled as its progress window.
:   **Fix.** Read the Session Console for the stop, check the free disk space, and try on a short part made with
    **Extract Sequence**.

## Related

- [Extract Sequence](extract-sequence.md), to copy a range of frames into a new sequence.
- [Masks](../../interface/masks.md), to make and attach the mask that **Negative Transformation** blends through.
- [Saving and Exporting](../../interface/saving-and-exporting.md), to keep a new view or sequence, and for
  **Export...**.
- [Image History](../../interface/image-history.md), to undo a command that replaced the image.
- [Sequences](../../interface/sequences.md), for the player, the automatic debayer and the temporary folder.
- [Process Windows](../../interface/process-windows.md), for the color components of **Channel** and how a mask
  blends a result.

## Credits

- The [ICC sRGB characterization](https://www.color.org/sRGB.pdf) defines the sRGB transfer, primaries and D65 white
  the color components are computed with.
- [CIE Standard Illuminant D65](https://www.cie.co.at/datatable/cie-standard-illuminant-d65) is the white the
  tristimulus values are divided by.
- [Recommendation ITU-R BT.601](https://www.itu.int/rec/R-REC-BT.601) gives the luma weights of YCbCr, which equation
  (3) rounds.
