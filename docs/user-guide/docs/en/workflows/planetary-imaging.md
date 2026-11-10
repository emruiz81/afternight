---
kind: workflow
stages: [register, stack, color, noise, finish]
summary: >-
  Open a planetary video, keep its steady frames, write them as image files, register them by phase correlation, stack
  them and sharpen the result.
needs: []
rewritten: true
---
# Planetary Imaging

You start with a video of a planet: a [SER](../reference/glossary.md#ser) capture, or an AVI, MP4, MOV, MKV, MPG,
MPEG or WEBM file. You end with one stacked and sharpened picture of the planet, and the stacked
[master](../reference/glossary.md#master) it came from. The guide's data is a one-minute SER video of Jupiter: 4,965
color frames of 448 by 448 pixels at 82.46 frames per second. Every frame you keep is written, registered and aligned
once, so the time and the disk the steps take grow with the number of frames. The same path serves a video of the
Moon or the Sun. Still frames that a capture program wrote start at step 4, and frames that another program aligned
start at step 6.

## What you need

- [ ] The video, as the capture program wrote it
- [ ] An empty folder for the frames as image files, and another for the aligned frames
- [ ] Room on disk: the whole Jupiter video takes about 3 GB as image files, and 12 GB once aligned

## Your data

- **A color video.** A SER that records red, green and blue, such as the guide's Jupiter video, opens in color. The
  steps follow this case.
- **A raw mosaic video.** A SER of a color camera's [CFA](../reference/glossary.md#cfa) mosaic, such as the guide's
  16-bit Saturn video, asks "Open CFA file" when it opens. Choose **Yes** to see it in color. The frames are written
  and registered as mosaics, and Image Registration [debayers](../reference/glossary.md#debayer) each one as it reads
  it: check its **CFA Pattern** in step 4.

## Steps

```figure
owed: true
image: ../assets/images/interface/sequences.webp
alt: >-
  The AfterNight main window with one sequence view, its tab named jupiter_8bit. Under the tab, the view's toolbar.
  Below, the disc of Jupiter with its belts on a black sky. Under the picture, the player's play, stop, previous and
  next buttons, a long slider and a box reading 1/4965. The status bar reads SER SEQUENCE, 448x448 (RGB), 1/4965 and
  82.46 fps.
caption: >-
  The guide's SER video of Jupiter in its view, on its first frame, with the player under the picture.
pins:
  - {n: 1, x: 16.2, y: 97.6}
  - {n: 2, x: 2.5, y: 91.0}
```

### Open the video

Choose **File > Open** and select the `.ser` file. The view opens on the first frame, with the player under the
picture, and keeps the file open until the view closes. The file is never changed. A video opens the same way, and
**Image > Sequence > Convert > Convert to SER** writes it again as a temporary SER in the same view.

!!! see "You should see"
    The status bar reads `SER SEQUENCE`, the frame's size and the frame shown out of the count: `448x448 (RGB)` and
    `1/4965` for the guide's video, at `82.46 fps`.

### Keep the steady part

Choose **Play sequence** and watch for frames blurred by the seeing, tracking jumps, clouds, a saturated disc and
focus drift. A smaller stack of sharp frames gives a better result than a larger one that holds poor frames. No tool
of AfterNight measures the sharpness of a frame: the player,
[Extract Sequence](../processes/utility/extract-sequence.md) and
[Image List Analyzer](../processes/stacking-integration/image-list-analyzer.md) show frames and rank none.

To keep one steady range, choose **Image > Sequence > Extract Sequence**, type **Start Frame** and **End Frame**, and
choose **OK**. AfterNight does not turn the frames back for the planet's rotation, so a long range smears the detail.
**Image > Sequence > Extract Frame** opens the frame shown as an image of its own, to look at it closely.

!!! see "You should see"
    Playback runs at the video's rate, held between 1 and 60 frames per second, and stops at the last frame. After
    **OK** a new [sequence](../reference/glossary.md#sequence) view opens beside the source with the frames you
    kept. It is a [temporary sequence](../reference/glossary.md#temporary-sequence), lost with the session unless you
    save it.

### Write the frames as image files

Image Registration and Image Stacking take still image files only, never a sequence. With the sequence's view
active, choose **File > Save as...** and type a name ending in `.anseq` in the frames folder. AfterNight writes each
frame as a [FITS](../reference/glossary.md#fits) file into a folder beside the list, named after it with `-frames`
added. A mosaic stays a mosaic.

!!! see "You should see"
    A **Save File** window follows the write. For a list named `jupiter.anseq`, the folder `jupiter-frames` then holds
    `jupiter-0.fits`, `jupiter-1.fits` and so on, one file of about 0.6 MB for each frame of the guide's video.

??? wrong "If it goes wrong: Convert to FITS is disabled"
    **Image > Sequence > Convert > Convert to FITS** is never available. Save as an `.anseq` list as above, or use
    **Convert to Image List**, which writes a temporary list that you then save.

### Register the frames

Choose [**Process > Preprocess > Image Registration**](../processes/registration-alignment/image-registration.md).
Choose **Add Folder** and pick the `-frames` folder, then choose **Browse** and pick the empty output folder. Set
**Method** to **Phase Correlation**: it measures each frame's shift from the whole picture, and needs no stars. Type
the row number of a sharp frame in the box beside **Reference**, and choose **Register**.

For a mosaic, open **Extended Settings... > Advanced Settings...** first, check **CFA Pattern** and choose **OK** in
each dialog.

!!! see "You should see"
    **Reference** reads **Manual** and **Transformation** is disabled. After the run each row reads **Registered**,
    and the line under the output folder says how many frames registered.

??? wrong "If it goes wrong: frames land in the wrong place"
    [Phase correlation](../reference/glossary.md#phase-correlation) measures a shift alone and judges no answer. A
    frame that also turns, or holds too little structure, is placed wrongly. Choose a sharper
    [reference frame](../reference/glossary.md#reference-frame), and keep the planet whole inside every frame.

### Align the frames

Under **Alignment**, keep **Standard** and **Interpolation** at **Auto**, and choose **Align**. Each registered frame
is written onto the reference's grid as a new 32-bit floating-point FITS file.

!!! see "You should see"
    The output folder holds one `<name>_reg.fits` for each frame, 2.4 MB each for the guide's video, and the line under
    it reads "Registration and alignment completed successfully".

### Stack the frames

Choose [**Process > Preprocess > Image Stacking**](../processes/stacking-integration/image-stacking.md). Choose **Add
Files** and select every `_reg.fits` file of the output folder. Keep **Method** at **Average**, with its
**Winsorized Sigma Clipping** [rejection](../reference/glossary.md#rejection). Type a name such as `jupiter.fit` in
**Name Template**, choose **Browse** for its folder, and choose **Stack Images**. Keep the rejection's limits as they
open: tighter ones reject samples along the bright limb, where the frames differ most.

!!! see "You should see"
    A progress window names the group as it is stacked. The master opens in its own view, smoother than any single
    frame, with the planet where it is in the reference frame.

??? wrong "If it goes wrong: the planet is smeared or doubled"
    The frames were not aligned, or the range holds poor frames or too much rotation. Stack the `_reg.fits` files,
    not the frames you wrote in step 3, and keep a shorter, steadier range.

### Balance the color

When the sky has a cast, [Background Neutralization](../processes/color-calibration/background-neutralization.md)
brings its three channels to one level. When the disc has a cast, choose
[**Process > Color Calibration > Color Calibration**](../processes/color-calibration/color-calibration.md). Under
**White Reference**, tick **Region of interest** and type a square inside the disc. Leave **Background Reference** on
the whole image, and choose **Apply**.

!!! see "You should see"
    On the guide's video, with the square at **Left** 118, **Top** 120, **Width** 200 and **Height** 200, red gains
    about 1.3, blue about 1.75 and green keeps 1. The square then stands as far above the background in each channel.

### Sharpen the detail

Choose [**Process > Multiscale > Multiscale Linear Transform**](../processes/multiscale/multiscale-linear.md). Click
the first row of the layer table and raise its **Bias** in small steps from 0.05, then do the same for the next
layers. Tick **Deringing** against the dark and bright rims that sharpening draws at the limb. Choose **Apply**. The
file the stack wrote keeps the master as it was, since sharpening is easy to overdo.

[Unsharp Mask](../processes/sharpening/unsharp-mask.md) and
[Local Contrast Enhancement](../processes/sharpening/local-contrast-enhancement.md) sharpen too, and
[Wavelet Denoise](../processes/noise-reduction/wavelet-denoise.md) smooths the grain scale by scale.

!!! see "You should see"
    At 100 %, the belts and the edges of the disc gain contrast. Judge the limb against the dark sky, where a halo
    shows first.

??? wrong "If it goes wrong: the limb has a bright or dark halo"
    The bias is too high, and [ringing](../reference/glossary.md#ringing) shows. Lower **Bias**, or raise **Dark** and
    **Bright** under **Deringing**, and compare at 100 %.

### Finish and save the picture

Raise the contrast with [Curves Transformation](../processes/stretching/curves-transformation.md) or
[Gamma Correction](../processes/stretching/gamma-correction.md), and frame the planet with
[Crop](../processes/geometry/crop.md), [Resize](../processes/geometry/resize.md) and
[Rotate](../processes/geometry/rotate.md). Then choose **File > Save as...** and type a name ending in `.fits` or
`.tif` to keep the full depth, and in `.png` or `.jpg` to share it.

!!! see "You should see"
    A **Save File** window follows each write, and the view's tab loses the asterisk of unsaved work.

## The result

You have the finished picture in the files you saved, and the stacked master `jupiter.fit`, as the stack wrote it,
in the folder you chose. A new try at the color or the sharpening starts from that master. The frames folder and the
aligned frames take most of the disk, and you may delete them once the master is right.

```figure
owed: true
image: ../assets/images/workflows/planetary-imaging-result.webp
alt: >-
  The AfterNight main window with one image open, in a tab named jupiter: the disc of Jupiter, its dark belts and pale
  zones crossed by fine detail, on a black sky. The Display Mode box in the view's toolbar reads Linear.
caption: >-
  Jupiter stacked from the guide's video and sharpened as this page describes, shown in the Linear display mode.
```

The **Chromatic aberration correction** of Image Registration matches stars, so the color fringe that the atmosphere
spreads along a planet's limb stays in the stack. An animation of a video is made with **Image > Sequence >
Export...**, as [Saving and Exporting](../interface/saving-and-exporting.md) describes.

## Next

- [Sequences](../interface/sequences.md) describes the player and what can be done with a sequence.
- [Extract Sequence](../processes/utility/extract-sequence.md) keeps a range of frames.
- [Image Registration](../processes/registration-alignment/image-registration.md) describes every setting of the
  registration and the alignment.
- [Image Stacking](../processes/stacking-integration/image-stacking.md) describes the combination, the rejection and
  the normalization.
- [Multiscale Linear Transform](../processes/multiscale/multiscale-linear.md) weighs the detail of each scale.
- [Supported Formats](../getting-started/supported-formats.md) lists the video containers AfterNight reads.
