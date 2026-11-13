---
kind: tutorial
summary: Stack a first set of frames, from the files on disk to one linear master.
stages: [calibrate, register, stack]
menu: Process > Stacking > First Stack
needs: []
---
# First Stack

A tutorial with every part the theme dresses: the checklist, the branch cards, the steps beside their figure, the
result and the next cards.

## What you need

- [ ] The light frames of one target
- [ ] Matching dark frames, if you have them
- [ ] An empty folder for the output

## Your camera

- **A color camera.** Import the raw files as they are; they are demosaiced after calibration.

    You get one color master.

- **A mono camera.** Import one filter at a time.

    You get one master per filter.

## Steps

The numbers on the window match the steps.

```figure
image: ../assets/window.png
alt: The First Stack window with its frame list on the left and the run summary on the right.
caption: The window once the frames are in.
pins:
  - {n: 1, x: 15, y: 20}
  - {n: 2, x: 15, y: 50}
  - {n: 3, x: 22.5, y: 89}
```

### Add the frames

Drop the folder on the list.

!!! see "You should see"
    Every frame listed with its type.

??? wrong "If it goes wrong: a frame has the wrong type"
    Select it and choose its type from the list.

### Check the groups

One group per filter appears under the list.

!!! see "You should see"
    A single group for a color camera.

### Run

Press Run and wait for the summary.

??? wrong "If it goes wrong: Run stays disabled"
    The output folder is missing or not writable. Choose another.

### Open the master

This step is in no figure, so its number is drawn plain.

## The result

One linear master per group in the output folder, and a report beside it.

## Next

- [Levels](../processes/stretching/levels.md) makes the master visible.
- [Image Viewer](../interface/viewer.md) shows it.
- [Deep Sky](../workflows/deep-sky.md) goes on from here.
