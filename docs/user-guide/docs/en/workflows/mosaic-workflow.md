---
kind: workflow
stages: [calibrate, register, stack, color, stretch, finish]
summary: >-
  Stack the overlapping panels of a mosaic in one Image Integration run onto one canvas, matched over their overlaps,
  then crop the master and finish it as one field.
needs: []
---
# Mosaic Workflow

You start with the frames of several overlapping panels of one target: the
[light frames](../reference/glossary.md#light-frame) of each panel and the
[calibration frames](../reference/glossary.md#calibration-frame) of each night. You end with one linear
[master](../reference/glossary.md#master) for each filter that holds every panel on one canvas, ready for the color
and the [stretch](../reference/glossary.md#stretch) of a single field.

AfterNight builds a mosaic inside [Image Integration](../processes/stacking-integration/image-integration.md), in one
run: it registers the lights of every panel onto one canvas, matches their levels over the areas they share, and
stacks them. It matches the panels' levels only inside that run, and it never blends the edges of the panels. Image
Integration Lite has neither the canvas nor the matching. The run takes as long as the stack of all the
panels' frames, and longer when panels are sent to a plate solver, above all an online one.

## What you need

- [ ] The lights of every panel, as the camera wrote them, with the flats of each night and filter, and the darks and
  [bias frames](../reference/glossary.md#bias-frame) you have
- [ ] Panels that overlap their neighbors, so that each panel shares stars with another
- [ ] Panels taken with one camera and one binning, so that every light has the same size
- [ ] A plate solver set up on the **Plate Solving** page of [Settings](../interface/settings-dialog.md), for a panel
  whose stars match no other panel's
- [ ] A new, empty output folder on a disk with far more room than for one field: each registered light is written
  at the size of the whole canvas

Panels of a similar depth, in the same number and length of exposures, stack to an even noise across the field.

## Steps

```figure
owed: true
image: ../assets/images/workflows/mosaic-workflow.webp
alt: >-
  The Registration Settings dialog of Image Integration, the name of its registration group under the title. Among
  its settings, the alignment row offers Standard, Mosaic and Intersection, with Mosaic chosen, and the Auto-solve
  unsolved frames during Registration box is ticked and greyed, with a note under it. Reset to Defaults, Apply to All
  Groups, OK and Cancel run along the bottom.
caption: >-
  The registration settings of a group of panels with the Mosaic canvas chosen and a plate solver set up.
```

### Add every panel's frames

Choose **Process > Preprocess > Image Integration**. The window opens with the project you left: choose **Frames >
Remove All Frames...** to start a new one. On each tab of **Frame Sources**, choose **Add Folder** and pick the folder
of that type, for every panel and every night. Type the new folder in **Output Directory** at the bottom.

!!! see "You should see"
    After each import a note says what was added. The **Lights** tab lists the lights of every panel.

### Give the panels one object

Capture programs often name each panel differently, and the registration and the stack split the lights by object.
On the **Lights** tab, choose **Hierarchical** under the list. Right-click the group of each panel, choose **Metadata
Overrides > Set Object...** and type the same name each time. Your files are not changed.

!!! see "You should see"
    The panels of one filter and night form one group. On **Workflow > Registration**, **Registration Groups** lists
    one [registration group](../reference/glossary.md#registration-group) for all the panels, whatever their filter,
    so the masters of the filters line up.

??? wrong "If it goes wrong: the panels stay in two registration groups"
    Their lights differ in size, and each size makes a group of its own. Only lights of one size share a canvas.

### Check the calibration

Open **Workflow > Calibration** and read the **Bias**, **Dark** and **Flat** cells of each light group. The lights of
each night take that night's flats.

!!! see "You should see"
    Each light group takes the masters you expect, and a green check marks an exact match.

??? wrong "If it goes wrong: "Missing flat calibration match""
    No flat group matches a group of lights in size, filter or night. Add the flats of that night and filter, or
    right-click the cell and choose a master by hand.

### Choose the mosaic canvas

On **Workflow > Registration**, select the group and choose **Registration Settings**. Choose **Mosaic**, then **OK**.
The canvas then holds every panel. A light that shares too few stars with the reference is tried again through the
lights already registered nearest it, in the order of their file paths. A light the stars cannot place at all is
placed by its sky solution: its header's, or the plate solver's. With several registration groups, choose **Apply to
All Groups** before **OK**.

!!! see "You should see"
    With a plate solver set up, **Auto-solve unsolved frames during Registration** shows ticked and greyed, with the
    note "Mosaic alignment sends every panel star matching cannot register to the configured plate solver, so
    Auto-solve stays on while Mosaic is chosen." under it. The **Alignment** column of **Registration Groups** reads
    **Mosaic**.

??? wrong "If it goes wrong: Auto-solve shows unticked and greyed"
    No plate solver is set up. Set one up on the **Plate Solving** page of the settings, or rely on panels that all
    share stars with a neighbor.

### Match the panels over their overlaps

On the **Stacking** tab, select each row of **Integration Groups** and choose **Stacking Settings...**. Leave
**Normalization** at **Additive With Scaling**, tick **Enable Overlap Normalization** and choose **OK**. Without it,
each light is matched to the reference alone, which a panel shares little or nothing of, and the panels can meet at a
step in brightness.

!!! see "You should see"
    **Min Overlap Pixels** and **Min Overlap Fraction** are enabled. A pair of lights that shares fewer pixels, or a
    smaller part of the smaller light, is left out of the matching.

### Run the integration

On **Summary**, choose **Execute Integration**, read the **Execution Plan**, and choose **Run**. To keep the disk
from filling, tick **Allow Registered Images to Be Discarded** in the stacking settings first: the stack then warps
each light as it reads it, and no registered light is written.

!!! see "You should see"
    During the normalization the progress window reads "Finding overlaps", then "Measuring overlaps". At the end a box
    reads "Workflow completed successfully.", and one master for each filter opens. Each holds every panel on one
    canvas, at 0 where no light reaches. Its [header cards](../reference/glossary.md#header-card) `RA` and `DEC` state
    the center of the solved field, or without a solution the mean of the lights' own pointings.

??? wrong "If it goes wrong: the log says "Overlap normalization fell back to standard global normalization:""
    The reason follows, such as "no two lights overlap enough" or "the overlapping lights do not connect". The
    overlaps are smaller than **Min Overlap Pixels** or **Min Overlap Fraction**, or a panel overlaps no other. Lower
    the two minimums, or add the panel that links the others, and run again.

### Look at the seams

Choose [Autostretch](../reference/glossary.md#autostretch) in **Display Mode** on the master's toolbar, which shows a
small difference of level clearly. Zoom to 100 % and follow the places where panels meet.

!!! see "You should see"
    No step in brightness or color where two panels meet, and single stars there. A panel of fewer or shorter
    exposures is noisier than its neighbors: each pixel is the mean of the lights that reach it, and the edges of the
    panels are not blended.

??? wrong "If it goes wrong: stars are doubled where two panels meet"
    The transforms do not follow the distortion of the optics at the edges of the panels. In **Registration
    Settings**, choose **Polynomial 2** or **Polynomial 3** in **Transform Model** and run again. A polynomial needs
    more matched stars, which a wider overlap gives.

### Crop the edges

The panels rarely fill a rectangle, and every pixel no light reaches is 0. Make the master the
[active image](../reference/glossary.md#active-image) and choose **Process > Geometry > Crop**. Type **Pos X** and
**Pos Y**, the pixels to cut from the left and the top, then **Width** and **Height** of the rectangle that every
panel covers, and choose **OK**.

!!! see "You should see"
    The black corners are gone, and the [history](../reference/glossary.md#history) reads `Cropped area : X:` with the
    four numbers you typed. A [plate solution](../reference/glossary.md#plate-solution) the master holds is carried
    with the cut.

### Remove the gradient of the whole field

Choose **Process > Background Modelization > Auto Gradient Removal**, then **Preview**, and set **Preview display** to
**Gradient model**. Over a mosaic the model must be one smooth surface, without the nebula or the outline of a panel.
Choose **Apply**. Where real glow spans several panels, place samples yourself with
[Background Extraction](../processes/background-extraction/background-extraction.md).

!!! see "You should see"
    After **Apply** the background is even from one end of the mosaic to the other, and the history lists **Auto
    Gradient Removal**.

### Finish it as one field

Choose **File > Save as...** and save the cropped, corrected master under a new name ending in `.xisf` or `.fits`: it
is the linear mosaic every later try starts from. Then go on
with [Broadband Imaging](broadband-imaging.md) from its color step, or with
[Narrowband Imaging](narrowband-imaging.md) from its palette step. The masters of the filters share the canvas, so
they combine as those of one field do. Resize the picture only once the mosaic is whole.

[Spectrophotometric Color Calibration](../processes/color-calibration/spectrophotometric-cc.md) needs a plate
solution. The master carries the registration's when its reference panel was solved. Otherwise, solve it with
[Plate Solving](../processes/registration-alignment/plate-solving.md), or tick **Plate-Solve Final Master If Needed**
in the stacking settings before the run.

!!! see "You should see"
    A **Save File** window follows the write, and the view's tab loses the asterisk of unsaved work.

## The result

You have one linear master for each filter that holds the whole field, in the output folder, and the cropped,
corrected copy you saved. Under `history` the run keeps a folder with its report, `workflow_report.html`, and its log.
The registered lights, unless you let them be discarded, take the most room, and you may delete them once the master
is right.

## Next

- [Image Integration](../processes/stacking-integration/image-integration.md) describes every setting of the run.
- [Image Integration Reference](../processes/stacking-integration/image-integration-reference.md) says how a light is
  registered through its neighbors and placed by its sky solution.
- [Plate Solving](../processes/registration-alignment/plate-solving.md) sets up the solver a mosaic needs.
- [Image Registration](../processes/registration-alignment/image-registration.md) puts files you list on a mosaic
  canvas, without matching their levels.
- [Broadband Imaging](broadband-imaging.md) and [Narrowband Imaging](narrowband-imaging.md) take the master on to a
  finished picture.
