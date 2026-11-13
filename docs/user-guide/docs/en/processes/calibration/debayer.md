---
kind: process
stage: calibrate
menu: Process > Color Spaces > Debayer
summary: >-
  Turns the one-channel color-filter mosaic of a one-shot-color camera into an RGB image, its three channels or both,
  opened as new views, by one of six demosaicing methods.
data_in: cfa
data_out: linear
destructive: false
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Debayer window is open on it yet, and no window that needs the image to
    itself is open on it.
  inputs: >-
    One one-channel mosaic, a still image or a sequence, that states its Bayer or X-Trans filter array, or a one-channel
    image you name a Bayer pattern for. The mask on the view is not used.
  outputs: >-
    New views beside the source: the RGB image, its red, green and blue channels, or both, in the mosaic's pixel type,
    each with one history step. A sequence gives new temporary sequences. The source is not changed.
  state: >-
    A calibrated linear mosaic, or a raw one, before registration, stacking and color calibration.
derived: true
---
# Debayer

[Debayer](../../reference/glossary.md#debayer) estimates, at every pixel of a [CFA](../../reference/glossary.md#cfa)
mosaic, the two colors its filter did not record, and keeps the one it did. The mosaic is the one-channel image a color
camera records. You choose where the [Bayer pattern](../../reference/glossary.md#bayer-pattern) comes from, the method,
and whether you get the color image, its three channels, or both.

**OK** demosaics the image, or every frame of a [sequence](../../reference/glossary.md#sequence), opens the result as
new views beside the source and closes the window. The source is not changed, and the results keep its size and
[pixel type](../../reference/glossary.md#pixel-type).

```figure
owed: true
image: ../../assets/images/processes/calibration/debayer.webp
alt: >-
  The Debayer window. Under its title and a line that says what it does, four lists: Bayer/mosaic pattern at From
  metadata, Demosaicing method at RCD, Output mode at Combined RGB Color, and Target Image naming the raw light. OK and
  Close run along the bottom.
caption: >-
  Debayer on a raw light of NGC 253 with its defaults: the pattern the file states, RCD and one RGB image.
pins:
  - {n: 1, x: 2.5, y: 70.0}
  - {n: 2, x: 2.5, y: 35.0}
  - {n: 3, x: 2.5, y: 47.0}
  - {n: 4, x: 2.5, y: 58.5}
  - {n: 5, x: 64.0, y: 91.0}
```

## Before you start

- Calibrate the mosaic first, while each pixel still holds one sensor sample, so that the bias, dark and flat
  [masters](../../reference/glossary.md#master) still match its sites. Then repair the
  [hot pixels](../../reference/glossary.md#hot-pixel) left, on the mosaic, and debayer last. A
  [drizzle](../../reference/glossary.md#drizzle) in Image Integration that reads the filter array keeps the mosaic
  instead.
- Open a raw frame, [FITS](../../reference/glossary.md#fits) or camera [RAW](../../reference/glossary.md#raw), as a
  mosaic: answer **No** when AfterNight asks "Do you want to automatically debayer it before opening?". **Yes**
  debayers it as it opens, and Debayer then refuses the color image.
- Check that the image really holds an untouched mosaic. A file can lack the statement of its filter array while its
  pixels are a mosaic, and can keep a stale one after a conversion elsewhere.
- **From metadata** reads the filter array the image states and never guesses. When the image states none, choose the
  camera's layout by name.
- Keep the mosaic: it is the starting point to compare patterns or methods. A
  [mask](../../reference/glossary.md#mask) on its view is not used.

## Steps

1. **Open the window.** Make the mosaic the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Color Spaces > Debayer**. Check that **Target Image** names it.
2. **Choose the pattern.** Keep **From metadata** when the file states its filter array. Otherwise choose the
   camera's layout.
3. **Choose the method.** Keep **RCD**. For a quick check of the pattern, choose **Bilinear**.
4. **Choose the output.** Keep **Combined RGB Color** for an ordinary color workflow. Ask for the separate channels
   only when you need them.
5. **Run.** Choose **OK**. If AfterNight asks whether to run anyway, the view shows no mosaic: choose **No**, unless
   you know that the samples are a mosaic and have named their pattern.
6. **Check the result.** The window closes, and a new view named after the mosaic with ` - RGB - RCD` added opens
   beside it, in color. At 100 %, look at star edges and fine detail for zippers, false color or a checkerboard, and
   check the pattern before you try another method.

## Controls

What every process window shares is on [Process Windows](../../interface/process-windows.md). Debayer has no preview
and no **Reset**.

```controls
window: Debayer
controls:
  - name: Bayer/mosaic pattern
    kind: combo
    default: From metadata
    options: [From metadata, RGGB, BGGR, GBRG, GRBG]
    effect: "**From metadata** demosaics under the filter array the image states, its phase included, and refuses an image that states none. A named pattern places that layout at the top-left pixel, whatever Bayer pattern the image states. The name gives the first two rows of each 2 x 2 cell: **RGGB** puts red at the top left and blue at the bottom right."
    range: A named pattern over an image that states an X-Trans array is refused by name, before anything runs.
    tradeoff: A wrong pattern swaps colors, or makes false-color edges or a checkerboard. A named pattern ignores the phase of a mosaic cropped at an odd offset, which **From metadata** keeps.
    scope: The whole image or every frame of the sequence, and every output.
    persistence: Until the window closes. A new window starts at **From metadata**.
    note: "A FITS file states its pattern in its `BAYERPAT` card, read at the top-left pixel. The tooltip reads \"From metadata demosaics under the filter array the image states, and refuses an image that states none.\" The text a saved pattern choice keeps for **From metadata** is `Auto`."
  - name: Demosaicing method
    kind: combo
    default: RCD
    options: [Bilinear, VNG4, AHD, AMaZE, LMMSE, RCD]
    effect: Chooses how the two missing colors are estimated. How it works describes each method. **Bilinear** is a gradient-weighted fast estimate, not textbook bilinear interpolation.
    range: Over an X-Trans mosaic of a Fujifilm camera every method runs as its X-Trans counterpart, since each is written for a 2 x 2 cell. **Bilinear** runs as the fast X-Trans estimate, and every other method as the three-pass Markesteijn method, which judges luma and color differences in camera RGB. The dialog offers no X-Trans method of its own, and DCB, HPHD and IGV are not offered. The methods take no settings of their own.
    tradeoff: The methods reach different distances and treat edges and color differences differently. A fast method can leave more zippers on difficult edges, and a heavier one can smooth or reshape the finest detail and costs more time and working memory. None of them repairs a wrong pattern, and no method is best for every camera, optics and subject. Compare them on one representative linear frame before you run a long sequence. Besides the mosaic, a run holds the color image in the mosaic's pixel type, and a mosaic not stored in 32-bit floating point is first copied into one.
    scope: The whole image or every frame of the sequence, and every output.
    persistence: Until the window closes. A new window starts at **RCD**.
    note: "The new views' names keep the method chosen here, and their lineage names the method that ran, as `Rcd`, `Fast` for **Bilinear**, `XTransFast` or `MarkesteijnThreePass`. **Bilinear** gives the same result on Windows and Linux."
  - name: Output mode
    kind: combo
    default: Combined RGB Color
    options: [Combined RGB Color, Separate RGB Channels, RGB Color + Separate RGB Channels]
    effect: Chooses what opens. **Combined RGB Color** opens one RGB image, **Separate RGB Channels** its red, green and blue channels as three grayscale images, and **RGB Color + Separate RGB Channels** all four, the RGB image first.
    tradeoff: The channels are taken from the same color image, so they change nothing in the estimate. They add three images in memory, and on a sequence three more sequences on disk. Separate channels alone can hide false color between them.
    scope: The whole image or every frame of the sequence.
    persistence: Until the window closes. A new window starts at **Combined RGB Color**.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence OK reads.
    range: It lists every open view, color and grayscale images included. A color image is refused when the run starts, and a grayscale image that states no filter array needs a named pattern.
    enabled: Disabled while a run works.
    persistence: Until the window closes.
  - name: OK
    kind: button
    default: ""
    effect: Reads the window, asks the CFA question when the view shows no mosaic, and runs. When the run succeeds, the new views open and the window closes.
    enabled: From the moment the window opens. While the run works, OK, **Close** and **Target Image** are disabled, and the three other lists stay editable. A change there does not reach the run.
    tradeoff: A still image has no progress window, and its run cannot be stopped once it starts. A sequence takes its view for the run and writes new sequences.
    scope: The whole image, or every frame of the sequence read as recorded, whatever frame the player shows. The mask is not used.
    persistence: "The source and its history are not changed. The RGB image opens as `<name> - RGB - <method>`, its history beginning `Debayer: RGB image created from RAW image '<name>'`. The channels open as `<name> - RED - <method>`, `<name> - GREEN - <method>` and `<name> - BLUE - <method>`, beginning `Debayer: Red channel extracted from RAW image '<name>'` and likewise. From a sequence, the RGB sequence is `<name> - RGB - <method>`, beginning `Debayer: RGB sequence created from RAW sequence '<name>'`, and the channels are `<name> - RED`, `<name> - GREEN` and `<name> - BLUE`, beginning `Red channel extracted from sequence '<name>'` and likewise."
    note: Its tooltip reads "Run debayer with the current settings." A run that fails, or that **Cancel** stops, leaves the window open, its controls enabled again. When another run holds the sequence, or the still image is being changed, it says "Another process is executing for this image, please cancel that job first before running this process", with "sequence" for a sequence, under the title **Debayer**, and runs nothing. Wait for the other run, then choose OK again.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without running.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either, and its views still open.
```

```controls
window: The CFA question
controls:
  - name: Question
    kind: readout
    default: ""
    effect: "Asks, under the title **Debayer**: \"This file doesn't have a CFA (color filter array) defined, please make sure this is a RAW CFA file before debayering. Do you want to run this process anyway?\""
    enabled: When you choose **OK** for an image or sequence its view does not show as a mosaic. A sequence its view shows debayered is not asked about, because Debayer reads its frames as recorded. A recorded frame that states no filter array is still refused by name under **From metadata**.
  - name: "Yes"
    kind: button
    default: ""
    effect: Runs anyway. **From metadata** then refuses an image that states no filter array, and a named pattern demosaics it.
    tradeoff: Choose it only when the camera or the acquisition tells you that the samples are a mosaic, and name the pattern.
  - name: "No"
    kind: button
    default: ""
    effect: Returns to the window without running. It is the default button.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows the share of the frames done, under the title **Debayer** and the text `Executing debayer to sequence <name>`.
    enabled: Only while OK works on a sequence.
    tradeoff: Each output is written whole as a temporary sequence, in the source's container and converted as the container requires, before it opens. The RGB sequence holds three times the mosaic's samples, and both outputs together six times.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run. The demosaic notices the stop between tiles of a frame, and the run between frames. Nothing opens, and the sequences being written are removed.
    note: Closing the Debayer window does not stop the run.
```

## How it works

1. **OK** reads the window. An image of more than one channel is refused.
2. The filter array is the image's own for **From metadata**, its phase included, or the named pattern placed at the
   top-left pixel.
3. The method estimates the two missing colors of every pixel, tile by tile on the run's threads, into a three-channel
   image of the same size and pixel type. The result does not depend on the number of threads.
4. Separate channels are the red, green and blue channels of that color image, extracted as three grayscale images.
5. Each result opens as a new view with its [history](../../reference/glossary.md#history) step. The color image
   keeps the mosaic's acquisition record, [header cards](../../reference/glossary.md#header-card),
   [plate solution](../../reference/glossary.md#plate-solution),
   [processing state](../../reference/glossary.md#processing-state) and the choices made for writing it to a file. It
   states no filter array, is marked as demosaiced, and its [lineage](../../reference/glossary.md#lineage) gains an
   entry for the demosaic and the method that ran. Each channel keeps the same.

### Model

With \(C_R\), \(C_G\) and \(C_B\) the full-color image and \(p(x, y)\) the color of the filter over pixel
\((x, y)\), which the pattern repeats every two pixels, the mosaic records

\[ M(x, y) = C_{p(x, y)}(x, y), \qquad p(x, y) \in \{R, G, B\} \tag{1} \]

Debayer keeps \(M\) as the color \(p\) and estimates the other two. VNG4 is the exception: at a green site it averages
the measured green with its own estimate. A wrong pattern files every measurement under the wrong color. Several
methods use the difference between the colors, which varies more smoothly than the brightness. With \(\hat G\) the
estimated green and \(\mathcal{I}\) a method's own interpolation of the difference, a red estimate is

\[ \hat R(x, y) = \hat G(x, y) - \mathcal{I}(G - R)(x, y) \tag{2} \]

Equation (2) shows the idea, not one formula all six share. The reach is how far a method reads from the pixel it
writes, and the passes are how many times it walks the image. The time of every method grows with the number of
pixels.

| Method | How it estimates | Reach, pixels | Passes |
| --- | --- | --- | --- |
| **Bilinear** | Green weighted by four directional gradients, red and blue from their difference to green | 5 | 3 |
| **VNG4** | A variable number of gradients over four colors, the two green sites kept apart | 4 | 3 |
| **AHD** | Horizontal and vertical candidates, chosen by how even their lightness is | 5 | 4 |
| **AMaZE** | Aliasing-minimized and zipper-free, with a highlight-safe estimate near the white level | 16 | 6 |
| **LMMSE** | Linear minimum mean-square error along each axis, meant for noisy data | 12 | 5 |
| **RCD** | Horizontal or vertical green chosen by a low-pass test, corrected by the local color ratios | 9 | 4 |

### Boundaries

- The band at the edge of the picture that a method's reach cannot cover, 4 to 16 pixels wide, is filled by one shared
  rule, a three-by-three average, whatever the method. Compare methods away from the edges.
- Samples keep their scale. The white level, which every highlight threshold follows, is an integer's largest code,
  and 1 for a floating-point mosaic stated as a
  [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale). A floating-point mosaic of
  [sensor codes](../../reference/glossary.md#sensor-code) or a
  [physical quantity](../../reference/glossary.md#physical-quantity) takes its brightest valid sample, and never less
  than 1. One hot pixel brighter than every saturated star then lets AMaZE estimate those stars as unclipped.
- A mosaic whose [validity rule](../../reference/glossary.md#validity-rule) leaves samples out keeps it: every color
  pixel whose estimate reads such a sample, within the method's reach, or one pixel in the edge band, is marked as
  meaning nothing, and the channels keep the marks. Such a pixel still holds a number.
- A failure shows a message box, "Error running Debayer", with "An error occurred when executing the Debayer process:"
  and the reason, and writes it to the log. It does not reach the window's message strip, and nothing opens.

### Worked example

The mosaic is one raw light of NGC 253 from a QHY183C: 5544 by 3684 pixels, 16-bit, whose FITS header states
`BAYERPAT = 'RGGB'`. It is opened with **No** to the question, so the view shows the mosaic. The window keeps its
defaults: **From metadata**, **RCD** and **Combined RGB Color**. By equation (1), the pattern files under red every
pixel whose x and y are both even, counting from 0 at the top left, and under blue every pixel whose x and y are both
odd.

**OK** opens one new view of 5544 by 3684 pixels and three channels, 16-bit, named after the mosaic with
` - RGB - RCD` added. The red sites of the mosaic have their median near 0.050 of full scale, the green near 0.062 and
the blue near 0.050, and the channels of the color image keep about those levels. Its background is therefore green
until color calibration balances it. The view's [Autostretch](../../reference/glossary.md#autostretch) display mode
shows each channel by its own statistics, so the background looks neutral there. The hot pixels of the mosaic become
colored dots: repair them on the mosaic first.

The pair shows the core of the galaxy, from x 2572 to 2971 and from y 1692 to 1991 of the frame, at 100 % in the
Autostretch display mode: the mosaic, then the new color view.

```figure
owed: true
images:
  - image: ../../assets/images/processes/calibration/debayer-before.webp
    alt: >-
      A crop at 100 % of the mosaic, gray with a fine checkered texture, the core of NGC 253 running diagonally across
      it, a few round stars and many single white pixels.
    label: Before
  - image: ../../assets/images/processes/calibration/debayer-after.webp
    alt: >-
      The same crop of the color image, the checkered texture gone, the core of the galaxy warm yellow on a neutral
      gray background, white stars, and the hot pixels turned into small red, green and blue dots.
    label: After
caption: >-
  The core of NGC 253 on a raw light at 100 %, the mosaic before and the RGB image RCD makes of it after, both in the
  Autostretch display mode.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A raw light whose file states its pattern | **From metadata**, **RCD**, **Combined RGB Color** | Compare **AMaZE** or **LMMSE** on one frame at 100 %. |
| A file that states no pattern | The camera's layout by name, with **Bilinear** | Check the colors of known stars, then run **RCD**. |
| A mosaic cropped at an odd offset | **From metadata** | Name a pattern only for a file that states none, and check the colors. |
| Separate channels for later work | **RGB Color + Separate RGB Channels** | Keep the RGB image to check false color. |

## When it goes wrong

!!! warning "A wrong pattern looks like a bad method"
    Swapped colors, false-color edges or a 2 x 2 checkerboard most often come from a pattern that does not match the
    mosaic, or that ignores the phase of a crop. Choose **From metadata** when the image states its filter array, and
    otherwise check the camera's layout. Check the pattern on one frame before you change the method or run a
    sequence.

"This file doesn't have a CFA (color filter array) defined, please make sure this is a RAW CFA file before debayering."
:   **Cause.** The view does not show the image as a mosaic.
:   **Fix.** Choose **No**, and open the file again as a mosaic. Choose **Yes** with a named pattern only when you know
    that the samples are a mosaic.

"Error running Debayer" with "the image states no colour filter array to demosaic under"
:   **Cause.** **From metadata** was chosen for an image that states no filter array.
:   **Fix.** Choose the camera's pattern by name.

"Error running Debayer" with "the image has more than one channel; only a one-channel mosaic is debayered"
:   **Cause.** The target is a color image, perhaps a raw frame debayered as it opened.
:   **Fix.** Open the original file again with **No** to the question, and run Debayer on that view.

"Error running Debayer" with "the image states an X-Trans filter array, which a two-by-two Bayer pattern does not describe; From metadata demosaics it under its own"
:   **Cause.** A named pattern was chosen for an image that states an X-Trans array.
:   **Fix.** Choose **From metadata**. The method then runs as its X-Trans counterpart.

"Error running Debayer" with another reason
:   **Cause.** The run was refused, for example for memory.
:   **Fix.** Try one representative still image, free memory, then try a lighter method or a shorter sequence.

The new sequence is not in any folder
:   **Cause.** It is a [temporary sequence](../../reference/glossary.md#temporary-sequence).
:   **Fix.** Save or export it to keep it.

## Related

- [Image Calibration](image-calibration.md) and [Cosmetic Correction](cosmetic-correction.md), the steps on the
  mosaic before the debayer.
- [Split CFA](split-cfa.md) and [Merge CFA](merge-cfa.md), which take a mosaic apart into its planes and join them
  back, without estimating colors.
- [Image Registration](../registration-alignment/image-registration.md) and
  [Image Stacking](../stacking-integration/image-stacking.md), the steps after the debayer.
- [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md), to balance the colors while
  the image is linear.
- [Image Integration](../stacking-integration/image-integration.md), whose drizzle can read the mosaic itself.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.

## Credits

- Every method this window offers is a derived work of the demosaic code of
  [RawTherapee](https://github.com/Beep6581/RawTherapee), as [librtprocess](https://github.com/CarVac/librtprocess)
  packages it, several of its methods descending from dcraw: by Emil Martinec (the fast estimate behind **Bilinear**,
  and AMaZE), Ingo Weyrich (the tiled AMaZE, RCD, AHD, VNG4, the Markesteijn adaptation and the faster LMMSE), Gabor
  Horvath (LMMSE and the border fill), Jacques Desmis (the LMMSE adaptation), Luis Sanz Rodriguez (RCD) and Frank
  Markesteijn (the X-Trans method), under the GNU General Public License, version 3 or any later version. AfterNight's
  adaptation is distributed under that licence. The tiling, the white level, the validity of the result and the
  cancellation are AfterNight's.
- Luis Sanz Rodriguez, [RCD Demosaicing](https://github.com/LuisSR/RCD-Demosaicing), release 2.3, for ratio-corrected
  demosaicing.
- Lei Zhang and Xiaolin Wu,
  [Color Demosaicking Via Directional Linear Minimum Mean Square-Error Estimation](https://doi.org/10.1109/TIP.2005.857260),
  IEEE Transactions on Image Processing 14(12), 2005, for LMMSE.
- Keigo Hirakawa and Thomas W. Parks,
  [Adaptive Homogeneity-Directed Demosaicing Algorithm](https://doi.org/10.1109/TIP.2004.838691), IEEE Transactions on
  Image Processing 14(3), 2005, the work AHD is based on.
