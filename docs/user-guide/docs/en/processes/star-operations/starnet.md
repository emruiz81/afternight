---
kind: process
stage: noise
menu: Process > Star Processing > Starnet++
summary: >-
  Removes the stars from an image with the separately installed StarNet2 tool, and opens the starless result and, if
  you ask, the stars.
data_in: linear
data_out: nonlinear
derived: true
---
# Starnet++

## Purpose and expected result

Starnet++ sends the current image, or every frame of the current sequence,
through a separately installed StarNet2 CLI 2.5.3 or newer. A successful run
always opens a new starless image. It can also open StarNet2's native
subtractive mask, native unscreen star layer, or both. The source is never
replaced and no history step is added to it.

Use this process to separate stars from a nebula, galaxy, or other background
before treating the components independently. It is not AfterNight's analytical
[Star Mask](star-mask.md) detector, and it is not the dedicated
[StarNet++ Star Mask](starnet-star-mask.md) cleanup workflow.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Star Processing > Starnet++** |
| Enabled when | The menu action follows the normal single-image launch gates: an image or sequence view is active, no duplicate Starnet++ window is attached to it, and no attached process holds exclusive access. The dialog remains reachable when the CLI is unavailable; its processing controls and **OK** require a configured `starnet2` executable to pass the bounded 2.5.3+ readiness probe. Current CLIs are product-validated through `--machine-info`; older supported CLIs fall back to `--version` |
| Inputs | The current grayscale or RGB image, handed to StarNet2 as a temporary 16-bit TIFF of the unit interval. A colour filter array mosaic is refused by name; demosaic it first |
| Outputs | Always `<target> - starless`; optionally `<target> - star mask`, `<target> - stars`, or both, opened beside the target in that order. A still image gives new stills; a sequence gives one new sequence per output |
| Recommended image state | A calibrated, registered, integrated linear master with **Pre-Stretch linear image** on; for already nonlinear data, leave it off |
| Preview | Not available. **OK** starts the external run directly |
| Sequence behavior | Every frame is handed to StarNet2 in turn, demosaiced first when the view demosaics its frames; each requested output becomes a new sequence, published whole and opened beside the target; the progress window counts the frames |
| Active mask | Ignored; the full current image is sent to StarNet2. Only a pixel the image's own validity rule rejects is handed as zero |
| Persistence | StarNet2 executable path is global Settings state; process options and custom weights are dialog-lifetime only |

## Before you start

1. Install an official StarNet2 CLI package, including its model and runtime
   files, and comply with the package's current `LICENSE.txt`.
2. In **Edit > Settings > External Tools**, select the `starnet2` executable.
   AfterNight requires version 2.5.3 or newer. The dialog's processing controls
   and **OK** stay disabled if the path is empty, missing, non-executable, too
   old, slow to answer, or returns unparseable version text.
3. Keep a portable archive together: executable, default
   `StarNet2_weights.onnx`, runtime files, README, and license. Native installers
   may put the executable and support files in separate documented locations.
4. Demosaic colour filter array data first. Starnet++ refuses a mosaic by name
   rather than sending its pattern to the network. A sequence's frames are
   demosaiced before the run when the view demosaics them.
5. Use an image at least `512 x 512` pixels in normal mode. This is StarNet2's
   published minimum; AfterNight does not prevalidate it.
6. Ensure the temporary-files volume has space for the 16-bit source and up to
   three 16-bit output TIFFs per image or frame. **Upsample x2** also raises
   external runtime and memory substantially.

!!! note "The run reads the dialog once"
    Pressing **OK** reads every option and the **Target Image** on the GUI
    thread and runs from that snapshot. A change made to the dialog while
    StarNet2 runs reaches the next run only, and **OK** stays disabled until the
    run lands. To stop a run, use **Cancel** on the progress window: closing the
    process window does not kill the program; the window hides until the run
    lands and then closes.

## Procedure

1. Select a calibrated master and open **Process > Star Processing > Starnet++**.
2. Confirm the green readiness message and the intended **Target Image**.
3. For a linear image, check **Pre-Stretch linear image**. Leave it unchecked
   for an image whose pixel values are already stretched.
4. Start with **Highlight protection** on, **Stride** at **Standard (256)**,
   and **Upsample x2** off.
5. Leave **Weights** empty to use the model bundled with the configured CLI.
6. Choose the optional star output. **Unscreen Star Layer** is usually the more
   direct component for later recombination; **Both** is useful for comparing
   the two native representations.
