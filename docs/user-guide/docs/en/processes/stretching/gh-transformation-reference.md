---
kind: reference
summary: >-
  The equations and tables behind Generalized Hyperbolic Transformation: the inverse curves of Invert, the curve STF
  Transformation derives, the Colour rule, checkpoints of a curve, what too little and too much look like, and the
  history text.
---
# Generalized Hyperbolic Transformation Reference

This page holds what [Generalized Hyperbolic Transformation](gh-transformation.md) leaves out to stay readable
beside its window. It gives the curve again with its slopes, the inverse that **Invert** applies, the curve **STF
Transformation** derives, the factor of **Colour**, a table of checkpoints, what too little and too much of each
value look like, and the text the [history](../../reference/glossary.md#history) records. Each section can be read on its own, and the equations are
numbered through the page.

## Symbols

| Symbol | Meaning | Where it comes from |
| --- | --- | --- |
| \(s\) | The [stretch](../../reference/glossary.md#stretch) factor | **Stretch factor(In(D+1))** |
| \(D\) | The strength of the curve | Equation (1) |
| \(b\) | The local intensity | **Local intensity (b)** |
| \(S\) | The [symmetry point (SP)](../../reference/glossary.md#symmetry-point) | **Symmetry point (SP)** |
| \(L\), \(H\) | The two [protection points (LP/HP)](../../reference/glossary.md#protection-point) | **Protect shadows (LP)**, **Protect highlights (HP)** |
| \(x\) | A sample of the image, cut to the range 0 to 1 | The image |
| \(y\) | A sample of the result, from 0 to 1 | The curve |
| \(t\) | The distance of a sample from the symmetry point | \(S - x\) below it, \(x - S\) above it |

The window keeps \(L \le S \le H\).

## The curve and its slopes

The strength is

\[ D = e^{s} - 1 \tag{1} \]

**Generalized Hyperbolic**, **Histogram Transformation** and **Arcsinh** each have a base curve \(g(t)\), which
starts at 0 and rises ever more slowly. For **Generalized Hyperbolic** it is chosen by \(b\):

\[
g(t) =
\begin{cases}
\dfrac{\ln(1 + D\,t)}{D}, & b = -1 \\[1.5ex]
\dfrac{1 - (1 - b\,D\,t)^{(b+1)/b}}{(b + 1)\,D}, & b < 0,\ b \ne -1 \\[1.5ex]
\dfrac{1 - e^{-D\,t}}{D}, & b = 0 \\[1.5ex]
\dfrac{1 - (1 + b\,D\,t)^{-1/b}}{D}, & b > 0
\end{cases}
\tag{2}
\]

For **Histogram Transformation**, with the balance \(m\), and for **Arcsinh** it is

\[ g(t) = \frac{(m - 1)\,t}{(2m - 1)\,t - m} \quad \text{with} \quad m = \frac{1}{2\,(D + 1)}, \qquad g(t) = \operatorname{asinh}(D\,t) \tag{3} \]

With \(g'\) the slope of \(g\), a sample is first given the value

\[
q(x) =
\begin{cases}
-g(S - L) - g'(S - L)\,(L - x), & x < L \\
-g(S - x), & L \le x < S \\
g(x - S), & S \le x \le H \\
g(H - S) + g'(H - S)\,(x - H), & x > H
\end{cases}
\tag{4}
\]

and the curve is that value brought to the range 0 to 1:

\[ F(x) = \frac{q(x) - q(0)}{q(1) - q(0)} \tag{5} \]

The slopes that equation (4) uses for its two straight lines are, in the order of equations (2) and (3):

\[
g'(t) =
\begin{cases}
\dfrac{1}{1 + D\,t}, & \text{Generalized Hyperbolic},\ b = -1 \\[1.5ex]
(1 - b\,D\,t)^{1/b}, & \text{Generalized Hyperbolic},\ b < 0,\ b \ne -1 \\[1.5ex]
e^{-D\,t}, & \text{Generalized Hyperbolic},\ b = 0 \\[1.5ex]
(1 + b\,D\,t)^{-(1+b)/b}, & \text{Generalized Hyperbolic},\ b > 0 \\[1.5ex]
\dfrac{m\,(1 - m)}{\bigl((2m - 1)\,t - m\bigr)^{2}}, & \text{Histogram Transformation} \\[1.5ex]
\dfrac{D}{\sqrt{1 + D^{2}\,t^{2}}}, & \text{Arcsinh}
\end{cases}
\tag{6}
\]

Every slope is greatest at \(t = 0\), so the curve is steepest at the symmetry point. With \(D = 0\) the curve
changes nothing.

## The inverse that Invert applies

With **Invert** on, a sample \(y\) becomes the \(x\) for which \(F(x) = y\). The window offers it for the three
types of the section above. The curve of equation (5) has four pieces, which meet at the results of the three
points:

\[ y_L = F(L), \qquad y_S = F(S), \qquad y_H = F(H) \tag{7} \]

The sample is first taken back from the range 0 to 1 to the value of equation (4):

\[ z = q(0) + y\,\bigl(q(1) - q(0)\bigr) \tag{8} \]

Each base curve has an inverse \(h(v)\), the distance \(t\) at which \(g(t) = v\). For **Generalized Hyperbolic**:

\[
h(v) =
\begin{cases}
\dfrac{e^{D\,v} - 1}{D}, & b = -1 \\[1.5ex]
\dfrac{1 - \bigl(1 - (b + 1)\,D\,v\bigr)^{b/(b+1)}}{b\,D}, & b < 0,\ b \ne -1 \\[1.5ex]
-\dfrac{\ln(1 - D\,v)}{D}, & b = 0 \\[1.5ex]
\dfrac{v}{1 - D\,v}, & b = 1 \\[1.5ex]
\dfrac{(1 - D\,v)^{-b} - 1}{b\,D}, & b > 0,\ b \ne 1
\end{cases}
\tag{9}
\]

For **Histogram Transformation**, with the \(m\) of equation (3), and for **Arcsinh**:

\[ h(v) = \frac{m\,v}{(2m - 1)\,v - (m - 1)}, \qquad h(v) = \frac{\sinh(v)}{D} \tag{10} \]

The inverse curve is then

\[
F^{-1}(y) =
\begin{cases}
L + \dfrac{z + g(S - L)}{g'(S - L)}, & y < y_L \\[1.5ex]
S - h(-z), & y_L \le y < y_S \\[1.5ex]
S + h(z), & y_S \le y \le y_H \\[1.5ex]
H + \dfrac{z - g(H - S)}{g'(H - S)}, & y > y_H
\end{cases}
\tag{11}
\]

- A sample of 0 stays 0 and a sample of 1 stays 1. Every other sample is first cut to the range 0 to 1, as for
  the forward curve.
- With \(D = 0\) the inverse changes nothing.
- The inverse undoes the curve, not the Apply. Samples that the first stretch cut at 0 or 1, the rounding to the
  [pixel type](../../reference/glossary.md#pixel-type) and a color conversion are not brought back, and the values must be the ones of the first stretch.
- The history names the type with `(Inverse)` after it, and the result is recorded as nonlinear.

## The curve STF Transformation derives

**STF Transformation** measures the image and derives a [midtones](../../reference/glossary.md#midtones) curve from
what it finds. For each channel \(j\) it takes the median \(v_j\) and the deviation \(\sigma_j\), which is 1.4826
times the [median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.001 when that is
zero. A curve has a [black point](../../reference/glossary.md#black-point) \(c\), a
[white point](../../reference/glossary.md#white-point) \(w\) and a balance \(m\). With \(u\) the place of the sample
between the two points, it is

\[
F(x) =
\begin{cases}
0, & x \le c \\[1ex]
\dfrac{(m - 1)\,u}{(2m - 1)\,u - m}, & c < x < w \\[1.5ex]
1, & x \ge w
\end{cases}
\qquad u = \frac{x - c}{w - c}
\tag{12}
\]

\(M_{0.25}(u)\) below is the middle line of equation (12) with \(m = 0.25\). The number 0.25 is the brightness the
background is aimed at, and 2.8 is the number of deviations between the median and the black point. Neither can be
changed.

With **Linked STF** off, each channel of an RGB image gets its own curve:

\[ c_j = \max(0,\ v_j - 2.8\,\sigma_j), \qquad w_j = 1, \qquad m_j = M_{0.25}(v_j - c_j) \tag{13} \]

With **Linked STF** on, and for a grayscale image, one curve is derived from the means over the channels, written
with a bar, and given to every channel:

\[ c = \max\bigl(0,\ \overline{v_j - 2.8\,\sigma_j}\bigr), \qquad w = 1, \qquad m = M_{0.25}\bigl(\overline{v_j} - c\bigr) \tag{14} \]

When the median of every channel is above 0.5, the image is taken to be a negative and the roles turn round. The
black point stays at 0 and the white point is derived. Channel by channel:

\[ c_j = 0, \qquad w_j = \min(1,\ v_j + 2.8\,\sigma_j), \qquad m_j = 1 - M_{0.25}(w_j - v_j) \tag{15} \]

and linked:

\[ c = 0, \qquad w = \min\bigl(1,\ \overline{v_j + 2.8\,\sigma_j}\bigr), \qquad m = 1 - M_{0.25}\bigl(w - \overline{v_j}\bigr) \tag{16} \]

- The choice between equations (13) and (15) is made once for the image, from all its medians, also when the
  channels are derived one by one.
- The median and the deviation are those of every valid sample of the whole image, measured exactly, once for each
  state of the image. An integer image is measured as fractions of its full range. A
  [**Preview area**](../../reference/glossary.md#preview-area) is stretched with the statistics of the whole image,
  and each frame of a [sequence](../../reference/glossary.md#sequence) with its own.
- An image without a valid sample is refused, and so is one whose median lies outside 0 to 1: "automatic stretch
  statistics must have normalized medians and nonnegative [MADs](../../reference/glossary.md#mad)".
- With **Lightness**, **Saturation** or **Colour** in **Channel** and **Linked STF** off, the component is
  stretched with the curve derived from the red channel, not with one derived from the component.
- The derived \(c\), \(w\) and \(m\) are shown nowhere and are not recorded. Both histograms draw the curve: the
  green or the blue channel's when **Channel** names that channel, the red channel's otherwise.

As an example, the [master](../../reference/glossary.md#master) of the process page has a median of 0.02766 and a median absolute deviation of 0.00175 in
every channel. The deviation is 0.0026, equation (14) gives \(c = 0.0204\) and \(m = 0.0215\), and equation (12)
sends the median to 0.25, a sample of 0.10 to 0.80 and one of 0.50 to 0.98.

## The factor of Colour

With **Channel** at **Colour**, the curve stretches a luminance and the three channels follow it. With \(R\),
\(G\) and \(B\) the channels of a pixel, cut to 0 to 1, and \(w_R\), \(w_G\) and \(w_B\) the three weights divided
by their sum, the luminance is

\[ Y = w_R\,R + w_G\,G + w_B\,B \tag{17} \]

The weights are equal with **Luminance coeffs** at **Default** or **Image**, and the three **R/G/B** boxes with
**Manual**. With all three boxes at zero they are equal again. With \(F\) the curve of the chosen type, the three
channels are multiplied by one factor, \(k\) with **Clip** and \(k'\) with **Rescale**:

\[ k = \frac{F(Y)}{Y}, \qquad k' = \frac{k}{\max\bigl(1,\ k \cdot \max(R, G, B)\bigr)} \tag{18} \]

- **Clip** cuts each multiplied channel at 1 on its own, so the ratios between the colors change where a channel
  reaches 1.
- **Rescale** lowers the factor of a pixel until its brightest channel ends at 1, so the ratios are kept and the
  pixel is brightened less.
- A pixel whose luminance is zero cannot be multiplied. When its three channels are zero too, each of them
  becomes \(F(0)\): black for a stretch, white for **Image Inversion**. When one of them is not zero, which needs a
  weight of zero, the pixel becomes black. Zero means at most \(10^{-12}\) here.
- **Colour clip** and the weights are not recorded in the history.

## Checkpoints of a curve

A curve to check your own reading of equations (1) to (11) against. The type is **Generalized Hyperbolic**, the
stretch factor 1.2528, which gives \(D = 2.5001\), the local intensity 0, **Protect shadows (LP)** 0.10, the
symmetry point 0.35 and **Protect highlights (HP)** 0.90. With **Invert** on and the same values, each result goes
back to its sample.

| Sample | Result | Piece of the curve |
| --- | --- | --- |
| 0 | 0 | The end of the range |
| 0.05 | 0.047488 | The straight line below LP |
| 0.10 | 0.094976 | LP, where the line meets the curve: \(y_L\) |
| 0.20 | 0.202880 | The curve below the symmetry point |
| 0.35 | 0.424834 | The symmetry point: \(y_S\) |
| 0.50 | 0.646787 | The curve above the symmetry point |
| 0.90 | 0.955138 | HP, where the curve meets the line: \(y_H\) |
| 0.95 | 0.977569 | The straight line above HP |
| 1 | 1 | The end of the range |

Neither protection point sends a sample to 0 or to 1.

## Too little and too much

What each pair of values looks like when it is set too low or too high, and the order to set it in. These are
places to start from, not results measured on an image.

| Values | Start | Too little | Too much | Order |
| --- | --- | --- | --- | --- |
| Stretch factor and symmetry point | Local intensity 0, LP 0, HP 1, and the symmetry point where the background meets the faint signal | The histogram and the preview stay close to the image | The tones you want crowd toward one end, and noise shows | The symmetry point, the stretch factor, then the protection points |
| The protection points | LP 0 and HP 1 | The ends look no different from the middle of the curve | Wide ranges of tones become almost straight and lose their separation | The symmetry point, LP, HP, then the stretch factor again |
| Local intensity | 0, then values near it with the other values unchanged | The contrast stays spread over a wide range | The contrast gathers in a narrow band at the symmetry point | The stretch factor and the symmetry point, the local intensity, then the protection points |
| **RGB**, **Lightness** or **Colour** | **RGB** | The colors come out as they do with **RGB** | Saturated colors clip, are rescaled or shift in the conversion | The choice of **Channel**, the curve, then a check of every channel at both ends |
| **Colour clip** | **Rescale** when the ratios between the colors matter | With **Rescale**, bright colors are lifted less | With **Clip**, a channel reaches 1 and the ratios change | The weights, the curve, then **Colour clip** |
| **Linked STF** | On for an image whose colors are balanced | On leaves the channels' backgrounds apart | Off changes the color balance | The choice, then Apply: the derived values cannot be tuned |
| Black and white point of **Linear Stretch** | 0 and 1 | The range is hardly widened | Shadows become exactly 0 and highlights exactly 1 | The black point, the white point, then a count of the clipped samples |

## What the history records

Apply adds one step named **Generalized Hyperbolic Transformation**. Its text states the type, the channel and
seven values, each with four decimals, whatever the type uses of them. The worked example of the process page
records:

```text
Transformation: Generalized Hyperbolic, Channel: RGB, Stretch factor: 2.5000, Local Intensity: 5.0000, Symmetry Point: 0.0250, Protect Shadows: 0.0000, Protect Highlights: 1.0000, Black Point: 0.0000, White Point: 1.0000
```

Transformation
:   `Generalized Hyperbolic`, `Histogram Transformation`, `Arcsinh`, `Linear Stretch`, `Image Inversion` or `STF
    Transformation`. With **Invert** on, the first three are followed by `(Inverse)`.

Channel
:   `RGB`, `Red`, `Green`, `Blue`, `Lightness`, `Saturation` or `Colour`. A grayscale image is recorded as `RGB`.

Stretch factor
:   The value of the box, not the strength \(D\).

Not recorded
:   The [mask](../../reference/glossary.md#mask), **Colour clip**, **Luminance coeffs** and the three weights,
    **Linked STF**, the curve an STF derived, and the choices of the histogram toolbar.

The image itself keeps its header, its acquisition data and its
[plate solution](../../reference/glossary.md#plate-solution). It no longer states a transfer function, and its
[processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
`ght-stretch`, whichever type was applied.
