---
kind: reference
summary: >-
  The formula of every Image Blend mode, the filters of the blend image, how grayscale and color images meet,
  Preserve lightness, the header and exposure of the result, and the history text.
rewritten: true
---
# Image Blend Reference

This page holds what [Image Blend](image-blend.md) leaves out to stay readable beside its window: the formula of each
of the 27 blend modes, what each filter does to the blend image, how a grayscale image meets a color one, what
**Preserve lightness** keeps, and what the result's header and [history](../../reference/glossary.md#history) say.
Each section can be read on its own.

In the formulas, \(A\) is a sample of the base and \(B\) the sample of the blend image at the same pixel, each read as
a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale) after its
[stretch](../../reference/glossary.md#stretch), and \(B\) after its filter too. \(R\) is the result of the mode,
before **Preserve lightness**, **Opacity** and the [mask](../../reference/glossary.md#mask). Every result is held
between 0 and 1.

## The blend modes

The modes are listed in the order of the **Blend mode** list, whose lines separate the same groups.

| Mode | Result \(R\) | What it does |
| --- | --- | --- |
| **Replace** | \(B\) | Takes the blend image. It is written in the base's [pixel type](../../reference/glossary.md#pixel-type), and **Preserve lightness**, **Opacity** and the mask still apply. |
| **Darken/Min** | \(\min(A,\ B)\) | The darker sample of each channel, the base's on a tie. |
| **Multiply** | \(A\,B\) | Darkens. White in the blend image leaves the base, black gives black. |
| **Color Burn** | \(1 - \min\bigl((1 - A)/B,\ 1\bigr)\) | Deepens the shadows strongly. 0 where \(B = 0\). |
| **Linear Burn** | \(A + B - 1\) | Darkens by the sum, black where it falls below 0. |
| **Darker Color** | The pixel of the lower CIE L* | Takes the whole pixel, base or blend, whose lightness is lower, the base's on a tie. With two grayscale images, \(\min(A,\ B)\). |
| **Lighten/Max** | \(\max(A,\ B)\) | The lighter sample of each channel. |
| **Screen** | \(1 - (1 - A)(1 - B)\) | Lightens. Black in the blend image leaves the base. |
| **Color Dodge** | \(A / (1 - B)\) | Brightens strongly. 0 where \(B\) is exactly 1. |
| **Linear Dodge/Add** | \(A + B\) | The sum, held at 1. |
| **Lighter Color** | The pixel of the higher CIE L* | Takes the whole pixel whose lightness is higher, the blend image's on a tie. With two grayscale images, \(\max(A,\ B)\). |
| **Overlay** | \(2AB\) for \(A \le 0.5\), \(1 - 2(1 - A)(1 - B)\) above | Contrast, switched on the base's level. |
| **Soft Light** | \((1 - 2B)\,A^2 + 2AB\) | Gentler contrast, a curve of the base. |
| **Hard Light** | \(2AB\) for \(B \le 0.5\), \(1 - 2(1 - A)(1 - B)\) above | Contrast, switched on the blend image's level. |
| **Vivid Light** | \(1 - (1 - A)/(2B)\) for \(B < 0.5\), \(A / \bigl(2(1 - B)\bigr)\) from 0.5 | Burns below middle gray and dodges above it. 0 where \(B = 0\), 1 where \(B = 1\). |
| **Linear Light** | \(A + 2B - 1\) | Adds twice the blend image's distance from middle gray. 0 where \(B = 0\). |
| **Pin Light** | \(\max\bigl(2B - 1,\ \min(A,\ 2B)\bigr)\) | Keeps the base between two limits the blend image sets. 0 where \(B = 0\). |
| **Difference** | \(\lvert A - B \rvert\) | 0 where the two are equal, so what is the same turns black. |
| **Exclusion** | \(0.5 - 2(A - 0.5)(B - 0.5)\) | A difference of lower contrast. |
| **Subtract** | \(A - B\) | The difference, held at 0. |
| **Divide** | \(A / B\) | The ratio, held between 0 and 1. 0 where \(B = 0\). |
| **Power** | \(A^{B}\) | The base raised to the blend image. A blend sample below 1 lifts a base that is not 0. |
| **Arctan** | \(\operatorname{atan2}(A,\ B)\) | The angle in radians, not scaled to a right angle, so it is 1 wherever \(A\) is more than 1.557 times \(B\). |
| **Hue** | The [HSI](../../reference/glossary.md#hsi) hue of the blend image, with the saturation and intensity of the base | Takes the blend image's hue. |
| **Saturation** | The HSI saturation of the blend image, with the hue and intensity of the base | Takes the blend image's saturation. |
| **Color** | The CIE L* of the base, with the a* and b* of the blend image | Takes the blend image's color and keeps the base's lightness. |
| **Lightness** | The CIE L* of the blend image, with the a* and b* of the base | Takes the blend image's lightness. A grayscale blend image is the lightness itself, \(L^* = 100\,B\). |

Every mode is computed on fractions of full scale in every pixel type, signed integers included, and the result is
written in the base's pixel type. **Linear Dodge/Add** and **Subtract** are computed in double precision.

## Grayscale and color

- A grayscale image meets a color one as three equal channels, and the result has three channels. A grayscale blend
  image under **Lightness** is read as the lightness itself instead.
- The result has three channels when either image has three, when the mode is **Hue**, **Saturation**, **Color** or
  **Lightness**, or when the filter is **Hue** or **Color**. It has one otherwise. Each image must have one channel or
  three.
- **Hue** and **Saturation** go through HSI. **Color**, **Lightness**, **Darker Color**, **Lighter Color**,
  **Preserve lightness** and the **Hue** filter go through [CIE Lab](../../reference/glossary.md#cie-lab), computed in
  double precision. The base's red, green and blue are read as linear when it is described as linear RGB, and as
  sRGB-encoded otherwise.
- The preview and **OK** use the same conversions, so the preview shows what **OK** writes.
- A color the conversion back to RGB puts outside the range is held at 0 or 1.

## The filters

The filter acts on the blend image alone, after its stretch and before the mode.

Gaussian Blur
:   The exact Gaussian whose standard deviation is **Radius**, along the rows and the columns, with the image
    mirrored beyond its edges. Above about 32 pixels it runs as one kernel up to 4,096 pixels wide, on a blend image
    more than twice that kernel's radius wide and high, and otherwise as several exact passes whose variances add up
    to it.

High Pass
:   The blend image less its Gaussian blur of **Radius**, plus 0.5, held between 0 and 1: middle gray where the image
    is smooth, lighter or darker at its detail.

Hue
:   The CIE lightness of the blend image, or the image itself when it is grayscale, used as the
    [HSV](../../reference/glossary.md#hsv) value of a color of **Hue** and **Saturation**. The result is one hue in
    the lightness of the blend image.

Color
:   Each channel of the blend image used, on its own, as the HSV value of a color of **Hue** and **Saturation**: the
    red of the result comes from the red channel, the green from the green, the blue from the blue. A grayscale blend
    image gets one hue. A color one is not recolored in one hue.

## Preserve lightness

After the mode, the result is taken to CIE Lab, its lightness is replaced by the lightness of the base after the
base's stretch, and it is taken back to RGB and held between 0 and 1. A color that leaves the range is clipped there.
**Opacity** and the mask act afterwards, in RGB, so where either is fractional the lightness of the image written is
not exactly the base's. The check box is disabled for **Color**, which keeps the base's lightness already, for
**Lightness**, which replaces it on purpose, and when both images are grayscale.

## The result's header and record

- **Size and type.** The base's width, height and pixel type, whatever the blend image's. An integer result is
  rounded to the nearest code, ties to the even one, and held at the ends of its type.
- **Exposure.** The light the two images hold together, whatever the mode, the **Opacity** or the mask: the sum of
  each image's whole integration. That is its `TOTEXP` card, else its `LIVETIME` card, else its exposure times its
  frame count, from `STACKCNT`, else `NCOMBINE`, else one. Two images that state the same exposure start are one
  acquisition, such as an image and its [starless image](../../reference/glossary.md#starless-image), its stars or a
  channel taken from it, and count once, with the larger integration and the larger frame count either states. A
  [master](../../reference/glossary.md#master) blended with its own starless image states the master's integration
  and frame count.
- **Exposure cards.** The images' `EXPOSURE` and `EXPTIME` cards are dropped. The result states its own `EXPTIME`
  and `TOTEXP` as the sum above, `LIVETIME` the same when either image states one, and `STACKCNT` and `NCOMBINE` as
  the frame counts summed the same way, each when either image states it.
- **Observation start.** The earliest either image states, with its `DATE-OBS`, `DATE-LOC`, `MJD-OBS`, `DATE-BEG`,
  `MJD-BEG` and `TIME-OBS` cards taken together from that image.
- **Other facts.** The object, instrument, telescope, observer, filter, sensor temperature, gain and binning are
  kept when both images state the same value, and dropped otherwise. Every other
  [header card](../../reference/glossary.md#header-card) of the base is kept only when the blend image states it
  identically.
- **Position on the sky.** The base's [WCS](../../reference/glossary.md#wcs) cards, its
  [plate solution](../../reference/glossary.md#plate-solution) and its pointing cards, `RA`, `DEC`, `OBJCTRA`,
  `OBJCTDEC`, `OBJRA`, `OBJDEC`, `TELRA` and `TELDEC`, are kept, because the pixel grid is the base's. Nothing checks
  that the blend image was registered to it.
- **Processing state.** The result is recorded as nonlinear, by a process, with the operation `image-blend`. It
  states every sample it wrote as valid, and its [lineage](../../reference/glossary.md#lineage) gains an entry for
  the blend with the text of the history step.
- **Without a blend image.** **OK** stretches the base alone, on its first three channels, and records it as
  nonlinear with the operation `image-blend`. At the stretch's defaults the samples are left as they were.

## The history text

The step is named **Image Blend**, and its text is the mode and the blend image's name, as
`Overlay with M42 stretched`. Without a blend image it gives the base's stretch with three decimals, as
`Base stretch: shadows 0.000, midtones 0.250, highlights 1.000`. The text records neither the filter, the
**Opacity**, **Preserve lightness**, the blend image's stretch nor the mask.

## What costs time

The modes work on every pixel in parallel, and their cost grows with the width, the height, the channels and the
frames of a [sequence](../../reference/glossary.md#sequence). Each stretch that is not at its defaults, and each filter, adds a full-size working copy. The
blur's work grows with **Radius** as one kernel and with its square as passes. **Darker Color**, **Lighter Color**,
the four component modes and **Preserve lightness** add color conversions. Each preview and each run computes
everything again: nothing is kept from one to the next.
