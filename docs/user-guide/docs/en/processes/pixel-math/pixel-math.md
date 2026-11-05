# Pixel Math

<!-- MEDIA_SLOT: UG-MEDIA-096 -->

## Purpose and expected result

Pixel Math evaluates a small expression language once for every output sample. Use
it for arithmetic between open images, coordinate-generated images, explicit
mask equations, channel-specific transforms, and compact formulas that do not
have a dedicated process.

The expected result is either a replacement of the selected target or a new
still or sequence. Every sample is read as a fraction of full scale, the result
is what the expression computes - **it is not clamped to `0..1`** - and it is
narrowed once to the output's representation, saturating an integer at its range.

Pixel Math does not provide a preview or formula-validation action. Prefer
**Create new image** while developing an expression, then compare the result
before replacing a source.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Pixel Math > Pixel Math** |
| Enabled when | An image or sequence view is active. Only one Pixel Math window may be attached to a target, and an exclusive process on that target blocks launch. |
| Inputs | Target still or sequence, any open image by its view name, the active image, the target's mask, numeric literals, and user symbols |
| Outputs | Replaced target, new image, replaced sequence, or new sequence; no auxiliary files except optional `.anpm` settings |
| Recommended image state | Use linear inputs for signal arithmetic when appropriate; every Pixel Math result is recorded nonlinear whatever the formula. |
| Preview support | None. **Apply** performs the full image or sequence operation. |
| Sequence support | Yes. `$T` follows the target's frames; other named images, `$A` and `$M` are the images their views showed when Apply was pressed. |
| Mask support | Explicit only through `$M`. An attached mask is not blended automatically. |
| External dependencies | None |

## Before you start

1. Open every image the expression will reference. Image identifiers are the
   case-sensitive view names shown in the **Symbols** list.
2. Select the intended **Target Image**. `$T` and the target view's own name mean
   the image being written; `$A` means the active view when Apply is pressed;
   `$M` exists only when the target has a mask.
3. Rename views whose names are not ordinary identifiers. Expression identifiers
   start with an ASCII letter or `_` and continue with letters, digits, or `_`.
4. Choose **Create new image** while testing. **Replace target image** changes a
   still or replaces its sequence and adds one Image History step.
5. Decide how mismatched geometry should be handled. Pixel Math resizes every
   image an expression names to the output's size with Lanczos-4; it does not
   register, align, or photometrically match them.
6. Remove CFA interpretation before formulas that assume ordinary grey or RGB
   channels.

## Procedure

1. Open **Process > Pixel Math > Pixel Math** on the target.
2. Enter the formula in **RGB/K**. For an RGB or RGBA output, clear **Use a
   single RGB/K expression** only when R, G, and B need different programs. A
   blank per-channel program keeps that target channel. Alpha alone can be changed
   in either mode by leaving the colour programs blank.
3. If needed, enter comma-separated initial values in **Local Symbols** or
   **Global Symbols**. Open **Summary** to inspect the assembled scripts and
   symbol text.
4. Choose **Create new image**, set the identifier, dimensions, colour space, and
   colour format.
5. Double-click a Functions, Operators, or Symbols row to insert its text into
   the active expression editor.
6. Select **Apply**. The dialog is read at that moment; changing it while the run
   works does not change the run. A sequence shows a progress dialog whose
   **Cancel** stops the run and leaves the sequence as it was.
7. Check the output dimensions, channels, type, extrema, and Image History.
8. Use **Export** to save all expressions, symbols, and settings as version-1
   `.anpm` XML. Export does not compile or validate the formula.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RGB/K / R expression | Script editor | Empty; **RGB/K** | Imported from `.anpm` | Grammar documented below | One program serves grey or R/G/B; in separate mode this tab becomes R | **Use a single RGB/K expression** controls label and sharing | A chain runs as one fused pass; anything else is interpreted per sample | Full Apply; grey or R/all RGB channels; still/sequence | Process-window lifetime; `.anpm` `expression0` |
