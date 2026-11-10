---
kind: reference
summary: >-
  The language of Pixel Math: how an expression is written, its operators, images and symbols, every function with its
  arguments and result, the two ways an expression runs, the messages, the settings file and what a run records.
---
# Pixel Math Reference

This page holds what [Pixel Math](pixel-math.md) leaves out to stay readable beside its window: how an expression is
written, its operators in the order they bind, the images and symbols it reads, every function with its arguments
and what it returns, the two ways an expression runs, the messages a refused expression gives, the `.anpm` settings
file and what a run records. Names are case-sensitive everywhere, and each section can be read on its own.

## Writing an expression

Numbers
:   Digits with at most one point, and an optional exponent: `0.25`, `.5`, `3.`, `1e-6`, `2E3`. The point is always
    `.`, whatever the language of the system. A number too large to hold, such as `1e999`, is refused, and so is
    one with letters after its exponent, such as `1e5x`.

Names
:   An image's or a symbol's name starts with an ASCII letter or `_` and goes on with letters, digits and `_`. Case
    counts: `X()` is the normalized column and `x()` the column in pixels.

Calls
:   A name followed by `(`, even after spaces, calls a function: `sqrt($T)` or `sqrt ($T)`. A name without
    parentheses is an image or a symbol, so `pi` is not `pi()`.

Meta symbols
:   `$T`, `$A` and `$M`. Any other `$` is refused.

Channel selectors
:   `NAME[n]` or `$T[n]` reads channel `n` of the image, from 0. `n` is a whole number written as a number, from 0 to
    65535, not an expression.

Spaces and comments
:   Spaces, tabs and new lines between the parts of an expression are ignored. `//` starts a comment that runs to the
    end of the line, and `/* ... */` a comment that may run over several lines.

Statements
:   `;` separates statements, which run from left to right, and the value of the last one is written. A `;` at the
    end changes nothing. Parentheses may hold statements too: `(a = 2; a * $T)` is worth `a * $T`.

Assignments
:   Only a local symbol can be assigned. Its value is kept from one statement to the next, for the sample being
    written only.

Positions in messages
:   A message gives the place of a mistake as a byte, counted from 0 at the first character of the tab that is not a
    space.

### Limits

| What | Most allowed |
| --- | --- |
| Bytes of text in one expression | 65,536 |
| Syntax nodes, each number, name, operator and call | 4,096 |
| Levels of nesting | 64 |
| Arguments of one call | 64 |
| Statements | 256 |
| Distinct names, functions included | 256 |
| Images a run reads, `$T`, `$A` and `$M` included | 16 |

## Operators

The operators bind in this order, the loosest first. Operators of one level group to the left, so `8 - 4 - 2` is 2,
except the assignments and `^`, which group to the right.

1. `;` between statements.
2. The assignments `=`, `+=`, `-=`, `--=`, `*=`, `/=`, `%=` and `^=`.
3. `||`.
4. `&&`.
5. `==` and `!=`.
6. `<`, `<=`, `>` and `>=`.
7. `+`, `-` and `--`, between two values.
8. `*`, `/` and `%`.
9. The signs `+`, `-`, `~` and `!` before a value, which nest: `- - a` is `a`.
10. `^`, whose exponent may carry a sign: `2^-1` is 0.5, `-2^2` is -4 and `2^3^2` is 512.
11. A number, a name, a call, or statements in parentheses.