7. Click **OK**. A progress window titled `Starnet++ <target>` opens; its
   **Cancel** kills StarNet2 and publishes nothing.
8. Inspect the new starless image and every requested star output at 100% with
   a consistent display stretch. Look for missed stars, removed nebula detail,
   bright-star residuals, seams, and halos before continuing.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Upsample x2 | Checkbox | Off | None | Off/On | Passes `--upsample` for StarNet2's intermediate 2x mode | Disabled until CLI readiness passes | Can help tight/tiny stars and radiating artifacts; upstream estimates about four times the compute/runtime and warns that very large stars may be harder | External execution; grayscale/RGB; still image or every frame of a sequence | Dialog lifetime |
| Pre-Stretch linear image | Checkbox | Off | None | Off/On | Applies the engine's automatic midtones transfer per channel before the 16-bit hand-off, then its inverse to the starless and unscreen outputs | Disabled until readiness passes | Helps a linear master meet model expectations; the shadow clipping of the stretch and the 16-bit quantization of the handed image and of StarNet2's answer are not recoverable; does not inverse-transform the subtractive mask | Pre/post hand-off; all channels independently; still image or every frame of a sequence | Dialog lifetime |
| Highlight protection | Checkbox | On | None | On/Off | On leaves StarNet2's default protection active; Off adds `--disable-highlights-protection` | Disabled until readiness passes | Off is an intentional comparison for saturated/near-saturated regions and may worsen bright-star residuals | Native CLI inference; still image or every frame | Dialog lifetime |
| Generate Stars Output | Checkbox | On | None | Off/On | Requests optional native star output(s); starless is always requested | Controls **Output** enablement | Extra output TIFFs, reads, conversions, views, memory, and disk | New stills or sequences only; not an active processing mask | Dialog lifetime |
| Output | Selector | **Subtractive Mask** | None | **Subtractive Mask**, **Unscreen Star Layer**, **Both** | Adds `--mask`, `--unscreen`, or both | Enabled only when **Generate Stars Output** is on and CLI is ready | Both performs one inference but writes/reads two auxiliary TIFFs; representations are not interchangeable | Native optional outputs; still image or every frame | Dialog lifetime |
| Stride | Selector, pixels | **Standard (256)** | None | **Standard (256)**, **Wide-field (384)**, **Custom** | Sets the CLI window stride; 384 is upstream's current wide-field/small-star suggestion | **Custom** enables its value field; disabled until readiness | Stride changes how inference windows advance. Upstream recommends 256 for most images and says values below 256 are rarely useful; exact blending is proprietary | Native CLI tiling; still image or every frame | Dialog lifetime |
| Custom stride value | Integer pixels | `256`, disabled | None | Even `2..512`; step `2`. Odd programmatic values are rounded down after clamping | Chooses the exact `--stride` value | Enabled only for **Custom** and CLI readiness | Extreme values may sharply increase work or change overlap behavior; no visual preview | Native CLI tiling | Dialog lifetime |
| Weights | Path | Empty; placeholder **Default StarNet2_weights.onnx** | None | Empty or model checkpoint path | Empty omits `--weights`; nonempty passes the trimmed path verbatim | Disabled until readiness passes | A custom model can change quality, compatibility, runtime, and licensing. AfterNight does not validate the file or model version before launch | Native CLI model; relative paths resolve from the executable's directory, where the program is started | Dialog lifetime; not saved globally |
| Browse | File action | No selection | Starts from a valid custom-model folder or StarNet2 executable folder | `*.onnx` filter plus all files | Writes the chosen path into **Weights** | Disabled until readiness passes | Selection is not a compatibility test | Model selection only | Remembers the browser folder context; chosen value remains dialog-only |
| Target Image | Image/sequence selector | Attached active target | None | Open eligible view | Selects the image or sequence sent to StarNet2 and the parent/name used for outputs | Active target required; remains enabled during execution, but a run is bound to the target selected when **OK** was pressed and a later change affects the next run only | An image another run is writing is refused with a busy warning; run it again once it is shown | Full current image; active mask ignored; a sequence means every frame | Target lifetime |
| Readiness message | Status text | Probe result | Executable path comes from global Settings | Ready, missing, non-executable, wrong product, unparseable, unsupported, or timeout detail | Reports whether **OK** and processing controls can be used | Rechecked at dialog creation, after **Configure**, and again on the worker before each run | Each check first requests `starnet2 --machine-info`; a valid v1 document supplies product, version, and progress capability. Output without the machine-readable document falls back to `--version` within the same 3-second budget | Configuration only | Reflects current global path |
| Configure | Settings action | Enabled | Global **External Tools > StarNet2** path persists | Opens Settings at External Tools | Reloads the saved path and repeats readiness detection after Settings closes | Remains enabled even when processing controls are disabled | Does not run an image or validate the bundled/default model | Configuration only | Global Settings persistence |
| OK | Execution action | Enabled only when ready and no run is in flight | None | Click | Reads the dialog once, opens the progress window and starts the run on a worker | Disabled while the run is in flight and re-enabled when it lands; the other controls stay editable but are not read by the running job | Always creates starless on success; requested auxiliaries add reads, conversions and views | No preview; new still(s) or new sequence(s) | One run; dialog remains open after a run |
| Close | Dialog action | Enabled | None | Click | While idle, closes without output; during a run, hides the window until the run lands and then closes it. The run itself is not stopped | Remains enabled during execution | Outputs of a run in flight still open when the run lands; use the progress **Cancel** to stop it | Current window | Discards dialog options |
| Progress / Cancel | Progress action | `0..100`; StarNet2's own progress maps to `10..90`, reading the answers reports `95` | None | Cooperative Cancel | Uses stable JSON Lines progress on stderr when `--machine-info` advertises the v1 schema; otherwise parses the printed percentages. Diagnostics the program prints are logged at debug level. Cancel kills the subprocess, publishes nothing and reports nothing | Exists only after OK | Cancellation is checked before anything is written, at every read of the program's output (at most 300 ms apart), after the input is written and after each answer is read; nothing is opened unless every answer came back | External run and output publication | Per run |