| G expression | Script editor | Empty; disabled | Imported from `.anpm` | Same grammar | Replaces only G; a blank program keeps target G | Enabled when single-expression mode is off and the output has colour channels | Separate compilation | Apply G channel; still/sequence | Window; `.anpm` `expression1` |
| B expression | Script editor | Empty; disabled | Imported from `.anpm` | Same grammar | Replaces only B; a blank program keeps target B | Same dependency as G | Separate compilation | Apply B channel; still/sequence | Window; `.anpm` `expression2` |
| A expression | Script editor | Empty; disabled | Imported from `.anpm` | Same grammar | Replaces A of a four-channel output; blank keeps target A | Enabled only while the output has an alpha channel: a new **RGBA** image, or a replaced RGBA target; over a grey or RGB target the tab is disabled from the moment the window opens | An alpha channel the target did not have starts at `0`, so enter `1` for opaque output | Apply alpha only; still/sequence | Window; `.anpm` `expression3` |
| Functions | Read-only insertion list | One row per function name | None | The engine's function table, each name once | Double-click inserts `name() `; the expression editors highlight every name in the table | One list beside each channel editor | Does not validate arguments | Authoring only | None |
| Operators | Read-only insertion list | One row per operator | None | Full inventory below | Double-click inserts the operator and a space | Beside each channel editor | None | Authoring only | None |
| Symbols | Read-only insertion list | Dynamic | None | `$T`, `$A`, `$M`, local and global symbols, open image names | Double-click inserts the name | Every tab's list is filled when the window opens | A missing image or mask is refused when Apply runs | Authoring only | Rebuilt during the window session |
| Local Symbols (Comma separated list) | Script text | Empty | Imported from `.anpm` | `name` or `name=value`, comma-separated | Declares variables every pixel starts from their stated value | Names must not be function names or another symbol's | A malformed value reads as `0` | All channel programs; still/sequence | Window; `.anpm` `local_symbols` |
| Global Symbols (Comma separated list) | Script text | Empty | Imported from `.anpm` | Same syntax | Declares constants for the whole run | An expression that assigns one is refused | Constants cost nothing per pixel | All channel programs; still/sequence | Window; `.anpm` `global_symbols` |
| Summary | Read-only generated script view | Empty | None | Current expression and symbol text | Rebuilds when the tab is opened | Includes enabled channel sections plus symbol text | Inspection only | Authoring only | None |
| Use a single RGB/K expression | Checkbox | On | Imported `.anpm` value | On/off | On applies RGB/K to every colour channel; off exposes R/G/B scripts | Forced on for a grey output | Separate programs are compiled separately | Apply channel routing | Window; `.anpm` `single_expr` |
| Import | Button/file chooser | No file | Folder chooser remembers its last Pixel Math folder | `.anpm`, version `1` | Replaces expressions, symbols, and settings | Confirmation appears only when an expression editor is nonempty | An invalid file or version is reported and changes nothing | Authoring/settings | Reads `.anpm` |
| Export | Button/file chooser | No file | Folder chooser remembers its last Pixel Math folder | `.anpm` XML | Saves current expressions, symbols, and settings | Always enabled | A failed write is reported | Authoring/settings | Writes `.anpm` version 1 |
| Single threaded | Checkbox | Off | A missing imported attribute reads as Off, the window's default | On/off | No effect on the result: every run computes the same image at any number of threads | None | None | Full Apply | Window; `.anpm` `single_threaded` |
| Process in 64 bits | Checkbox | On; disabled | Import always leaves it on | On only | States that every expression is evaluated in double precision | Always disabled | None | Full Apply | Export writes `.anpm` `process_64_bits`; import ignores it |
| Code optimization | Checkbox | On | A missing imported attribute reads as On, the window's default | On/off | No effect: every expression is compiled once and a chain is always fused | None | None | Full Apply | Window; `.anpm` `optimize` |
| Rescale result | Checkbox | Off | A missing imported attribute reads as Off, the window's default | On/off | On maps the smallest and largest finite result of all channels to the bounds | Enables Lower/Upper bound | One extra pass over the result; a result with no range is left as it is | After evaluation; all channels together; still/sequence | Window; `.anpm` `rescale` |
| Lower bound | Normalized float | `0`; disabled | Imported from `.anpm` | `0..1`, 8 decimals, step `0.01` | The value the smallest result becomes | Enabled by **Rescale result** | May reverse contrast when above Upper bound | Post-evaluation; all channels | Window; `.anpm` `rescale_low` |
| Upper bound | Normalized float | `1`; disabled | Imported from `.anpm` | `0..1`, 8 decimals, step `0.01` | The value the largest result becomes | Enabled by **Rescale result** | May compress or reverse range | Post-evaluation; all channels | Window; `.anpm` `rescale_high` |
| Replace target image / Create new image | Selector | **Replace target image** | Imported index | Two choices | Replace changes the selected view; Create opens a new still or sequence | Create enables identity, geometry and format controls | Create is safer for testing | Still/sequence output | Window; `.anpm` `output_mode` |
| New Image Id | Text | `PIXEL_MATH_IMAGE`; disabled | Imported from `.anpm` | Any text; blank uses `PIXEL_MATH_IMAGE` for a still or `PIXEL_MATH_SEQUENCE` for a sequence | Names the created view | Create only | None | Create output | Window; `.anpm` `new_image_id` |
| Width | Integer pixels | `0`; disabled | Imported from `.anpm` | `0..12000`; `0` means target width | Sets output width, to which every image read is resampled | Create only | Larger output costs more per sample | Create output; still/sequence | Window; `.anpm` `width` |
| Height | Integer pixels | `0`; disabled | Imported from `.anpm` | `0..12000`; `0` means target height | Sets output height | Create only | Same as Width | Create output; still/sequence | Window; `.anpm` `height` |
| Color space | Selector | **As target**; disabled | Imported from `.anpm` | As target, Grayscale, RGB, RGBA | Selects the new image's channels; `$T` is converted to them | Create only; a replaced target keeps its own channels | Conversion is not colour calibration | Create output | Window; `.anpm` `color_space` |
| Color format | Selector | **As target**; disabled | Imported from `.anpm` | As target, 8 bits, 16 bits, 32 bits int, 32 bits float, 64 bits float | Selects the new image's representation | Create only; a replaced target keeps its own | Integer formats quantize and saturate | Create output | Window; `.anpm` `color_format` |
| Target Image | Image/sequence selector | Launch target | Current process-window selection | Open views accepted | Changes `$T`, `$M`, the replace destination, and default geometry/type | Process must remain attached to a valid view | None | Full Apply | Process-window lifetime |
| Apply | Dialog action | Enabled | None | One execution at a time | Reads the dialog and the open images, then runs | Disabled until the run lands | Full-resolution work | Still/sequence | None |
| Close | Dialog action | Enabled | None | Close window | Closes when idle; during a run the window hides and closes when the run lands | Re-enabled after the run | None | Process window | None |
| Cancel | Progress action | Shown for a sequence | None | One request | Stops the run; nothing is published | Sequence progress dialog | No partial sequence is left | Running sequence | None |

