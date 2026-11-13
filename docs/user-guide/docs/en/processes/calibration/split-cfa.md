---
kind: process
stage: calibrate
menu: Process > Color Spaces > Split CFA
summary: >-
  Takes a Bayer mosaic apart into its four positional planes as new grayscale images or sequences, or extracts its red
  sites as Ha, its mean green, or a derived Ha and OIII pair for a dual-band filter.
data_in: cfa
data_out: linear
destructive: false
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active. Target Image lists only the open one-channel mosaics, and OK refuses a
    target that states no two-by-two Bayer pattern.
  inputs: >-
    One undebayered mosaic of an RGGB, BGGR, GRBG or GBRG pattern at any phase, as a still image or a sequence, in any
    pixel type.
  outputs: >-
    One, two or four new grayscale views, or new temporary sequences, in the mosaic's pixel type and not yet saved. The
    mosaic and its history are not changed. No file, preset or report is written.
  state: >-
    A raw or calibrated mosaic of a one-shot color camera, before the debayer. A light taken through a dual-band filter
    for Extract Ha/OIII.
derived: false
---
# Split CFA

This process takes a raw [CFA](../../reference/glossary.md#cfa) mosaic apart without estimating a color. **Split CFA
Channels** opens the four positions of each two-by-two cell of its
[Bayer pattern](../../reference/glossary.md#bayer-pattern) as four grayscale images, each a quarter of the mosaic. The other methods extract the red sites as Ha, the mean of the
two green sites, or an Ha and OIII pair derived for a dual-band filter.

**OK** opens each result in a new view beside the mosaic, or as a new [sequence](../../reference/glossary.md#sequence)
for a sequence, and closes the window. Each result keeps the mosaic's
[pixel type](../../reference/glossary.md#pixel-type). The mosaic and its
[history](../../reference/glossary.md#history) are not changed.

```figure
owed: true
image: ../../assets/images/processes/calibration/split-cfa.webp
alt: >-
  The Split CFA window. Under the title, the process name and the line Extract CFA channels or derived Ha/OIII/green
  components from RAW mosaic frames. Below, Method at Split CFA Channels, Ha/OIII Resampling greyed at No Resampling,
  and Target Image naming the light. OK and Close run along the bottom.
caption: >-
  Split CFA on a raw NGC 253 light with Method at Split CFA Channels, which opens the light's four positional planes.
pins:
  - {n: 2, x: 2.8, y: 67.5}
  - {n: 3, x: 2.8, y: 40.0}
  - {n: 4, x: 2.8, y: 53.5}
  - {n: 5, x: 54.0, y: 90.0}
```

## Before you start

- The image is a mosaic that states its pattern. Do not [debayer](../../reference/glossary.md#debayer) it: when a raw
  [FITS](../../reference/glossary.md#fits) file opens with the question "Do you want to automatically debayer it before
  opening?", answer **No**.
- Check that the stated pattern, a FITS file's `BAYERPAT` card, is the camera's. The pattern and its phase name the
  planes and place the Ha and OIII sites, and a wrong one gives wrong names and products without a warning.
- Calibrate the mosaic first: the results state no filter array, and are smaller than the mosaic.
- **Extract Ha** and **Extract Ha/OIII** assume that the red sites record mainly hydrogen-alpha and the green and blue
  sites mainly OIII, as behind a dual-band filter. Check the filter's red leak and cross-talk: nothing removes the
  [continuum](../../reference/glossary.md#continuum) or fits a spectrum.
- No result states a [plate solution](../../reference/glossary.md#plate-solution). Plate-solve a result again when
  you need one.
- A [mask](../../reference/glossary.md#mask) on the view is not used. On a sequence, every frame is split, whatever
  frames its player has selected, and each result is a sequence of its own: check the free disk first.

## Steps

1. **Open the window.** Make the mosaic the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Color Spaces > Split CFA**. The title names the mosaic.
2. **Check the target.** Check that **Target Image**, which lists the open mosaics, names the one you mean.
3. **Choose the method.** Choose **Method**. **Split CFA Channels** keeps every sample.
4. **Size the pair.** For **Extract Ha/OIII** only, choose **Ha/OIII Resampling**: keep each product at its own size,
   or bring one to the other's.
5. **Split.** Choose **OK**. The window closes and new views open beside the mosaic, named after it and the plane,
   as `<mosaic> - CFA0 - Red`.

## Controls

The window has no preview, no **Channel** and no **Reset**. What every process window shares is on
[Process Windows](../../interface/process-windows.md).

```controls
window: Split CFA
controls:
  - name: Method
    kind: combo
    default: Split CFA Channels
    options: [Split CFA Channels, Extract Ha, Extract Ha/OIII, Extract Green]
    effect: Chooses what the window makes. **Split CFA Channels** makes four images, one for each position of the cell. **Extract Ha** makes one of the red sites. **Extract Green** makes one of the mean of the two green sites of each cell. **Extract Ha/OIII** makes an Ha and an OIII image for a dual-band filter.
    enabled: "**Ha/OIII Resampling** is enabled only with **Extract Ha/OIII**."
    tradeoff: Only the four positional planes can be merged back into the mosaic. The three extractions drop, average or estimate sites, and cannot.
    scope: OK, on the image or on every frame of the sequence. No mask is used.
    persistence: Until the window closes. The next window opens at **Split CFA Channels**.
    note: "**Split CFA Channels** makes four results, named `<mosaic> - CFA0 - Red` to `<mosaic> - CFA3 - Blue` by position and color. **Extract Ha** makes `<mosaic> - Ha`, **Extract Green** `<mosaic> - Green`, and **Extract Ha/OIII** both `<mosaic> - Ha` and `<mosaic> - Oiii`. On a sequence each result is a new sequence."
  - name: Ha/OIII Resampling
    kind: combo
    default: No Resampling
    options: [No Resampling, Upsample Ha, Downsample OIII]
    effect: "**No Resampling** keeps Ha at half the mosaic's width and height and OIII at its full size. **Upsample Ha** enlarges Ha by two to the size of OIII. **Downsample OIII** reduces OIII by two to the size of Ha."
    enabled: With **Method** at **Extract Ha/OIII**. It is disabled when the window opens.
    tradeoff: "**Upsample Ha** makes samples that were never measured and takes four times the memory of Ha. It adds no resolution. **Downsample OIII** discards the resolution of OIII. Both resample with a Lanczos filter, which can ring beside a sharp edge such as a star."
    scope: The Ha and OIII results only.
    persistence: Until the window closes.
  - name: Target Image
    kind: combo
    default: The active mosaic
    options: [The active mosaic, Every other open one-channel mosaic]
    effect: Chooses the mosaic or mosaic sequence OK splits. It is read and never changed.
    range: It lists the views that show a one-channel mosaic, a still image that states a filter array, or a sequence whose view does not debayer its frames. An X-Trans mosaic is listed, and OK refuses it.
    enabled: Disabled while a run works. **Method** and **Ha/OIII Resampling** stay enabled, and a change to them does not reach the run.
    tradeoff: A longer sequence takes more time and more temporary disk.
    persistence: Until the window closes.
    note: Opened on an image that is no mosaic, the window works on that image, which its title names, and OK refuses it even while this box shows a mosaic.
  - name: OK
    kind: button
    default: ""
    effect: Checks that the target is a two-by-two mosaic, reads **Method** and **Ha/OIII Resampling**, and makes the results away from the window. New views open beside the mosaic, and the window closes.
    enabled: Enabled when the window opens, also on an image that is no mosaic, which it refuses when you press it. Disabled while a run works, with **Close** and **Target Image**.
    tradeoff: "**Extract Ha/OIII** works on a floating copy of the whole mosaic, and a resampling holds a 64-bit working plane on top of it. A still image shows no progress window, and its run cannot be stopped once it starts."
    scope: The whole image or every frame of the sequence, without the mask.
    persistence: "Each new view is unsaved, and its history starts with `Split CFA: CFA0 extracted from RAW image '<mosaic>'`, where `CFA0` is `CFA0` to `CFA3`, `Ha`, `Green` or `OIII`, and `RAW sequence` replaces `RAW image` for a sequence. Its lineage records the split and the plane, or the extraction. The mosaic's history gets no step."
    note: A run that fails leaves the window open with its controls enabled.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without splitting.
    enabled: Disabled while a run works.
    note: Closing the window another way while a run works hides it, and it closes when the run has ended. The run is not stopped, on a sequence either.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: "Shows the share of the frames done, from 0 to 100 percent, under the title **Split CFA** and the text `Executing split CFA to sequence <name>`."
    enabled: Shown only while OK works on a sequence. A still image has no progress window.
    tradeoff: Each result is written to a temporary file in the sequence's container until the run ends. The four positional planes together hold as many samples as the mosaic. An OIII sequence at full size holds as many alone. Try the method on one frame as a still image before a long sequence.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run, before the next frame at the latest. Nothing opens, the temporary files are removed, and the Split CFA window stays open.
    note: Closing the Split CFA window does not stop the run.
```

## How it works

1. **OK** checks that the target is a one-channel mosaic of a two-by-two pattern. Each cell is then read by position,
   whatever its colors, and an odd last column or row, which completes no cell, is left out.
2. The pattern and phase the mosaic states give each position its color, and the two greens are numbered in plane
   order.
3. **Split CFA Channels** and **Extract Ha** copy sites, and **Extract Green** averages two. All three work in the
   mosaic's own pixel type. **Extract Ha/OIII** works on a floating copy of the mosaic, as a
   [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale).
4. A positional plane keeps the mosaic's acquisition record, its
   [processing state](../../reference/glossary.md#processing-state) and its
   [header cards](../../reference/glossary.md#header-card), except the [WCS](../../reference/glossary.md#wcs) cards and
   the `PLTSOLVR` card of a plate solution, which described the mosaic's grid. An Ha, Green or OIII product keeps the
   acquisition record, drops every header card and any decision on how to write it, and is recorded as demosaiced.
5. A sequence's frames are read as recorded, and each result is written into a new
   [temporary sequence](../../reference/glossary.md#temporary-sequence) in the sequence's
   container, published only when every frame is written.

### Model

Plane \(k\), from 0 to 3, holds the position \((k \bmod 2,\ \lfloor k/2 \rfloor)\) of every cell. With \(M\) the
mosaic, \(W\) and \(H\) its width and height, and \(0 \le x < \lfloor W/2 \rfloor\), \(0 \le y < \lfloor H/2 \rfloor\):

\[ P_k(x, y) = M\bigl(2x + (k \bmod 2),\ 2y + \lfloor k/2 \rfloor\bigr) \tag{1} \]

So CFA0 is the top-left site of every cell, CFA1 the top-right, CFA2 the bottom-left and CFA3 the bottom-right. The
pattern decides their colors:

| Pattern | CFA0 | CFA1 | CFA2 | CFA3 |
| --- | --- | --- | --- | --- |
| RGGB | Red | Green1 | Green2 | Blue |
| BGGR | Blue | Green1 | Green2 | Red |
| GRBG | Green1 | Red | Blue | Green2 |
| GBRG | Green1 | Blue | Red | Green2 |

A mosaic cut at an odd column states the shifted pattern, and the names follow it: an RGGB mosaic cropped one column
in is GRBG, and its planes are Green1, Red, Blue and Green2. Equation (1) is a copy, so the four planes give back the
mosaic's even part exactly. **Extract Ha** is the red plane. **Extract Green** gives, with \(G_1\) and \(G_2\) the two
green planes in fractions of full scale,

\[ G = \tfrac{1}{2}\,(G_1 + G_2) \tag{2} \]

rounded to the nearest code, ties to even, for an integer mosaic.

**Extract Ha/OIII** works in 32-bit floating point, or 64-bit for a 64-bit or a 32-bit integer mosaic, on the even
part of the mosaic. Ha is the red plane. For OIII, let \(\mu_1\), \(\mu_2\) and \(\mu_B\) be the means of the finite
samples at the two green positions and the blue one. Each of those sites is multiplied by \(\bar\mu / \mu_i\) of its
position, where

\[ \bar\mu = \tfrac{1}{3}\,(\mu_1 + \mu_2 + \mu_B) \tag{3} \]

A position whose mean is zero, to double precision, is left as it is. Each red site of OIII is then the mean of its
eight equalized neighbours, the four beside it weighted by \(\sqrt{2}\) and the four on its corners by 1. Both
products return to the mosaic's pixel type, an integer to the nearest code. **Upsample Ha** and **Downsample OIII**
resample by two with the separable kernel \(L(d) = \operatorname{sinc}(d)\,\operatorname{sinc}(d/4)\) for \(|d| < 4\),
where \(\operatorname{sinc}(t) = \sin(\pi t)/(\pi t)\): eight taps normalized by their sum, with pixel centres at
half-pixel positions, reading the nearest edge sample beyond the picture.

### Boundaries

- **OK** refuses an image that states no filter array, one of more than one channel and an X-Trans mosaic, each by
  name, and asks no question to run anyway. Nothing opens.
- **Split CFA Channels** refuses a mosaic whose valid samples are stated in a mask. The three extractions refuse,
  before any work, a mosaic that states any sample invalid, and **Extract Ha/OIII** one where a green or blue position
  holds no finite sample.
- In a floating mosaic, a sample that is not a number is copied as it is by **Extract Ha**, and makes its cell's
  **Extract Green** value one too. **Extract Ha/OIII** leaves it out of its means and fills and writes 0 in its place,
  and a red site without a finite neighbour becomes 0.
- The Lanczos result is not clamped. Beside a sharp edge a floating product can fall outside 0 to 1, and an integer
  one stops at the ends of its type.
- A refusal or a failure is reported in a message box titled "Error running Split CFA", which starts "An error
  occurred when executing the Split CFA process:", and in the log, not in the window's message strip. No view opens.

### Worked example

The data is a raw light of NGC 253 from a QHY183C: a FITS file of 5544 by 3684 16-bit
[sensor codes](../../reference/glossary.md#sensor-code), whose header states `BAYERPAT = 'RGGB'` with no offset.
Opened with **No** to the debayer question, its view takes the file's name, `<light>` below. With **Method** at
**Split CFA Channels**, **OK** opens four 16-bit planes of 2772 by 1842 pixels: `<light> - CFA0 - Red`,
`<light> - CFA1 - Green1`, `<light> - CFA2 - Green2` and `<light> - CFA3 - Blue`. Their medians are 3248, 4064, 4096
and 3280 codes: in this uncalibrated sky the green sites read a quarter more than the red and blue ones.

At the cell of the galaxy's core, at (2772, 1842) of the light, red, green, green and blue read 4736, 6048, 6096 and
4080. By equation
(1) they are pixel (1386, 921) of each plane, and by equation (2) **Extract Green** gives 6072 there. A plane's pixels
lie two of the sensor's apart, 2.29″ on the sky in place of 1.15″, and the plane keeps the light's `XPIXSZ` of 2.4 µm.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A first look at a mosaic | **Split CFA Channels** | Compare the planes' names and medians with the camera's pattern before you derive anything. |
| A dual-band light | **Extract Ha/OIII** with **No Resampling** | Choose one size for both products, then resample once: **Upsample Ha** or **Downsample OIII**. |
| Planes to change and merge back | **Split CFA Channels** | Change the four planes alike, keep their size, and join them with Merge CFA in CFA0 to CFA3 order. |

## When it goes wrong

!!! warning "Ha and OIII are estimates, not narrowband masters"
    The pair holds what the red, green and blue sites of a dual-band light recorded, cross-talk and continuum
    included. Compare it with what the filter passes before you combine it with true narrowband data.

**Target Image** does not list the mosaic
:   **Cause.** The view shows no one-channel mosaic: the file was debayered as it opened, or a sequence's view
    debayers its frames.
:   **Fix.** Open the raw file or the sequence again, and answer **No** when AfterNight asks whether to debayer it.

"the image states no colour filter array"
:   **Cause.** The target states no Bayer pattern, or the window opened on an image that is no mosaic.
:   **Fix.** Open the mosaic from a file that states its pattern, make its view active, and open the window again. Do
    not guess a pattern you do not know.

"the XTrans filter array is not a two-by-two arrangement, which is all this process takes apart"
:   **Cause.** The mosaic is X-Trans, whose pattern repeats over six pixels.
:   **Fix.** Demosaic it with Debayer and **From metadata**, or open the raw file with the automatic debayer.

"an image whose valid samples are stated in a mask cannot be split", or "... cannot have a narrowband channel extracted"
:   **Cause.** The mosaic states some samples invalid, which no result can carry.
:   **Fix.** Use the mosaic as it was opened, which states every sample valid.

**Ha/OIII Resampling** is greyed
:   **Cause.** **Method** is not **Extract Ha/OIII**.
:   **Fix.** Choose **Extract Ha/OIII** when the light was taken through a dual-band filter.

A plane is one pixel short of half the mosaic
:   **Cause.** The mosaic's width or height is odd, and its last column or row completes no cell.
:   **Fix.** Nothing else is lost. Crop the mosaic to an even size first when its planes must match another's.

A plane's plate solve fails, or finds twice the scale its hints give
:   **Cause.** No result states a solution, and a plane keeps the sensor's `XPIXSZ` and `YPIXSZ` though its pixels lie
    two apart.
:   **Fix.** Plate-solve the plane with twice the sensor's pixel size in the hints.

"Another process is executing for this image, please cancel that job first before running this process", or the same for a sequence
:   **Cause.** The mosaic is being changed, or another run holds the sequence, and nothing started.
:   **Fix.** Wait for that run to end, or cancel it, then choose **OK** again.

## Related

- [Image Calibration](image-calibration.md), to calibrate the mosaic before you split it.
- [Merge CFA](merge-cfa.md), to join the four positional planes back into a mosaic.
- [Debayer](debayer.md), for a color image made from every site.
- [CFA Integer Resample](cfa-integer-resample.md), to bin or enlarge the mosaic and keep its filters.
- [Plate Solving](../registration-alignment/plate-solving.md), to solve a plane or a product again.
- [Process Windows](../../interface/process-windows.md), for what every process window shares.
