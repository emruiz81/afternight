---
kind: process
stage: calibrate
menu: Process > Color Spaces > Merge CFA
summary: >-
  Joins four grayscale planes of one size, the positions CFA0 to CFA3 of a two-by-two cell, into a new Bayer mosaic
  that states the pattern you choose: the inverse of Split CFA Channels.
data_in: linear
data_out: cfa
destructive: false
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    Always. The window chooses its images itself and opens without an active image. OK is enabled once each of the
    four plane boxes names an open image.
  inputs: >-
    Four one-channel images of one width and height, or four one-channel sequences of one size and frame count, in any
    pixel types.
  outputs: >-
    One new, unsaved mosaic named CFA MERGED, or a new temporary sequence, at twice the planes' width and height, in the
    highest of their pixel types and stating the chosen pattern. The planes are not changed. No file, preset or report
    is written.
  state: >-
    The four positional planes Split CFA Channels made of a mosaic, unchanged or changed alike, before the debayer.
derived: false
rewritten: true
---
# Merge CFA

This process joins four grayscale planes into one raw [CFA](../../reference/glossary.md#cfa) mosaic: each pixel of
the planes becomes one site of a two-by-two cell. It is the inverse of **Split CFA Channels** in
[Split CFA](split-cfa.md), and gives back the mosaic exactly when the four planes are that split's, unchanged, in
their order. The [Bayer pattern](../../reference/glossary.md#bayer-pattern) you choose says which color each site
holds. It moves no plane.

**OK** opens the new mosaic, `CFA MERGED`, in a view beside the first plane's, or as a new
[sequence](../../reference/glossary.md#sequence) for four sequences, and closes the window. The planes are not
changed.

```figure
owed: true
image: ../../assets/images/processes/calibration/merge-cfa.webp
alt: >-
  The Merge CFA window. Under the title, the process name and the line Merge four monochrome CFA quadrants into a
  single Bayer-pattern image. Below, CFA Pattern at RGGB, then the CFA0, CFA1, CFA2 and CFA3 boxes, each naming one
  plane of the light, from CFA0 - Red to CFA3 - Blue. OK and Close run along the bottom.
caption: >-
  Merge CFA with CFA Pattern at RGGB and the four planes Split CFA made of a raw NGC 253 light in CFA0 to CFA3.
pins:
  - {n: 2, x: 20.5, y: 32.7}
  - {n: 3, x: 20.5, y: 43.6}
  - {n: 5, x: 54.7, y: 91.6}
```

## Before you start

- Make the four planes with **Split CFA Channels** in Split CFA, and keep them open. An Ha, Green or OIII product, a
  channel made by a [debayer](../../reference/glossary.md#debayer), or a plane aligned or cropped on its own, is no
  position of the mosaic and joins into a wrong one.
- Change the four planes alike or not at all: the same size, the same crop and, for sequences, the same frames.
- Note the pattern the original mosaic stated. Split CFA named each plane after the color it found at its position.
- To calibrate or resample a mosaic, work on the mosaic itself: Image Calibration and CFA Integer Resample keep its
  filters without a split and a merge.

## Steps

1. **Open the window.** Choose **Process > Color Spaces > Merge CFA**, with or without an
   [active image](../../reference/glossary.md#active-image). The four plane boxes read `<--NO IMAGE-->` and **OK** is
   disabled.
2. **Choose the pattern.** Set **CFA Pattern** to the pattern of the original mosaic.
3. **Choose the planes by position.** Choose the plane named CFA0 in **CFA0**, and so on to **CFA3**: the top-left,
   top-right, bottom-left and bottom-right sites of each cell.
4. **Check the planes.** Rest the pointer on a plane in a box: its tooltip gives its size and
   [pixel type](../../reference/glossary.md#pixel-type). The four must have one width and height, and be all images
   or all sequences.
5. **Merge.** Choose **OK**. The window closes, and `CFA MERGED` opens beside the view of the **CFA0** plane, twice
   as wide and twice as high.

## Controls

The window has no preview, no **Target Image** and no **Reset**. What every process window shares is on
[Process Windows](../../interface/process-windows.md).

```controls
window: Merge CFA
controls:
  - name: CFA Pattern
    kind: combo
    default: RGGB
    options: [RGGB, BGGR, GBRG, GRBG]
    effect: States the pattern of the new mosaic at zero phase, which says the color of each position of the cell. It moves no plane. **CFA0** is the top-left site whatever its color.
    enabled: Always, also while a run works. A change made during a run does not reach it.
    tradeoff: A wrong pattern makes a later debayer read each color at the wrong sites, without a warning.
    scope: The new mosaic's description, on an image or on every frame of a sequence.
    persistence: Until the window closes. The next window opens at **RGGB**.
  - name: CFA0
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the plane placed at the top-left site of every cell. The new mosaic takes its metadata from this plane.
    range: "It lists `<--NO IMAGE-->` and every open view whose image has one channel: a grayscale image, a sequence of them, or a mosaic. It follows views as they open, close, change or are renamed. The tooltip of each item gives its source file, size, channels, pixel type and memory."
    enabled: Stays enabled while a run works, and a change does not reach the run.
    tradeoff: A wrong plane is joined into the mosaic without a warning. The same view may be chosen in several boxes.
    scope: "OK. On sequences, the new sequence takes the container of this plane's sequence."
    persistence: Until the window closes. A box whose view closes returns to `<--NO IMAGE-->`.
  - name: CFA1
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the plane placed at the top-right site of every cell.
    range: The same list as **CFA0**.
    tradeoff: A wrong plane is joined into the mosaic without a warning.
    scope: OK.
    persistence: Until the window closes.
  - name: CFA2
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the plane placed at the bottom-left site of every cell.
    range: The same list as **CFA0**.
    tradeoff: A wrong plane is joined into the mosaic without a warning.
    scope: OK.
    persistence: Until the window closes.
  - name: CFA3
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the plane placed at the bottom-right site of every cell.
    range: The same list as **CFA0**.
    tradeoff: A wrong plane is joined into the mosaic without a warning.
    scope: OK.
    persistence: Until the window closes.
  - name: OK
    kind: button
    default: ""
    effect: Checks the four planes, joins them away from the window and opens `CFA MERGED` in a new view beside the view of **CFA0**, or where new views open when that view has closed. The window then closes.
    range: The four must be all images or all sequences, of one width and height, and sequences of one frame count.
    enabled: Disabled when the window opens, and until none of the four plane boxes reads `<--NO IMAGE-->`. Disabled while a run works, with **Close**. **CFA Pattern** and the plane boxes stay enabled.
    tradeoff: The time grows with the mosaic's pixels, and with its frames for sequences. A merge of images holds a copy of each plane of a lower pixel type, the four planes together and the new mosaic, which is four times a plane. It shows no progress window.
    scope: The four planes, without a mask.
    persistence: "The new view is unsaved, and its history starts with `MergeCFA: created 'RGGB' pattern from image channels CFA0:'<a>' CFA1:'<b>' CFA2:'<c> CFA3:'<d>'`, with the pattern and the planes' names, `sequence channels` for sequences, and no closing quote after the third name. The planes and their histories are not changed."
    note: A run that fails leaves the window open with its controls enabled.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without merging.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works, with ++esc++ or its own close button, stops the run. Nothing opens, and a sequence being written is removed.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames joined, from 0 to 100 percent, under the title **Merge CFA** and the text `Executing Merge CFA`."
    enabled: Shown only while OK works on sequences.
    tradeoff: The new sequence is written to a temporary file as large as the four plane sequences together, until the run ends. Try a short sequence first.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run before the next frame. Nothing opens, the temporary sequence is removed, and the Merge CFA window stays open, with no message.
```

## How it works

1. **OK** checks that each box names an open image, that the four are all images or all sequences, that sequences
   hold one frame count, and that the four have one width and height.
2. A plane of a lower pixel type is converted to the highest of the four, at the same
   [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), to the nearest value.
   From the lowest: 8-bit signed, 8-bit unsigned, 16-bit signed, 16-bit unsigned, 32-bit signed, 32-bit unsigned,
   32-bit floating point and 64-bit floating point.
3. Each pixel of each plane becomes one site of a cell of the new mosaic. Nothing is interpolated, normalized,
   averaged, clipped or converted to another color.
4. The new mosaic keeps the **CFA0** plane's acquisition record, its
   [processing state](../../reference/glossary.md#processing-state) and its
   [header cards](../../reference/glossary.md#header-card), except the [WCS](../../reference/glossary.md#wcs) cards and
   the `PLTSOLVR` card of a [plate solution](../../reference/glossary.md#plate-solution). It states the chosen
   pattern at zero phase and no plate solution. What the other planes state is not used, and its
   [lineage](../../reference/glossary.md#lineage) records the join with the pattern.
5. Four sequences are joined frame by frame, every frame whatever their players have selected, into a new
   [temporary sequence](../../reference/glossary.md#temporary-sequence) in the container of the **CFA0** sequence,
   published only when every frame is written. Its view shows the frames as mosaics.

### Model

With \(P_0\) to \(P_3\) the planes of **CFA0** to **CFA3**, each \(W\) by \(H\) pixels, the mosaic \(M\) is \(2W\) by
\(2H\) pixels, and for \(0 \le x < W\) and \(0 \le y < H\):

\[ \begin{aligned} M(2x,\ 2y) &= P_0(x, y), & M(2x + 1,\ 2y) &= P_1(x, y), \\ M(2x,\ 2y + 1) &= P_2(x, y), & M(2x + 1,\ 2y + 1) &= P_3(x, y) \end{aligned} \tag{1} \]

**CFA Pattern** then says which color those positions hold, as Split CFA named them:

| Pattern | CFA0 | CFA1 | CFA2 | CFA3 |
| --- | --- | --- | --- | --- |
| RGGB | Red | Green1 | Green2 | Blue |
| BGGR | Blue | Green1 | Green2 | Red |
| GRBG | Green1 | Red | Blue | Green2 |
| GBRG | Green1 | Blue | Red | Green2 |

Planes that Split CFA made of a mosaic cut at an odd column or row are named by the shifted pattern it stated, as
Green1, Red, Blue and Green2 for GRBG. Choose that pattern here. Only these four two-by-two patterns can be stated: an
X-Trans or four-color mosaic cannot be joined.

### Boundaries

- **OK** refuses four planes that are not all images or all sequences, sequences of different frame counts, and
  planes of different sizes, each by name, before any work.
- A frame of another size, or a sequence that ends first, stops the run with what is wrong. A plane whose
  [validity rule](../../reference/glossary.md#validity-rule) lists its valid samples one by one is refused.
- A refusal or a failure is reported in a message box titled "Error running Merge CFA", which starts "An error
  occurred when executing the Merge CFA process:", and in the log, not in the window's message strip. Nothing opens.
- A merge of images cannot be stopped once it starts, except by closing the window. A run whose window closed, or
  whose view could not be made, opens nothing and leaves no sequence behind.

### Worked example

The planes are the four that Split CFA made of the raw NGC 253 light in its worked example: 16-bit, 2772 by 1842
pixels, named `<light> - CFA0 - Red` to `<light> - CFA3 - Blue`. With **CFA Pattern** at RGGB and each plane in its
box, **OK** opens `CFA MERGED`, a 16-bit RGGB mosaic of 5544 by 3684 pixels. By equation (1), pixel (1386, 921) of
the CFA1 plane, 6048, lands at (2773, 1842), where the light holds it, and every sample of the mosaic equals the
light's. The mosaic keeps the cards the Red plane kept, whose `XPIXSZ` of 2.4 µm fits its pixels again.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Planes from Split CFA Channels | The original mosaic's pattern, and each plane in the box its name gives | Debayer the result and check its colors. |
| Planes of a mosaic cut at an odd column or row | The pattern Split CFA stated for the cut | Check the planes' names against the table under Model. |
| Four plane sequences | Four sequences of one split | Check their frame counts, and try a short range first. |

## When it goes wrong

!!! warning "The pattern names colors and places no plane"
    **CFA0** is always the top-left site. Choosing BGGR does not move the blue plane there: choose the planes by
    position first, then the pattern those positions hold.

**OK** stays disabled
:   **Cause.** A plane box reads `<--NO IMAGE-->`.
:   **Fix.** Choose an open one-channel image in each of the four boxes.

"Missing images to assemble the CFA"
:   **Cause.** A chosen view closed, or held no image, when you pressed **OK**.
:   **Fix.** Choose the four planes again among the open views.

"All the selected images must be either sequences or images"
:   **Cause.** Some boxes name images and others sequences.
:   **Fix.** Choose four images, or four sequences.

"All the images must have the same size", "All the sequences must have the same size" or "the four planes must have one width and height"
:   **Cause.** The planes, or a later frame of a sequence, differ in width or height.
:   **Fix.** Use the planes of one split, or change all four alike.

"All the selected sequences must have the same number of frames"
:   **Cause.** The four sequences hold different numbers of frames.
:   **Fix.** Make the four from one sequence and one range.

"an image whose valid samples are stated in a mask cannot be merged"
:   **Cause.** A plane lists its valid samples one by one, which the join cannot carry.
:   **Fix.** Use planes that state every sample valid.

The debayered result has wrong colors
:   **Cause.** **CFA Pattern** is not the original's, or the planes were chosen by color, not by position.
:   **Fix.** Merge again with each plane in the box its name gives and the original mosaic's pattern.

The merged mosaic has no plate solution
:   **Cause.** A plane's solution does not describe the mosaic's grid, so none is kept.
:   **Fix.** Plate-solve the merged mosaic.

## Related

- [Split CFA](split-cfa.md), to make the four positional planes.
- [Debayer](debayer.md), to turn the merged mosaic into a color image and check its pattern.
- [CFA Integer Resample](cfa-integer-resample.md), to change a mosaic's size and keep its filters, with no split.
- [Plate Solving](../registration-alignment/plate-solving.md), to solve the merged mosaic.
- [Sequences](../../interface/sequences.md), for the player and to save the new temporary sequence.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