| Operator | Written | Result |
| --- | --- | --- |
| `+` | `+a`, `a + b` | `a`, or the sum |
| `-` | `-a`, `a - b` | The negative of `a`, or the difference |
| `--` | `a -- b` | The absolute difference \(\lvert a - b \rvert\) |
| `*` | `a * b` | The product |
| `/` | `a / b` | The quotient. A division by 0 gives an infinite value, and `0 / 0` a value that is not a number. |
| `%` | `a % b` | The remainder of `a / b`, with the sign of `a`: `7 % 3` is 1 and `-7 % 3` is -1. Not a number when `b` is 0. |
| `^` | `a ^ b` | `a` to the power `b` |
| `==`, `!=` | `a == b` | 1 when the two are exactly equal, or for `!=` when they are not, otherwise 0 |
| `<`, `<=`, `>`, `>=` | `a < b` | 1 when the comparison holds, otherwise 0. Any comparison with a value that is not a number is 0. |
| `&&` | `a && b` | 1 when both are not 0, otherwise 0. Both sides are evaluated. |
| `\|\|` | `a \|\| b` | 1 when either is not 0, otherwise 0. Both sides are evaluated. |
| `!` | `!a` | 1 when `a` is 0, otherwise 0 |
| `~` | `~a` | \(1 - a\), the complement of a fraction |
| `=` | `v = a` | Gives the local symbol `v` the value `a`, and is worth `a` |
| `+=`, `-=`, `--=`, `*=`, `/=`, `%=`, `^=` | `v += a` | Gives `v` the value of `v + a`, or of `v` and `a` through the other operator, and is worth it |
| `;` | `a; b` | Evaluates `a`, then `b`, and is worth `b` |
| `//`, `/* */` | `// note` | A comment, which is ignored |

Any value other than 0, a value that is not a number included, counts as true in `&&`, `||`, `!` and `iif`.

## Images and symbols

