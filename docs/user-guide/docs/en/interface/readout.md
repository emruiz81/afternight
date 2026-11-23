---
kind: interface
summary: >-
  The readout is the column at the right of a workspace that measures the active image: its histogram, its
  statistics, its plate solution and its facts, each in a card that folds, with buttons to the windows about the image.
---
# Readout

The [readout](../reference/glossary.md#readout) is the column at the right of the main window while a [workspace](workspaces.md) is shown. It follows the
[active image](../reference/glossary.md#active-image), docked or floating, and reads it in four cards: **Histogram**,
**Statistics**, **Astrometry** and **Image**. It measures in the background and never changes the image. Home, the
Integration place and a [task](../reference/glossary.md#task)'s page hide it; a task shows a compact readout of its preview in its own column instead.

```figure
owed: true
image: ../assets/images/interface/readout.webp
alt: >-
  The readout column. Its header reads READOUT and RGB, with a panel button at its right. The Histogram card, marked
  RGB, shows a plot of three channels near its left edge with Clipping Low 0.000% and Clipping High 0.019% over it, a
  row of zoom, fit, grid, information, scale and area buttons, and a box reading 8-bit (256). The Statistics card has
  the segments Normalized, 8-bit and 16-bit, Normalized chosen, and a table of Min, Max, Mean, Median, StdDev and MAD
  for R, G and B. The Astrometry card, marked Solved and SIP 4/4, gives the field centre 10:44:30.67 and -59:53:31.7,
  a scale of 1.095 arcseconds per pixel and a rotation of 107.75 degrees, the chips Grid, Annotations, Star labels,
  Detected stars and SIP map with Grid selected, and the buttons Plate Solve, Annotate... and Info. At the foot, the
  Image card begins with Dimensions 7246 by 5429.
caption: >-
  The readout of the linear Carina master: its histogram, its statistics per channel, its plate solution and its
  Image card.
pins:
  - {n: 1, x: 3.0, y: 2.5}
  - {n: 2, x: 91.7, y: 2.5}
  - {n: 3, x: 3.6, y: 7.2}
  - {n: 4, x: 10.0, y: 24.1}
  - {n: 5, x: 3.6, y: 35.5}
  - {n: 6, x: 22.2, y: 35.5}
  - {n: 7, x: 3.6, y: 63.1}
  - {n: 8, x: 3.6, y: 77.0}
  - {n: 9, x: 3.6, y: 85.6}
  - {n: 10, x: 3.6, y: 92.2}
```

## What you see

1. **The column's header.** READOUT, then the name of the image it reads, shortened in the middle.
2. **Collapse the readout column.** It folds the column to this one button, and opens it again.
3. **Histogram.** The histogram of the image's samples, with the shares clipped at either end over it. Its header
   says RGB or Grayscale.
4. **The plot's tools.** Zoom, grid, clipping, scale and area buttons, and the plot's resolution.
5. **Statistics.** Min, Max, Mean, Median, StdDev and [MAD](../reference/glossary.md#mad): six figures for a grayscale image, a column for each
   channel of a color one.
6. **Range.** The scale the figures are given in.
7. **Astrometry.** The [plate solution](../reference/glossary.md#plate-solution) of a solved image - the field centre,
   the scale and the rotation - or "No astrometric solution". Its header says "Solved", the solver and the
   [SIP](../reference/glossary.md#sip) orders, or "Not solved".
8. **The overlays.** One chip for each overlay of a solved image.
9. **Plate Solve, Annotate... and Info.** The windows about the solution.
10. **Image.** The image's dimensions, format, size in memory and channels, and buttons to its
    [FITS](../reference/glossary.md#fits) header, its [EXIF](../reference/glossary.md#exif) fields and its summary.

## What you can do

### Fold a card

Click a card's header, or press ++space++ or ++enter++ on it. A folded card is its header alone, and the others take
its room. The folded Image card shows its facts in its header, as `7246x5429 · 32 bit float · 150.07 MB`. When the
open cards need more height than the column has, they scroll.

In a window under 820 pixels high the Image card starts folded, and the Astrometry card too under 720 pixels, until
you fold or open a card yourself. Which cards are folded is kept from one session to the next.

### Collapse the column

Choose the button in the column's header, **View > Readout**, or the status line's **Show or hide the readout**. The
collapsed column is its button alone, whose tooltip then reads **Open the readout column**, and the image gets the
room. It stays collapsed at the next start until you open it.

### Read the figures

The readout measures the image itself, never the stretched picture a view shows in
[Autostretch](../reference/glossary.md#autostretch). The histogram counts
its samples at the plot's resolution, and shows the channels **View Channel** shows: **Red** shows only the red one.
The statistics are those of Image Statistics as it opens, with the clipped samples left out.

After a change - another image, an Apply, an Undo - the readout measures again a moment later, and a burst of changes
is measured once. Until the new figures land, the old ones stay, dimmed. While the application is busy with a run,
the readout waits and tries again by itself.

### Open a window about the image

The buttons in the cards' headers open the windows the menus open: **Open the Histogram window**, **Open Image
Statistics**, **FITS Header**, **EXIF Viewer** and **Summary**, and in the Astrometry card **Info**. Each acts on the
image the readout reads.

### Show the astrometry over a solved image

Click a chip of the Astrometry card - **Grid**, **Annotations**, **Star labels**, **Detected stars** or **SIP map** -
to show or hide that overlay. Turning one on turns on **Show Astrometry** on the floating toolbar, which shows every
overlay chosen. **Astrometry Settings** opens the overlays' menu with the choices for the annotations.
[Astrometry Overlays](astrometry-overlays.md) describes each overlay.

### Read a task's preview

A task's column starts with a compact readout of its preview: its histogram, tagged **Preview**, with the clipped
shares, and Min, Max, Mean, Median and StdDev, each with its change against the original. Its button opens the full
readout over the task. [Tasks](tasks.md) describes it.

## Controls

### The column

```controls
window: Readout
controls:
  - name: Collapse the readout column
    kind: toolbutton
    default: ""
    effect: Folds the column to this button, or opens it again, where its tooltip reads **Open the readout column**. **View > Readout** and the status line's **Show or hide the readout** do the same.
    persistence: Kept between sessions as `interface/readoutCollapsed`.
    note: The column is 360 pixels wide, or 320 in a window under 1200 pixels wide.
  - name: Image read
    kind: readout
    default: ""
    effect: The full name of the image the column reads, shortened in the middle. The readout follows the active image, docked in a workspace or floating; with none, its cards show `-`.
```

### Histogram

```controls
window: Histogram
controls:
  - name: Histogram
    kind: button
    default: ""
    effect: The card's header. A click folds the card, or opens it. It says **RGB** or **Grayscale** at its right.
    persistence: Which cards are folded is kept between sessions as `interface/readoutCards`.
  - name: Open the Histogram window
    kind: toolbutton
    default: ""
    effect: Opens Histogram Analysis for the image, a new window at each choice. ++ctrl+h++ does the same.
  - name: Plot
    kind: readout
    default: ""
    effect: The count of the image's samples, each channel drawn in its color, with **Clipping Low** and **Clipping High** over it while **Show clipping info** is on.
    range: It counts the image as it stands, at full precision, not the picture a view shows in Autostretch.
  - name: Zoom in
    kind: toolbutton
    default: ""
    effect: Enlarges the plot.
  - name: Zoom out
    kind: toolbutton
    default: ""
    effect: Reduces the plot.
  - name: Zoom reset
    kind: toolbutton
    default: ""
    effect: Shows the whole plot again.
  - name: Show grid
    kind: toolbutton
    default: "On"
    effect: Draws a grid over the plot, or takes it away.
  - name: Show clipping info
    kind: toolbutton
    default: "On"
    effect: Shows the shares of samples clipped at either end over the plot, or hides them.
  - name: Absolute scales
    kind: toolbutton
    default: "Off"
    effect: Gives each channel a scale of its own, so a channel with fewer counts is not flattened by the others.
  - name: Draw histogram areas
    kind: toolbutton
    default: "On"
    effect: Fills the area under each channel's curve, or draws its line alone.
  - name: Plot resolution
    kind: combo
    default: 8-bit
    options: [6-bit, 7-bit, 8-bit, 9-bit, 10-bit, 12-bit, 14-bit, 16-bit, 100 levels, 1000 levels, 10000 levels]
    effect: The number of levels the samples are counted in. The image is measured again at the new resolution.
    note: The box shows the count of levels beside each, as `8-bit (256)`. In a narrow column it moves under the tools.
```

### Statistics

```controls
window: Statistics
controls:
  - name: Statistics
    kind: button
    default: ""
    effect: The card's header. A click folds the card, or opens it.
  - name: Range
    kind: radio
    default: Normalized
    options: [Normalized, 8-bit, 16-bit]
    effect: Gives the figures as fractions of full scale with four decimals, or on a scale of 0 to 255 with two, or of 0 to 65535 with one.
    scope: The card only. The image is not measured again.
  - name: Open Image Statistics
    kind: toolbutton
    default: ""
    effect: Opens Image Statistics, with its 13 statistics per channel, a new window at each choice. ++ctrl+shift+t++ does the same.
  - name: Figures
    kind: readout
    default: ""
    effect: "**Min**, **Max**, **Mean**, **Median**, **StdDev** and **MAD** of the image: six figures in three columns for a grayscale image, and a row each with a column per channel, **R**, **G** and **B**, for a color one."
    range: The figures of Image Statistics as it opens, with the clipped samples left out. A figure that cannot be measured reads `-`.
```

### Astrometry

```controls
window: Astrometry
controls:
  - name: Astrometry
    kind: button
    default: ""
    effect: The card's header. A click folds the card, or opens it. It says **Not solved**, or **Solved** with the solver that wrote the solution and the SIP orders, as `SIP 4/4`.
  - name: Astrometry Settings
    kind: toolbutton
    default: ""
    effect: "Opens the overlays' menu: **Equatorial Grid** (++ctrl+1++), **Deep-Sky Annotations** (++ctrl+2++), **Star Labels** (++ctrl+3++), **SIP Distortion Map** (++ctrl+5++) and **Detected Stars** (++ctrl+4++), then **Level of Detail**, **Labels**, **Palette** and **Text Size** for the annotations."
    enabled: While the readout reads an image's own view. The shortcuts work while **Show Astrometry** is on.
    persistence: Every choice of the menu is kept between sessions for every view. The grid is on and the other four overlays off on a new profile.
  - name: Solution
    kind: readout
    default: ""
    effect: "Of a solved image: **Field centre RA** and **Field centre Dec**, the **Scale** in arcseconds per pixel with three decimals and the **Rotation** in degrees with two. Of another: \"No astrometric solution\"."
  - name: Grid
    kind: button
    default: "On"
    effect: Shows the equatorial grid over the image, or hides it.
    enabled: Shown for a solved image.
  - name: Annotations
    kind: button
    default: "Off"
    effect: Names the deep-sky objects of the catalogs over the image.
    enabled: As **Grid**.
  - name: Star labels
    kind: button
    default: "Off"
    effect: Names the bright stars over the image.
    enabled: As **Grid**.
  - name: Detected stars
    kind: button
    default: "Off"
    effect: Marks the stars the solver detected. They are not a selection you can edit.
    enabled: As **Grid**.
  - name: SIP map
    kind: button
    default: "Off"
    effect: Draws the distortion the SIP terms of the solution describe.
    enabled: As **Grid**.
    note: Each chip turns its overlay on or off as its menu entry and its shortcut do, and turning one on turns **Show Astrometry** on.
  - name: Plate Solve
    kind: button
    default: ""
    effect: Opens Plate Solve for this image. One window per view. ++ctrl+p++ does the same.
  - name: Annotate...
    kind: button
    default: ""
    effect: Opens Annotate Image, to export a picture of the solved image with its objects, stars and coordinates named. One window per view. ++ctrl+shift+l++ does the same.
    enabled: Shown for a solved image.
  - name: Info
    kind: button
    default: ""
    effect: Opens Astrometry Info with the solution's details. One window per view; choosing it again brings that window forward. ++ctrl+shift+p++ does the same.
    enabled: Shown for a solved image. **Image > Information > Astrometry Info** is enabled for any image, and says "No astrometric solution" for one not solved.
```

### Image

```controls
window: Image
controls:
  - name: Image
    kind: button
    default: ""
    effect: The card's header. A click folds the card, or opens it. Folded, it shows the image's facts, as `7246x5429 · 32 bit float · 150.07 MB`.
  - name: FITS Header
    kind: toolbutton
    default: ""
    effect: Opens the FITS header of the image, keyword, value and comment, a new window at each choice. ++ctrl+shift+h++ does the same.
    note: An image that carries no FITS card opens an empty window.
  - name: EXIF Viewer
    kind: toolbutton
    default: ""
    effect: Opens the camera fields the image's file stated, a new window at each choice. ++ctrl+shift+x++ does the same.
    note: An image whose file stated no EXIF field opens an empty window.
  - name: Summary
    kind: toolbutton
    default: ""
    effect: Opens the summary of the image, or of the sequence, a new window at each choice. ++ctrl+shift+m++ does the same.
  - name: Facts
    kind: readout
    default: ""
    effect: "**Dimensions**, as `7246 × 5429`, **Format**, the sample type as `32 bit floating point`, **Size in memory** in MB or GB, and **Channels**, RGB or Grayscale."
```

## When it goes wrong

The readout column is missing
:   **Cause.** Home, the Integration place or a task's page is shown, or the column is collapsed to its button.
:   **Fix.** Show a [workspace](../reference/glossary.md#workspace), and choose the button at the top right of the column, or **View > Readout**.

The figures are dimmed
:   **Cause.** The image changed and is being measured again, or a run keeps the application busy.
:   **Fix.** Wait: the new figures replace the dimmed ones as soon as they are measured.

The histogram shows one channel of a color image
:   **Cause.** **View Channel** on the floating toolbar shows that channel.
:   **Fix.** Choose **RGB** in **View Channel**.

The overlay chips are missing
:   **Cause.** The image is not plate-solved.
:   **Fix.** Choose **Plate Solve** in the Astrometry card. An image mirrored or turned has lost its solution: solve it
    again.

## Related

- [Image Viewer](image-viewer.md), for the floating toolbar and **Show Astrometry**.
- [Histogram Analysis](image-histogram.md), for the window **Open the Histogram window** opens.
- [Image Statistics](image-statistics.md), for the 13 statistics of each channel.
- [Astrometry Overlays](astrometry-overlays.md), for what each overlay draws.
- [Tasks](tasks.md), for the compact readout of a task's preview.
- [Workspaces](workspaces.md), for the place the readout stands beside.