## Expression language

### Lexical rules

- Numbers are invariant decimal literals: digits with at most one `.`, and an
  optional signed exponent (`1e-6` is accepted).
- Identifiers start with an ASCII letter or `_` and continue with letters,
  digits, or `_`. Case is significant: `X()` is the normalized column while `x()`
  is the integer column.
- Whitespace is ignored. `//` starts a comment through the end of the line;
  `/* ... */` is a block comment.
- A semicolon separates statements. The value of the final statement is written
  to the output sample.
- Syntax errors in every tab are reported together before any image is read,
  each under the tab it was written in and at the text it is about; only the
  first error of each statement is reported. Unknown names, wrong argument
  counts and other binding errors are found after the images are read, and the
  first expression with one stops the run.

### Operators

`^` binds tighter than `*`, `/` and `%` and groups to the right: `2 * 3 ^ 2` is
`18` and `2 ^ 3 ^ 2` is `512`. Prefix operators nest: `- - x` is `x`. `&&` and
`||` evaluate both operands; only `iif()` evaluates one branch.

| Operator | Arity | Result |
| --- | --- | --- |
| `+` | Unary/binary | Identity, or addition |
| `-` | Unary/binary | Negation, or subtraction |
| `--` | Binary | Absolute difference, `abs(a-b)` |
| `*` | Binary | Multiplication |
| `/` | Binary | Floating-point division |
| `%` | Binary | Floating remainder with the sign of `a` |
| `^` | Binary | `pow(a,b)` |
| `==`, `!=` | Binary | Exact comparison returning `1` or `0` |
| `<`, `<=`, `>`, `>=` | Binary | Comparison returning `1` or `0` |
| `&&`, `\|\|` | Binary | Nonzero logical AND/OR returning `1` or `0`; both sides evaluate |
| `!` | Unary | `1` when the operand is zero, otherwise `0` |
| `~` | Unary | Complement, `1-a` |
| `=`, `+=`, `-=`, `--=`, `*=`, `/=`, `%=`, `^=` | Assignment | Assign or update a Local Symbol and return it |
| `;` | Sequence | Evaluate left, then return the right statement |
| `//`, `/* ... */` | Comment | Ignore the text |