## How the algorithm works

### Conceptual pipeline

1. Read the dialog on the GUI thread when **OK** is pressed: stride, upsample,
   highlight protection, weights, pre-stretch, the star outputs and the target.
   For a still image, lease the target's current image for reading; an image
   another run is still writing is refused with a busy warning and nothing runs.
   For a sequence, take the view for the run.
2. On a worker, refuse by name an image that is a colour filter array mosaic or
   that has neither one nor three channels, then recheck the configured
   executable with `--machine-info`. A valid v1 document must identify product
   `starnet2`; it supplies the semantic version and whether stable v1 machine
   progress is supported. Older CLIs that do not return the schema fall back to
   `--version` within the same timeout budget. Anything below 2.5.3 or without a
   parseable version fails the run.
3. Hand the image to the engine's external-tool hand-off. An integer image is
   read as its fraction of full scale; a floating-point image is mapped from the
   unit interval widened to its smallest and largest finite valid samples, so
   samples below `0` or above `1` are kept rather than clipped. With
   **Pre-Stretch linear image**, derive one automatic midtones transfer per
   channel from that channel's exact median and median absolute deviation and
   apply it. Write every sample once as an unsigned 16-bit code rounded to
   nearest (ties to even); a pixel the image's validity rule rejects is left
   out of the range and the statistics and handed as zero. The image is written
   as `srcFile.tiff` into a temporary directory named after `starnet2` below the
   session's temporary directory, whole or not at all.
4. Run StarNet2 from the executable's directory with named `--input`,
   `--output`, `--stride`, and optional `--mask`, `--unscreen`, `--weights`,
   `--upsample`, and `--disable-highlights-protection` arguments. Add
   `--machine-progress` only when the machine-info capability is present;
   otherwise parse the printed percentages. The program's progress moves the
   progress window from 10% to 90%, never backwards; a stop kills the program.
5. At 95%, read each requested answer back through the engine's TIFF codec and
   hold it to the source's width, height and channel count. The starless image
   comes back through the inverse of each channel's transfer and the range's
   offset and span; the unscreen star layer through the inverse and the span
   alone, because StarNet2 screens it onto the image rather than adding it; the
   subtractive mask is only narrowed to the source's representation, never
   unstretched or mapped. Each answer is computed in double and written once in
   the source's representation, rounded to nearest and saturating, with the
   metadata described below.
6. Once every answer is back, open the views on the GUI thread in the order
   starless, star mask, stars - new stills for a still image, new sequences for
   a sequence. Nothing is opened unless the whole run succeeded. The temporary
   directory and its files are removed when the run ends.

### Automatic MTF and inverse

For a normal, non-inverted channel, the engine calculates the exact normalized
median `median_c` and normalized MAD `mad_c` of the mapped channel, the shadow
point `s_c = max(0, median_c - 2.8 x 1.4826 x mad_c)` - the MAD scaled to a
normal standard deviation, a zero deviation taken as `0.001` - and a midtones
value that maps `median_c - s_c` toward target background `0.25`. It then applies the rational
midtones transfer per channel:

