---
kind: process
stage: finish
menu: Process > Color Spaces > LRGB Combination
summary: >-
  Gives a stretched color image the lightness of a luminance image of its size and keeps its hue and chroma, with a
  transfer for the luminance, one for the chroma and a weight for each channel.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An RGB image or sequence view is active, no LRGB Combination window is open on it yet, and no window that needs
    the image for itself is open on it. Apply waits for a luminance image.
  inputs: >-
    One RGB image or sequence, a grayscale luminance image of exactly its width and height in any pixel type, and an
    optional mask on the color image's view.
  outputs: >-
    The color image, replaced, with one history step, in its own size and pixel type and recorded as nonlinear. No
    file is written.
  state: >-
    A color image and a luminance image of one target, registered and cropped alike, and both stretched so that their
    backgrounds have about the same lightness.
derived: false
rewritten: true
---
# LRGB Combination

LRGB Combination gives a color image the lightness of a separate luminance image, such as a
[master](../../reference/glossary.md#master) taken through a clear filter. It reads the color image as the lightness,
chroma and hue of [CIE Lab](../../reference/glossary.md#cie-lab), replaces its lightness with the luminance or blends
the two, and converts back to red, green and blue. A
[midtones transfer function](../../reference/glossary.md#midtones-transfer-function) can reshape the luminance and
another the chroma first, and a weight can lower each channel last.

**Apply** replaces the color image, or every frame of its [sequence](../../reference/glossary.md#sequence), keeps its
size and [pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). The result is recorded as nonlinear.

```figure
image: ../../assets/images/processes/color-composition/lrgb-combination.webp
alt: >-
  The LRGB Combination window. On the left, a histogram of three colored peaks over its toolbar, then Luminance naming
  the stretched luminance master and RGB naming the color image, the Channel Weights group with Lightness, Red, Green
  and Blue at 1.00, and the Transfer Functions group with Lightness at 0.50 and Saturation at 0.40. On the right, the
  preview shows the red and white clouds of the nebula. Reset, Close and Apply run along the bottom.
caption: >-
  LRGB Combination on the stretched Carina master, with the luminance master stretched to the same background as its
  Luminance and Saturation at 0.40. The preview shows what Apply writes.
pins:
  - {n: 2, x: 36.0, y: 44.1}
  - {n: 3, x: 26.0, y: 77.3}
  - {n: 4, x: 25.0, y: 56.3}
  - {n: 5, x: 34.0, y: 81.0}
  - {n: 6, x: 25.0, y: 63.6}
  - {n: 7, x: 62.0, y: 21.0}
```

## Before you start

- [Stretch](../../reference/glossary.md#stretch) both images first. The luminance's samples become the lightness as
  they are, 0 black and 1 white, so its background should sit at the color image's lightness, and nothing matches
  the two for you. A linear luminance makes a nearly black image.
- Register the two images to one reference and crop them alike. The luminance is offered by its width and height
  alone: nothing matches stars, compares world coordinates, or moves, turns, scales or resamples it.
- Remove gradients and calibrate the color while both are linear. The result is recorded as nonlinear, and the color
  calibrations refuse it.
- The two may differ in pixel type. Each is read as a
  [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), so a 16-bit luminance meets a 32-bit
  floating-point color image without a conversion, and the result keeps the color image's type.
- A [mask](../../reference/glossary.md#mask) on the color image's view blends the result with the original after the
  combination. On a sequence, every frame gets the one luminance image.

## Steps

1. **Open the window.** Make the stretched color image the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Color Spaces > LRGB Combination**.
   **RGB** names it, **Luminance** reads `<--NO IMAGE-->`, and **Apply** is dimmed.
2. **Choose the luminance.** Pick the luminance image in **Luminance**. **Apply** enables, and the preview shows the
   color image with the luminance's lightness.
3. **Match the lightness.** Hold ++space++ over the preview to compare its background with the original. If it is
   darker, lower **Lightness** under **Transfer Functions**. If it is paler, raise it.
4. **Blend, if you want.** Lower **Lightness** under **Channel Weights** to keep part of the color image's own
   lightness.
5. **Set the color.** Lower **Saturation** below 0.5 for stronger colors, or raise it for weaker ones. Gray stays
   gray.
6. **Trim a channel.** Lower **Red**, **Green** or **Blue** only to take some of one color out at the end.
7. **Check at 100 %.** Zoom the preview into stars and edges, or draw a
   [preview area](../../reference/glossary.md#preview-area) there, which crops both images alike and shortens each
   preview. Doubled stars or colored fringes mean the two images are not registered.
8. **Apply.** Choose **Apply**. The image in its view has the luminance's detail, its history lists **LRGB
   Combination** with the luminance's name and the six values, and the values return to their defaults.

## Controls

The window has no **Preview** check box: the preview is always on. Its histogram, the histogram's toolbar, the
preview's toolbar and the progress window of a sequence are those of every process window: see
[Process Windows](../../interface/process-windows.md). The sliders change their value when you let them go.

```controls
window: LRGB Combination
controls:
  - name: Luminance
    kind: combo
    default: "<--NO IMAGE-->"
    options: ["<--NO IMAGE-->"]
    effect: Chooses the luminance image, whose samples become the lightness of the result. Until one is chosen the preview shows the color image unchanged.
    range: After `<--NO IMAGE-->` the list holds every open grayscale image of exactly the color image's width and height, of any pixel type, and never the color image itself. The tooltip of each item gives its file, size, channels, pixel type and memory.
    enabled: Disabled while a run works. The list is filled again when a view opens, closes, changes or is renamed, and when **RGB** names another image.
    tradeoff: Nothing checks that it is registered to the color image. A pair off by a pixel gives doubled stars and colored edges.
    scope: The image its view shows when the preview or **Apply** asks for it, read as a fraction of its own full scale. A preview area crops it to the same area as the color image, without registering the two. On a sequence, the one image its view shows when you press **Apply** serves every frame, and two sequences are not paired frame by frame.
    persistence: Kept by **Reset** and **Apply**. Another image in **RGB**, and a luminance view that closes or changes into an image the list does not take, return it to `<--NO IMAGE-->`.
  - name: RGB
    kind: combo
    default: The active image
    options: [The active image, Every other open RGB image or sequence]
    effect: Chooses the color image, which gives the hue and the chroma, the mask, the header and the pixel grid, and which **Apply** replaces.
    range: It lists the open views with three channels, never a grayscale one.
    tradeoff: The image chosen here is replaced. Check it, and the window's title, before you apply.
    persistence: Until the window closes. **Reset** leaves it. Choosing another fills the luminance list again.
  - name: Lightness
    object: lightnessWeightValue
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets how much of the lightness the luminance supplies, by equation (2). At 1 it replaces the color image's lightness, at 0 the color image keeps its own, and between them the two are mixed in proportion.
    tradeoff: Higher brings more of the luminance's detail and contrast, and more of any difference between the two backgrounds.
    scope: The lightness only, after the luminance's transfer, before the channel weights and the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: Red
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Multiplies the red of the result after the conversion back. Lower takes red out, and 0 removes it. It cannot raise a channel above its value at 1.
    tradeoff: A strong cut shifts the color and sends faint red detail to black.
    scope: The red channel of the result, held between 0 and 1 again afterwards, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: Green
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Multiplies the green of the result after the conversion back. Lower takes green out, and 0 removes it.
    tradeoff: A strong cut shifts the color and darkens the image most, since green carries most of the lightness.
    scope: The green channel of the result, held between 0 and 1 again afterwards, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: Blue
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Multiplies the blue of the result after the conversion back. Lower takes blue out, and 0 removes it.
    tradeoff: A strong cut shifts the color and can hide faint blue structure.
    scope: The blue channel of the result, held between 0 and 1 again afterwards, before the mask.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 1.
  - name: Lightness
    object: lightnessMTFValue
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets the midtones balance the luminance is stretched by before it is used, by equation (1). Below 0.5 it brightens the luminance between black and white, above 0.5 it darkens it. At 0.5 the luminance is used as it is.
    range: Black and white stay where they are. At 0 every sample between them turns white, and at 1 black.
    tradeoff: It reshapes all the luminance's tones, not only its background. Near either end small moves change the lightness a lot.
    scope: The luminance only, before **Lightness** of **Channel Weights** blends it.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.5.
    note: Any value but 0.5 records the operation as `lrgb-mtf-combination`.
  - name: Saturation
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Sets the midtones balance the color image's chroma is stretched by, by equation (3). Below 0.5 the colors grow stronger, above 0.5 weaker. At 0.5 the chroma is kept.
    range: It acts on the CIE chroma, not on the saturation of HSL or HSV, and keeps each pixel's hue. A gray pixel stays gray.
    tradeoff: Strong values distort colors and push them out of the range, where the conversion back clips them.
    scope: The chroma of the color image, before the conversion back, the channel weights and the mask. The luminance has no color and plays no part.
    persistence: Until the window closes. **Reset** and a finished Apply return it to 0.5.
    note: Any value but 0.5 records the operation as `lrgb-mtf-combination`.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the four weights to 1 and both transfers to 0.5.
    range: It leaves **Luminance**, **RGB**, the preview area and the histogram's settings as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Combines the color image, or every frame of its sequence, with the luminance as the window stands, replaces it and returns the six values to their defaults. **Luminance** stays chosen.
    enabled: While **RGB** and **Luminance** both name an image, at any values. While the run works, **Luminance**, **RGB**, the six values, **Apply**, **Reset** and **Close** are disabled.
    tradeoff: Every pixel is converted to CIE Lab and back, in one pass spread over the processor's threads, and a transfer other than 0.5 adds work to each pixel but no image. An integer image, the color image or the luminance, is worked on a 32-bit floating-point copy, or 64-bit for a 32-bit integer. On a large image, try the settings on aligned crops of both images first.
    scope: The whole image, through the mask on its view. A preview area does not limit it.
    persistence: "The history step is **LRGB Combination** and reads like `Luminance: '<name>', lightness weight 1.00, channel weights R 1.00 G 1.00 B 1.00, lightness midtones 0.50, saturation midtones 0.40`, every value with two decimals. The result is recorded as nonlinear with the operation `lrgb-combination`, or `lrgb-mtf-combination` when a transfer is not 0.5, whatever the two images stated."
    note: "The header is built from both images. The exposure is the light they hold together: each one's `TOTEXP`, else its `LIVETIME`, else its exposure times its `STACKCNT`, else `NCOMBINE`, else one, summed. Two images that state the same exposure start are one acquisition, such as an image and a lightness channel taken from it, and count once with the larger integration and frame count either states. Their `EXPOSURE` and `EXPTIME` cards are dropped, and the result states its own `EXPTIME` and `TOTEXP`, `LIVETIME` when either image states one, and `STACKCNT` and `NCOMBINE` summed the same way, each when either image states it. The observation start is the earliest, with its `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`, `MJD-BEG` and `TIME-OBS` together. The object, instrument, telescope, observer, filter, sensor temperature, gain and binning are kept when both images state the same, and every other card of the color image only when the luminance states it alike. The color image's WCS cards, plate solution and pointing cards `RA`, `DEC`, `OBJCTRA`, `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA` and `TELDEC` are kept, without a check that the luminance shares its grid."
```

## How it works

1. Both images are read as fractions of full scale, an integer one through a floating-point copy, and each pixel is
   computed in double precision.
2. The color image's red, green and blue are read as sRGB-encoded, or as linear when the image states linear RGB,
   and converted to CIE \(L^*\), \(a^*\) and \(b^*\) with a D65 white. No color profile is read or converted.
3. The luminance, through its transfer, gives the lightness, mixed with the color image's own by **Lightness** of
   **Channel Weights**. The chroma goes through its transfer, and \(a^*\) and \(b^*\) are scaled to it, which keeps
   the hue.
4. The color is converted back the way it was read, each channel is held between 0 and 1, multiplied by its weight
   and held again. A mask on the view then blends the result with the original.
5. The result is written once in the color image's pixel type, rounded to the nearest code, and replaces it with one
   history step.

### Model

The lightness is read as \(\ell = L^*/100\) and the chroma as \(c = C^*/200\), with \(C^* = \sqrt{a^{*2} + b^{*2}}\),
both from 0 to 1. A transfer with the balance \(m\) sends a value \(x\) between 0 and 1 to

\[ T_m(x) = \frac{(m - 1)\,x}{(2m - 1)\,x - m} \tag{1} \]

and gives 0 at or below 0 and 1 at or above 1. With \(m = 0.5\) it changes nothing and is skipped, and always
\(T_m(m) = 0.5\). With \(y\) the luminance
sample, \(w\) the value of **Lightness** under **Channel Weights** and \(m_L\) that under **Transfer Functions**, the
new lightness is

\[ \ell' = (1 - w)\,\ell + w\,T_{m_L}(y) \tag{2} \]

With \(m_C\) the value of **Saturation**, the chroma becomes \(c'\), and \(a^*\) and \(b^*\) are scaled together,
so the hue angle \(h = \operatorname{atan2}(b^*, a^*)\) is kept without being computed:

\[ c' = T_{m_C}(c), \qquad a^{*\prime} = a^*\,\frac{c'}{c}, \qquad b^{*\prime} = b^*\,\frac{c'}{c} \tag{3} \]

With **Lightness** of **Channel Weights** at 0.25, a lightness of 0.40 and a luminance of 0.70 give 0.475. A
**Saturation** of 0.25 turns a chroma of 0.20 into 0.43. After the conversion back, a channel \(R\) with the weight
\(w_R\) becomes

\[ R' = \min\bigl(1,\ w_R \min(1,\ \max(0,\ R))\bigr) \tag{4} \]

and green and blue alike.

### Boundaries

- A pixel either image marks as invalid keeps the color image's samples. A sample that is not a finite number, in a
  pixel both images state valid, stops the run.
- A grayscale image cannot be the color image: the menu command is greyed while one is active, and **RGB** lists
  none.
- The weights only lower a channel, and a color the conversion back puts out of range is clipped before them.
- The result is recorded as nonlinear even when both images were linear.

### Worked example

The color image is the Carina master after Histogram Transformation with **Shadows** and **Midtones** at 0.02, which
puts each channel's median at 0.278. A gray of 0.278 has a CIE lightness of 30.2. The luminance is the luminance
master of the same night, whose median is 0.144, after Histogram Transformation with **Shadows** at 0.135 and
**Midtones** at 0.023, which puts its median at 0.302. With the defaults, equation (2) makes the background's
lightness 0.302, so each channel's background moves only from 0.278 to 0.279, while the clouds take the luminance's
structure. Set
**Saturation** to 0.40: equation (3) turns a pale color of chroma 20 into 28.6, and leaves the gray background gray.

```figure
images:
  - image: ../../assets/images/processes/color-composition/lrgb-combination-before.webp
    alt: The whole stretched color image, the red and white clouds of the nebula and its stars over a dark gray background.
    label: Before
  - image: ../../assets/images/processes/color-composition/lrgb-combination-after.webp
    alt: The whole image after Apply, the same field with the clouds in stronger reds and the background as dark as before.
    label: After
caption: >-
  The stretched Carina master before and after Apply with the stretched luminance master and Saturation at 0.40.
  Both are the whole frame in the Linear display mode, without a display stretch.
```

Had the luminance been stretched like the color image, with 0.02 and 0.02, its median would land at 0.876. The
result would be pale: a lightness of 0.876 is a gray of 0.86.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Both images stretched to the same background | The defaults | **Saturation** below 0.5 if the colors look thin. |
| The backgrounds differ | The luminance stretched again to the color image's background | **Lightness** under **Transfer Functions** for what is left, one control at a time, then **Lightness** under **Channel Weights** near 0.5 if the join still shows. |
| Colors too strong, or clipped | **Saturation** toward 0.5, channel weights at 1 | Lower one channel's weight only for a cast left at the end. |
| A mask over bright detail | A smooth mask on registered images | Soften the mask or lower **Lightness** under **Channel Weights** if halos show at its edges. |

## When it goes wrong

!!! warning "Apply replaces the color image and records it as nonlinear"
    Duplicate the color image first, or use its history to return. Keep the separate masters.

Apply stays dimmed
:   **Cause.** **Luminance** reads `<--NO IMAGE-->`, or the image it named was closed or changed.
:   **Fix.** Choose the luminance again in **Luminance**.

The luminance image is not in the list
:   **Cause.** It is a color image, its width or height differs from the color image's, or it is the color image
    itself.
:   **Fix.** Take one channel out of a color luminance, or register and crop it to the color image's grid.

The result is paler or darker than the color image
:   **Cause.** The luminance's background is brighter or darker than the color image's lightness, and its samples
    replace the lightness as they are.
:   **Fix.** Stretch the luminance again so its median matches, or move **Lightness** under **Transfer Functions**:
    above 0.5 darkens, below brightens.

Doubled stars or colored fringes, also in a preview area
:   **Cause.** The two images have one size but are not registered to each other.
:   **Fix.** Register and crop the luminance to the color image's grid, then combine them again.

The result is nearly white or black
:   **Cause.** A transfer is near 0 or 1.
:   **Fix.** Press **Reset**, then move the transfers from 0.5 in small steps.

The colors change in a way you did not expect
:   **Cause.** **Saturation** stretches the CIE chroma, and colors pushed out of range are clipped. A channel weight
    below 1 shifts the balance.
:   **Fix.** Move **Saturation** toward 0.5 with the channel weights at 1, and lower a weight only at the end.

Every frame of a sequence has the same detail
:   **Cause.** The one luminance image its view showed at **Apply** serves every frame.
:   **Fix.** Combine paired still frames one by one, or use a single luminance on purpose.

## Related

- [Channel Composition](channel-composition.md), to compose the color image from red, green and blue masters.
- [Histogram Transformation](../stretching/histogram-transformation.md), to stretch both images to one background
  first.
- [Image Registration](../registration-alignment/image-registration.md), to put both images on one grid.
- [Color Saturation](../color-adjustment/color-saturation.md), to adjust the color afterwards.
- [Image Statistics](../../interface/image-statistics.md), to read the medians the two stretches are matched by.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every
  process window shares.

## Credits

- International Color Consortium, [How to interpret the sRGB color space](https://www.color.org/sRGB.pdf), for the
  sRGB transfer, primaries and D65 white the conversion uses.
- CIE, [Colorimetry, Part 4: CIE 1976 L\*a\*b\* Colour Space](https://cie.co.at/publications/colorimetry-part-4-cie-1976-lab-colour-space-1),
  for the lightness, chroma and hue the combination works in.
