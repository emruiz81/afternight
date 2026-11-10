---
kind: process
stage: stretch
menu: Process > Color Spaces > Narrowband Palette Composer
summary: >-
  Maps Ha, OIII and SII masters, or one-shot-color images, into twelve fixed color palettes, shows them side by side,
  and opens the one you choose as a new color image.
data_in: linear
data_out: nonlinear
destructive: false
preview: real-time
sequences: false
masks: none
external: none
glance:
  enabled: >-
    Always, even with no image open. The window chooses its images itself and opens without an active image. Apply
    waits for OIII and Ha or SII, or a one-shot-color image that gives them.
  inputs: >-
    Up to three open narrowband still images, Ha, OIII and SII, and up to two one-shot-color still images, of any size
    and pixel type. A color image in a narrowband box gives its red channel.
  outputs: >-
    One new 32-bit floating-point RGB image beside the OIII image, recorded as nonlinear. The sources stay as they
    are. No file, snapshot, report or preset is made.
  state: >-
    Registered linear narrowband masters of one target, cropped alike, after gradient removal and the noise reduction
    that needs linear data.
derived: true
rewritten: true
---
# Narrowband Palette Composer

Narrowband Palette Composer makes a color image from narrowband
[masters](../../reference/glossary.md#master): Ha, OIII and SII, or one-shot-color
([OSC](../../reference/glossary.md#osc)) images that stand in for them. It maps the three lines into twelve fixed
palettes at once, such as the Hubble palette [SHO](../../reference/glossary.md#sho), and shows them side by side for
you to choose. With **Input autostretch** on, it [stretches](../../reference/glossary.md#stretch) each master first,
so linear masters give a visible picture.

**Apply** opens the chosen palette as a new 32-bit floating-point color image beside the OIII master, recorded as
nonlinear. The masters stay as they are.

```figure
image: ../../assets/images/processes/color-composition/narrowband-palette-composer.webp
alt: >-
  The Narrowband Palette Composer window, its title naming the OIII master. On the left, the Sources group with the
  Ha, OIII and SII boxes naming the three Eagle Nebula masters and OSC1 and OSC2 empty, the Preparation group with
  Input autostretch and Match sizes to reference ticked, Mode at Palette Grid, the Ha, OIII and SII sliders at 100%,
  and a Status line. On the right, the Palette Grid shows tiles of the nebula in different colors, the SHO tile first
  and framed, green clouds over a gray background. Reset, Close and Apply run along the bottom.
caption: >-
  Narrowband Palette Composer with the Ha, OIII and SII masters of the Eagle Nebula and every other control at its
  default: the grid shows each palette as Apply would write it, SHO selected.
pins:
  - {n: 3, x: 2.0, y: 17.7}
  - {n: 4, x: 2.0, y: 41.8}
  - {n: 5, x: 45.7, y: 13.2}
  - {n: 6, x: 2.0, y: 54.7}
  - {n: 7, x: 2.0, y: 64.7}
  - {n: 8, x: 90.5, y: 96.8}
```

## Before you start

- Register the masters to one reference and crop them alike: each pixel is combined with the pixels at the same place.
- Finish the linear work on each master first, such as gradient removal and noise reduction. The result is recorded as
  nonlinear, and the color calibrations refuse it.
- The boxes say which line an image is. The window reads no filter card, passband, exposure or world coordinates, and
  its twelve palettes are fixed: there is no mapping of your own.
- Leave the masters linear with **Input autostretch** on, or stretch them yourself and turn it off.
- Every palette is divided by its brightest value, so the result never keeps the scale of the masters.
- Choose only the images you mean to use. Every chosen image takes part in the palettes and in the result's header.

## Steps

1. **Open the masters.** Open the registered Ha, OIII and SII masters, each a grayscale image. A one-shot-color
   image can stand in for missing lines.
2. **Open the window.** Choose **Process > Color Spaces > Narrowband Palette Composer**. The five boxes read
   `<--NO IMAGE-->`, and **Apply** is dimmed.
3. **Assign the lines.** Choose each master in its box under **Sources**. Once OIII and Ha or SII are chosen, the
   grid fills with twelve tiles.
4. **Keep the preparation.** Leave **Input autostretch** on for linear masters, and **Match sizes to reference** on.
   The line under **Status** reports anything the preparation changed.
5. **Choose the palette.** Click its tile. The tile is framed, and **Status** names the palette **Apply** creates.
6. **Compare in detail.** Use the wheel over the grid to zoom every tile alike, and drag to pan, to compare clipped
   highlights, noise and star alignment. Set **Mode** to **Single Preview** to see one palette large.
7. **Balance the lines, if needed.** Move **Ha**, **OIII** or **SII** under **Adjust Selected Palette**, one at a time.
   The grid is built again after each change.
8. **Apply.** Choose **Apply**. The new color image opens as a tab beside the OIII master, and its
   [history](../../reference/glossary.md#history) names the palette.

## Controls

The window has no **Target Image** or **Channel** box and no preview pane or histogram: its grid takes their place. It
reads no [mask](../../reference/glossary.md#mask). What **Close** does is said in
[Process Windows](../../interface/process-windows.md).

```controls
window: Narrowband Palette Composer
controls:
  - name: Ha
    object: haSourceSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open grayscale or color still image]
    effect: Chooses the hydrogen-alpha image. A color image gives its red channel, with a warning. Left empty, SII stands in for Ha.
    range: After `<--NO IMAGE-->` the list holds every open still image, grayscale or color, of any size and pixel type, an image made by an earlier Apply among them. One view may fill several boxes. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: The list follows the views as they open, close and are renamed. A box whose view closes returns to `<--NO IMAGE-->`.
    tradeoff: An image in the wrong box, or one not registered to the others, gives false colors. Each chosen image costs a full-size working copy at every build.
    scope: The grid and Apply. Its width and height are the size every line is matched to.
    persistence: Until the window closes or its view goes. **Reset** and **Apply** keep it.
    note: With **OSC1** also chosen, the two are averaged half and half.
  - name: OIII
    object: oiiiSourceSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open grayscale or color still image]
    effect: Chooses the doubly ionized oxygen image, which every palette needs. A color image gives its red channel, with a warning. Left empty, an OSC image must give it.
    range: The same list as **Ha**.
    tradeoff: OIII is in every palette but HSS, so a weak or noisy OIII shows its noise in the teal and blue of most of them.
    scope: "The grid and Apply. Its view is the window's target, which the title names. The result opens beside it and takes its name. When the target's view closes, the next box that names an image gives the target. With none left, the window says \"The target image '<name>' has been closed. The process will be closed as well.\" and closes."
    persistence: Until the window closes or its view goes. **Reset** and **Apply** keep it.
    note: With **OSC1** or **OSC2** also chosen, the OSC estimate is averaged with it half and half.
  - name: SII
    object: siiSourceSelector
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open grayscale or color still image]
    effect: Chooses the ionized sulfur image. A color image gives its red channel, with a warning. Left empty, Ha stands in for SII, so a palette that needs SII repeats Ha there.
    range: The same list as **Ha**.
    tradeoff: The stand-in is the Ha signal again, not a guess at the sulfur.
    scope: The grid and Apply.
    persistence: Until the window closes or its view goes. **Reset** and **Apply** keep it.
    note: With **OSC2** also chosen, the two are averaged half and half.
  - name: OSC1
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open grayscale or color still image]
    effect: Chooses a one-shot-color image whose red gives Ha and whose mean of green and blue gives OIII.
    range: A grayscale image gives the same plane for both, with a warning.
    enabled: Optional. Alone, it gives both lines Apply needs.
    tradeoff: Where a box already holds the line, the two are averaged half and half, so the color image's stars and gradients reach the palette. The weights ignore each image's exposure, and the header adds up every image's exposure whatever its weight. Each estimate costs working planes of the image's size.
    scope: The grid and Apply. With **Input autostretch** on, each of its channels is stretched on its own before they are split.
    persistence: Until the window closes or its view goes. **Reset** and **Apply** keep it.
  - name: OSC2
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->", Every open grayscale or color still image]
    effect: Chooses a one-shot-color image whose red gives SII and whose mean of green and blue gives OIII.
    range: A grayscale image gives the same plane for both, with a warning.
    enabled: Optional. Alone, it gives both lines Apply needs.
    tradeoff: It is averaged in after **OSC1**. With an OIII image and both OSC images, OIII is a quarter of the OIII image, a quarter of OSC1 and half of OSC2. Use one OSC image, or combine them first with the weights you want.
    scope: The grid and Apply, prepared as **OSC1** is.
    persistence: Until the window closes or its view goes. **Reset** and **Apply** keep it.
    note: An OSC image never names the result. Rename the result after Apply if its name should say where it came from.
  - name: Input autostretch
    kind: check
    default: "On"
    effect: Stretches every chosen image before the palettes read it, each channel by its own statistics, by equation (2), so the background of each line lands near 0.25. Off uses the images as they are.
    range: The background of 0.25 and the black point 2.8 deviations below the median are fixed. The statistics are the median and median absolute deviation of every sample.
    tradeoff: It makes linear masters visible, and its stretch is part of what Apply writes. On masters already stretched it stretches them again. Each image costs its statistics and a full pass.
    scope: The grid and Apply, before a color image's channels are split and before the sizes are matched.
    persistence: Until the window closes. **Reset** and a finished Apply turn it on. The history says `stretched inputs` while it is on.
    note: Off, the result is still recorded as nonlinear, and its brightest sample is still 1. Bring the masters to one background yourself first, with a stretch of each or with Linear Match.
  - name: Match sizes to reference
    kind: check
    default: "On"
    effect: Resizes every line to the width and height of Ha, else OIII, else SII, when their sizes differ. Off refuses lines of different sizes.
    range: Width and height are matched each on its own, by Lanczos interpolation, so the aspect ratio can change. Nothing is aligned, turned or matched in sharpness.
    enabled: It also decides whether an OSC estimate of another size is resized to the image it is averaged with. Off, that is refused too.
    tradeoff: Resampling changes the sampling and the star profiles. Off is safer while you check that the masters share one grid.
    scope: The grid and Apply. The line under **Status** says "Resized mismatched channels to Ha reference dimensions." when it acted, with the reference's name.
    persistence: Until the window closes. **Reset** and a finished Apply turn it on. The history says `no resize` while it is off.
  - name: Mode
    kind: combo
    default: Palette Grid
    options: [Palette Grid, Single Preview]
    effect: Shows the twelve palettes as a grid, or one large over a gallery of all twelve. The title over them follows.
    tradeoff: Nothing is computed again. Both show the same pictures, and the palette chosen in one is chosen in the other.
    scope: The window only. It does not change the palette Apply creates.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **Palette Grid**.
  - name: Ha
    object: haIntensitySpin
    kind: spinbox
    default: "100"
    widget: {min: 0, max: 200, step: 1}
    effect: Multiplies the Ha line, in percent, before the palette maps it, by equation (3). Higher strengthens the colors Ha drives, lower weakens them, and 0 removes Ha from every palette.
    range: The slider beside it moves 10 a page and changes the value when you let it go. The box changes it at each step, and each step asks for a new grid, so on a large image the slider costs fewer builds.
    enabled: With no Ha chosen, it scales the SII that stands in for Ha.
    tradeoff: Above 100, bright Ha is clipped to 1 before the frame is divided by its maximum, which flattens highlights. Lower the strongest line instead of raising the others. With **Input autostretch** on, any value but 100 also tints the background, whose lines all sit near 0.25.
    scope: All twelve palettes and Apply. Each change builds the grid again.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 100.
    note: The division by the maximum can hide a change of scale. Lowering all three intensities alike changes no palette but Foraxx, whose blend follows the values themselves, so compare the channels and their clipping, one line at a time.
  - name: OIII
    object: oiiiIntensitySpin
    kind: spinbox
    default: "100"
    widget: {min: 0, max: 200, step: 1}
    effect: Multiplies the OIII line, in percent, by equation (3). Higher pushes teal and blue, lower weakens them, and 0 empties every channel that takes OIII alone.
    range: Its slider works as the **Ha** slider does.
    tradeoff: Above 100, OIII drives teal and blue into clipping.
    scope: All twelve palettes and Apply. Each change builds the grid again.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 100.
  - name: SII
    object: siiIntensitySpin
    kind: spinbox
    default: "100"
    widget: {min: 0, max: 200, step: 1}
    effect: Multiplies the SII line, in percent, by equation (3). Higher strengthens the colors SII drives, lower weakens them, and 0 removes SII from every palette.
    range: Its slider works as the **Ha** slider does.
    enabled: With no SII chosen, it scales the Ha that stands in for SII.
    tradeoff: Above 100, bright SII is clipped to 1 before the division.
    scope: All twelve palettes and Apply. Each change builds the grid again.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 100.
  - name: Status
    kind: readout
    default: Select OIII plus Ha or SII to build palettes.
    effect: Says what the window waits for or does, such as "Updating palette grid..." while a grid is built and "Palette grid is ready. Apply will create a new SHO RGB image view." with the selected palette's name.
    range: It also reads "Select OIII plus Ha or SII, or use OSC sources that synthesize them.", "Ready to update SHO. The palette grid refreshes automatically.", "Updating the synchronized palette grid..." while a change waits for the build on its way, and "Palette grid update failed. Check the status details below."
    tradeoff: It gives no percentage.
  - name: Status details
    kind: readout
    default: ""
    effect: Lists what the preparation changed, one line each, such as "Ha source is RGB; using the red channel to match SetiAstro SASPro.", "OSC1 source is mono; broadcasting it to RGB before OSC synthesis." or "Resized synthetic OIII contribution before blending it with the selected channel." After a failed build it gives the reason.
    enabled: Empty while a build is on its way and while the boxes give no palette.
    scope: The window only. The result keeps neither these notes nor the weights of an OSC estimate.
  - name: Reset
    kind: button
    default: ""
    effect: Turns **Input autostretch** and **Match sizes to reference** on, returns **Mode** to **Palette Grid**, the palette to SHO and the three intensities to 100.
    range: It keeps the five boxes and the zoom of the pictures. A value it changes builds the grid again.
    note: Its tooltip reads "Restore palette, stretch, resize, and intensity defaults."
  - name: Apply
    kind: button
    default: ""
    effect: Prepares the images again as their views show them and opens the selected palette as a new 32-bit floating-point RGB image, as a tab beside the target's view. The sources stay as they are.
    range: "The target is the first of **OIII**, **Ha**, **SII**, **OSC1** and **OSC2** that names an image. The name is the OIII image's, else the Ha's, else the SII's, then the palette's, as `stacked_3008x3004_Oiii_M_16 - SHO Narrowband Palette`. With only OSC images chosen it is the palette's alone, as `SHO Narrowband Palette`."
    enabled: While OIII and Ha or SII are chosen, or an OSC image gives them, whether or not the grid has landed. While it works, every control, the pictures, **Reset** and **Close** are disabled.
    tradeoff: It prepares the images and maps the palette again at full size, apart from the grid. It shows no progress window and cannot be cancelled. A failure shows "Error executing the process" with the reason.
    scope: Still images only. No mask is read, and nothing is written to disk.
    persistence: "The new image's history starts with `Created SHO Narrowband Palette Composer image (Ha 1.00, OIII 1.00, SII 1.00, stretched inputs)`: the palette, the three intensities as factors, and `, no resize` when **Match sizes to reference** is off. Image History shows the target's steps as its inherited lineage. The result is recorded as nonlinear with the operation `narrowband-palette-composer`. The five boxes keep their images, and the other controls return to their defaults."
    note: "The header is built from the chosen images, in the order Ha, OIII, SII, OSC1 and OSC2. The exposure is the light they hold together: the sum, over the distinct acquisitions, of each one's `TOTEXP`, else its `LIVETIME`, else its exposure times its `STACKCNT`, else `NCOMBINE`, else one. Images that state the same exposure start are one acquisition, such as an image, its starless image and a channel taken from it, and count once with the largest integration and frame count any of them states. An image that states no start counts on its own. One image keeps its own exposure. The images' `EXPOSURE` and `EXPTIME` cards are dropped, and the result states its own `EXPTIME` and `TOTEXP`, `LIVETIME` when an image states one, and `STACKCNT` and `NCOMBINE` summed the same way, each when an image states it. The observation start is the earliest, with its `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`, `MJD-BEG` and `TIME-OBS` together. The object, instrument, telescope, observer, filter, sensor temperature, gain and binning are kept when every image states the same, and so is every other card. No processing-state or file identity card is kept. The first image's pointing cards `RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA` and `TELDEC` are kept. Its WCS cards and plate solution are kept only when the result has its width and height: an image resized to the reference, or a reference made from an OSC image of another size, leaves none."
```

```controls
window: The palette grid
controls:
  - name: Palette tiles
    kind: radio
    default: SHO
    options: [SHO, HOO, HSO, HOS, OSS, OHH, OSH, OHS, HSS, Realistic1, Realistic2, Foraxx]
    effect: Each tile shows one palette of the chosen images as Apply would write it, under its name. A click selects the palette Apply creates and frames its tile.
    range: Up to four tiles a row, as the pane's width allows. The table under the model gives each palette's mapping, and a tile's tooltip describes it.
    enabled: The tiles appear when a grid lands. Until then the grid reads "Select sources to update the palette grid".
    tradeoff: Each tile is an 8-bit picture of a full-size result, and a build holds all twelve 32-bit results at once, about 1.3 GB for a 3008 × 3004 image. Every change of an image, of **Preparation** or of an intensity prepares the images and maps the twelve palettes again. For trials on large masters, compose crops of them, cropped alike, and close the images you do not use.
    scope: The window only. Choosing another tile computes nothing again.
    persistence: The choice is the single preview's too. **Reset** and a finished Apply return it to SHO.
  - name: Zoom
    kind: action
    default: ""
    effect: The wheel over the grid zooms every tile alike, on the same part of the image, 1.15 times a notch, from the whole image up to 32 times.
    scope: The pictures only. Nothing is computed again.
    persistence: A new grid, or a double-click, shows the whole image again.
  - name: Pan
    kind: action
    default: ""
    effect: Dragging a tile with the left button moves every tile to the same place while they are zoomed in. The press also selects that tile.
  - name: Fit
    kind: action
    default: ""
    effect: A double-click shows the whole image in every tile again.
  - name: Busy indicator
    kind: readout
    default: ""
    effect: Turns over the grid while a grid is built. Its tooltip reads "The synchronized palette grid is updating."
    range: It gives no percentage, and a build cannot be cancelled. Closing the window stops it.
    note: The window stays responsive. A change made during a build is built next, with the latest values, when this one lands, and only the latest grid is shown.
```

```controls
window: The single preview
controls:
  - name: Selected palette
    kind: readout
    default: SHO
    effect: Shows the selected palette large, fitted to the pane, under its name, with its description as its tooltip.
    tradeoff: Fitting a large image to the pane shrinks it on the screen. The result Apply writes is not changed.
    note: The wheel over it zooms 1.15 times a notch, up to 32 times, dragging pans while it is zoomed in, and a double-click fits it again. These gestures do not reach the gallery, and a new grid fits it again.
  - name: Gallery
    kind: radio
    default: SHO
    options: [SHO, HOO, HSO, HOS, OSS, OHH, OSH, OHS, HSS, Realistic1, Realistic2, Foraxx]
    effect: Shows the twelve palettes small under the large one, each with its description as its tooltip. A click selects the palette shown large and created by Apply.
    scope: The same pictures as the grid. Nothing is computed again.
    persistence: The choice is the grid's too.
```

## How it works

1. Each chosen image is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) into
   32-bit floating point and, with **Input autostretch** on, stretched by equation (2), each channel on its own.
2. A color image in a narrowband box gives its red channel. **OSC1** gives Ha from its red and OIII from the mean of
   its green and blue, and **OSC2** gives SII and OIII the same way. A line that already has an image is averaged with
   the estimate half and half, **OSC1** first.
3. A line of another size is resized to the width and height of Ha, else OIII, else SII. With **Match sizes to
   reference** off, the build is refused instead.
4. Each palette maps the three lines, times their intensities, to red, green and blue by equation (3) and the table
   below it, holds each channel between 0 and 1, and divides the frame by its brightest sample, by equation (4).
5. The grid shows the twelve results as 8-bit pictures. **Apply** composes the selected one again, opens it, and
   records it as nonlinear with the operation `narrowband-palette-composer`, with **Input autostretch** on or off.

### Model

A [midtones](../../reference/glossary.md#midtones) transfer with the balance \(m\) sends a value \(t\) between 0 and 1
to

\[ T_m(t) = \frac{(m - 1)\,t}{(2m - 1)\,t - m} \tag{1} \]

With \(\tilde v\) the median of a channel and \(\sigma\) 1.4826 times its
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.001 when that is zero,
**Input autostretch** takes the [black point](../../reference/glossary.md#black-point) \(c\) and the balance \(m\), and
sends a sample \(x\) to \(y\):

\[ c = \max(0,\ \tilde v - 2.8\,\sigma), \qquad m = T_{0.25}(\tilde v - c), \qquad y = T_m\!\left(\frac{x - c}{1 - c}\right) \tag{2} \]

A sample at or below \(c\) becomes 0, and the median lands near 0.25. Then each sample \(H\), \(O\) or \(S\) of the
three lines is held between 0 and 1, a value that is not a finite number becoming 0, and with \(k_H\), \(k_O\) and
\(k_S\) the three intensities over 100,

\[ h = k_H\,H, \qquad o = k_O\,O, \qquad s = k_S\,S \tag{3} \]

A missing Ha takes the SII samples, \(h = k_H\,S\), and a missing SII takes the Ha samples, \(s = k_S\,H\). Each
palette maps them as follows:

| Palette | Red | Green | Blue |
| --- | --- | --- | --- |
| SHO | \(s\) | \(h\) | \(o\) |
| [HOO](../../reference/glossary.md#hoo) | \(h\) | \(o\) | \(o\) |
| HSO | \(h\) | \(s\) | \(o\) |
| HOS | \(h\) | \(o\) | \(s\) |
| OSS | \(o\) | \(s\) | \(s\) |
| OHH | \(o\) | \(h\) | \(h\) |
| OSH | \(o\) | \(s\) | \(h\) |
| OHS | \(o\) | \(h\) | \(s\) |
| HSS | \(h\) | \(s\) | \(s\) |
| Realistic1 | \(0.5\,h + 0.5\,s\) | \(0.3\,h + 0.7\,o\) | \(0.1\,h + 0.9\,o\) |
| Realistic2 | \(0.7\,h + 0.3\,s\) | \(0.3\,s + 0.7\,o\) | \(o\) |
| Foraxx | \(a\,s + (1 - a)\,h\) | \(b\,h + (1 - b)\,o\) | \(o\) |

In Foraxx, \(p = h\,o\) and \(b = p^{\,1-p}\), and with \(q\) the value of \(o\) held between \(10^{-6}\) and 1,
\(a = q^{\,1-q}\). With Ha chosen and no SII, the red of Foraxx is \(h\). An OSC estimate counts as the line it gives. Each
channel \(R\) is then held between 0 and 1, a value that is not finite becoming 0, and with \(M\) the largest value of
the three channels over the frame, it becomes

\[ R' = R / M \tag{4} \]

when \(M > 0\). A result that is not black thus has a sample at 1: with \(M = 0.80\), a pixel of (0.80, 0.20, 0.40)
becomes (1.00, 0.25, 0.50).

### Boundaries

- A channel is clipped at 1 before \(M\) is taken, so raising an intensity stops changing what is already at 1. With
  every intensity at 0, \(M\) is 0 and the result stays black.
- The boxes list no [sequence](../../reference/glossary.md#sequence). Take a frame out of one to compose it.
- An edit of an open image builds the grid again. **Apply** reads the images as their views show them when you press
  it.

### Worked example

The Eagle Nebula masters of a QHY533M camera, Ha, OIII and SII, are 3008 × 3004 pixels in 32-bit floating point,
stacked from 25, 20 and 19 frames of 300 s and registered to one another. They are linear: their medians are
0.0378, 0.0386 and 0.0364, and their median absolute deviations 0.00056, 0.00104 and 0.00131. Choose them in
**Ha**, **OIII** and **SII**, and leave every other control at its default: SHO, with **Input autostretch** on.

For Ha, equation (2) puts the black point at 0.0355 and the balance at 0.0069, which lifts the median to 0.257. OIII
and SII land at 0.257 and 0.256, so the SHO background is a neutral gray. The brightest cloud, beside the pillars near
the middle, averages 0.67 in the stretched Ha, 0.36 in OIII and 0.27 in SII: SHO makes it (0.27, 0.67, 0.36), a strong
green, because the cloud is brightest in Ha. Stars reach 1 in OIII and SII, so \(M = 1\) and equation (4) changes
nothing. With **Ha** at 50 %, green is halved, and the background turns magenta at (0.256, 0.128, 0.257).

**Apply** opens `stacked_3008x3004_Oiii_M_16 - SHO Narrowband Palette` beside the OIII master. Its header states
`EXPTIME` and `TOTEXP` 19200, the sum of 7500, 6000 and 5700 seconds, `STACKCNT` 64, the Ha master's `DATE-OBS`,
which is the earliest, and no `FILTER` card.

```figure
images:
  - image: ../../assets/images/processes/color-composition/narrowband-palette-composer-before.webp
    alt: The whole Ha master in the view's Autostretch, gray clouds of nebula with the dark pillars at the middle and a dark lane on the right, over many stars.
    label: Before
  - image: ../../assets/images/processes/color-composition/narrowband-palette-composer-after.webp
    alt: The whole SHO image in the Linear display mode, the same clouds in green over a gray background that turns violet where Ha is faint, the stars white.
    label: After
caption: >-
  The Ha master, and the SHO image Apply makes from the three masters at the defaults. The master is shown in the
  view's Autostretch, which stretches it the way Input autostretch does, so it shows the green channel of the result. The
  result is shown in the Linear display mode, since it is already stretched.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Linear Ha, OIII and SII masters | The defaults, SHO | Another tile, then [SCNR](../color-calibration/scnr.md) on the result if its green is too strong. |
| Masters you stretched yourself | **Input autostretch** off | Stretch them again to one background if the colors are unbalanced. |
| Ha and OIII only | HOO | **OIII** for the strength of the teal. |
| A one-shot-color image taken through a dual-band filter | That image alone in **OSC1**, and HOO | A mono master in **Ha** or **OIII** only to average it with the estimate. |

## When it goes wrong

!!! warning "Match sizes to reference is not registration"
    It matches width and height only. It cannot repair a shift, a rotation, distortion, a different crop or a
    different sharpness, so masters that were not registered give doubled stars and colored edges at any size.
    Register and crop them first.

Apply stays dimmed
:   **Cause.** No box gives OIII, or none gives Ha or SII. An OSC image gives both.
:   **Fix.** Choose OIII and Ha or SII, or a one-shot-color image in **OSC1** or **OSC2**.

"Palette grid update failed. Check the status details below." with "Narrowband Palette Composer channel dimensions do not match and resize-to-reference is disabled."
:   **Cause.** The lines differ in size and **Match sizes to reference** is off.
:   **Fix.** Register and crop the masters to one size, or tick **Match sizes to reference** if resizing is enough.

The image you want is not in the lists
:   **Cause.** It is a sequence, or it is not open.
:   **Fix.** Open it as a still image, or take a frame out of the sequence.

Doubled stars or colored fringes in every palette
:   **Cause.** The masters share a size but not a grid.
:   **Fix.** Register them to one reference with a common crop, and choose them again.

The palettes are nearly black, or pale and flat
:   **Cause.** **Input autostretch** is off for linear masters, or on for masters already stretched.
:   **Fix.** Turn it on for linear masters and off for stretched ones.

The SHO image is mostly green
:   **Cause.** The nebula is brightest in Ha, and SHO puts Ha in green.
:   **Fix.** Apply as it is and take the green out with SCNR, or choose another palette.

The background turns a color after you change an intensity
:   **Cause.** With **Input autostretch** on, every line's background sits near 0.25, and an intensity scales it too.
:   **Fix.** Return the intensities to 100, and balance the colors of the result after **Apply** instead.

"Ha source is RGB; using the red channel to match SetiAstro SASPro."
:   **Cause.** A color image is in a narrowband box, and only its red channel is used.
:   **Fix.** Choose a grayscale master there, or put a one-shot-color image in **OSC1** or **OSC2**.

## Related

- [Narrowband Imaging](../../workflows/narrowband-imaging.md), the workflow from the narrowband frames to this window
  and beyond.
- [Image Registration](../registration-alignment/image-registration.md), to put the masters on one grid first.
- [Histogram Transformation](../stretching/histogram-transformation.md), to stretch the masters yourself with
  **Input autostretch** off.
- [SCNR](../color-calibration/scnr.md), to take the green out of an SHO image.
- [Channel Composition](channel-composition.md), [NB Color Mapper](nb-color-mapper.md) and
  [NBRGB Combination](nbrgb-combination.md), the other ways to make color from narrowband images.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- The palettes, their names, order and equations, the stand-in for a missing Ha or SII, the clamp, the normalization
  and the one-shot-color estimates are derived from the narrowband palette process of
  [Seti Astro Suite Pro](https://github.com/setiastro/setiastrosuitepro), by Franklin Marek, under GPL-3.0, and this
  process is distributed under that licence. No Python, PyQt or Seti Astro Suite Pro asset is included.
- The licence page of **About > About AfterNight** carries the same credit.