`u = (x - s_c) / (1 - s_c)`

`MTF(u, m_c) = ((m_c - 1) x u) / (((2 x m_c - 1) x u) - m_c)`

Values at or below the shadow point map to zero; values at or above the
highlight point map to one. Whether the image is treated as inverted is decided
once, from every channel's median: when every median exceeds `0.5`, the
highlight point is derived instead of the shadow point.

After inference, the starless and unscreen samples are passed through the
inverse of the same channel's transfer: the complementary midtones balance
between the same endpoints, evaluated in double. An output at or below zero
answers the shadow point and one at or above one the highlight point. The
inverse cannot recover source values the stretch clipped together below the
shadow point, nor the precision StarNet2's 16-bit answer does not carry; the
handed image's 16-bit quantization does not matter for an unchanged sample,
because the inverse reads the program's answer, not the handed file. This is
why the round trip is not lossless even though the inverse is exact. A program
that changes nothing gives back the source within `1.5e-5` of full scale,
`3.2e-5` for an image beyond the unit interval and its star layer, `6.2e-5`
through the pre-stretch, and four codes of a 16-bit image.

!!! note "The native mask stays in the stretched domain"
    With **Pre-Stretch linear image** on, AfterNight inverse-transforms the
    starless image and unscreen star layer only. The native subtractive mask is
    read and narrowed to the source sample type without that inverse and without
    the range mapping. Do not assume it is numerically
    `original - inverse-stretched starless`.

### External model and outputs

StarNet2 owns neural inference, its model architecture/weights, tiling blend,
highlight-protection implementation, and the formulas used for native mask and
unscreen outputs. AfterNight chooses arguments and transports images; it does
not recreate those algorithms. The current official CLI reference confirms:

- 8-bit/16-bit grayscale or RGB TIFF/PNG input, plus standard integer or
  floating-point FITS image HDUs in CLI 2.5.4;
- even stride `2..512`, optional 2x upsampling, model override, and default
  highlight protection;
- mandatory starless output plus independently optional subtractive mask and
  unscreen star layer; and
- 16-bit TIFF/PNG output by default, or 32-bit floating-point FITS output when a
  FITS destination is requested.

AfterNight always hands a 16-bit TIFF, because StarNet2 2.5.3 states that it
refuses floating-point images; it never requests the CLI's 8-bit mode and does
not expose quiet mode. Leaving **Weights** empty delegates default-model
discovery to the installed CLI package.

### Boundary and failure behavior

- CLI readiness does not validate the default or custom model. Missing,
  incompatible, or inaccessible weights fail only when StarNet2 starts.
- A mosaic, an image with two or four channels, a program that could not start,
  crashed, or exited with a nonzero code, a missing answer file, or an answer
  of another size or channel count is reported under **Error running StarNet2**
  and publishes nothing. Nonzero-exit text includes what the program printed.
  Machine progress uses separate stdout/stderr channels; printed progress uses
  the merged-channel path.
- The temporary directory is removed when the run ends, however it ends.
- The source image, its metadata and its history are untouched. Every output
  is derived from the target: it keeps the target's acquisition record (object,
  exposure, instrument and the rest), its plate solution - the pixel grid is
  unchanged - and its lineage, carries the processing-state cards that mark it
  nonlinear, and no other header card of the target. Its lineage records the
  program name and version, the exact arguments, the format handed (`UInt16`)
  and the pre-stretch (`none` or `unlinked`). Each new view receives a StarNet
  history description.
- Every answer is read back before any view opens, so a failure of the program
  or of one answer publishes nothing. Only a view the application itself cannot
  open leaves the views opened before it.
- The run works from the dialog as it was when **OK** was pressed. Changing an
  option or the target during execution cannot make preparation and
  postprocessing disagree; it affects the next run.

## Parameter interactions and starting points

| Situation | Starting point | What to compare |
| --- | --- | --- |
| Linear RGB or grayscale master | Pre-Stretch On; Standard 256; highlight protection On; upsample Off; default weights | Starless structure, mask/stars layer, and background at identical display stretch |
| Wide-field image dominated by small stars | Wide-field 384; then compare Standard 256 | Missed small stars, seams, and runtime |
| Tight stars or radiating residuals | Standard 256 plus Upsample x2 | Tiny-star completeness and bright-star artifacts versus the much longer run |
| Bright saturated stars | Highlight protection On; run a deliberate Off comparison only if needed | Cores, halos, residual rings, and removed background structure |
| Linear recombination workflow | Pre-Stretch On; Unscreen Star Layer | Whether starless + chosen recombination method restores stars without clipping |
| Custom model evaluation | Duplicate source; keep every other option fixed | Model-specific quality, compatibility, licensing, and runtime; AfterNight provides no model preview/validation |