| Written | Reads |
| --- | --- |
| `$T` | The image being written: the target, or the frame of its [sequence](../../reference/glossary.md#sequence) being worked, at the result's size and in the result's channels |
| The target's view name | The same image as `$T` |
| `$A` | The image of the view that was active when you pressed **Apply**, or `$T` when that view is the target or no view is active |
| `$M` | The image the target's view uses as its [mask](../../reference/glossary.md#mask), enabled or not, as its samples are: **Invert Mask** does not turn it round. Refused when the view has no mask. |
| `NAME` | The image the view of that name showed when you pressed **Apply**: for a sequence, the frame on show |
| `NAME[n]`, `$T[n]` | Channel `n` of that image, from 0 |
| A local symbol | A variable, which every sample starts at its stated value |
| A global symbol | A constant, which no expression can change |

- Only the images the expressions name are read, `$A` and `$M` included.
- An image of another width or height than the result is resampled to the result's size with Lanczos-4, in its own
  [pixel type](../../reference/glossary.md#pixel-type), before it is read. Positions, `w()`, `h()`, `pixel()` and
  the statistics then see the resampled image.
- Each image is read as a [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), in one working
  copy for the whole run: 64-bit floating point when the target, the result or an image the expressions read is
  64-bit floating point or a 32-bit integer, and 32-bit floating point otherwise.
- Read at the sample being written, an image of one channel gives that channel to every channel of the result, and
  an image with the result's channels gives the channel being written. Any other image is refused unless the
  expression names one of its channels.
- An image must be the same kind of number as the target, as its
  [numeric domain](../../reference/glossary.md#numeric-domain) states. Fractions of full scale are read together
  whether they are linear or encoded, and any other domain must be the target's own.
- A symbol hides an open image of the same name, the target's included. A view named like a function, such as `x`,
  cannot be read: "the image 'x' cannot be named in an expression: the name an image was bound to belongs to the
  language".
- A symbol takes no channel selector: `a[1]` is refused with `channel 1 of 'a' at byte <n> does not exist; it has 0`.

### The symbol lists

- Each list is split at its commas, and each entry at its `=`. Spaces around the name and the value are dropped,
  and an entry without a name is ignored.
- The value is read from the number it starts with: `0.5x` reads 0.5. A value that starts with no number, an entry
  with no `=`, and an entry with two, declare 0.
- A name declared twice in one list takes the later value. A name in both lists is refused.
- A value that is not finite, such as `inf`, is refused: "a constant was declared with a value that is not finite".
- A name the language owns, every function's and `$T`, `$A` and `$M`, is refused: "a constant was declared with an
  empty name or one the language owns".

## Functions

A function is called with its arguments in parentheses, separated by commas. Angles are in radians. A value outside
a function's domain gives a value that is not a number, or an infinite one, as floating-point arithmetic does, and
nothing repairs it. An argument written `IMG` takes an image name, `$T`, `$A` or `$M`. A channel selector on it, such
as `med(SRC[1])`, is not used.

### Constants

| Function | Returns |
| --- | --- |
| `e()` | 2.718281828459045, the base of the natural logarithm |
| `pi()` | 3.141592653589793 |

### Functions of a number

| Function | Also written | Returns |
| --- | --- | --- |
| `abs(x)` | | The magnitude of `x` |
| `sign(x)` | | -1, 0 or 1, and 0 for a value that is not a number |
| `sqrt(x)` | | The square root. Not a number below 0. |
| `exp(x)` | | \(e^{x}\) |
| `ln(x)` | | The natural logarithm. Minus infinity at 0, not a number below 0. |
| `log(x)` | `log10(x)` | The base-10 logarithm |
| `log2(x)` | | The base-2 logarithm |
| `sin(x)`, `cos(x)`, `tan(x)` | | The sine, cosine and tangent of an angle in radians |
| `asin(x)`, `acos(x)` | | The arc sine and arc cosine, in radians. Not a number outside -1 to 1. |
| `atan(x)` | | The arc tangent, in radians from \(-\pi/2\) to \(\pi/2\) |
| `atan2(y, x)` | | The angle of the point \((x, y)\), in radians from \(-\pi\) to \(\pi\) |
| `sinh(x)`, `cosh(x)`, `tanh(x)` | | The hyperbolic sine, cosine and tangent |
| `asinh(x)`, `acosh(x)`, `atanh(x)` | | Their inverses. `acosh` is not a number below 1, and `atanh` is infinite at -1 and 1. |
| `ceil(x)` | | The smallest whole number at or above `x` |
| `floor(x)` | | The largest whole number at or below `x` |
| `trunc(x)` | | `x` without its fraction, toward 0 |
| `clamp(x, a, b)` | `range(x, a, b)` | `x` held between `a` and `b`. With `a` above `b`, it is `a`. |
| `mtf(m, x)` | | The [midtones transfer function](../../reference/glossary.md#midtones-transfer-function) of balance `m`, equation (1) |
| `fac(x)` | | The factorial of `x` cut to a whole number. Not a number below 0, infinite above 170. |
| `ncr(n, r)` | | The number of ways to choose `r` of `n`, both cut to whole numbers. Not a number when either is below 0 or `r` is above `n`, and infinite when it overflows. |
| `npr(n, r)` | | The number of ordered ways: `ncr(n, r) * fac(r)` |

With \(m\) the balance and \(x\) the value,

\[ \mathrm{mtf}(m, x) = \frac{(m - 1)\,x}{(2m - 1)\,x - m} \tag{1} \]

It sends 0 to 0, 1 to 1 and \(m\) to 0.5, and holds neither argument to 0 to 1, so clamp \(x\) first when it may
leave that range. The balance comes first.

### Functions of a list of values

Each takes from two arguments, three for `med`, up to 64, and computes over them for the sample being written.

| Function | Also written | Returns |
| --- | --- | --- |
| `max(a, b, ...)` | | The largest |
| `min(a, b, ...)` | | The smallest |
| `sum(a, b, ...)` | | The sum |
| `mean(a, b, ...)` | | The arithmetic mean |
| `med(a, b, c, ...)` | | The median: the middle value, or the mean of the two middle values of an even count |
| `var(a, b, ...)` | | The population variance: the mean of the squared distances from the mean |
| `sdev(a, b, ...)` | | The population standard deviation, the square root of `var` |
| `asqr(a, b, ...)` | | The mean of the squares |
| `ssqr(a, b, ...)` | | The sum of the squares |
| `mad(a, b, ...)` | `mdev(a, b, ...)` | The [median absolute deviation](../../reference/glossary.md#median-absolute-deviation): the median of the distances from the median, not scaled |
| `bwmv(a, b, ...)` | | The biweight midvariance, equation (2) |

With \(n\) values \(v_i\), their median \(\tilde v\) and their median absolute deviation \(M\), each value has the
weight \(u_i = (v_i - \tilde v) / (9M)\), and only the values with \(\lvert u_i \rvert < 1\) enter the sums:

\[ \mathrm{bwmv} = n\,\frac{\sum (v_i - \tilde v)^2\,(1 - u_i^2)^4}{\Bigl(\sum (1 - u_i^2)(1 - 5u_i^2)\Bigr)^2} \tag{2} \]

It is 0 when \(M\) is 0.

### Statistics of a whole image

`min`, `max`, `mean`, `med`, `mad`, `mdev`, `sdev`, `var`, `asqr`, `ssqr` and `bwmv` called with exactly one argument
that names an image, and `adev` and `noise`, compute a statistic of the whole image instead.

- The statistic is computed once, before the first sample, for each channel of the result: of the image's channel
  of that number, or of its only channel. So `med($T)` is the red median in the red channel and the green median
  in the green one.
- It counts the samples the run selects: those the target's
  [validity rule](../../reference/glossary.md#validity-rule) keeps when the result has the target's size and
  channels, and every sample otherwise.
- It is of the image as the run reads it: resampled to the result's size, as fractions of full scale.
- An image of fewer channels than the result, other than one, is refused for the channels it lacks: "a whole-image
  statistic was asked of a channel the image does not have". A grayscale result reads the first channel of a color
  image.
- An expression with a statistic goes through the evaluator.

| Function | Also written | Returns |
| --- | --- | --- |
| `min(IMG)` | | The smallest sample |
| `max(IMG)` | | The largest sample |
| `mean(IMG)` | | The mean |
| `med(IMG)` | | The median |
| `mad(IMG)` | `mdev(IMG)` | The median absolute deviation from the median, not scaled: multiply it by 1.4826 for the deviation of a normal distribution |
| `sdev(IMG)` | | The population standard deviation |
| `var(IMG)` | | The population variance |
| `adev(IMG)` | | The mean distance from the mean |
| `asqr(IMG)` | | The mean of the squared samples |
| `ssqr(IMG)` | | The sum of the squared samples |
| `bwmv(IMG)` | | The biweight midvariance around the median, equation (2) over the samples |
| `noise(IMG)` | | An estimate of the background noise, below |

`noise` takes, along each row, the differences between neighboring samples, skipping invalid ones. It computes the
population standard deviation of each row's differences, after up to three passes that drop the differences more than
five deviations away. It returns the median of those deviations over the rows that hold at least two differences,
divided by \(\sqrt{2}\), or 0 when no row holds two.

### Facts about an image

Each takes an image, or nothing for `$T`, and is worked out once, before the first sample.

| Function | Also written | Returns |
| --- | --- | --- |
| `w(IMG)` | `width(IMG)` | The width the image is read at, which is the result's: every image is resampled to it |
| `h(IMG)` | `height(IMG)` | The height the image is read at, which is the result's |
| `n(IMG)` | `channels(IMG)` | The image's number of channels. For `$T`, the result's. |
| `iscolor(IMG)` | | 1 for an image of three channels or more, otherwise 0 |

### Color components

Each takes an image of three channels or more, or nothing for `$T`, and reads its first three channels at the pixel
being written, the same for every channel of the result. The red, green and blue are read as linear, with sRGB
primaries and a D65 white, or through the sRGB curve when the image's numeric domain says its fractions are encoded.
An image of fewer channels is refused: `the colour component 'CIEL' at byte <n> needs an image with three colour
channels`.

| Function | Returns |
| --- | --- |
| `CIEL(IMG)` | The [CIE Lab](../../reference/glossary.md#cie-lab) lightness \(L^*\), divided by 100 |
| `CIEa(IMG)`, `CIEb(IMG)` | \(a^*\) and \(b^*\), each plus 128 and divided by 255, so a neutral gray is near 0.5 |
| `CIEc(IMG)` | The chroma \(C^*\), divided by 200 |
| `CIEh(IMG)` | The hue angle \(h\) from -180 to 180 degrees, as \((h + 180) / 360\) |
| `CIEX(IMG)`, `CIEY(IMG)`, `CIEZ(IMG)` | The tristimulus values, with the D65 white at a luminance \(Y\) of 1 |
| `HSLh(IMG)`, `HSLs(IMG)`, `HSLl(IMG)` | The [HSL](../../reference/glossary.md#hsl) hue as a fraction of a turn, the saturation and the lightness |
| `HSVh(IMG)`, `HSVs(IMG)`, `HSVv(IMG)` | The [HSV](../../reference/glossary.md#hsv) hue as a fraction of a turn, the saturation and the value |
| `YCbCrY(IMG)` | The [YCbCr](../../reference/glossary.md#ycbcr) luma, with the BT.601 weights at full range |
| `YCbCrCb(IMG)`, `YCbCrCr(IMG)` | The blue and the red chroma, centered on 0.5 |

### Position

With \(W\) and \(H\) the result's width and height:

| Function | Also written | Returns |
| --- | --- | --- |
| `x()` | `xpos()` | The column being written, from 0 |
| `y()` | `ypos()` | The row being written, from 0 |
| `X()` | | \(x / (W - 1)\): 0 at the first column and 1 at the last, and 0 when the result is one column wide |
| `Y()` | | \(y / (H - 1)\), likewise for the rows |
| `c()` | `channel()` | The channel being written: 0 for gray or red, 1 for green, 2 for blue and 3 for alpha |

### Region tests

`inrect(x0, y0, w, h)`
:   1 when the column is from `x0` to `x0 + w` and the row from `y0` to `y0 + h`, both edges included, and 0
    elsewhere. The four numbers are cut toward 0 first, so a width of `w` spans `w + 1` columns. A number that is not
    finite holds no pixel.

`inellipse(xc, yc, rx, ry)`
:   1 where \(\bigl((x - x_c)/r_x\bigr)^2 + \bigl((y - y_c)/r_y\bigr)^2 \le 1\) at the column \(x\) and the row \(y\)
    being written, and 0 elsewhere. The numbers are not cut.

For a result of 3 by 2 pixels and an open image `SRC` of the same size:

```text
iif(inrect(0, 0, 1, 0), pixel(SRC, x(), y()), X())
```

The rectangle holds columns 0 and 1 of row 0, which take `SRC`'s samples. At column 2 of row 0 the result is `X()`,
which is 1, and at column 1 of row 1 it is 0.5. `inellipse(1, 0, 1, 1)` is 1 along row 0 and at column 1 of row 1,
and 0 at columns 0 and 2 of row 1.

### Reading a stated sample

`pixel(IMG, x, y)` or `pixel(IMG, x, y, c)`, also written `p`
:   The sample of the image at column `x` and row `y`, without interpolation, the numbers cut toward 0. Without `c`,
    the channel being written, or the only channel of a one-channel image. It reads 0 for a position that is
    negative, not finite or outside the image, and for a channel the image does not have.

### The conditional

`iif(test, a, b)`
:   `a` when `test` is not 0, and `b` when it is. Only the chosen one is evaluated, so `iif(SRC != 0, $T / SRC, 0)`
    never divides by 0. A `test` that is not a number chooses `a`.

### Random numbers

`random()`, also written `rand()`
:   A number drawn evenly from 0 up to, not including, 1. Each sample, each channel and each call in an expression
    draws its own. Every Apply draws a new seed, which the Session Console line of the run and the
    [lineage](../../reference/glossary.md#lineage) of the result give, and every frame of a sequence draws from a
    seed of its own. One seed gives the same image at any number of threads.

## How an expression runs

An expression runs one of two ways, and the lineage of the result says which.

A fused pass
:   An expression that is a chain runs as one pass over the image, with compiled arithmetic: one image, then steps
    that each combine the running value with a number or with another whole image. `$T * 0.5 + 0.25`, `$T - SRC`,
    `$T / 4`, `max($T, SRC)` and `clamp($T, 0, 1)` are chains. The chain computes in the precision of the working
    copy, and rounds after each step.

The evaluator
:   Every other expression is evaluated in double precision: a run of samples at a time, or one sample at a time
    when it branches with `iif`. These stop a chain: a function other than `min` or `max` of two images and `clamp`
    between two numbers, a number on the left of `-`, a nested part on the right, such as `$T * (SRC + 1)`, `min` or
    `max` with a number, a remainder, `--`, a comparison with a number, a logical operator, a division by a number
    other than a power of two, a channel selector, a position, a statistic, a color component, `pixel`, `iif`,
    `random`, a variable, a second statement, and an expression that reads no image.

- The two ways compute the same functions in the same order. A chain on a single-precision working copy rounds each
  step to single precision, and its powers may use a vector approximation. Where one argument of `min` or `max` is
  not a number, a chain returns the other argument, and the evaluator returns the first.
- A statistic costs passes over its image before the first sample. Every image the expressions read costs a working
  copy at the result's size, and a sequence repeats the work for every frame.
- With one expression per color channel, a chain runs over one channel at a time.

## Messages

Every refusal is shown under "Error running PixelMath", after "An error occurred when executing the PixelMath
process:", and written to the log. A reason about an expression starts with its tab, `RGB/K`, `R`, `G`, `B` or `A`,
and the part of the expression it is about: `RGB/K expression at 'NAME': nothing is bound to the name 'NAME' at byte
<n>`. Below, the reasons follow the tab's part.

### Mistakes in the writing

These are found before any image is read, in every tab at once, and only the first mistake of each statement is
given.

`a block comment opened at byte <n> was never closed`
:   A `/*` has no `*/` after it.

`the character '<c>' at byte <n> begins no token of this language`
:   A character the language does not use, such as `#`, a single `&` or `|`, or a `$` that is not `$T`, `$A` or `$M`.

`the number '<text>' at byte <n> is not written as one`
:   Letters after an exponent, such as `1e5x`.

`the number '<text>' at byte <n> is outside what this engine can hold`
:   A number too large for a double, such as `1e999`.

`the token '<text>' at byte <n> cannot appear there`
:   Two values without an operator between them, an operator without its value, a `)` without its `(`, or `()`
    around nothing. An operator missing its value at the end reads `the token at byte <n> cannot appear there`.

`a parenthesis at byte <n> has no partner`
:   A `(` without its `)`.

`a channel selector opened at byte <n> was never closed`
:   A `[` without its `]`.

`the channel selector after '<text>' at byte <n> is not a whole number`
:   A selector that is not a whole number from 0 to 65535, or not a number at all, such as `SRC[c()]`.

`the assignment at byte <n> writes to '<name>', which is not a declared variable`
:   An assignment to something other than a local symbol. One to `$T` or a call is found here, and one to an image
    or a global symbol after the images are read.

`the source holds no expression`
:   The tab holds only a comment.

`the expression is <n> bytes long and the limit is 65536`, `the expression uses more than 256 distinct names`, `the expression needs more than 4096 syntax nodes`, `the expression nests deeper at byte <n> than the limit of 64`, `the call '<name>' at byte <n> has more than 64 arguments`, `the source holds more than 256 statements`
:   The expression passes one of the limits above.

### Names, calls and images

These are found after the images are read. The first expression with one stops the run, with all its reasons. In a
reason about a call's image, the name is the function's: `the image 'med' at byte <n> ...`.

`nothing is bound to the name '<name>' at byte <n>`
:   The name is neither an open view's nor a declared symbol, or a function was written without parentheses.

`this language has no function called '<name>' at byte <n>`
:   No function has that name, with that case.

`the call '<name>' at byte <n> was given <k> arguments and takes <m>`
:   Too few or too many arguments, such as `med(a, b)` or `atan2(1)`.

`the first argument of '<name>' at byte <n> has to name an image`
:   An image function, such as `adev` or `pixel`, was given a number in place of an image.

`channel <k> of '<name>' at byte <n> does not exist; it has <m>`
:   The channel selector names a channel the image does not have, or follows a symbol.

`the image '<name>' at byte <n> means a different kind of number from the others in this expression`
:   The image's numeric domain is not the target's: fractions beside [sensor codes](../../reference/glossary.md#sensor-code)
    or a [physical quantity](../../reference/glossary.md#physical-quantity).

`the image '<name>' at byte <n> does not have the shape of the image being written`
:   The image's channels are neither one nor the result's, and the expression names none of them.

`the colour component '<name>' at byte <n> needs an image with three colour channels`
:   A color component reads an image of one or two channels.

`the expression names more than 16 images`
:   The expression reads too many images.

### The window's own refusals

"The pixel math expression is empty"
:   Every expression the result uses is blank.

"$M reads the target's mask, and the target has none"
:   An expression reads `$M`, and no mask is attached to the target's view. Found before any image is read.

`the local symbol '<name>' cannot be declared: <reason>`, `the global symbol '<name>' cannot be declared: <reason>`
:   The reason is "a constant was declared with an empty name or one the language owns", "a symbol is already
    declared with that name" for a name in both lists, or "a constant was declared with a value that is not finite".

`the image '<name>' cannot be named in an expression: <reason>`
:   The reason is "the name an image was bound to belongs to the language" for a view named like a function, or
    "more images were bound than one expression may name" past 16 images.

`a new image of <width> by <height> pixels and <n> channels cannot be made`
:   **Create new image** asks for a size the engine cannot hold.

`a new image of UInt16 samples cannot hold what the target's samples mean`
:   **Color format** asks for an integer type, here 16 bits, for a target of physical quantities.

"a whole-image statistic was asked of a channel the image does not have"
:   A statistic reads an image of fewer channels than the result, other than one.

### The settings file and the editor

`Could NOT parse pixel math file: <path>. Error: '<reason>'`
:   Under "Error importing pixel math script": the file cannot be opened or read as XML. The reason is also
    'No pixel_math node found', 'No global_settings node found' or 'No output_settings node found' for a file that
    lacks one of them.

`Could NOT parse pixel math file: <path>. Version is not compatible`
:   Under "Error importing pixel math script": the file is not version 1.

`the pixel math file <path> cannot be written: <reason>`
:   Under "Error exporting pixel math script": the folder or the file refuses the write.

"WARNING: Target image doesn't have any mask applied"
:   The tooltip over `$M` when the target's view has no mask.

## The settings file

**Export** writes, and **Import** reads, an XML file of the extension `.anpm`. It holds no image.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<pixel_math version="1">
	<global_settings single_expr="true" single_threaded="false" process_64_bits="true" optimize="true" rescale="false" rescale_low="0" rescale_high="1"/>
	<output_settings output_mode="1" new_image_id="STRETCHED" width="0" height="0" color_space="0" color_format="0"/>
	<expression0>mtf(0.02, clamp(($T - 0.02) / 0.98, 0, 1))</expression0>
	<expression1></expression1>
	<expression2></expression2>
	<expression3></expression3>
	<local_symbols></local_symbols>
	<global_symbols></global_symbols>
</pixel_math>
```

| Attribute | Of | Holds | When left out |
| --- | --- | --- | --- |
| `single_expr` | `global_settings` | **Use a single RGB/K expression** | On |
| `single_threaded` | `global_settings` | **Single threaded** | Off |
| `process_64_bits` | `global_settings` | **Process in 64 bits**, written and never read | On |
| `optimize` | `global_settings` | **Code optimization** | On |
| `rescale` | `global_settings` | **Rescale result** | Off |
| `rescale_low`, `rescale_high` | `global_settings` | **Lower bound** and **Upper bound** | 0 and 1 |
| `output_mode` | `output_settings` | 0 for **Replace target image**, 1 for **Create new image** | 0 |
| `new_image_id` | `output_settings` | **New Image Id** | `PIXEL_MATH_IMAGE` |
| `width`, `height` | `output_settings` | **Width** and **Height** | 0 |
| `color_space` | `output_settings` | 0 As target, 1 Grayscale, 2 RGB, 3 RGBA | 0 |
| `color_format` | `output_settings` | 0 As target, 1 8 bits, 2 16 bits, 3 32 bits int, 4 32 bits float, 5 64 bits float | 0 |

- `expression0` holds **RGB/K**, or **R**, `expression1` **G**, `expression2` **B** and `expression3` **A**. An
  element left out reads as empty, and so do `local_symbols` and `global_symbols`.
- A file without `pixel_math` of version 1, `global_settings` and `output_settings` is refused.
- A switch reads On when its value starts with `1`, `t`, `T`, `y` or `Y`, and Off otherwise, an empty value
  included. A number that cannot be read reads 0, and a choice outside its list reads the first choice.
- **Export** writes the numbers to 17 significant digits, the switches as `true` or `false`, and every element even
  when it is empty.

## What a run records

- With **Replace target image**, the target's [history](../../reference/glossary.md#history) gets one step named
  **PixelMath**, without a description, which undoes the run. A new image's history begins with "Image created from
  PixelMath script", and a new sequence's with "Sequence created from PixelMath script".
- The result's [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the
  operation `pixelmath`, whatever the expression. An image described as linear is no longer described so, and an
  encoding it stated is kept.
- Its lineage gains an entry naming the fused pass or the evaluator, with each expression, the channels it wrote and
  the way it ran, the seed and, with **Rescale result**, the bounds:
  `channels 0,1,2 'mtf(0.02, clamp(($T - 0.02) / 0.98, 0, 1))' evaluated; seed 1234567890123456789`.
- A replaced target keeps its size, channels and pixel type, every
  [header card](../../reference/glossary.md#header-card), its
  [plate solution](../../reference/glossary.md#plate-solution), its acquisition data and its validity.
- A new image keeps the target's acquisition data and lineage, its plate solution only at the target's width and
  height, and no header card but those of the processing state. It keeps the target's validity only at the target's
  size and channels, and is all valid otherwise. It keeps a [CFA](../../reference/glossary.md#cfa) mosaic only as a
  grayscale image of the target's own size.

The [Session Console](../../interface/log-viewer.md) keeps these lines of a run:

| Line | When |
| --- | --- |
| `Executing process 'PixelMath' on image 'RGB' with random seed <seed>...` | Apply starts a run. On a sequence, the line says `sequence`. |
| `Process 'PixelMath' executed successfully on 'RGB'` | The result is shown or opened. |
| `PixelMath: sequence '<name>'` | A sequence has been replaced. |
| `Image created from PixelMath script into new image '<name>'` | A new image has opened. A new sequence's line says `Sequence` and `new sequence`. |
| `An error occurred when executing the PixelMath process:` and the reason | A run was refused or failed. |
| `PixelMathProcess: Imported expression from file <path>`, or `Exported expression to file <path>` | An Import or an Export succeeded. |
