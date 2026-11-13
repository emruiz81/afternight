---
kind: process
stage: any
menu: Process > Pixel Math > Pixel Math
summary: >-
  Computes an image sample by sample from expressions you write over open images, the target's mask, positions,
  statistics and numbers, and replaces the target or opens the result as a new image or sequence.
data_in: either
data_out: nonlinear
destructive: true
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    An image or a sequence view is active, no Pixel Math window is open on it yet, and no window that needs the image
    to itself is open on it.
  inputs: >-
    The target image or sequence, of any pixel type, every open image by its view's name, the active image, the mask
    on the target's view, numbers and the symbols you declare.
  outputs: >-
    The target replaced with one history step, or a new image or sequence beside it. No file is written but the
    .anpm settings file that Export saves.
  state: >-
    Any image. Arithmetic on the signal belongs on linear images, and every result is recorded as nonlinear, whatever
    the expression computes.
derived: false
---
# Pixel Math

Pixel Math computes an image from expressions you write, evaluated once for every sample of the result. An expression
reads the target, any open image by its view's name, the [mask](../../reference/glossary.md#mask) on the target's
view, the sample's position, statistics of whole images, numbers and your symbols. Use it for arithmetic between
images, a mask written as a formula, a curve no process offers, or one formula per color channel.

**Apply** replaces the target, or every frame of its [sequence](../../reference/glossary.md#sequence), with one
[history](../../reference/glossary.md#history) step, or opens the result as a new image or sequence. Samples are read
as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), and the result is what the
expression computes, never clamped to 0 to 1. It is recorded as nonlinear.

```figure
image: ../../assets/images/processes/pixel-math/pixel-math.webp
alt: >-
  The PixelMath window. Under the title, a row of tabs: RGB/K, then G, B and A grayed, then Symbols and Summary. In
  the RGB/K tab, a large editor holds one line, mtf(0.02, clamp(($T - 0.02) / 0.98, 0, 1)), with the function names,
  the target symbol and the numbers in their own colors. Beside it, a narrow column of three lists: Functions,
  Operators, and Symbols with a Type column. Under the tabs, Use a single RGB/K expression is ticked, with Import
  and Export at the right. A Settings group holds Single threaded, Process in 64 bits ticked and grayed, Code
  optimization ticked, Rescale result unticked with two grayed bounds, a box reading Create new image, New Image Id
  reading STRETCHED, and Width 0, Height 0, Color space and Color format at As target. Target Image names the
  master, and Apply and Close run along the bottom.
caption: >-
  Pixel Math on the linear Carina master with the curve of the worked example in RGB/K and Create new image chosen,
  before Apply.
pins:
  - {n: 2, x: 5.5, y: 20.0}
  - {n: 3, x: 89.0, y: 60.0}
  - {n: 4, x: 18.5, y: 67.4}
  - {n: 5, x: 53.8, y: 10.6}
  - {n: 6, x: 66.2, y: 10.6}
  - {n: 7, x: 19.0, y: 83.5}
  - {n: 8, x: 13.5, y: 75.5}
  - {n: 9, x: 94.3, y: 70.0}
  - {n: 10, x: 95.4, y: 93.5}
```

## Before you start

- The window has no preview: **Apply** works on the whole image, or every frame of a sequence. Try an expression
  with **Create new image**, and replace the target once the new image is right.
- Open every image the expression reads. An image of another width or height is resampled to the result's size, not
  registered, aligned or matched: register it to the target first.
- The expression reads an image by the name of its view. A name it can read starts with an ASCII letter or `_`, goes
  on with letters, digits and `_`, and is not the name of a function, such as `x`, `c` or `w`. Rename a view whose
  name is not such a name.
- The images must be the same kind of number as the target, which their
  [numeric domain](../../reference/glossary.md#numeric-domain) states. A floating-point
  [FITS](../../reference/glossary.md#fits) file opens as a
  [physical quantity](../../reference/glossary.md#physical-quantity), and a TIFF file or a 0 to 1
  [XISF](../../reference/glossary.md#xisf) image as fractions.
- The mask on the target's view is not blended. Read it as `$M` and write the blend in the expression.
- Every result is recorded as nonlinear, and the color calibrations refuse such an image: calibrate color first. A
  raw [CFA](../../reference/glossary.md#cfa) mosaic is worked as one gray channel, so
  [debayer](../../reference/glossary.md#debayer) it when the expression needs colors.

## Steps

1. **Open the window.** Make the target the [active image](../../reference/glossary.md#active-image) and choose
   **Process > Pixel Math > Pixel Math**. The title reads `PixelMath <name>`, **RGB/K** is empty, and the
   **Symbols** list beside it names `$T`, `$A`, `$M` and every open image.
2. **Write the expression.** Type it in **RGB/K**, or double-click a row of the **Functions**, **Operators** or
   **Symbols** list to insert it at the cursor. The editor colors every name it knows.
3. **Read other images.** Write an open image's name as the **Symbols** list spells it, `$A` for the active image or
   `$M` for the target's mask. Rest the pointer on a name to see its size, channels and
   [pixel type](../../reference/glossary.md#pixel-type).
4. **Give a channel its own expression.** For a color result, untick **Use a single RGB/K expression**. The first tab
   becomes **R**, and **G** and **B** open. A blank tab keeps that channel of the target.
5. **Declare symbols.** In the **Symbols** tab, list variables under **Local Symbols** and constants under **Global
   Symbols**, as `name = value` separated by commas.
6. **Read the script.** Open **Summary** to see every expression and both lists as one script.
7. **Choose where the result goes.** Choose **Create new image** while you try an expression, and type its **New
   Image Id**. **Width**, **Height**, **Color space** and **Color format** give it another shape, and 0 or As target
   keep the target's.
8. **Rescale only on purpose.** Tick **Rescale result** to map the smallest and the largest result onto **Lower
   bound** and **Upper bound**. Leave it off to keep the values the expression computes.
9. **Keep the expression.** Choose **Export** to save the expressions, the symbols and the settings in an `.anpm`
   file, which **Import** reads back. The history keeps no expression.
10. **Apply.** Choose **Apply**. A new view opens beside the target, or the view shows the result and its history
    lists **PixelMath**. The window keeps your values for another run.
11. **Check the values.** Open Image Statistics on the result. Its minimum and maximum show whether the expression
    wrote values below 0 or above 1, which are kept.

## Controls

Pixel Math has no preview, no histogram, no **Channel** box and no **Reset**, and nothing checks an expression before
**Apply** runs it. What it shares with the other windows, such as
**Target Image** and the progress window of a sequence, is on [Process Windows](../../interface/process-windows.md).

### The expression tabs

```controls
window: PixelMath
controls:
  - name: RGB/K
    kind: field
    default: ""
    effect: >-
      Holds the expression every sample of the result takes: every channel of a grayscale or color result, or the red
      channel alone when each color channel has its own expression, and the tab then reads **R**.
    range: >-
      Numbers, image names, symbols, operators and functions, in statements separated by `;`. The value of the last
      statement is written. The reference page lists the language.
    tradeoff: >-
      An expression that is a chain, one image combined step by step with numbers or other whole images, runs as one
      fused pass. Any other expression goes through the evaluator, which takes longer.
    scope: >-
      Apply, on every frame of a sequence. A blank expression keeps the target's samples in the channels it would
      write.
    persistence: Until the window closes. Apply keeps it. Export saves it as `expression0`.
    note: >-
      A mistake in the writing is reported with the tab's name before any image is read, as "RGB/K expression at
      '(': a parenthesis at byte 0 has no partner". The mistakes of every tab are reported together, the first of
      each statement.
  - name: G
    kind: field
    default: ""
    effect: Holds the green channel's expression.
    enabled: >-
      While **Use a single RGB/K expression** is unticked and the result has three or four channels. Disabled when
      the window opens.
    scope: The green channel alone. A blank expression keeps the target's green.
    persistence: Until the window closes. Export saves it as `expression1`.
  - name: B
    kind: field
    default: ""
    effect: Holds the blue channel's expression.
    enabled: As **G**.
    scope: The blue channel alone. A blank expression keeps the target's blue.
    persistence: Until the window closes. Export saves it as `expression2`.
  - name: A
    kind: field
    default: ""
    effect: Holds the expression of the alpha channel, the fourth channel of the result.
    enabled: >-
      Only while the result has four channels: a new image whose **Color space** is RGBA, or a four-channel target.
      Over a grayscale or RGB target it is disabled from the moment the window opens.
    tradeoff: >-
      An alpha channel the target did not have starts at 0, which is transparent. Write `1` here for an opaque
      image.
    scope: >-
      The alpha channel alone, whether **Use a single RGB/K expression** is ticked or not. Leave the color tabs blank
      to change the alpha channel alone.
    persistence: Until the window closes. Export saves it as `expression3`.
  - name: Local Symbols (Comma separated list)
    kind: field
    default: ""
    effect: >-
      Declares variables, in the **Symbols** tab. Each sample starts with the value stated here, and an expression
      may change it with `=`, `+=` or another assignment.
    range: >-
      Entries `name` or `name = value`, separated by commas. A name alone, or a value that does not start with a
      number, declares 0. A name declared twice takes the later value.
    tradeoff: An expression that assigns a variable always goes through the evaluator, never as a fused pass.
    scope: Every expression of the run. A symbol hides an open image of the same name.
    persistence: Until the window closes. Export saves it as `local_symbols`.
    note: >-
      A function's name, such as `x`, `c` or `e`, is refused when Apply runs: "the local symbol 'x' cannot be
      declared: a constant was declared with an empty name or one the language owns".
  - name: Global Symbols (Comma separated list)
    kind: field
    default: ""
    effect: >-
      Declares constants, in the **Symbols** tab. Every sample reads the stated value, and no expression can change
      it.
    range: The same entries as **Local Symbols**.
    tradeoff: A constant costs nothing per sample.
    scope: Every expression of the run. A symbol hides an open image of the same name.
    persistence: Until the window closes. Export saves it as `global_symbols`.
    note: >-
      An expression that assigns a constant is refused, as "the assignment at byte 0 writes to 'k', which is not a
      declared variable". A name also declared under **Local Symbols** is refused: "the global symbol 'k' cannot be
      declared: a symbol is already declared with that name".
  - name: Summary
    kind: readout
    default: ""
    effect: >-
      Shows the expressions and the two symbol lists as one script, each under a comment that names it: RGB/K, or
      Red, Green and Blue, then Alpha, then Symbols.
    enabled: Read-only. Written again each time you open the tab, and after an Import.
    scope: The window only. Blank expressions are left out. The run reads the tabs, not this text.
  - name: Use a single RGB/K expression
    kind: check
    default: "On"
    effect: >-
      On writes every color channel with the **RGB/K** expression. Off gives the red, green and blue channels the
      expressions of the **R**, **G** and **B** tabs, each compiled on its own.
    enabled: >-
      While the result has three or four channels. Over a grayscale target, or for a new Grayscale image, it is
      disabled and read as ticked.
    tradeoff: >-
      Different expressions for the channels can shift the colors. Start with one expression, then change one channel
      at a time.
    scope: Apply. The alpha channel takes the **A** expression either way.
    persistence: >-
      Until the window closes. Export saves it as `single_expr`. An imported choice opens the **G** and **B** tabs
      only where the result has three channels or more.
  - name: Import
    kind: button
    default: ""
    effect: >-
      Reads an `.anpm` file and replaces every expression, both symbol lists and the settings with what it holds. The
      window then shows the first tab.
    range: >-
      Version 1 of the format. The file chooser opens in the folder last used for a Pixel Math file, and lists
      `.anpm` files.
    enabled: Always, while a run works too.
    tradeoff: >-
      When an expression tab holds text, it first asks "This will override all the current content. Do you want to
      continue?" under "Import pixel math script", and **No** changes nothing.
    note: >-
      A file that cannot be read, of another version or without its settings is reported under "Error importing
      pixel math script", and the window stays as it was. **Process in 64 bits** stays ticked whatever the file says.
  - name: Export
    kind: button
    default: ""
    effect: Saves every expression, both symbol lists and the settings in an `.anpm` file, version 1 of an XML format.
    range: The file chooser opens in the folder last used for a Pixel Math file.
    enabled: Always.
    tradeoff: It saves the text as it is, without checking the expression, and no image.
    note: A file that cannot be written is reported under "Error exporting pixel math script".
```

### Settings

```controls
window: Settings
controls:
  - name: Single threaded
    kind: check
    default: "Off"
    effect: Changes nothing a run does. Every run computes the same image at any number of threads.
    persistence: Until the window closes. Export saves it as `single_threaded`, and a file without it reads Off.
  - name: Process in 64 bits
    kind: check
    default: "On"
    effect: States that the expressions are evaluated in double precision. It cannot be changed.
    enabled: Always disabled.
    persistence: Export saves it as `process_64_bits`. Import leaves it ticked.
    note: >-
      An expression that goes through the evaluator computes in double precision. A fused chain computes in the
      precision of the working copy: double when the target, the result or an image the expressions read is 64-bit
      floating point or a 32-bit integer, and single otherwise.
  - name: Code optimization
    kind: check
    default: "On"
    effect: Changes nothing. Every expression is compiled once, and a chain always runs as a fused pass.
    persistence: Until the window closes. Export saves it as `optimize`, and a file without it reads On.
  - name: Rescale result
    kind: check
    default: "Off"
    effect: >-
      On maps the smallest and the largest finite result, over all the channels together, onto **Lower bound** and
      **Upper bound**, by equation (2). Off writes the values as the expressions compute them.
    range: >-
      A result with one value everywhere, or with no finite value, is left as it is. A value that is not finite stays
      as it is.
    enabled: It enables the two bounds.
    tradeoff: >-
      One more pass over the result. A few extreme samples, such as star cores above 1, take the ends and press
      everything else into a narrow band of low contrast.
    scope: The whole result, after every expression and before it is written in its pixel type.
    persistence: Until the window closes. Export saves it as `rescale`, and a file without it reads Off.
  - name: Lower bound
    kind: spinbox
    default: "0.00000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 8}
    effect: The value the smallest result becomes.
    enabled: While **Rescale result** is ticked.
    tradeoff: Above **Upper bound**, it turns the result's tones round.
    persistence: Until the window closes. Export saves it as `rescale_low`.
  - name: Upper bound
    kind: spinbox
    default: "1.00000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 8}
    effect: The value the largest result becomes.
    enabled: While **Rescale result** is ticked.
    tradeoff: Lower, it compresses the range of the result. Below **Lower bound**, it turns the tones round.
    persistence: Until the window closes. Export saves it as `rescale_high`.
  - name: Output mode
    object: outputModeSelector
    kind: combo
    default: Replace target image
    options: [Replace target image, Create new image]
    effect: >-
      **Replace target image** writes the result over the target, or over every frame of its sequence. **Create new
      image** opens the result as a new image, or a new sequence, beside the target and leaves the target as it was.
    enabled: >-
      **Create new image** enables **New Image Id**, **Width**, **Height**, **Color space** and **Color format**.
    tradeoff: >-
      Replacing changes the target, and its history keeps the step before. A new image is safer while you try an
      expression.
    persistence: Until the window closes. Export saves it as `output_mode`.
  - name: New Image Id
    kind: field
    default: PIXEL_MATH_IMAGE
    effect: Names the new view.
    range: >-
      Spaces around the name are dropped. Blank names it `PIXEL_MATH_IMAGE`, or `PIXEL_MATH_SEQUENCE` for a
      sequence.
    enabled: With **Create new image**.
    persistence: Until the window closes. Export saves it as `new_image_id`.
    note: >-
      The new image's history begins with "Image created from PixelMath script", and a new sequence's with "Sequence
      created from PixelMath script".
  - name: Width
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 12000, step: 1}
    effect: Sets the new image's width in pixels. 0 takes the target's width, even above 12000.
    range: Every image the expressions read is resampled to this width with Lanczos-4.
    enabled: With **Create new image**.
    tradeoff: >-
      A larger result costs more for every sample. An enlarged image holds interpolated samples, which look sharper
      and hold no more detail.
    scope: The new image. A replaced target keeps its size.
    persistence: Until the window closes. Export saves it as `width`.
  - name: Height
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 12000, step: 1}
    effect: Sets the new image's height in pixels. 0 takes the target's height, even above 12000.
    range: Every image the expressions read is resampled to this height with Lanczos-4.
    enabled: With **Create new image**.
    tradeoff: As **Width**.
    scope: The new image. A replaced target keeps its size.
    persistence: Until the window closes. Export saves it as `height`.
  - name: Color space
    kind: combo
    default: As target
    options: [As target, Grayscale, RGB, RGBA]
    effect: >-
      Chooses the new image's channels: the target's, one gray channel, red, green and blue, or those and an alpha
      channel. `$T` is converted to them: gray from color by equation (4), color from gray by repeating it, an alpha
      channel added at 0 or dropped.
    enabled: >-
      With **Create new image**. Grayscale disables **Use a single RGB/K expression** and the **G**, **B** and **A**
      tabs. RGBA enables **A**.
    tradeoff: >-
      The conversion is no color calibration. Only `$T` is converted: another image whose channels are neither one
      nor the result's is refused unless the expression names one of its channels.
    scope: The new image. A replaced target keeps its channels.
    persistence: Until the window closes. Export saves it as `color_space`.
  - name: Color format
    kind: combo
    default: As target
    options: [As target, 8 bits, 16 bits, 32 bits int, 32 bits float, 64 bits float]
    effect: >-
      Chooses the new image's pixel type: the target's, unsigned integers of 8, 16 or 32 bits, or floating point of 32
      or 64 bits.
    range: An integer type rounds each value to the nearest code and holds it to the ends of the type, by equation (3).
    enabled: With **Create new image**.
    tradeoff: >-
      An integer type cuts every value below 0 and above 1, and adds steps between tones. A target of physical
      quantities, such as a floating-point FITS file, can only be written in floating point. 64 bits float makes the
      whole run work in double precision.
    scope: The new image. A replaced target keeps its pixel type.
    persistence: Until the window closes. Export saves it as `color_format`.
    note: >-
      An integer type for a target of physical quantities is refused with "a new image of UInt16 samples cannot hold
      what the target's samples mean", or UInt8 or UInt32.
```

### Target Image, Close and Apply

```controls
window: PixelMath
controls:
  - name: Target Image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence the window can work on]
    effect: >-
      Chooses the image or sequence the expressions call `$T` and Apply writes. The name of its view reads it too,
      and the mask on its view is what `$M` reads. As target, and a **Width** or **Height** of 0, take its values.
    enabled: Also while a run works. A change reaches the next Apply, not the run on its way.
    persistence: >-
      Until the window closes. Choosing another target keeps the expressions, and offers the tabs its channels
      allow.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. Its expressions and settings are discarded, so export them first to keep them.
    enabled: Disabled while a run works.
    note: >-
      Closing the window another way while a run works hides it, and it closes when the run ends. The run is not
      stopped, on a sequence either.
  - name: Apply
    kind: button
    default: ""
    effect: >-
      Reads the window, the image of every open view, the active image and the target's mask, draws a new random
      seed, and runs the expressions over the whole target or every frame of its sequence.
    enabled: >-
      When the window opens, and again when a run ends. While a run works it is disabled with **Close**, and the other
      controls stay editable. A change made then does not reach the run on its way.
    tradeoff: >-
      The run holds working copies of the target, the result and every image the expressions read. A single image
      shows no progress window, and its run cannot be stopped once it starts.
    scope: >-
      The whole target, never through the mask on its view. On a sequence, every frame, whatever frames its player
      selects, debayered first when its view debayers them. Other images are read as their views showed them when
      you pressed Apply.
    persistence: >-
      With **Replace target image**, the target's history gets one step named **PixelMath**, which records no
      expression and undoes the run. With **Create new image**, a new view opens beside the target. The window keeps
      its values for the next run.
    note: >-
      A refusal is shown under "Error running PixelMath", after "An error occurred when executing the PixelMath
      process:", and in the log. A refused or stopped run writes nothing and leaves no sequence behind.
```

### The lists and the editor

```controls
window: The lists beside each expression
controls:
  - name: Functions
    kind: action
    default: ""
    effect: >-
      Lists every function name of the language once, in alphabetical order. A double-click inserts the name and
      `() ` at the cursor of the tab's editor.
    range: The tooltip of a row describes the function and its arguments.
    tradeoff: It inserts no arguments and checks none.
  - name: Operators
    kind: action
    default: ""
    effect: >-
      Lists every operator, the statement separator `;` and the two comments. A double-click inserts it and a space
      at the cursor.
    range: The tooltip of a row says what the operator does.
  - name: Symbols
    kind: action
    default: ""
    effect: >-
      Lists `$A`, `$M` and `$T`, the declared local and global symbols and every open image, each with its type. A
      double-click inserts the name and a space at the cursor.
    range: >-
      The list follows views as they open, close and are renamed. It reads the symbol lists again when you open the
      **RGB/K**, **G**, **B** or **Summary** tab.
    tradeoff: >-
      It lists `$M` and every image whether they can be read or not. A missing mask, or an image of another kind of
      number, is refused when Apply runs.
  - name: Colors of the editor
    kind: readout
    default: ""
    effect: >-
      The editor draws functions, the region tests with `iif` and `pixel`, image names, symbols, numbers, operators,
      brackets and comments each in its own color, in a fixed-width font.
    note: A name drawn in the color of an image names an open view, or `$T`, `$A` or `$M`.
  - name: Resting the pointer on a name
    kind: action
    default: ""
    effect: >-
      After half a second, a tooltip describes the name: a function's arguments and result, a symbol's kind and
      starting value, or an image's name, width, height, channels and pixel type.
    note: >-
      Over `$M` without a mask, it reads "WARNING: Target image doesn't have any mask applied". Nothing shows over a
      comment.
```

```controls
window: Sequence progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: >-
      Shows the share of the frames done, from 0 to 100 percent, under the title **PixelMath** and the text
      `Executing PixelMath to sequence <name>`.
    enabled: Only while Apply works on a sequence. A single image has no progress window.
    tradeoff: >-
      The result is written to a temporary sequence, which needs the disk space of a second copy until the run ends.
    note: Each frame draws its random numbers from a seed of its own, so two frames draw different numbers.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run. Nothing is published, the temporary sequence is removed, and the sequence stays as it was.
    note: Closing the Pixel Math window does not stop the run.
```

## How it works

1. **Apply** reads the window and takes the image of every view as it is. Every expression is parsed first, so a
   mistake in any tab, or a `$M` without a mask, stops the run before an image is read.
2. Only the images the expressions name are read. Each is resampled to the result's size with Lanczos-4 when its size
   differs, and read as fractions of full scale in one working copy. `$T` is converted to the result's channels.
3. Each expression is bound to its images and symbols and compiled once. It is then evaluated for every sample of the
   channels it writes: as one fused pass when it is a chain, through the evaluator otherwise. A whole-image statistic
   is computed once, before the first sample.
4. **Rescale result** maps the range of the result, and the values are written once in the result's pixel type, to
   the nearest value.
5. The result replaces the target as one history step, or opens in a new view. Its
   [processing state](../../reference/glossary.md#processing-state) is recorded as nonlinear with the operation
   `pixelmath`, and its [lineage](../../reference/glossary.md#lineage) gains an entry with every expression, the way
   it ran and the random seed.

### Model

A stored sample \(s\) of an integer type, whose smallest and largest values are \(s_{\min}\) and \(s_{\max}\), is
read as the fraction

\[ v = \frac{s - s_{\min}}{s_{\max} - s_{\min}} \tag{1} \]

so an unsigned type, whose \(s_{\min}\) is 0, is divided by its largest value. A floating-point sample is read as it
is, so a sample of a [linear image](../../reference/glossary.md#linear-image) above 1, such as a star core, stays
above 1.

The expression is then evaluated for one sample at a time, at column \(x\), row \(y\) and channel \(c\) of the
result:

- An image name reads the image at the same pixel. A one-channel image gives its channel to every channel of the
  result, an image with the result's channels gives channel \(c\), and `NAME[n]` gives channel \(n\).
- Statements separated by `;` run from left to right, and the value of the last one is written. Each sample starts
  the variables from their stated value again.
- `iif(test, a, b)` evaluates only the branch it chooses. `&&` and `||` evaluate both sides.
- `^` binds tighter than a sign and groups to the right, so `-2^2` is -4, `2 * 3^2` is 18 and `2^3^2` is 512.
- A whole-image statistic, such as `med($T)`, is of the channel being written, so each channel of a color image
  gets its own.

Let \(r\) be the value the expression computes, and \(r_{\min}\) and \(r_{\max}\) the smallest and the largest
finite \(r\) over the whole result. With **Rescale result** ticked, and \(L\) and \(U\) the two bounds, each finite
value becomes

\[ r' = L + (U - L)\,\frac{r - r_{\min}}{r_{\max} - r_{\min}} \tag{2} \]

A floating-point result keeps \(r'\), or \(r\), as it is, in its own precision. An integer result stores, with
\(\operatorname{round}\) to the nearest code,

\[ s = \min\Bigl(s_{\max},\ \max\bigl(s_{\min},\ \operatorname{round}\bigl(s_{\min} + r\,(s_{\max} - s_{\min})\bigr)\bigr)\Bigr) \tag{3} \]

so a value below 0 is stored as the type's smallest value and one above 1 as its largest.

When a new image has one channel and the target has three, with \(R\), \(G\) and \(B\) the target's channels at a
pixel, `$T` reads

\[ K = 0.2989\,R + 0.5870\,G + 0.1140\,B \tag{4} \]

### Boundaries

- A division by zero, or the square root or the logarithm of a negative number, gives an infinite value or one that
  is not a number. A floating-point result keeps it. Nothing repairs it.
- When the result has the target's size and channels, a pixel the target's
  [validity rule](../../reference/glossary.md#validity-rule) marks invalid is not evaluated, keeps the target's
  samples, and is left out of every statistic. A result of another shape is evaluated everywhere and is all valid.
- `pixel(NAME, x, y)` reads 0 outside the image, and for a channel the image does not have.
- A refused run, and one stopped with **Cancel**, change nothing and leave no sequence behind.
- An expression may hold 65,536 bytes, 256 statements, 64 arguments in a call and 16 images, `$T` included. The
  reference page lists every limit.

### Worked example

The linear Carina [master](../../reference/glossary.md#master) is a 32-bit floating-point RGB image whose three
channels have their median at 0.02766, with samples from about -0.02 to 1.84. Its view is named `RGB`. Make it the
active image, open Pixel Math and type in **RGB/K**:

```text
mtf(0.02, clamp(($T - 0.02) / 0.98, 0, 1))
```

Choose **Create new image**, type `STRETCHED` in **New Image Id**, and choose **Apply**. Each sample has 0.02 taken
off and is divided by 0.98, then held to 0 to 1, and `mtf` lifts it through the
[midtones transfer function](../../reference/glossary.md#midtones-transfer-function) with a balance of 0.02. The
median becomes 0.0078 and then 0.28. A sample of 0.10 becomes 0.81 and one of 0.50 becomes 0.98, while everything at
or below 0.02 is 0 and everything at or above 1 is 1. The new view `STRETCHED` opens beside the master, which is
unchanged. It is the picture of the worked example of
[Histogram Transformation](../stretching/histogram-transformation.md), whose **Shadows** and **Midtones** at 0.02
compute this curve.

The same kind of curve can come from the image's own statistics. Type `b, m` under **Local Symbols**, and in
**RGB/K**:

```text
b = max(0, med($T) - 2.8 * 1.4826 * mad($T));
m = mtf(0.25, med($T) - b);
mtf(m, clamp(($T - b) / (1 - b), 0, 1))
```

Every channel computes its own [black point](../../reference/glossary.md#black-point) `b` and balance `m` from its
median and its
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation). On this master, `b` is 0.0204,
`m` is 0.0215, and the median lands at about 0.25.

Other expressions to type:

- `($T + SRC) / 2` averages the target with an open image `SRC`. Where the target reads 0.2, 0.4 and 0.6 and a
  grayscale `SRC` reads 0.8, the result is 0.5, 0.6 and 0.7: the gray image is read for every channel.
- `$T * (1 - $M) + SRC * $M` blends `SRC` into the target where the mask is white, as a process window does with its
  own result.
- With `a = 0.25` under **Local Symbols**, `k = 2` under **Global Symbols** and `a = a + $T; a * k` in **RGB/K**, a
  target sample of 0.6 becomes 1.7: every sample starts with `a` at 0.25.
- `$T` alone, with **Rescale result** ticked and the bounds at 0 and 1, maps the master's range of about -0.02 to
  1.84 onto 0 to 1 by equation (2). The median lands at about 0.026, and the picture stays dark: a few star cores
  hold the top of the range.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A new expression | **Create new image**, with the other settings As target | Compare the new view with the target, then apply with **Replace target image**. |
| Images of another size or framing | Register them to the target first, and leave **Width** and **Height** at 0 | Look at the stars at 100 % for doubled or soft edges. |
| A result that leaves 0 to 1 | **Rescale result** off, and the extremes read in Image Statistics | `clamp()` where the values must stay in range, or **Rescale result** to fit the whole range. |
| One formula per color channel | One expression in **RGB/K** | Untick **Use a single RGB/K expression**, and change one channel at a time. |

## When it goes wrong

!!! danger "The mask on the view is not used"
    Pixel Math writes the whole target, whatever mask is attached, enabled or inverted. Read the mask as `$M` and
    write the blend, such as `$T * (1 - $M) + RESULT * $M`: `$M` weighs the new value and `1 - $M` the target.
    Swapped, the blend changes what the mask protects.

!!! warning "The history keeps no expression"
    The step reads **PixelMath** and nothing else. Export the window's settings before you close it, to run the same
    expression again.

!!! warning "Images are resampled, never aligned"
    An image of another size is resized to the result's size with Lanczos-4. Its stars do not land on the target's
    unless it was registered to the target first.

"The pixel math expression is empty"
:   **Cause.** Every expression the result uses is blank, such as an expression left in a tab the result does not
    write.
:   **Fix.** Write the expression in **RGB/K**, or in the tab of a channel the result has.

`RGB/K expression at 'NAME': nothing is bound to the name 'NAME' at byte <n>`
:   **Cause.** The name is neither an open view's, with the same case, nor a declared symbol. A function written
    without its parentheses, such as `pi` for `pi()`, gives the same message.
:   **Fix.** Copy the name from the **Symbols** list, rename the view, or add `()` to the function.

"$M reads the target's mask, and the target has none"
:   **Cause.** No mask is attached to the target's view.
:   **Fix.** Attach one with **Image > Mask > Select Mask**, or remove `$M` from the expression.

"the local symbol 'x' cannot be declared", or "the global symbol 'k' cannot be declared"
:   **Cause.** The name is a function's, such as `x`, `c`, `e` or `w`, or it is declared in both lists.
:   **Fix.** Rename the symbol, such as `x0`, or keep it in one list.

`the assignment at byte <n> writes to 'k', which is not a declared variable`
:   **Cause.** The expression assigns a global symbol, an image or `$T`.
:   **Fix.** Declare the name under **Local Symbols**, and assign that.

`the image 'NAME' at byte <n> means a different kind of number from the others in this expression`
:   **Cause.** The image is another kind of number than the target, such as fractions from a TIFF file beside a
    floating-point FITS file, or [sensor codes](../../reference/glossary.md#sensor-code) from an integer FITS file.
:   **Fix.** Combine images opened from the same kind of file.

`the image 'NAME' at byte <n> does not have the shape of the image being written`
:   **Cause.** The image's channels are neither one nor the result's: a color image beside a grayscale result, or an
    RGBA image beside an RGB one.
:   **Fix.** Name one of its channels, such as `SRC[0]`, or give the result its channels with **Color space**.

"a new image of UInt16 samples cannot hold what the target's samples mean"
:   **Cause.** The target is a physical quantity, such as a floating-point FITS file, and **Color format** asks for
    an integer type.
:   **Fix.** Choose As target, 32 bits float or 64 bits float.

The new RGBA image is transparent
:   **Cause.** The alpha channel the target did not have starts at 0.
:   **Fix.** Write `1` in the **A** tab, or the alpha you want.

The result holds infinite samples, or samples that are not a number
:   **Cause.** The expression divides by zero, or takes a square root or a logarithm of a negative value.
:   **Fix.** Guard the operation, such as `iif(SRC != 0, $T / SRC, 0)`, or keep the argument in range with `max()`.

The result looks white, black or clipped in the view
:   **Cause.** The expression wrote values above 1 or below 0, which a floating-point result keeps.
:   **Fix.** Read them in Image Statistics, then hold them with `clamp()`, fit them with **Rescale result**, or look at
    the result in the [Autostretch](../../reference/glossary.md#autostretch) display.

The result is the target unchanged
:   **Cause.** The last statement returns `$T` or a variable it never changed, or the channel's tab is blank.
:   **Fix.** End the script with the value you want written, such as the symbol that holds it, and read **Summary**
    for the tabs that hold text.

## Related

- [Pixel Math Reference](pixel-math-reference.md), for every operator, function and symbol, the messages, the
  settings file and what a run records.
- [Image Blend](../analysis/image-blend.md), for blend modes with an opacity and the mask applied for you.
- [Range Selection](../analysis/range-selection.md) and [Binarize](../masking/binarize.md), for the usual masks
  without an expression.
- [Masks](../../interface/masks.md), to attach the mask that `$M` reads.
- [Image Statistics](../../interface/image-statistics.md), to read the extremes of a result.
- [Process Windows](../../interface/process-windows.md), for **Target Image**, the progress window of a sequence and
  what every process window shares.