## Inputs, outputs, and metadata

| Aspect | Current contract |
| --- | --- |
| Source dimensions/channels | StarNet2 receives the full image. Official normal mode requires at least `512 x 512`; grayscale and RGB are supported. A colour filter array mosaic and an image with two or four channels are refused by name |
| Source precision | Any supported sample type - 8-, 16- or 32-bit integer, signed or unsigned, Float32 or Float64 - is handed as unsigned 16-bit codes of the unit interval, rounded to nearest. An integer image is handed as its fraction of full scale. A floating-point image is mapped from the unit interval widened to its smallest and largest finite valid samples, and the starless and unscreen outputs are mapped back, so samples outside `[0,1]` are not clipped; the 16-bit quantization of the hand-off remains |
| Starless output | Always read, brought back through the inverse stretch and the range, written in the source type, named `<target> - starless`, and opened as a new still or sequence |
| Subtractive mask | Optional native `--mask` TIFF, narrowed to the source type as the program's own answer, named `<target> - star mask`, and opened as a new still or sequence |
| Unscreen stars | Optional native `--unscreen` TIFF, brought back through the inverse stretch and the range's span alone, written in the source type, named `<target> - stars`, and opened as a new still or sequence |
| Dimensions/channel count | Each answer must match the source's width, height and channel count; one that does not is refused and nothing is published |
| Linearity state | Every published output is explicitly marked nonlinear (`starnet-starless`, `starnet-star-mask`, or `starnet-stars`), including inverse-stretched outputs |
| Metadata | Every output keeps the target's acquisition record, plate solution and lineage, carries the processing-state cards and no other header card of the target, and adds a lineage entry naming the program, its version, its arguments, the format handed and the pre-stretch |
| Active mask | Ignored for input and output; a generated "star mask" is an ordinary new image until assigned through the mask system |
| Sequence | Every frame is processed in turn, demosaiced first when the view demosaics its frames; each output is a new sequence of the same frame count, published whole or not at all |

## Performance

Neural inference dominates runtime. Image area, backend/hardware chosen by the
installed CLI, stride, 2x upsampling, output count, and temporary TIFF I/O are
the main drivers. StarNet2's current Windows package can use DirectML with CPU
fallback; AfterNight does not select or report that backend itself.

**Upsample x2** is the largest exposed cost multiplier; upstream guidance says
it can take roughly four times the compute/runtime. Smaller strides can imply
more overlapping window positions, but the current public documentation does
not define the proprietary blending or an exact cost formula. **Both** outputs
do not repeat inference, but they add TIFF writing, reading, conversion, and two
view allocations. A sequence multiplies all of it by its frame count, one
program run per frame.

No timing, GPU-utilization, temporary-disk, or peak-memory measurement on
approved astronomical data was run for this page, so it gives no production
estimate.

## Worked example

This bounded example is a verification procedure, not a claimed image-quality
result:

1. Duplicate a linear, demosaiced, integrated RGB master at least `512 x 512`.
2. Open Starnet++ and verify the ready message and duplicate target.
3. Select **Pre-Stretch linear image**, **Highlight protection**,
   **Generate Stars Output**, **Unscreen Star Layer**, and **Standard (256)**.
   Leave **Upsample x2** off and **Weights** empty.
4. Click **OK** and wait for the progress window to close.
5. Confirm two new stills: `<target> - starless` appears before
   `<target> - stars`. Both have the source sample type, are marked nonlinear,
   and carry the source's acquisition record and plate solution.
6. Compare the source, starless result, and stars layer at 100%. Check bright
   cores, small stars, nebula filaments, galaxy knots, and tile-like boundaries.
7. If only tight small stars are missed, repeat from the unchanged duplicate
   with **Upsample x2**. If the field is wide and stars are small, compare stride
   384 separately. Change only one factor per run.

## Warnings and limitations

- StarNet2 is external software and model data, not bundled or licensed by this
  user-guide page. Use the official package and its current license.
- There is no preview, ROI, Apply/Reset model, active mask blend, channel
  selector, or persistent process preset.
