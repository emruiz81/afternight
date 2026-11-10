---
kind: process
stage: finish
menu: Process > Color Spaces > Colorize
summary: >-
  Turns a grayscale or RGB image into an RGB image of one hue and saturation that you choose, keeping each pixel's
  intensity as HSV value or HSI intensity.
data_in: nonlinear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Colorize window is open on it yet, and no window that needs the image
    for itself is open on it.
  inputs: >-
    One grayscale or RGB image or sequence, a mosaic read as one grayscale plane, and an optional mask of the image's
    size on its view. An image with an alpha channel is refused.
  outputs: >-
    The same view, replaced by an RGB image of the same size and pixel type, with one history step. A grayscale image
    becomes three channels. No file, preset or report is written.
  state: >-
    A stretched image, or one plane of it, after calibration and color calibration. A linear image is accepted, and its
    result is recorded as nonlinear all the same.
derived: false
---
# Colorize

Colorize gives an image one hue and one saturation that you choose. Each pixel keeps its intensity, as the value of
the [HSV](../../reference/glossary.md#hsv) model or the intensity of the [HSI](../../reference/glossary.md#hsi) model,
and the hue and saturation are yours. It turns a mono plane into a layer of one color, or makes a false-color version
of a [mask](../../reference/glossary.md#mask) or of another support image. It is a construction for display, not a
color calibration or a physical narrowband mapping.

**Apply** replaces the image or [sequence](../../reference/glossary.md#sequence) with an RGB image of the same size and
[pixel type](../../reference/glossary.md#pixel-type), and adds one step to its
[history](../../reference/glossary.md#history). A grayscale image becomes three channels. The result is recorded as
nonlinear.

```figure
image: ../../assets/images/processes/color-composition/colorize.webp
alt: >-
  The Colorize window. On the left, a histogram with a red, a green and a blue peak apart from each other, its
  toolbar, the Method list at HSV, the Hue box at 200.00 and the Saturation box at 0.60 with their sliders, Use
  lightness ticked, Target Image and Preview. On the right, the preview shows the nebula in shades of blue and cyan.
  Reset, Close and Apply run along the bottom.
caption: >-
  Colorize on a stretched master with Hue at 200, Saturation at 0.60 and Use lightness on: the lightness of the image
  in one blue hue.
pins:
  - {n: 2, x: 13.0, y: 70.4}
  - {n: 3, x: 2.0, y: 55.6}
  - {n: 4, x: 2.0, y: 60.5}
  - {n: 5, x: 2.0, y: 65.7}
  - {n: 6, x: 47.6, y: 40.0}
  - {n: 7, x: 92.3, y: 96.0}
```

## Before you start

- Decide what gives the intensity. A grayscale image gives its one plane. An RGB image gives its
  [CIE L*](../../reference/glossary.md#cie-lab) lightness with **Use lightness**, or each of its channels without it.
- **Apply** replaces the image and turns a grayscale image, or a mosaic, into RGB. Its history keeps the step before.
- Use samples between 0 and 1. HSV adds no clamp, so a floating-point sample outside that range gives channels
  outside it. HSI holds its channels between 0 and 1 at any saturation above 0.
- The preview is shown as its samples are, with no
  [display stretch](../../reference/glossary.md#display-stretch). A
  [linear image](../../reference/glossary.md#linear-image) looks nearly black in it: read the histogram, or
  [stretch](../../reference/glossary.md#stretch) the image first.
- A mask on the view blends the colorized image with the original afterwards. A grayscale original is blended into
  each of the three channels.

## Steps

1. **Open the window.** Make the image active and choose **Process > Color Spaces > Colorize**. At the defaults the
   preview shows the image in red, and **Apply** is already enabled.
2. **Choose the intensity.** For an RGB image, tick **Use lightness** to color its lightness. Leave it off only when
   you want each channel mapped on its own.
3. **Choose the model.** Leave **Method** at **HSV** to keep each intensity as the brightest channel, or choose
   **HSI** to keep it as the mean of the three.
4. **Set the hue.** Move **Hue** to the color you want, in degrees: 0 is red, 120 green and 240 blue.
5. **Set the saturation.** Lower **Saturation** from 1 toward 0 for a paler color. At 0 the result is gray.
6. **Check the preview.** Press ++space++ over it to compare with the original, read the histogram, and look at the
   channels with the preview's **View Channel**. Untick **Preview area**, if you drew one, for the last look.
7. **Apply.** Choose **Apply**. The image in its view is RGB in the color you set, and its history lists **Colorize**
   with the hue and the saturation.

## Controls

The histogram and its toolbar, **Target Image**, **Preview**, the preview pane, **Close** and the progress window of a
sequence work as in every process window: see [Process Windows](../../interface/process-windows.md). The preview pane
of this window has no **Display Mode**.

```controls
window: Colorize
controls:
  - name: Method
    kind: combo
    default: HSV
    options: [HSV, HSI]
    effect: Chooses the model that turns an intensity into a color. **HSV** keeps the intensity as the brightest of the three channels. **HSI** keeps it as their mean, before they are held between 0 and 1.
    enabled: It stays enabled while Apply runs, and the run keeps the choice it started with.
    tradeoff: HSI clips bright, saturated colors and loses their detail. HSV keeps every intensity between 0 and 1 inside the range. Both cost the same small work for each pixel, HSI a little more for its cosines and its three clamps.
    scope: The preview and Apply, all three channels of the result, on images and sequences.
    persistence: Until the window closes. **Reset** and a finished Apply return it to **HSV**.
  - name: Hue
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 360, step: 1, decimals: 2}
    effect: Sets the color, in degrees, through red, yellow, green, cyan, blue and magenta. 360 is the same as 0.
    range: The slider beside the box has 1,000 steps, about 0.36 degrees each.
    enabled: Disabled while Apply runs.
    tradeoff: It sets the ratios between the channels. On an RGB image without **Use lightness**, unequal channels give another hue than the one you set.
    scope: The preview and Apply, on images and sequences.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Saturation
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: Sets how strong the color is. At 0 the result is gray, and at 1 it is the strongest color the model gives.
    range: The slider moves in steps of 0.001, and the box rounds its value to two decimals.
    enabled: Disabled while Apply runs.
    tradeoff: Higher values separate the channels more, and with **HSI** clip more highlights. Lower values come closer to gray or to the original.
    scope: The preview and Apply, on images and sequences.
    persistence: Until the window closes. **Reset** and a finished Apply return it to its default.
  - name: Use lightness
    kind: check
    default: "Off"
    effect: On, an RGB image is colored from one plane, its CIE L* lightness divided by 100, so every pixel takes the hue and saturation you set. Off, each of its channels is colored from its own value.
    range: The lightness is read as the Channel list of a process window reads it. The RGB is converted as linear when the image is described as linear RGB, and as sRGB-encoded otherwise. An image without a full scale is read as the fractions of full scale the view shows.
    enabled: Only while the target is RGB without an alpha channel. A grayscale target, or one with alpha, disables it and turns it off. It stays enabled while Apply runs, and the run keeps the choice it started with.
    tradeoff: It keeps the structure of the image together, but the result's own lightness is not the source's. It adds a pass over the image and a floating-point plane of the lightness, which has the precision of 32-bit floating point, or of 64-bit for a 64-bit floating-point image.
    scope: The preview and Apply, which convert alike, for RGB images and sequences.
    persistence: Until the window closes, or until the target is no longer RGB. **Reset** and a finished Apply leave it as it is.
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: Chooses the image or sequence Apply replaces, with the mask on its view. The preview and whether **Use lightness** is offered follow it.
    range: It lists every open image. An image with an alpha channel, or any channels other than one or three, is refused when the preview or Apply runs.
    persistence: Until the window closes. **Reset** leaves it.
  - name: Preview
    kind: check
    default: "On"
    effect: Shows the colorized image beside the controls, with the mask blended in. The histogram counts it.
    enabled: It stays enabled while Apply runs.
    tradeoff: The preview holds an RGB result as large as the image, three times a grayscale one.
    scope: "The window only. The pane shows the samples as they are, in its Linear mode, and offers no **Display Mode** or **Autostretch Settings**. **Preview area** is off when the window opens, and ticked it starts at 600 by 600 pixels in the middle of the image. A snapshot is an RGB image named `<target> - Colorize - SNAPSHOT`, not recorded as nonlinear."
    persistence: Until the window closes. **Reset** leaves it.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Method** to **HSV**, **Hue** to 0 and **Saturation** to 1, and one preview of that red follows.
    range: It leaves **Use lightness**, **Target Image**, **Preview** and the preview area as they are.
  - name: Apply
    kind: button
    default: ""
    effect: Colorizes the whole image, or every frame of the sequence, with the values it finds when you press it, and then returns **Method**, **Hue** and **Saturation** to their defaults.
    enabled: From the moment the window opens. While the run works it is disabled, with **Target Image**, **Hue**, **Saturation**, **Reset**, **Close**, **Preview area** and **Take snapshot**. **Method**, **Use lightness**, **Preview** and the histogram stay enabled.
    tradeoff: The result is three channels in the image's pixel type, and an integer image also needs a three-channel floating-point working copy. A sequence of grayscale frames is written again as RGB, which can take up to three times its disk space.
    scope: The whole image, never limited by a preview area, with the mask on the view blended afterwards.
    persistence: "The history step is **Colorize** and reads, for example, `Colorize hue to 200 and saturation to 0.6`. It does not record **Method** or **Use lightness**."
```

## How it works

1. Each sample is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale). An integer
   image is worked on in a floating-point copy, and every pixel is computed in double precision.
2. The intensity of a pixel is the sample of a grayscale image, the lightness of an RGB image with **Use lightness**,
   or, without it, each of the three channels in turn.
3. The intensity becomes red, green and blue by equation (1) for **HSV** or (2) for **HSI**. Without **Use
   lightness**, the red of the result comes from the red channel, the green from the green and the blue from the blue.
4. The result is written as a new RGB image of the source's size and pixel type, rounded to the nearest value, with
   no color filter array. A mask on the view then blends it with the original.
5. The image keeps its header, its [plate solution](../../reference/glossary.md#plate-solution), its acquisition data
   and its validity mask. A saved file records `ANLIN` as `nonlinear`, `ANASRC` as `process` and `ANOP` as
   `colorize`, and no linear encoding is stated. The [lineage](../../reference/glossary.md#lineage) gains an entry with
   the hue, the saturation, the model and whether the lightness was used.

### Model

With \(H\) the hue as a share of a full turn, \(S\) the saturation and \(V\) the intensity, HSV computes

\[ C = V\,S, \qquad X = C\,\bigl(1 - \lvert (6H \bmod 2) - 1 \rvert\bigr), \qquad m = V - C \tag{1} \]

The 60-degree sector the hue falls in, from red, chooses \((C, X, 0)\), \((X, C, 0)\), \((0, C, X)\), \((0, X, C)\),
\((X, 0, C)\) or \((C, 0, X)\), and \(m\) is added to each channel. For \(V\) between 0 and 1 the brightest channel is
\(V\) and none leaves the range. Nothing is clamped afterwards.

With \(I\) the intensity and \(h\) the hue in degrees, HSI computes in its first 120-degree sector

\[ B = I\,(1 - S), \qquad R = I\Bigl(1 + \frac{S \cos h}{\cos(60^\circ - h) + 10^{-12}}\Bigr), \qquad G = 3I - R - B \tag{2} \]

The next two sectors give the same roles to green and to blue, after 120 or 240 degrees are taken from \(h\). The mean
of the three is \(I\). Each channel is then held between 0 and 1, which breaks that mean for a bright, saturated
color. At a saturation of 0, HSI returns \(I\) in every channel without that hold.

Without **Use lightness**, with \(F_R\), \(F_G\) and \(F_B\) the red, green and blue that equation (1) or (2) gives for
an intensity, an RGB pixel \((R, G, B)\) becomes

\[ R' = F_R(R), \qquad G' = F_G(G), \qquad B' = F_B(B) \tag{3} \]

At a saturation of 0 that is the pixel itself. At any other saturation it does not keep the pixel's value or intensity
while it replaces its hue.

### Boundaries

- An image with an alpha channel, or with channels other than one or three, is refused: "the colorization needs a grey
  image or an image of red, green and blue". Nothing is written.
- A pixel holding not-a-number or an infinity, or one the image marks as invalid, is not colorized. Without **Use
  lightness** an RGB pixel stays as it was. A grayscale pixel, or one read through its lightness, keeps its intensity
  in all three channels.
- A floating-point result keeps values outside 0 to 1 where HSV or a gray HSI gives them. An integer result is
  rounded and held to the range of its type.
- A source whose [processing state](../../reference/glossary.md#processing-state) records the basis `monochrome`
  keeps it, although the result has three colored channels. Any other source becomes `display-rgb`. A saved
  [FITS](../../reference/glossary.md#fits) file carries no `BAYERPAT` card, because the result has no color filter
  array.
- A mask with one channel weighs all three channels, and a mask of three weighs each by its own. A mask of another
  size or channel count makes the run fail. The whole image is recorded as colorized, also where the mask is black.

### Worked example

The picture above shows the window on the linear Carina [master](../../reference/glossary.md#master) after Histogram
Transformation with **Shadows** and **Midtones** at 0.02, with **Method** at **HSV**, **Hue** at 200, **Saturation**
at 0.60 and **Use lightness** on. The gray background, near 0.28 in each channel, is read as sRGB and has a lightness
of 30, so \(V = 0.30\). Hue 200 lies in the fourth sector of equation (1), and the background becomes
\((0.12, 0.24, 0.30)\), a dark blue. A white star, \(V = 1\), becomes \((0.40, 0.80, 1.00)\).

```figure
images:
  - image: ../../assets/images/processes/color-composition/colorize-before.webp
    alt: The whole frame before Colorize, the red and white clouds of the nebula across a field of stars on a dark gray background.
    label: Before
  - image: ../../assets/images/processes/color-composition/colorize-after.webp
    alt: The whole frame after Colorize, the same nebula and stars in shades of one blue, from a dark blue background to light blue clouds and stars.
    label: After
caption: >-
  The stretched Carina master before and after Apply with HSV, Hue at 200, Saturation at 0.60 and Use lightness on.
  Both are the whole frame in the Linear display mode, without a display stretch.
```

Four more pixels of intensity 0.40 or 0.50 show the two models:

| Settings | Result | Why |
| --- | --- | --- |
| HSV, **Hue** 0, **Saturation** 0.50, intensity 0.40 | (0.40, 0.20, 0.20) | The brightest channel is the intensity. |
| HSI, **Hue** 0, **Saturation** 0.50, intensity 0.40 | (0.80, 0.20, 0.20) | The mean is the intensity. |
| HSI, **Hue** 0, **Saturation** 1, intensity 0.50 | (1.00, 0, 0) | 1.50 is held at 1, and the mean is lost. |
| Either, **Saturation** 0, intensity 0.40 | (0.40, 0.40, 0.40) | No saturation is gray. |

Without **Use lightness**, the RGB pixel (0.20, 0.40, 0.60) under HSV, **Hue** 60 and **Saturation** 1 takes its red
from an intensity of 0.20, its green from 0.40 and its blue from 0.60, by equation (3). It becomes (0.20, 0.40, 0),
whose own hue is 90 degrees, not 60.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A mono plane, or an RGB image with **Use lightness**, under HSV | **Saturation** from 0.40 to 0.80, at the hue you want | Lower **Saturation** if one channel dominates or an integer image shows bands. Check at 100 %. |
| A bright plane under HSI | **Saturation** below 0.50 | If bright parts clip in one or two channels and lose detail, lower **Saturation** or return to **HSV**. |
| An RGB image without **Use lightness** | Only when you want each channel mapped on its own | At **Saturation** 0 it is the original. If the hue is not the one you set, tick **Use lightness**. |
| Colorize through a mask | A soft mask, with its inversion checked | A hard edge gives a colored seam: soften or repair the mask before you apply. |

## When it goes wrong

!!! warning "Without Use lightness, the hue is not the one you set"
    An RGB image without **Use lightness** is mapped channel by channel, by equation (3). Only on neutral pixels does
    the result have the hue you set. Tick **Use lightness**, or colorize one plane of the image.

!!! warning "The history does not record Method or Use lightness"
    The step names the hue and the saturation only, so its text cannot repeat the result. The lineage of the result
    records all four. Note **Method** and **Use lightness** when you must repeat the step.

The preview is nearly black
:   **Cause.** The preview shows the samples as they are, and a linear image is dark.
:   **Fix.** Read the histogram, or stretch the image before you colorize it.

Bright parts lose their detail with HSI
:   **Cause.** Equation (2) went beyond 1, and the hold flattened the channels.
:   **Fix.** Lower **Saturation**, or choose **HSV**.

The result has samples outside 0 to 1 with HSV
:   **Cause.** The floating-point source had them, and HSV adds no clamp.
:   **Fix.** Bring a duplicate into the range first, then colorize it.

Not-a-number or infinite pixels are still there
:   **Cause.** A pixel holding no number is not colorized.
:   **Fix.** Repair those pixels on a duplicate before you colorize.

Use lightness is disabled
:   **Cause.** The target is grayscale, or has an alpha channel.
:   **Fix.** Nothing for a grayscale image, whose one plane is the intensity. Remove the alpha channel first.

Two images with the same samples give different results with Use lightness
:   **Cause.** One is described as linear RGB and is read as linear, the other is read as sRGB-encoded.
:   **Fix.** Check how each image's color space is described.

"the colorization needs a grey image or an image of red, green and blue"
:   **Cause.** The target, or a frame of the sequence, has an alpha channel or another number of channels.
:   **Fix.** Remove the alpha channel, or extract one plane and colorize it. The target was not changed.

Reset did not turn Use lightness off
:   **Cause.** **Reset** leaves **Use lightness** as it is.
:   **Fix.** Untick it yourself before the next run.

## Related

- [NB Color Mapper](nb-color-mapper.md), which colors several narrowband layers and mixes them.
- [Narrowband Palette Composer](narrowband-palette-composer.md), which mixes narrowband masters by fixed palettes.
- [Adjust HSL](../color-adjustment/adjust-hsl.md), to change the hue and saturation an RGB image already has.
- [Masks](../../interface/masks.md), to make and attach the mask Apply blends through.
- [Image History](../../interface/image-history.md), to undo the step.
- [Process Windows](../../interface/process-windows.md), for the preview, the histogram toolbar and what every process
  window shares.

## Credits

- Alvy Ray Smith, ["Color Gamut Transform Pairs"](https://alvyray.com/Papers/CG/color78.pdf), SIGGRAPH 1978,
  describes the hexcone HSV model.
- International Color Consortium, [How to interpret the sRGB color space](https://www.color.org/sRGB.pdf), describes
  the sRGB transfer and the D65 basis the CIE L* conversion uses for an image not described as linear RGB. No embedded
  ICC profile is read.
