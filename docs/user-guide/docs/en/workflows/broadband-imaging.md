---
kind: workflow
stages: [calibrate, register, stack, color, noise, stretch, finish]
summary: >-
  Take one night of broadband frames from a color or a mono camera through calibration, stacking, gradient and color
  correction, noise reduction and the stretch to a finished picture.
needs: []
rewritten: true
---
# Broadband Imaging

You start with the frames of one target taken through broadband filters: the
[light frames](../reference/glossary.md#light-frame) of a color camera, or the red, green, blue and luminance lights of
a mono camera, with their darks, flats and [bias frames](../reference/glossary.md#bias-frame). You end with a finished
color picture saved for sharing, and the linear [master](../reference/glossary.md#master) it came from kept on disk.
Each step names the process it uses and links its page, which says more. The guide's data is 35 lights of NGC 253 from
a color camera. Their integration took about 40 seconds on a computer with 32 threads, and each later step works on
one image.

## What you need

- [ ] The lights of one target, as the camera wrote them, with the darks, flats and bias frames of the same setup
- [ ] Each kind of frame in a folder of its own
- [ ] A new, empty folder on a disk with room for several times your lights
- [ ] For a mono camera, flats through every filter, and the filter's name in each file's header

A registered light of a color camera holds three 32-bit channels. On the guide's data, 1.4 GB of lights fill about
9 GB of the output folder, 245 MB for each registered light. The **Disk space** line of the Summary counts each
registered light at the size of its raw file, which is too little for a color camera.

## Your camera

- **A color camera.** Add the files as the camera wrote them, and do not
  [debayer](../reference/glossary.md#debayer) them: they are calibrated as a mosaic, and the registration turns them
  into color by the [Bayer pattern](../reference/glossary.md#bayer-pattern) their headers state. A wrong pattern
  gives wrong colors: set it in the registration settings. The steps follow this case, on the guide's data.
- **A mono camera.** Add the lights of every filter with their flats in one run. Each filter makes a master of its own,
  and the lights of one object share one registration reference, so the masters land on one grid. Tick **Combine RGB
  images** on the **Stacking** tab before step 4 to get the color master too: the filter names must hold "red",
  "green" and "blue", and `R` does not count. Or compose the three masters with
  [Channel Composition](../processes/color-composition/channel-composition.md), **Linear Match** off. Take the
  luminance master through steps 5, 6 and 8 with the color image's settings, and add it in step 10.

## Steps

```figure
owed: true
image: ../assets/images/workflows/broadband-imaging.webp
alt: >-
  The Image Integration window on the Lights tab of Frame Sources. Under the menus Frames, Project, Edit, Run, Navigate
  and Help, the tabs Frame Sources and Workflow, and the tabs Bias, Darks, Flats and Lights. Below the Add Files, Add
  Folder, Remove Selected and Remove All buttons, the list shows two groups of lights, one for the object NGC253 with
  18 frames and one for the object Target with 17. To the right, the preview of the selected light and the Group By
  box. Display is set to Hierarchical under the list, and the Output Directory field with its button runs along the
  bottom.
caption: >-
  Image Integration with a night on NGC 253 added: the Lights tab in its Hierarchical display shows the two groups that
  the headers make.
pins:
  - {n: 1, x: 44.0, y: 13.8}
  - {n: 2, x: 16.5, y: 87.8}
  - {n: 3, x: 70.0, y: 92.2}
  - {n: 4, x: 20.7, y: 11.4}
```

### Add the frames to Image Integration

Choose [**Process > Preprocess > Image Integration**](../processes/stacking-integration/image-integration.md). The
window opens with the project you left: choose **Frames > Remove All Frames...** to start a new one. On each tab of
**Frame Sources** - **Bias**, **Darks**, **Flats** and **Lights** - choose **Add Folder** and pick the folder that
holds the frames of that type. Your files are only read.

[Image Integration Lite](../processes/stacking-integration/image-integration-lite.md), which the
[Quick Start](../getting-started/quick-start.md) uses, takes the lights of one object name only. The full window can
join two names, as the guide's data needs.

!!! see "You should see"
    An **Importing files...** window reads the headers, then a note says what was added, such as "Imported: 35
    Lights". On the guide's data the tabs hold 30 bias frames, 20 darks, 40 flats and 35 lights.

??? wrong "If it goes wrong: a folder adds nothing"
    "No supported image files found in the selected directory." means the folder holds no still image AfterNight
    reads. A video or a [SER](../reference/glossary.md#ser) file is not taken: see
    [Planetary Imaging](planetary-imaging.md) for those.

### Put the lights of one target in one group

On the **Lights** tab, choose **Hierarchical** under the list. Each group names what its frames share, and each group
of lights becomes a master of its own. Lights whose headers write the object, the filter or the night differently
land in different groups.

To join two groups of one target, right-click one of them and choose **Metadata Overrides > Set Object...**. Type the
other group's object and choose **OK**. Your files are not changed.

!!! see "You should see"
    On the guide's data the capture program wrote `NGC253` in the first 18 lights and `Target` in the other 17, so the
    tab shows two groups. After **Set Object...** with `NGC253` on the **Target** group, one group holds the 35
    lights.

??? wrong "If it goes wrong: a frame sits on the wrong tab"
    Its header names another type. Right-click it and choose **Change Type**: this changes how the run uses the file,
    never the file.

### Check the calibration and the output folder

Type the new folder in **Output Directory** at the bottom, or choose **...** beside it. Open **Workflow >
Calibration** and read the **Bias**, **Dark** and **Flat** cells of the light group. A green check is an exact match,
and `-` under **Bias** means the dark, which holds the bias, takes its place. Leave the registration and the stacking
at their defaults, and open **Summary**.

!!! see "You should see"
    The light group takes the master dark and the master flat. The flat group finds no dark of its exposures, so its
    **Dark** cell shows a warning sign and the bias calibrates the flats. The Summary card reads **Ready to Execute**
    or **Ready with Warnings**, with "35 light frames → 1 stacked image(s)".

??? wrong "If it goes wrong: a warning about a missing flat calibration match"
    "Missing flat calibration match" means that no flat group matches the lights' size, filter or night. Add the
    flats of that night and filter, or right-click the cell and choose a master by hand.

### Run the integration

Choose **Execute Integration** on **Summary**, or press ++ctrl+enter++. The **Execution Plan** lists each stage with
**Recompute**, **Reuse** or **Skip**. Choose **Run**, and keep the frames and the output folder where they are until
the run ends. **Cancel** stops at the next safe point, and the groups finished by then keep their masters.

!!! see "You should see"
    The **Image Integration - Processing** window marks each stage from **Bias** to **Stacking**, then a box reads
    "Workflow completed successfully." The master `stacked_5544x3684_NGC253.fit` opens in its own view. It is a
    [linear image](../reference/glossary.md#linear-image), nearly black: choose
    [Autostretch](../reference/glossary.md#autostretch) in **Display Mode** on its toolbar, which changes the display
    only.

??? wrong "If it goes wrong: lights stay unregistered"
    The log says "star pairs matched" and how many were needed: too few stars matched the reference. On
    **Registration**, choose a reference with more stars, lower **Threshold** or raise **Maximum Stars per Frame**,
    and run again: the plan reuses the calibration masters.

### Crop the stacking edges

Registration moves each light onto the reference's grid, so the edges of the master hold fewer lights. Make the master
the [active image](../reference/glossary.md#active-image) and choose
[**Process > Geometry > Crop**](../processes/geometry/crop.md). Type **Pos X** and **Pos Y**, the pixels to cut from
the left and the top, then **Width** and **Height**, which end the rectangle before the other edges. Choose **OK**.

!!! see "You should see"
    On the guide's data the lights taken after the meridian flip lie up to about 110 pixels away from the others
    along each axis. With **Pos X** and **Pos Y** at 128, **Width** at 5288 and **Height** at 3428, every edge loses
    128 pixels, and the [history](../reference/glossary.md#history) reads
    `Cropped area : X: 128 Y: 128 W:5288 H:3428`.

### Remove the gradient

Choose
[**Process > Background Modelization > Auto Gradient Removal**](../processes/background-extraction/auto-gradient-removal.md),
then **Preview**. Set **Preview display** to **Gradient model**: the model must be a smooth surface, without the
galaxy or its stars. Choose **Apply**.

!!! see "You should see"
    The view shows the corrected image, and its history lists **Auto Gradient Removal**. The background keeps its
    level: the subtraction puts back the model's median.

??? wrong "If it goes wrong: the model shows the galaxy or its halo"
    Raise **Scale**, or lower **Threshold** to keep more of the bright structure out of the fit, and choose
    **Preview** again. To place the samples yourself, use
    [Background Extraction](../processes/background-extraction/background-extraction.md).

### Balance the color

First bring the three backgrounds to one level. Choose
[**Process > Color Calibration > Background Neutralization**](../processes/color-calibration/background-neutralization.md),
keep **Reference image** at `<--TARGET IMAGE-->` and choose **OK**. **Upper Limit** must lie above each channel's
background, whose median [Image Statistics](../interface/image-statistics.md) gives.

Then choose
[**Process > Color Calibration > Color Calibration**](../processes/color-calibration/color-calibration.md).
Under **White Reference**, tick **Region of interest** and type a rectangle around the galaxy, or around a field of
stars without nebula. Under **Background Reference**, tick it too and type a rectangle of sky beside it. Choose
**Apply**. With a [plate solution](../reference/glossary.md#plate-solution) and the catalog data installed,
[Spectrophotometric Color Calibration](../processes/color-calibration/spectrophotometric-cc.md) balances the color by
the stars' spectra instead.

!!! see "You should see"
    On the guide's data the backgrounds sit near 0.02 in red and blue and 0.034 in green, under the default **Upper
    Limit**. With the white at **Left** 2000, **Top** 1300, **Width** 1300 and **Height** 900, and the sky at 400,
    400, 800 and 800, blue gains about 1.7 and red and green stay near 1: the galaxy stood weakest in blue. With
    **Linked channels** on in the view's Autostretch Settings, the sky is gray.

??? wrong "If it goes wrong: nothing seems to change"
    The view's Autostretch treats each channel on its own, which hides most of a calibration. Turn on **Linked
    channels** in its Autostretch Settings.

### Reduce the noise

Choose [**Process > Noise Reduction > Wavelet Denoise Filter**](../processes/noise-reduction/wavelet-denoise.md).
The preview shows the result inside a framed
[preview area](../reference/glossary.md#preview-area): move it over the galaxy's faint arms and the sky beside them.
Raise **Threshold** while grain remains, and lower it when small stars turn soft. Choose **Apply**.

To make the stars smaller in step 11, make their [mask](../reference/glossary.md#mask) now, while the image is still
linear: [Star Mask](../processes/star-operations/star-mask.md) opens it as a new image named after the master.

!!! see "You should see"
    Inside the area the grain of the sky fades, and the stars keep their size. After **Apply** the history lists
    **Wavelet Denoise** with its four values.

### Stretch the image

Choose
[**Process > Intensity Transformations > Histogram Transformation**](../processes/stretching/histogram-transformation.md)
and keep **Channel** at **RGB**. Raise **Shadows** to the foot of the peak, while the **Clipped** field beside it
stays near zero. Lower **Midtones** until the faint arms show in the preview. Choose **Apply**: the
[stretch](../reference/glossary.md#stretch) makes the image a
[nonlinear image](../reference/glossary.md#nonlinear-image), and every later step works on it as it is.

!!! see "You should see"
    On the guide's data, **Shadows** and **Midtones** at 0.02 bring the background to about 0.2, a dark gray, with
    the galaxy spread above it, and the **Clipped** fields stay near zero. Choose **Linear** in **Display Mode**: the
    picture is now in the image itself.

??? wrong "If it goes wrong: the colors shift"
    One channel was stretched alone. Undo the step in the history and stretch again with **Channel** at **RGB**.

### Add the luminance, with a mono camera

Skip this step with a color camera. Make the luminance master active and stretch it with Histogram Transformation
until its background median sits at the color image's lightness. A color background of 0.28 has a lightness of
0.30, for example. Read both medians in [Image Statistics](../interface/image-statistics.md). Then make the color
image active, choose
[**Process > Color Spaces > LRGB Combination**](../processes/color-composition/lrgb-combination.md), pick the
luminance in **Luminance** and choose **Apply**.

!!! see "You should see"
    The preview shows the color image with the luminance's detail, its background as dark as before. After **Apply**
    the history lists **LRGB Combination** with the luminance's name.

??? wrong "If it goes wrong: the luminance is not in the list"
    Its width or height differs from the color image's. Crop both with the same rectangle in step 5.

### Finish the colors and the stars

Choose
[**Process > Intensity Transformations > Color Saturation**](../processes/color-adjustment/color-saturation.md),
leave **Hue** at **Global**, raise **Amount** in small steps and choose **Apply**. For more contrast, add a gentle
curve with [Curves Transformation](../processes/stretching/curves-transformation.md).

To make the stars smaller, attach the star mask with **Image > Mask > Select Mask**: its white parts take the change.
Then choose
[**Process > Morphology > Morphological Transformation**](../processes/masking/morphological-transformation.md),
keep **Operator** at **Erosion (Minimum)**, lower **Amount** until the stars shrink without dark rings, and choose
**Apply**.

!!! see "You should see"
    With **Amount** at 0.50 in Color Saturation the galaxy takes stronger color, and the gray sky keeps its own. After
    the [erosion](../reference/glossary.md#erosion) the stars are smaller, and nothing outside the mask has changed.

### Save the picture

Choose **File > Save as...** and type a name ending in `.xisf`, `.fits` or `.tif`, which keeps the 32-bit result for
more work. Then save again under a name ending in `.png` or `.jpg` to share it: PNG writes 16 bits and JPEG 8. No
command writes back over the master in the output folder, which the steps never changed.

!!! see "You should see"
    A **Save File** window follows each write, and the view's tab loses the asterisk of unsaved work. AfterNight does
    not open a file again to check it: open what you wrote before you delete anything.

??? wrong "If it goes wrong: the saved picture is nearly black"
    The view's Autostretch is a display only and is never saved. Stretch the image with Histogram Transformation and
    save again.

## The result

You have the finished picture in the two files you saved. The output folder holds the linear master,
`stacked_5544x3684_NGC253.fit`, which a new try at the later steps starts from, and the bias, dark and flat masters.
Keep the separate masters of a mono camera too. Beside them are the registered lights, which you may delete once you
are done, and under `history` a folder for each run with its report, `workflow_report.html`, its log and its settings.
`history` keeps the newest runs only, within 20 runs and 5 GiB. To keep a run, copy the frames and the whole output
folder, with `.afternight`, which lets a later run reuse this one.

```figure
owed: true
image: ../assets/images/workflows/broadband-imaging-result.webp
alt: >-
  The AfterNight main window with one image open, in a tab named stacked_5544x3684_NGC253: the galaxy NGC 253 seen
  almost edge-on across the middle of the picture, its dust lanes dark against its disc, in a field of white stars on
  an even dark gray background. The Display Mode box in the view's toolbar reads Linear.
caption: >-
  NGC 253 after the steps on this page, shown in the Linear display mode: the picture is in the image itself.
```

The image's history lists each step in order. Return to any of them through
[Image History](../interface/image-history.md) to try other settings from there.

## Next

- [Image Integration](../processes/stacking-integration/image-integration.md) describes every setting of the run, for
  several targets, nights and filters.
- [Spectrophotometric Color Calibration](../processes/color-calibration/spectrophotometric-cc.md) balances the color
  against star catalogs.
- [Generalized Hyperbolic Transformation](../processes/stretching/gh-transformation.md) stretches with more control
  over the faint and the bright parts.
- [Starnet++](../processes/star-operations/starnet.md) separates the stars from the galaxy for work on each.
- [Narrowband Imaging](narrowband-imaging.md) and [Mosaic Workflow](mosaic-workflow.md) take other kinds of data
  through the same stages.
- [Saving and Exporting](../interface/saving-and-exporting.md) says what each file format keeps.