- A run over a sequence processes every frame. To process one frame, extract it
  first.
- A colour filter array mosaic is refused; demosaic it first.
- **Pre-Stretch linear image** is an actual pixel transform, not the viewer's
  display autostretch. Its shadow clipping and the 16-bit bridge are not
  lossless, although the inverse itself is exact.
- The subtractive mask is not inverse-stretched. Native mask and unscreen
  outputs are independent upstream products; do not treat their pixel values as
  equivalent.
- The target's header cards other than the processing state are not carried
  into the outputs; its acquisition record, plate solution and lineage are.
- Custom weights receive only a file-picker filter. AfterNight does not verify
  model format, tensor compatibility, provenance, license, or expected quality.
- StarNet2 can mistake compact nebula/galaxy detail for stars or miss unusual,
  soft, saturated, elongated, or undersampled stars. Always inspect the star
  output, not only the plausible-looking starless image.
- Closing the process window does not stop a run; use the progress window's
  **Cancel**. A stop is quiet and publishes nothing.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Process options and OK are disabled | Empty/missing/non-executable path, wrong machine-info product, unparseable/old version, or readiness probing exceeding the shared 3-second budget | Click **Configure**, select StarNet2 2.5.3+, save Settings, and return. For 2.5.4+, verify `starnet2 --machine-info`; for older supported versions, verify `--version` |
| OK is disabled although the message is green | A run of this window is still in flight | Wait for the progress window to close, or cancel the run |
| "Could not start" or crash message | Executable/runtime files are missing, blocked, or incompatible | Keep the complete package files in their official layout; current FITS-capable packages also require their bundled CFITSIO/zlib runtime files |
| Error says the image is a colour filter array mosaic | The target is an undemosaiced camera frame | Demosaic the image (or enable demosaicing for the sequence) and run again |
| Nonzero exit mentions input size/layout | Image is below normal-mode minimum or has an unsupported layout after external decoding | Use at least `512 x 512`; use grayscale/RGB |
| Nonzero exit mentions weights/model | Custom path is wrong/incompatible, or default package model cannot be found | Clear **Weights** and restore the official model/runtime layout; or select a known-compatible ONNX model |
| Busy warning instead of a run | The target's image is being written by another process or edit | Wait until the view shows the result, then run again |
| Result looks permanently stretched | Pre-stretch was used on already nonlinear data, or the stretch's shadow clipping removed detail the inverse cannot restore | Rerun from an unchanged duplicate with the checkbox off for nonlinear input |
| Star mask and stars layer do not match | They are separate native subtractive and unscreen outputs; only unscreen receives the inverse stretch and the range mapping | Choose the representation required by the next workflow and do not compare as identical pixel layers |
| The whole sequence was processed when one frame was wanted | Starnet++ runs over every frame of a sequence view | Extract the intended frame to a still image first |
| Cancel closes progress without output | The subprocess was killed before normal publication | This is expected; rerun when ready |
| The process window vanished while the run continued | Close was pressed during a run; the window hides until the run lands | Wait for the outputs to open, or cancel from the progress window |

## Related workflow steps

Use [Star Mask](star-mask.md) for analytical detection and a controllable mask
profile without an external model. Use [StarNet++ Star Mask](starnet-star-mask.md)
when you need its separately documented mask-focused workflow. Continue with
[Star Reduction](star-reduction.md), whose **Generate starless view** opens this
window set to the unscreen star layer with no star output, takes the starless
view it makes, and closes it; [Starmask Recombination](starmask-recombination.md);
or a composition method appropriate to the chosen native star layer. Configure
the executable under [Settings Dialog](../../interface/settings-dialog.md) and
assign any output used as a processing mask through [Masks](../../interface/masks.md).

## Credits

- [StarNet command-line tool reference](https://starnetastro.com/documentation/starnet/command-line-tool/)
  documents the CLI options, input/output layouts, minimum size, model/package
  layout, stride, upsampling, highlight protection, mask, and unscreen outputs.
- [StarNet tips and tricks](https://starnetastro.com/documentation/starnet/tips-and-tricks/)
  supplies current upstream guidance for color input, 256/384 stride, 2x
  upsampling, and inspecting removed structure.
- [Current command-line releases](https://starnetastro.com/cli-tools/) and
  [CLI installer locations](https://starnetastro.com/documentation/cli-installers/)
  define the current 2.5.4 platform packages, runtime/backend lanes, and native
  installer layouts. AfterNight accepts 2.5.3 as its compatible minimum.