Assignments are valid only for Local Symbols; a Global Symbol, an image or a
meta symbol cannot be assigned.

### Symbols, images, and channels

| Form | Meaning |
| --- | --- |
| `$T` | The image being written: the target, or each frame of its sequence, converted to the output's channels |
| target view's name | The same image as `$T` |
| `$A` | The active view's image when Apply was pressed; `$T` when the target is active |
| `$M` | The target's mask; an expression naming it without a mask is refused |
| `ImageName` | The image that open view held when Apply was pressed |
| `ImageName[n]` | Zero-based channel `n` |
| `c()` / `channel()` | Zero-based channel being written |
| `x()` / `xpos()`, `y()` / `ypos()` | Zero-based column and row being written |
| `X()`, `Y()` | Column and row divided by one less than the width and height; `0` for a one-pixel side |

A one-channel image is read for every channel being written. An image with the
output's channels is read channel for channel. Any other image - a colour image
beside a grey output, an RGB image for an alpha channel - is refused by name
unless the expression states the channel it reads, such as `ImageName[0]`.
`pixel()` reads `0` outside its image or for a channel it does not have.

Local Symbols are variables every pixel starts from its stated value. Global
Symbols are constants. A list is read leniently: `name` declares `0`, a value is
read from the number it starts with and is `0` otherwise, and a name declared
twice takes the later value. A symbol named like a function, or declared in both
lists, is refused by name.

### Scalar functions

Angles use radians. Domain errors and divisions by zero follow floating-point
arithmetic and are not repaired.

| Function | Arguments | Result |
| --- | --- | --- |
| `abs(x)` | 1 | Absolute value |
| `acos(x)`, `asin(x)`, `atan(x)` | 1 | Inverse trigonometric function |
| `acosh(x)`, `asinh(x)`, `atanh(x)` | 1 | Inverse hyperbolic function |
| `atan2(y,x)` | 2 | Quadrant-aware arc tangent |
| `asqr(a,b,...)` | 2 or more | Mean of the squares |
| `bwmv(a,b,...)` | 2 or more | Biweight midvariance |
| `ceil(x)`, `floor(x)`, `trunc(x)` | 1 | Integral value as a double |
| `clamp(x,a,b)`, `range(x,a,b)` | 3 | `x` held to `a..b` |
| `cos(x)`, `sin(x)`, `tan(x)` | 1 | Trigonometric function |
| `cosh(x)`, `sinh(x)`, `tanh(x)` | 1 | Hyperbolic function |
| `e()`, `pi()` | 0 | The constants |
| `exp(x)` | 1 | Natural exponential |
| `fac(x)` | 1 | Factorial |
| `ln(x)`; `log(x)`, `log10(x)`; `log2(x)` | 1 | Natural, base-10 and base-2 logarithms |
| `max(a,b,...)`, `min(a,b,...)` | 2 or more | Maximum/minimum |
| `mad(a,b,...)`, `mdev(a,b,...)` | 2 or more | Median absolute deviation |
| `mean(a,b,...)` | 2 or more | Arithmetic mean |
| `med(a,b,c,...)` | 3 or more | Median |
| `mtf(m,x)` | 2 | Midtones transfer function |
| `ncr(n,r)`, `npr(n,r)` | 2 | Combinations, permutations |
| `random()`, `rand()` | 0 | Uniform sample in `[0,1)` from the run's seed |
| `sdev(a,b,...)`, `var(a,b,...)` | 2 or more | Population standard deviation, variance |
| `sign(x)` | 1 | `-1`, `0`, or `1` |
| `sqrt(x)` | 1 | Square root |
| `ssqr(a,b,...)` | 2 or more | Sum of the squares |
| `sum(a,b,...)` | 2 or more | Sum |

