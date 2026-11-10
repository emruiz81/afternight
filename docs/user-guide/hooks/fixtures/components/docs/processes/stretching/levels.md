---
kind: process
stage: stretch
menu: Process > Stretching > Levels
summary: Maps the image through a black point, a midpoint and a white point, the step from a linear master to a picture.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: An image view is active and no other process holds it.
  inputs: One image or sequence view.
  outputs: The same view, replaced, with a history step.
  state: A calibrated linear master after background and color work.
---
# Levels

Levels sets where black, the middle and white fall. Apply replaces the target with the stretched image and records a
history step.

```figure
image: ../../assets/window.png
alt: The Levels window, its controls on the left and the curve above the preview on the right.
caption: The controls on the left, the transfer curve above the preview. The numbers match the **steps** below.
pins:
  - {n: 1, x: 15, y: 20}
  - {n: 2, x: 66, y: 30}
  - {n: 4, x: 22.5, y: 89}
```

## Before you start

- Work on a copy while you learn the controls.
- Remove gradients first: a stretch makes them harder to remove.

## Steps

1. **Open the window.** Choose `Process > Stretching > Levels` with the image active.
2. **Read the curve.** The curve shows the transfer the three points make.
3. **Move the midpoint.** Lower it until the faint signal shows and the background stays dark.
4. **Apply.** The view is replaced and the history gains a step.

## Controls

Every control of the window, drawn as it appears when the window opens.

```controls
window: Levels
controls:
  - name: Black point
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    range: Never above the white point.
    effect: Higher maps more low samples to black and rescales the rest; lower keeps more shadow range.
    enabled: Shared by the selected channels.
    tradeoff: Raising it can erase faint signal for good.
    scope: Preview and Apply; selected channels; before the mask blend.
    persistence: For the window's lifetime; reset after Apply.
  - name: Midpoint
    kind: slider
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.01}
    effect: Lower brightens the middle of the range; `0.50` leaves it as it is.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue]
    effect: Chooses the channel the points move.
    note: A mono image shows a disabled Grayscale.
  - name: Curve
    kind: radio
    default: Smooth
    options: [Smooth, Linear]
    effect: Chooses how the curve is drawn between the points.
  - name: Clip highlights
    kind: check
    default: "On"
    effect: Shows clipped highlights in the preview.
  - name: Link channels
    kind: toolbutton
    default: Linked
    effect: Moves the three channels together.
  - name: Preset name
    kind: field
    default: ""
    effect: Names the preset that Save writes.
  - name: Apply
    kind: button
    default: Disabled until a point moves
    effect: Replaces the target with the stretched image.
  - name: Reset all
    kind: action
    default: Always enabled
    effect: Returns every control to its default.
```

```controls
window: Levels options
controls:
  - name: Channel
    kind: combo
    default: All
    options: [All, Selected]
    effect: A second window may reuse a name; its row gets its own anchor.
```

## How it works

1. The samples below the black point become 0, and those above the white point 1.
2. The rest are rescaled and bent around the midpoint.

### Worked example

A master whose background sits at 0.02 is stretched with the midpoint at 0.12.

```figure
images:
  - image: ../../assets/before.png
    alt: The linear master, almost black with a few stars.
    label: Before
  - image: ../../assets/after.png
    alt: The stretched master, the disc of the object visible.
    label: After
caption: The same field before and after the stretch.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A dark linear master | Midpoint 0.12 | The black point, up to the foot of the histogram |
| A bright background | Black point at the histogram's foot | The midpoint |

## When it goes wrong

!!! warning "A stretch cannot be taken back by another stretch"
    Undo it from the history instead.

!!! danger "Clipped shadows are gone"
    A black point above the background erases faint signal for good.

The preview is black
:   **Cause.** The black point is above most of the image.
:   **Fix.** Lower it to the foot of the histogram.

"The view is held by another process"
:   **Cause.** Another window is attached to the view.
:   **Fix.** Close that window, or apply it, then open Levels again.

## Related

- [Soft Stretch](soft-stretch.md), a gentler curve for the same step.
- [The catalog's Stretch stage](../index.md#stage-stretch).

## Credits

- A made-up process: nothing to credit.
