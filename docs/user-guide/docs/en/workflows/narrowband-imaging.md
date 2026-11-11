---
kind: workflow
stages: [calibrate, register, stack, color, noise, stretch, finish]
summary: >-
  Stack Ha, OIII and SII frames into three masters that line up, remove their gradients, map them into a color palette
  such as SHO and finish the picture.
needs: []
---
# Narrowband Imaging

You start with the narrowband frames of one target: the [light frames](../reference/glossary.md#light-frame) of a mono
camera through Ha, OIII and SII filters, with their flats, or those of a color camera through a dual-band filter. You
end with a color picture in a palette such as [SHO](../reference/glossary.md#sho), and the linear
[master](../reference/glossary.md#master) of each line kept on disk to start again from. Each step names the process
it uses and links its page, which says more. On the guide's data, 64 lights of the Eagle Nebula, the stacking took
under a minute on a computer with 32 threads.

## What you need

- [ ] The lights of each filter, as the camera wrote them, with the [flat frames](../reference/glossary.md#flat-frame)
  of each filter
- [ ] The filter's name in the header of each light and each flat, as capture programs write it
- [ ] A new, empty folder for the results, on a disk with room for several times the size of your lights

The guide's data comes from a QHY533M camera: 25 lights through Ha, 20 through OIII and 19 through SII, each of 300 s
and 3008 × 3004 pixels, in a folder `LIGHT`, and 50 flats for each filter in a folder `FLAT`. It has no darks and no
[bias frames](../reference/glossary.md#bias-frame). Add yours when you have them.

## Your camera

- **A mono camera.** Add the lights of every filter in one run. Each filter gives a master of its own, and the
  masters line up, because every light of one target is registered onto one reference. The steps follow this case, on
  the guide's data.
- **A color camera with a dual-band filter.** Add the files as the camera wrote them, and do not
  [debayer](../reference/glossary.md#debayer) them first. The run gives one color master: take it through steps 4
  and 5 as one image. In step 6, choose it alone in **OSC1**, which takes Ha from its red and OIII from the mean of its
  green and blue, and click the [HOO](../reference/glossary.md#hoo) tile. For a filter that passes SII and OIII, use
  **OSC2**, which takes SII from the red.

## Steps

```figure
image: ../assets/images/processes/stacking-integration/image-integration-lite.png
alt: >-
  The Image Integration Lite window after a run. An amber banner at the top reads Ready with 4 warnings. The Dataset
  card counts 64 light, 150 flat, 0 dark and 0 bias frames, with the filters Ha, Oiii and Sii and the object M 16. The
  Setup card holds the Output folder with its Browse button and the options of the run, and Processing Settings lists
  four closed lines. The Frames card has the Add as box, the Add Files, Add Folder, Change Type and Clear All buttons,
  and the table of the files with their type, filter, object, exposure and calibration. Run Integration, Open Full
  Process and Close are at the bottom.
caption: >-
  Image Integration Lite after a run on the Eagle Nebula's Ha, OIII and SII lights and their flats, every setting at
  its default.
pins:
  - {n: 1, x: 43.6, y: 71.6}
  - {n: 2, x: 66.0, y: 97.3}
```

### Stack the three filters in one run

Choose **Process > Preprocess > Image Integration Lite**. Choose **Browse** beside **Output** and pick the new, empty
folder. Leave **Add as** at **Auto-detect**, choose **Add Folder** and pick the folder of the lights, then choose it
again for the flats. Each file's header gives its type and its filter. Leave the four lines of **Processing
Settings** as they are.

!!! see "You should see"
    The **Dataset** card reads 64 light and 150 flat, the filters Ha, Oiii and Sii, and the object M 16. Each light's
    **Calibration** reads `Flat`. The banner is amber and reads "Ready with 4 warning(s)." Its tooltip says "Missing
    dark/bias calibration match" once for each filter's lights, since the guide's data has no darks, and "Flat frames
    have no bias or Dark Flat calibration. Master flats will be uncalibrated and may over-correct lights."

??? wrong "If it goes wrong: the banner reads "Image Integration Lite expects one target object.""
    The lights carry more than one object name in their headers. Choose **Open Full Process**, and in
    [Image Integration](../processes/stacking-integration/image-integration.md) give them one name with **Metadata
    Overrides > Set Object...**.

### Run the integration

Choose **Run Integration**, and keep the frames and the output folder where they are while it runs. A progress
window names each stage. The lights of all three filters are registered onto one reference, then each filter is
stacked on its own.

!!! see "You should see"
    A box titled **Complete** reads "Workflow completed successfully." The three masters open, each in its own view:
    `stacked_3008x3004_Ha_M_16.fit`, `stacked_3008x3004_Oiii_M_16.fit` and `stacked_3008x3004_Sii_M_16.fit`. On the
    guide's data the registration chose one OIII light as the reference of all 64.

### Check that the masters line up

The masters are [linear images](../reference/glossary.md#linear-image), nearly black: choose
[Autostretch](../reference/glossary.md#autostretch) in **Display Mode** on the toolbar of each view, which changes the
display only. Make the Ha master the [active image](../reference/glossary.md#active-image), choose **Process >
Utilities > Image Compare** and pick the OIII master in **Image 2**. Hold ++space++ over the preview to switch between
the two, then do the same with the SII master, and choose **Close**. Read each master's **Median** in **Image >
Information > Statistics**.

!!! see "You should see"
    The stars stay in place while the preview switches. The medians are about 0.0378 for Ha, 0.0386 for OIII and
    0.0364 for SII. No master has an empty border, so none needs a crop. In Autostretch the left side of the OIII and
    SII masters is brighter than their right side: that is the gradient the next step removes.

??? wrong "If it goes wrong: the stars jump when the preview switches"
    The masters do not share one grid, as when they come from separate runs. Stack the filters in one run, or put the
    masters on one grid with [Image Registration](../processes/registration-alignment/image-registration.md). If a
    master then has an empty border, cut all three with the same rectangle in
    [Crop](../processes/geometry/crop.md).

### Remove the gradient of each master

Make the Ha master active and choose **Process > Background Modelization > Auto Gradient Removal**. A gradient that
runs smoothly across the whole frame, as on these masters, suits a polynomial: tick **Simplified polynomial model**
and leave **Polynomial degree** at 2. Choose **Preview**, set **Preview display** to **Gradient model**, and choose
**Apply**. Then choose the OIII master in **Target image** and do the same, and the SII master after it. **Apply**
returns the controls to their defaults, so tick the box again each time. Close the window when the three are done.

!!! see "You should see"
    The model is a smooth surface, without the clouds, the pillars or the stars. After **Apply** the
    [history](../reference/glossary.md#history) of each master lists **Auto Gradient Removal** with `Model: Simplified
    polynomial, Degree: 2`, and in Autostretch both sides of the frame sit at one level.

??? wrong "If it goes wrong: the model shows the shape of the nebula"
    The fit followed the nebula, which fills much of this frame. Set **Polynomial degree** to 1, or place the samples
    yourself, off the nebula, with
    [Background Extraction](../processes/background-extraction/background-extraction.md).

### Reduce the noise, if you want

Make each master active in turn and choose **Process > Noise Reduction > Wavelet Denoise Filter**. Move the framed
[preview area](../reference/glossary.md#preview-area) over faint nebula and background. Raise **Threshold** while
grain remains, lower it when small stars turn soft, and choose **Apply**. Use the same values on the three masters.
They stay linear.

!!! see "You should see"
    In the preview area the grain of the background fades and the stars keep their size. After **Apply** the history
    lists **Wavelet Denoise** with its four values.

### Map the lines to a palette

Choose **Process > Color Spaces > Narrowband Palette Composer**. Under **Sources**, choose the Ha master in **Ha**,
the OIII master in **OIII** and the SII master in **SII**. Leave **Input autostretch** and **Match sizes to reference**
on: the window [stretches](../reference/glossary.md#stretch) each master by its own statistics as it maps it, so
linear masters give a picture. Click the tile of the palette you want, and choose **Apply**.

The new image is a [nonlinear image](../reference/glossary.md#nonlinear-image): the stretch is in its samples, and
it is recorded so. Choose **Linear** in **Display Mode** to see it as it is, and do not stretch it again. The color
calibrations need linear data and refuse it.

For Ha and OIII alone, click the HOO tile. To give each line a hue of your own, use
[NB Color Mapper](../processes/color-composition/nb-color-mapper.md) instead. To put the masters into red, green and
blue as they are, SII in **Red**, Ha in **Green** and OIII in **Blue** for SHO, use
[Channel Composition](../processes/color-composition/channel-composition.md), then stretch the result with
[Histogram Transformation](../processes/stretching/histogram-transformation.md).

!!! see "You should see"
    The grid fills with twelve tiles, the SHO tile framed, and **Status** reads "Palette grid is ready. Apply will
    create a new SHO RGB image view." **Apply** opens `stacked_3008x3004_Oiii_M_16 - SHO Narrowband Palette` beside the
    OIII master. Its background is a neutral gray near 0.25, and its clouds are green where Ha is strongest. Its header
    states `EXPTIME` 19200 and `STACKCNT` 64, the three masters together.

??? wrong "If it goes wrong: doubled stars or colored fringes in every tile"
    The masters have one size but not one grid. **Match sizes to reference** matches width and height, never the
    stars. Go back to step 3.

### Take out the green, if you want

SHO puts Ha in green, and on the guide's data the clouds are brightest in Ha. Make the palette image active, choose
**Process > Color Calibration > SCNR Filter**, keep **Average Neutral** and **Preserve Lightness**, and choose
**Apply**. Skip this step for HOO, whose green is OIII. Another tile of the grid is the other way to change the
colors.

!!! see "You should see"
    The clouds lose their green: each pixel keeps its lightness and takes the color its SII and its OIII give it. The
    gray background stays gray, since its green is already about the mean of its red and blue. The history lists
    **SCNR**.

### Finish the picture

Choose **Process > Intensity Transformations > Curves Transformation**. Press on the line over the peak of the
histogram, near 0.25, drag it down to about 0.15 for a darker sky, and choose **Apply**. Then choose **Process >
Intensity Transformations > Color Saturation**, leave **Hue** at **Global**, raise **Amount** in small steps, to 0.50
for example, and choose **Apply**. To treat the nebula without its stars, separate them with
[Starnet++](../processes/star-operations/starnet.md) and add them back with
[Starmask Recombination](../processes/star-operations/starmask-recombination.md).

!!! see "You should see"
    After the curve the sky is darker, and black and white stay where they were. With **Amount** at 0.50 the clouds
    take stronger color, and most of the sky keeps its own.

### Save the picture and the masters

Choose **File > Save as...** and type a name ending in `.xisf` or `.fits`, which keeps the 32-bit result for more
work. Save again under a name ending in `.png` or `.jpg` to share it. Save the three corrected masters under new names
too: a new palette starts from them, not from the picture.

!!! see "You should see"
    A **Save File** window follows each write, and the view's tab loses the asterisk of unsaved work.

??? wrong "If it goes wrong: a saved master looks nearly black"
    The view's Autostretch is a display only and is never saved. A master stays linear on disk, as it should.

## The result

You have the picture in the files you saved, and the three corrected masters. The output folder holds the masters as
the run wrote them, `stacked_3008x3004_Ha_M_16.fit` and its Oiii and Sii twins, untouched by the steps. Beside them
are the flat masters, the registered lights under `registered_lights`, and under `history` a folder for the run with
its [report](../processes/stacking-integration/integration-report.md), `workflow_report.html`.

```figure
owed: true
image: ../assets/images/workflows/narrowband-imaging-result.webp
alt: >-
  The AfterNight main window with one image open, in a tab named stacked_3008x3004_Oiii_M_16 - SHO Narrowband
  Palette: the clouds of the Eagle Nebula with the dark pillars near the middle, in warm and blue tones without the
  green of SHO, over a dark gray background with many stars. The Display Mode box in the view's toolbar reads Linear.
caption: >-
  The Eagle Nebula in SHO after the steps on this page, shown in the Linear display mode: the picture is in the image
  itself.
```

Each image's history lists its steps in order. Return to any of them through
[Image History](../interface/image-history.md) to try other settings from there.

## Next

- [Narrowband Palette Composer](../processes/color-composition/narrowband-palette-composer.md) describes the twelve
  palettes, the strength of each line and the one-shot-color estimates.
- [NB Color Mapper](../processes/color-composition/nb-color-mapper.md) gives each master a hue of your own and
  blends them.
- [NBRGB Combination](../processes/color-composition/nbrgb-combination.md) adds the line signal of an Ha master to
  the red of a broadband color image.
- [Image Integration](../processes/stacking-integration/image-integration.md) stacks several nights, with darks
  matched by hand and a project you save.
- [Broadband Imaging](broadband-imaging.md) and [Mosaic Workflow](mosaic-workflow.md) take other kinds of data
  through the same stages.
- [Saving and Exporting](../interface/saving-and-exporting.md) says what each file format keeps.