### Image statistics functions

An image-statistics call is chosen when one of these names receives one image.
The statistic is computed once over the whole image, before any sample is
written, for the channel being written. When the output has the target's size and
channels, the target's validity selects its samples.

| Function | Result for one image argument |
| --- | --- |
| `adev(image)` | Mean absolute deviation from the mean |
| `asqr(image)`, `ssqr(image)` | Mean and sum of the squares |
| `bwmv(image)` | Biweight midvariance |
| `mad(image)`, `mdev(image)` | Median absolute deviation |
| `max(image)`, `min(image)` | Maximum/minimum |
| `mean(image)`, `med(image)` | Mean, median |
| `noise(image)` | Background-noise estimate |
| `sdev(image)`, `var(image)` | Population standard deviation, variance |

### Image property and colour-component functions

Each accepts zero or one image argument; omitted means `$T`.

| Functions | Result |
| --- | --- |
| `w(image)`, `width(image)`; `h(image)`, `height(image)` | Width, height in pixels |
| `n(image)`, `channels(image)` | Channel count |
| `iscolor(image)` | `1` for three or more channels |
| `CIEL`, `CIEa`, `CIEb`, `CIEc`, `CIEh`, `CIEX`, `CIEY`, `CIEZ` | CIE components, packed into `0..1`; these and the HSL, HSV and Y/Cb/Cr components refuse an image of fewer than three channels |
| `HSLh`, `HSLs`, `HSLl`, `HSVh`, `HSVs`, `HSVv` | HSL and HSV components |
| `YCbCrY`, `YCbCrCb`, `YCbCrCr` | Y/Cb/Cr components |

### Special functions

| Function | Result |
| --- | --- |
| `iif(condition, whenTrue, whenFalse)` | Only the chosen branch is evaluated; any nonzero condition is true |
| `inrect(x0,y0,width,height)` | `1` when the column is within `x0..x0+width` and the row within `y0..y0+height`, both edges included, the four numbers truncated toward zero |
| `inellipse(xc,yc,rx,ry)` | `1` when `((x-xc)/rx)^2 + ((y-yc)/ry)^2 <= 1` |
| `pixel(image,x,y[,c])`, `p(...)` | The sample at truncated coordinates; omit `c` for the channel being written; `0` outside the image |

## How the algorithm works

### Conceptual pipeline

1. Apply reads the dialog and the image every open view holds, the active view's
   and the target's mask. Every expression is parsed first, so a syntax error or
   a missing `$M` is reported before any image is read.
2. Only the images an expression names are read. An image of another width or
   height than the output is resampled to it with Lanczos-4 in its own
   representation. `$T` is converted to the output's channels: grey from colour by
   `0.2989 R + 0.5870 G + 0.1140 B`, colour from grey by repeating it, an alpha
   channel added at `0` or dropped.
3. Every sample is read as a fraction of full scale - an unsigned integer divided by
   its largest value, a signed one counted from its smallest value, a floating
   sample as it is - in one floating working representation: 64-bit when any image
   read or the output holds 64-bit floats or 32-bit integers, 32-bit otherwise.
4. Each expression is resolved and compiled once. A chain - a sample of one image
   combined step by step with constants or other whole images - runs as one fused
   pass; anything else, such as a branch, a coordinate, a statistic or `random()`,
   is interpreted. Both evaluate in double precision.
5. The destination starts as `$T`, so a blank channel program keeps the
   target's samples. When the output has the target's size and channels, a pixel
   the target's validity rejects keeps them too; any other result is all valid.
6. Optional Rescale result maps the global minimum and maximum across every output
   channel to Lower/Upper bound. The result is narrowed once to the output's
   representation, to the nearest value.
7. A replaced target keeps its metadata and solution and is recorded nonlinear with
   `pixelmath` provenance; a new image keeps the target's acquisition record and,
   at the target's size, its solution.

### Model and equations

For an expression result `e(x,y,c)` over fractions of full scale, the stored sample
before optional rescaling is the result itself:

```text
work(x,y,c) = e(x,y,c)
```

Rescale result uses one finite minimum `m` and maximum `M` across every channel:

```text
rescaled = lower + (upper - lower) * (work - m) / (M - m)
```

When `M = m`, or the result has no finite value, there is nothing to map and the
result is left as it is. The stored
value is the narrowed result: a floating output keeps any value, an integer output
saturates at the ends of its range.

### Boundary and failure behavior

- `pixel()` returns `0` for an invalid coordinate or channel.
- Nothing is clamped: `$T * 2` keeps values above `1` in a floating image and
  saturates an integer image.
- An empty set of expressions reports `The pixel math expression is empty`.
- Every refusal is reported under **Error running PixelMath** and in the log; an
  expression's refusal names the tab and the text: unknown names, a malformed channel selector, a bad argument count, a
  reserved symbol name, an image of another kind of number, an image whose channels
  do not line up, a missing `$M`.
- A stopped or refused run changes nothing and leaves no sequence behind.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Formula development + output mode | Create a same-size, As target, 32-bit float result | Output is identical because the formula returns `$T` | Values beyond `0..1` look clipped on display | Create new; validate formula; inspect extrema; only then replace |
| Mismatched images + Width/Height | Align externally and leave both at `0` | Inputs appear shifted/soft because the resize did not align them | Enlarged data looks sharper but holds interpolated samples | Register/align; match dimensions; then evaluate |
| Local Symbols + statement sequence | Use locals for intermediate terms, ending with the output symbol | Earlier calculations have no effect because the final statement returns another value | Long chains are hard to verify | Declare; assign; return the final symbol; test a small case |
| Rescale + bounds | Leave off until the formula's range is understood | Narrow output range remains low contrast | Outliers compress everything else | Inspect min/max; enable rescale; set ordered bounds |
| Active mask + `$M` | Use an explicit blend equation | Mask has no effect because attachment alone is ignored | Inverted weighting edits the protected region | Confirm mask; test `$M`; then blend explicitly |
| Separate RGB expressions | Begin with one RGB/K expression | Channels remain identical | Different formulas introduce colour casts | Verify shared formula; disable single expression; change one channel at a time |

## Inputs, outputs, and metadata

- **Units:** every image is read as fractions of full scale. Images whose samples
  are fractions - linear or encoded - are read together; an image of sensor codes
  or physical quantities beside them is refused by name.
- **Geometry:** Width/Height `0` inherit the target's dimension. Every image an
  expression names is Lanczos-4-resized to the output's size when it differs.
- **Colour model:** only `$T` is converted to the output's channels. A grey target
  asked for RGBA becomes four channels with alpha `0`.
- **Replace still:** the result keeps the target's description, metadata and
  solution, is recorded nonlinear, and is one PixelMath history step that undoes.
- **Create still:** the result takes the dialog's name, size, channels and format,
  keeps the target's acquisition record, keeps the solution only at the target's
  size, and keeps no header card but the processing state.
- **Sequence:** every frame is read - demosaiced when the view demosaics its frames
  - and the result is written into a new sequence, shown in the view as one history
  step or opened beside it. Other named images, `$A` and `$M` are the images their
  views showed when Apply was pressed.
- **Random:** each Apply draws a new seed, recorded in the result's lineage; each
  frame of a sequence draws from its own stream. A run computes the same image at
  any number of threads.
- **`.anpm`:** version-1 XML stores four expressions, both symbol lists, the three
  option boxes, rescale settings, output mode, identity, dimensions, colour space,
  and colour format. It stores no image data.

## Performance

A chain runs as one fused pass with compiled kernels and is many times faster than
an interpreted expression; anything else - a coordinate, a statistic or
`random()` - is interpreted a run of samples at a time, or one sample at a time
when it branches. A statistic costs one pass over its image
before evaluation. Only named images are read and resized, but each costs a working
copy in the working representation. A sequence repeats the work for every frame.

## Worked example

Evidence mode: **Source-and-test-derived**. The numeric checkpoints below are
held by `PixelMathRunTest` on synthetic samples. They make no astronomical
visual-quality claim.

### Average two images

With RGB `$T=(0.2,0.4,0.6)` and grey `SRC=0.8` at one coordinate:

```text
($T + SRC) / 2
```

The output is `(0.5,0.6,0.7)`: grey `SRC` is read for every colour channel.

### Blend through the attached mask explicitly

```text
$T * (1 - $M) + SRC * $M
```

Merely attaching a mask without referencing `$M` does not blend it.

### Coordinate region plus fallback ramp

For a `3×2` target and an image `SRC`:

```text
iif(inrect(0, 0, 1, 0), pixel(SRC, x(), y()), X())
```

The rectangle includes columns `0` and `1` of row `0`, both edges included. At
`(2,0)` the fallback is `X()=1`; at `(1,1)` it is `0.5`.

### Local and global symbols

Declare Local Symbols `a=0.25` and Global Symbols `k=2`:

```text
a = a + $T; a * k
```

Every pixel starts with `a=0.25`, so a target sample `0.6` produces `1.7`.

## Warnings and limitations

- There is no preview, Reset, or Validate action. Apply is full-resolution.
- A result beyond `0..1` is kept in a floating image; display it with a stretch or
  use `clamp()` or **Rescale result** when a bounded result is wanted.
- Active masks are input only through `$M`; there is no automatic mask blend.
- Lanczos-4 resizing does not register images or conserve flux.
- **Single threaded** and **Code optimization** change nothing; **Process in 64
  bits** is always on.
- A Global Symbol cannot be assigned; use a Local Symbol.
- `random()` draws differently on every Apply; its seed is in the result's lineage.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| `The pixel math expression is empty` | Every program the output uses is blank | Enter RGB/K, or at least one channel program | `PixelMathRunTest` |
| `RGB/K expression at 'NAME': nothing is bound to the name 'NAME' at byte <n>` | Identifier is neither a symbol nor an open image name | Check case, view name, and Symbols list; declare numeric symbols | Engine resolution |
| `$M reads the target's mask, and the target has none` | The target has no mask | Attach a mask or remove `$M` | `PixelMathRunTest` |
| `the local symbol 'NAME' cannot be declared` | The symbol is a function name or declared twice | Rename the symbol | `PixelMathWindowTest` |
| An image of another kind of number is refused | Sensor codes or physical quantities beside fractions | Convert or calibrate the image first | `PixelMathRunTest` |
| A colour image beside a grey output is refused | Its channels do not line up with the output's | Name the channel, such as `SRC[0]` | Engine resolution |
| Requested RGBA is transparent | An added alpha channel starts at `0` | Enter A=`1` for opaque output | `PixelMathRunTest` |
| Output contains NaN | Division or domain error | Guard denominators with `iif`, avoid invalid domains | Floating evaluator |
| Named source looks softened or misaligned | Pixel Math resized it but did not align it | Register and resize sources before Pixel Math | Binding policy |
| Mask has no effect | Attached masks are not automatically blended | Reference `$M` explicitly | `PixelMathWindowTest` |

## Related workflow steps

- Use [Image Blend](../analysis/image-blend.md) when a named blend mode, opacity,
  and automatic active-mask behavior are clearer than a custom equation.
- Use [Range Selection](../analysis/range-selection.md) or
  [Binarize](../masking/binarize.md) for standard mask generation before writing
  a coordinate/intensity expression.
- Review [Masks](../../interface/masks.md) before using `$M`; Pixel Math's mask
  behavior is explicit and differs from ordinary process blending.
- Review [Sequences](../../interface/sequences.md) before running expressions on
  a video/SER/image-list target, especially when other open sequences are inputs.
- Use [Process Windows](../../interface/process-windows.md) for target selection,
  progress, cancellation, and Image History conventions shared with other tools.

## References and provenance

- The engine's expression language - parser, resolution, compiler, fused lowering
  and evaluator - under
  `src/AfterNightEngine/include/AfterNightEngine/Algorithms/PixelMath/`.
- The Pixel Math window, its `.anpm` version-1 import/export and its binding policy
  in `PixelMathProcess.*` and `PixelMathProcessSupport.*`.
