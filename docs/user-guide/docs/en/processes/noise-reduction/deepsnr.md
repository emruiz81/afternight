---
kind: process
stage: noise
menu: Process > Noise Reduction > DeepSNR
summary: >-
  Denoises an image with the separately installed DeepSNR tool, a learned model for astronomical images, and replaces
  the image with its result.
data_in: either
derived: true
---
# DeepSNR

<!-- MEDIA_SLOT: UG-MEDIA-070 -->

## Purpose and expected result

DeepSNR sends the selected image through the separately installed DeepSNR
command-line tool, then replaces that image with the denoised result. It
is intended for calibrated and integrated astronomical images whose remaining
noise is mostly random, uncorrelated, high-frequency noise. The upstream
guidance favors monochrome-camera data, including grayscale, RGB, and
narrowband combinations; drizzle-integrated one-shot-color data may also be a
suitable input.

A successful run preserves the image dimensions and channel count, brings the
result back in the image's own sample type after a 16-bit TIFF round trip,
keeps the image's acquisition record, plate solution, FITS cards and
processing state, and records one **DeepSNR** step in Image History before the
view shows it. Over a sequence view, every frame is denoised and the view shows
the new sequence as one history step. It does not create a new view or save a
final output file. Because the external model is a learned restoration model,
inspect faint detail, star profiles, and background texture at 100% before
continuing.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Noise Reduction > DeepSNR** |
| Enabled when | An image view is active and no conflicting process is attached. The process window opens even when DeepSNR is not configured, but its processing fields and **OK** are disabled until the configured executable exists, is executable, reports a parseable version, and is version `1.2.0` or newer. |
| Inputs | The selected view's image, at least `512 × 512` pixels, with exactly one grayscale channel or three RGB channels, and not a colour filter array mosaic. Whatever its sample type, the image is handed to the program as unsigned 16-bit codes of the unit interval, because DeepSNR refuses floating-point images. |
| Outputs | On success, the image of the view selected when **OK** was pressed is replaced in place, as an edit Image History records before the view shows it. Dimensions and channels are held to the input's; an optional pre-stretch is reversed before the result is written once in the original sample type; the image's metadata is kept whole, every sample is marked valid, and the lineage records the program, its version and exact arguments. Over a sequence, a new sequence of every denoised frame replaces the view's sequence as one history step. No final file or auxiliary map is retained. |
| Recommended image state | A high-quality calibrated and integrated image with mostly random high-frequency noise. Enable **Pre-Stretch linear image** for a linear source when DeepSNR benefits from a visible nonlinear handoff; leave it disabled for an already stretched source. AfterNight trusts this manual choice rather than inspecting the recorded processing state. |
| Preview support | None. There is no RT or full-image preview and no strength/blend control. **OK** runs the complete external process and changes the target only after the program exits normally and its output loads. |
| Sequence support | Yes. Over a sequence view every frame - demosaiced when the view demosaics its frames - is handed to the program in turn, written into a new sequence that is published whole or not at all, and shown in the view as one **DeepSNR** history step. The progress window counts the frames and its **Cancel** stops the run. Extract a frame first when only one frame is wanted. |
| Mask support | Unsupported. DeepSNR neither reads nor post-blends the image-view mask. Duplicate or blend results later if the effect must be localized. |
| External dependencies | DeepSNR CLI `1.2.0` or newer, its compatible model weights, package-local runtime libraries, and acceptance of the DeepSNR software license. Network access is needed only to obtain the package; AfterNight runs it locally. Current upstream packages use ONNX Runtime on Windows/Linux/Intel macOS and CoreML on Apple Silicon. |

## Before you start

1. Read the DeepSNR license before installing or using the package. It is a
   separate, restrictive license, not AfterNight's GPL license. In particular,
   it limits use to astrophotography, prohibits using the software, its
   components, or its outputs to create commercial software, and restricts
   sharing full-scale raw demonstration inputs.
2. Install a current DeepSNR CLI package and keep its executable, runtime
   libraries, weights, README, and license in the layout supplied by its
   installer or portable archive. Do not mix files from different packages.
3. Open **Edit > Settings... > External Tools**. Set **DeepSNR** to the executable
   or use its detection action. Apply the setting and wait for a success status
   such as `DeepSNR 1.2.3 detected`. An invalid path may be saved, but the
   process remains disabled until the readiness check passes.
4. Prepare a grayscale or RGB image at least `512 × 512`. Demosaic a colour
   filter array image first; the run refuses a mosaic. Correct calibration,
   dithering, integration, and walking-noise problems first; the upstream model
   treats correlated noise more like structure and is not expected to remove it
   cleanly.
5. Image History records the run before the view shows it, so **Undo** takes
   it back. Duplicate the target anyway when you want to compare runs side by
   side: DeepSNR has no preview or partial-strength control, and the program
   itself takes only 16-bit input.
6. Ensure the configured temporary directory has enough free space for both a
   full-size 16-bit input TIFF and a denoised output TIFF. The external runtime
   and working buffers require additional memory.

## Procedure

1. Select the intended image and open **Process > Noise Reduction > DeepSNR**.
   Verify **Target Image**. A success readiness message confirms that the
   configured CLI passed the version check.
2. Leave **Stride** at **Default (480)** for the first run. This is the upstream
   CLI default and gives adjacent 512-pixel windows 32 pixels of nominal
   overlap.
3. Leave **Weights** empty to use the CLI's default model 2 weights. Choose a
   custom `.onnx` file only when its origin, model compatibility, and license
   are known.
4. For a linear source, optionally enable **Pre-Stretch linear image**. This
   applies the same unlinked automatic midtones preparation used by Starnet++
   and reverses it on the denoised result. Leave it disabled for nonlinear data.
5. Click **OK**. AfterNight reads the dialog once, on the GUI thread - the
   target, the stride, the weights path and the pre-stretch choice - disables
   the controls, and runs the rest on a worker: it writes a temporary 16-bit
   TIFF, starts DeepSNR in the executable directory, and shows a progress
   window titled **DeepSNR <target>** whose bar follows the program's own
   progress between 10% and 90%. A change made to the dialog while the program
   runs does not reach the run. The target is not replaced during this stage.
6. Wait for **Processing output...** and completion. The target updates only
   after the program exits normally with code `0`, its output TIFF loads with
   the input's size and channels, and Image History has recorded the step.
   Image History shows **DeepSNR** with the used stride and pre-stretch state.
7. Inspect the result and the pre-process history state at 100% with the same
   display stretch. Look for erased faint structures, artificial smoothness,
   altered small stars, or tile-boundary changes. If boundaries are suspected,
   undo, choose **Conservative (256)**, and compare again. The window stays open
   for another run.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Pre-Stretch linear image | Checkbox | Off | None | Off or on; intended for linear grayscale or RGB input | When enabled, the engine derives an automatic midtones transfer independently per channel from that channel's exact median and median absolute deviation, stretches the handed copy before the 16-bit handoff, and brings the DeepSNR result back through the transfer's inverse in double precision. When disabled, the source goes directly to the 16-bit handoff. | Disabled until the CLI readiness check passes and throughout execution. AfterNight does not automatically detect whether the image is linear. | Can give DeepSNR a brighter, nonlinear working image while returning the result toward the original linear state. The inverse cannot recover values lost to the program's 16-bit input or to model changes, so the round trip is not mathematically lossless. Enabling it for already stretched data can produce an unsuitable result. | Full execution; the state is read when **OK** is pressed; unlinked per channel; mask ignored; every frame of a sequence | Process-window lifetime only; reopening restores Off. |
| Stride | Selector; pixels between neighboring 512-pixel inference windows | **Default (480)** | None | **Default (480)**, **Conservative (256)**, or **Custom** | A smaller stride increases overlap; a larger stride reduces overlap. For a 512-pixel window, nominal overlap is `512 - stride`. | Disabled until the CLI readiness check passes and throughout execution. Selecting **Custom** enables the adjacent numeric field while idle. | More overlap can reduce sensitivity to tile boundaries but repeats more inference work and increases runtime. Less overlap is faster but gives the merge less shared context. | Full execution; the value is read when **OK** is pressed; all source channels together; mask ignored; every frame of a sequence | Process-window lifetime only; reopening restores **Default (480)**. |
| Custom stride value | Integer; pixels | `480` | None | Even integers `2`–`512`; step `2`. Odd typed values are rounded down to the preceding even value. | Lower values increase overlap and tile count; `512` gives zero nominal overlap between adjacent full windows. | Enabled only for **Custom**, when CLI readiness passes, and while no execution is active. | Very small values can multiply runtime dramatically without a guaranteed visual benefit. `512` minimizes repeated work but offers no nominal overlap. | Full execution; read when **OK** is pressed and passed as `--stride`; every frame of a sequence | Process-window lifetime only; reopening restores `480`. |
| Weights | Optional path to an ONNX model checkpoint | Empty; placeholder says `Default DeepSNR_weights_v2.onnx` | None | Blank or a path accepted by the CLI. The browser filters `*.onnx` but also permits all files. Relative paths are passed unchanged and resolve from the DeepSNR executable's working directory. | Blank omits `--weights`, so the CLI uses its default model 2. A nonblank path overrides the bundled model choice. | Field and **Browse** are disabled until CLI readiness passes and throughout execution. AfterNight does not prevalidate the file, model version, channel support, or license. | Custom weights can change compatibility and results or make execution fail. They do not provide a strength control. Large model files also affect startup and memory. | Full execution; the path is read when **OK** is pressed; all channels according to the selected model; mask ignored | Process-window lifetime only; not stored in settings. |
| Browse | File-selection button | — | The remembered DeepSNR weights folder may affect the next file-dialog location | ONNX files or all files | Selects a value for **Weights**. With an empty value, the initial folder prefers the install support directory, such as `../lib/deepsnr` beside a `bin` directory, then the executable directory. | Enabled only when CLI readiness passes and while no execution is active. | Selecting an incompatible or untrusted model can fail or produce unsuitable output. | Configuration only; no execution | The chosen path remains only in the open process window; the browser folder is remembered for later selection. |
| Target Image | Image selector | View used to launch the process | None | Any open image view exposed by the base process control, still or sequence | Changes the view the run reads and replaces while idle. The view is fixed when **OK** is pressed. | Requires an open view and is disabled throughout execution. The selector does not filter by size, channel count, filter array or sequence state; validation occurs during execution, before anything is written. | Large targets increase TIFF disk use, tile count, memory, and runtime. A sequence target runs the program once per frame. | Full execution; the still image, or every frame of the sequence | Current process-window selection only. |
| Readiness status | Status text | A reason such as `DeepSNR executable path is empty.` until configured | Reads persisted `tools/deepsnrPath` | Empty, missing, non-executable, wrong product, unparseable/old version, or supported version state | Reports why execution is unavailable or confirms readiness. | The executable is checked with `--machine-info` first. A valid v1 document supplies product, version, and progress capability; output without the machine-readable document falls back to `--version` within the same 3-second budget. The run repeats the check on the worker before writing anything. | A passing readiness check proves the executable contract, not that weights/runtime providers can complete inference. | Process window and execution preflight | Executable path persists globally through **Settings > External Tools**; status is recalculated. |
| Configure | Button | — | Opens the persisted global settings page | Click action | Opens **Settings > External Tools**, then reloads and rechecks the saved DeepSNR executable path after the settings dialog closes. | Available while idle and disabled throughout execution. The process fields and **OK** reflect the new check result. | Detection/version checks start the external executable; no image is sent during readiness checking. | Configuration only | The saved executable path persists globally; process-specific stride/weights do not. |
| OK | Execution button | Disabled until ready | None | One run at a time per window | Reads the target, stride, weights and pre-stretch state on the GUI thread, then runs the TIFF/CLI/TIFF transaction on a worker. On success the answer comes back through any pre-stretch's inverse, written once in the source sample type with the image's metadata kept whole, and replaces the target as one history step recording the stride and pre-stretch state. | Requires a supported CLI. **OK** and all mutable execution controls are disabled before the worker starts and remain disabled until the run lands. A sequence view another run already holds shows a busy warning and nothing runs. | Destructive in-place update after success, which **Undo** reverses; potentially expensive CPU/GPU, RAM, and temporary-disk work. No preview or blend amount. | Full execution only; whole grayscale/RGB image; mask ignored; every frame of a sequence | Controls remain in the open process window after a run; reopening restores defaults. |
| Progress window Cancel | Cancellation action | Not requested | None | User cancel while the run is on its way | Stops the run: the DeepSNR subprocess is killed within a short read wait, the progress window closes, nothing is published and no message is shown. | Shown for a still image and for a sequence while the run is on its way. | Force-killing the subprocess gives it no graceful checkpoint. The temporary directory is scoped to the run and is removed when the run ends. | Active full execution | No persistence. |
| Close | Button | — | None | Click action | Closes the process window without starting a new run. While a run is on its way the window goes out of sight and closes once the run lands; closing it does not stop the run - use the progress window's **Cancel** for that. | Always shown. | Unapplied stride/weights values are discarded. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. On the GUI thread, when **OK** is pressed, AfterNight reads the dialog once -
   the target view, the normalized stride, the weights path and the pre-stretch
   state - builds the request the program will be run with, and disables the
   mutable controls. The worker reads only that request, never the window.
2. On the worker, the run refuses before anything is written: a colour filter
   array mosaic, an image with neither one nor three channels, and an image
   below `512 × 512`. It then rechecks the captured executable by running
   `--machine-info`. A valid v1 document must identify product `deepsnr`; it
   supplies the version and stable-progress capability. Older CLIs without the
   schema fall back to `--version` within the same timeout budget. An empty,
   missing, non-executable, wrong-product, unparseable, or older-than-1.2.0 CLI
   is refused before any image conversion.
3. The engine writes the image the program is handed, as unsigned 16-bit codes
   of the unit interval rounded to nearest: an integer image as its fraction of
   full scale, a floating-point image mapped from the unit interval widened to
   its smallest and largest finite valid sample, so samples below `0` or above
   `1` reach the program whole rather than clipped. A pixel the image's validity
   rule rejects is left out of that range and of the statistics and handed as
   zero. With pre-stretch enabled, the engine derives a midtones transfer per
   channel from that channel's exact median and median absolute deviation, with
   shadow clipping `-2.8` deviations and target background `0.25`, and applies
   it to the handed copy. The selected image is not changed at this stage.
4. The handed image is saved as `srcFile.tiff` in a per-run temporary directory
   below AfterNight's configured temporary location, through the engine's
   output transaction, so the file is written whole or not at all. AfterNight
   does not send the original FITS/XISF file or its metadata to DeepSNR.
5. AfterNight runs `deepsnr --input <temp TIFF> --output <temp TIFF>
   --stride <value>` in the executable's directory. It adds `--weights <path>`
   only when **Weights** is nonblank. It deliberately omits `--model`,
   `--quiet`, and `--eight`, so the CLI uses model 2 and 16-bit TIFF output by
   default. It adds `--machine-progress` only when machine-info advertises the
   stable v1 JSON Lines schema; for older supported versions it parses the
   plain-text percentages. The program's progress moves the run's progress window
   from 10% to 90%, never backwards; lines that are not progress go to the log.
6. The external CLI evaluates a learned neural restoration model through its
   packaged runtime. DeepSNR states that its architecture is based on NAFNet,
   but the DeepSNR executable and weights are not implementation source. The
   exact trained mapping, normalization, padding, tile-merge weights, and
   invalid-sample policy are not disclosed in the reviewed artifacts and are
   not claimed here.
7. After a normal zero exit, AfterNight reads `dstFile.tiff` through the
   engine's codecs and holds it to the input's width, height and channels. The
   engine brings every sample back through the recorded hand-off: the inverse
   of its channel's transfer when pre-stretch was enabled, then the mapping back
   over the recorded range, computed in double precision and written once in
   the image's own representation, rounded to nearest and saturating. The
   result carries the image's metadata whole - acquisition record, plate
   solution, FITS cards and processing state - with every sample marked valid
   and a lineage entry naming the program, its version, its exact arguments,
   the format handed (`UInt16`) and the pre-stretch.
8. The result replaces the view's image as an edit Image History records before
   the view shows it, under the step **DeepSNR** with the used stride and
   pre-stretch state. Over a sequence, steps 2 to 7 run once per frame, the
   frames are written into a new sequence that is published whole, and the view
   shows it as one history step.

### Model and equations

The externally visible tiling relationship is a fixed 512-pixel processing
window and an even stride `s` in `[2, 512]`. For adjacent full windows, the
nominal shared width is

\[
o = 512 - s,
\]

where `o` is the overlap in pixels. Default stride `s = 480` gives `o = 32`;
the conservative preset `s = 256` gives `o = 256`. Reducing `s` increases the
number of overlapping evaluations. The upstream CLI documentation does not
publish the edge-window placement or merge equation, so overlap should be used
as a performance/artifact diagnostic rather than interpreted as a documented
blend kernel.

The hand-off maps a sample `v` of a floating-point image to the program's unit
interval by `(v - low) / (high - low)`, where `low` is the smaller of `0` and
the smallest finite valid sample and `high` the larger of `1` and the largest,
and brings the answer back by the same offset and span; an integer image uses
`low = 0` and `high` = full scale. The pre-stretch's inverse is the midtones
transfer with the complementary midtones balance between the same shadows and
highlights endpoints.

DeepSNR identifies NAFNet as its architectural basis. The NAFNet paper describes
an image-restoration network that replaces common nonlinear activation
functions with simpler operations, including multiplication. That paper
explains the upstream architecture family; it does not prove the private
DeepSNR training data, weights, inference normalization, or output accuracy.

### Boundary and failure behavior

- Images smaller than `512 × 512`, alpha/four-channel images, channel counts
  other than one or three, and colour filter array mosaics are refused by name
  before the CLI starts and before any file is written.
- Every sample type is handed as unsigned 16-bit codes, because DeepSNR refuses
  floating-point images. The program's answer therefore has 16-bit resolution
  over the handed range; bringing it back to a float, double or wider integer
  representation does not add precision the program did not produce.
  Floating-point samples outside `[0,1]` are not clipped: the handed range is
  widened to the image's finite valid extremes and the answer is mapped back
  over it. A program that changes nothing gives the source back within `1.5e-5`
  of full scale - `3.2e-5` for an image with samples outside `[0, 1]`, `6.2e-5`
  through the pre-stretch, and four codes of a 16-bit image.
- A pixel the image's validity rule rejects is handed as zero and left out of
  the range and the stretch statistics. A sample that is not a finite number in
  a pixel the validity rule admits makes the engine refuse the hand-off, and the
  run fails under **Error running DeepSNR** with nothing written. Inside the
  program, invalid-pixel behavior is governed by the proprietary CLI.
- The CLI owns image-edge padding and tile merging. The reviewed upstream docs
  specify the 512-pixel window and stride range but not those boundary details.
- Machine-progress runs keep stdout and stderr separate, parse v1 JSON Lines
  from stderr, tolerate non-JSON ONNX/runtime diagnostics, and map reported
  percentages from 10% to 90%. A run of an older DeepSNR command line without
  machine progress merges stdout and stderr and parses its plain-text
  percentages. Collected output is shown when a nonzero exit occurs.
- Failure to create the temporary directory, write or read the TIFF, start the
  process, survive without crashing, or exit with code zero - and an answer that
  is missing or of another size than the input - leaves the target unchanged
  and is reported under **Error running DeepSNR**. Cancellation kills the
  subprocess, publishes no partial output and shows no message.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Linear state + pre-stretch | Enable **Pre-Stretch linear image** only for an actually linear source; keep it off for nonlinear data | With it off, DeepSNR may respond poorly to a very dark linear handoff | With it on for already stretched data, the inverse can produce an unnatural tonal result | Confirm the source state first. Compare enabled and disabled runs on duplicates with the same display stretch, then keep the better state before tuning stride or weights. |
| Default model 2 + stride | Empty **Weights** and **Default (480)** | Residual random grain or suspected boundary variation; there is no strength control to increase | Fine texture or faint signal looks unnaturally smooth, or small structures change | First verify input quality/model suitability. Compare history or a duplicate; then try **Conservative (256)** only for suspected tiling artifacts. Use later blending rather than an unknown custom model to reduce strength. |
| Stride + image size | **Default (480)** for a full integrated master | Boundary-related changes remain visible in repeated locations | Runtime becomes excessive after choosing a very small custom stride | Keep weights fixed, compare 480 with 256, and reduce stride further only when that controlled comparison supports it. |
| Custom weights + channel layout | Prefer blank weights/model 2 for grayscale or RGB | The chosen model under-denoises or rejects the input | Detail/color behavior is unsuitable, the CLI fails, or an RGB-only model is used for grayscale | Verify model provenance, supported channels, and license first; change weights only after a default-model baseline. |
| DeepSNR + correlated noise | Fix calibration/dithering/integration and use default model 2 afterward | Walking noise or patterned structure remains | The learned model alters real structure while the correlated pattern persists | Correct acquisition/integration defects first. Do not compensate with a smaller stride or arbitrary weights. |

## Inputs, outputs, and metadata

- Input count: the selected view's image, or every frame of its sequence. No
  reference image, noise profile, mask, strength map, or frame range is
  accepted.
- Handoff files: `srcFile.tiff` and `dstFile.tiff` in a temporary directory of
  the run's own, created below AfterNight's configured/system temporary
  location. They are removed when the run ends and are not final deliverables.
- Handoff precision: unsigned 16-bit TIFF, codes rounded to nearest. The CLI
  writes 16-bit LZW TIFF because AfterNight does not pass `--eight`.
- Final image: the target is replaced rather than duplicated. The answer is
  written once in the original sample type - 8-, 16- or 32-bit integer, signed
  or unsigned, or 32- or 64-bit floating point - but its values are the
  program's 16-bit answer brought back over the handed range.
- Metadata: the image's acquisition record, plate solution, FITS cards and
  processing state are kept whole; a linear image stays recorded linear. Every
  sample of the result is marked valid, since the program answers every pixel.
  The lineage records the program name, the version it reported, the exact
  arguments, the format handed (`UInt16`) and whether a pre-stretch was applied.
  DeepSNR-generated TIFF metadata is not used as processing provenance.
- History: **DeepSNR** with description
  `Denoised with DeepSNR. Stride: <s>. Pre-stretch: <enabled|disabled>`.
  The step is recorded before the view shows the result, and **Undo** restores
  the image as it was. The history text does not record the CLI version or
  weights path; the lineage does.
- Naming and files: the in-memory image keeps the current view name. AfterNight
  does not automatically save, rename, or retain the external output file.

## Performance

Runtime grows with image area and with the number of overlapping 512-pixel
windows. Reducing stride increases repeated inference; a stride of 256 has much
more overlap than 480, and values near 2 can be impractically expensive. Model
loading, 16-bit TIFF write/read, conversion, and the external runtime add fixed
and image-size-dependent costs. Enabling pre-stretch adds exact statistics plus
forward and inverse full-image transformation passes. A sequence costs one
program run per frame.

Current Windows DeepSNR 1.2.3 packages can use bundled DirectML and fall back to
CPU. The upstream package may also try a compatible CUDA provider when one is
deliberately installed. Linux and Intel macOS current packages use ONNX Runtime;
Apple Silicon packages use CoreML. AfterNight exposes no backend selector and
does not apply its own processing-thread/GPU settings to the external tool.
Read the subprocess debug output in the log to identify the backend actually
chosen.

For safe prototyping, duplicate the full image and begin with stride 480. A crop
smaller than 512 cannot run, and a crop that does run cannot reveal tile behavior
or noise compatibility across the full field.

## Worked example

- Evidence mode: Source-and-test-derived with native Windows CLI readiness
  evidence, not an approved-data GUI execution.
- Input condition/provenance and settings: The current AfterNight source and
  tests define pre-stretch Off, an empty **Weights** path, and stride `480`,
  which produces `--input`, `--output`, and `--stride 480`, plus
  `--machine-progress` when advertised. The reviewed Windows executable reported
  DeepSNR `1.2.3`; its help and machine-info output confirmed model 2 default,
  grayscale/RGB support, TIFF and standard FITS input, 16-bit TIFF and float32
  FITS output, the 512-pixel minimum, stride range, and v1 JSON progress.
- Expected checkpoint: Before image processing, **Settings > External Tools**
  and the process status should detect a supported `1.2.0+` version. A real
  successful run adds the DeepSNR history step only after a zero CLI exit and
  output load. The test suite's smoke case runs the installed program through
  the window over a synthetic `512 × 512` 16-bit gradient with Gaussian noise
  and requires the noise about the gradient to fall and the result to stay
  16-bit with one **DeepSNR** step; it is skipped where DeepSNR is not
  installed.
- Inspect at 100%/statistics/history: On approved project data, compare the
  result with the prior history state at matched stretch; inspect background
  grain, faint structures, small stars, and possible 512-window boundaries.
- Limitations: There was no approved noisy project image for execution. This
  example verifies readiness, the invocation contract and the synthetic smoke
  case only; it makes no visual-quality, timing, or backend-selection claim for
  real data.

## Warnings and limitations

- **OK** replaces the selected image after success. Image History records the
  step first, so **Undo** restores it; keep a duplicate when you want to compare
  runs side by side.
- DeepSNR is separately licensed proprietary software. Installing or using it
  requires accepting its license; AfterNight does not redistribute or relicense
  the executable or model weights through this process page.
- The DeepSNR license reviewed for 1.2.2 limits use to astrophotography,
  prohibits using the software, its components, or its outputs to create
  commercial software, and restricts sharing full-scale raw input images that
  demonstrate its performance or aid competing development. Read the installed
  `LICENSE.txt`; this guide is not legal advice.
- The learned model is best matched to mostly uncorrelated high-frequency noise.
  It is not a repair for walking noise, calibration residuals, banding, or poor
  integration.
- There is no preview, amount/blend, per-channel selector, or mask support.
  Localize or soften the result with a later, separately controlled workflow.
- Model 2 is selected implicitly by omitting `--model`. A custom **Weights** path
  overrides that default and may have different channel, quality, or license
  constraints that AfterNight cannot validate.
- DeepSNR takes only 16-bit input, so every image reaches it as 16-bit codes
  over its valid range. Samples outside `[0,1]` are not clipped, but the answer
  has the program's 16-bit resolution whatever the original sample type.
- **Pre-Stretch linear image** is a manual state choice. Enable it only for a
  linear source. The inverse returns the output toward the original linear
  scale, but cannot undo the program's 16-bit input or changes made by the
  learned model exactly.
- A sequence target is processed frame by frame and replaced as a whole. To
  denoise one frame, extract it first.
- A colour filter array mosaic is refused; demosaic it first.
- The exact DeepSNR trained mapping, edge padding, tile merge, clipping, and
  invalid-pixel behavior are not published upstream. Do not infer
  them from the NAFNet paper alone.
- External CLI versions and packages can change independently of AfterNight.
  AfterNight enforces only a minimum version, not a maximum or exact package
  hash.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| `DeepSNR executable path is empty.` | No global executable path is saved. | Click **Configure**, choose **Settings > External Tools > DeepSNR**, select the CLI executable, and Apply/OK. | `DeepSNRCliTools.cpp`; `ExternalToolsSettingsPage.cpp` |
| `DeepSNR executable was not found.` or `DeepSNR path is not executable.` | The saved path is missing or fails the platform executable check. | Restore the installed/portable package, select its actual executable, and keep runtime files in the documented layout. | `DeepSNRCliTools.cpp`; upstream installer layout |
| `Could not detect DeepSNR version. DeepSNR 1.2.0 or newer is required.` | Machine-info was unsupported or invalid and the bounded `--version` fallback failed or produced no parseable version. | For 1.2.3+, run `deepsnr --machine-info`; for older supported versions, run `--version`. Restore a current complete package if both readiness paths fail. | `parseMachineInfoText`; `readVersionOutput`; `checkExecutable` |
| `DeepSNR <version> detected. DeepSNR 1.2.0 or newer is required.` | The parsed version is below the supported CLI contract. | Install a current 1.2.x or newer CLI package and update the settings path. | `minimumSupportedVersion` in `DeepSNRCliTools.cpp` |
| `DeepSNR requires input images at least 512x512 pixels.` under **Error running DeepSNR** | Width or height is below the CLI processing window; the run refused before writing anything. | Use a larger image or choose another denoiser; do not upscale solely to bypass the validation. | `runExternalTool` in `ExternalToolSupport.cpp`; `DeepSnrWindowTest.AnImageBelowTheFloorIsRefusedByName` |
| `DeepSNR supports grayscale and RGB images only.` | The image has neither one nor three channels. | Remove alpha/unsupported channels or create a supported grayscale/RGB working copy. | `runExternalTool`; `ExternalToolsUnitsTest.ARunIsRefusedBeforeAnythingIsWritten` |
| `DeepSNR takes a grey or colour picture, not a colour filter array mosaic; demosaic the image first` | The image is a colour filter array mosaic. | Demosaic the image, then run DeepSNR on the result. | `runExternalTool`; `ExternalToolsUnitsTest.ARunIsRefusedBeforeAnythingIsWritten` |
| `Could NOT set temporary dir for DeepSNR execution` | The configured/system temporary root cannot create the per-run directory. | Check the General temporary-files location, permissions, and free space, then retry. | `runExternalTool`; `ensureTempDirPathOrSystemTemp` |
| `Could not start the DeepSNR process. Check the DeepSNR executable path in the configuration settings.` | The executable passed the readiness check but could not be started at execution time. | Recheck the path, permissions, package-local DLLs/runtime files, and security software. | `runProgram` `FailedToStart` branch; `ExternalToolsUnitsTest.EveryWayTheProgramCanFailIsSaid` |
| `DeepSNR process crashed. Check the DeepSNR executable path in the configuration settings.` | The external process terminated abnormally. | Restore matching executable/runtime/weights from one package; remove unsupported manual runtime substitutions; retry with default weights. | `runProgram` `Crashed` branch; upstream package README |
| `DeepSNR exited with code <n>.` followed by CLI text | The CLI returned a nonzero code, commonly due to weights, input, runtime, or backend errors. | Read the appended CLI output or machine-progress error message; clear custom weights, verify package files, free space, and supported input. | Nonzero-exit branch and collected CLI output; `ExternalToolsWindowTest.AFailedProgramIsSaidUnderTheWindowsTitle` |
| `the tool wrote no image file` or `the tool's image is <w>x<h> with <c> channels where the image is ...` | The program exited with code 0 but left no output, or an output of another size. | Verify the package and weights; the answer must be the input's size and channel count. | `readAnswer`; `ExternalToolsUnitsTest.EveryWayTheProgramCanFailIsSaid` |
| An engine refusal naming a sample that is not finite | A NaN or infinity sits in a pixel the image's validity rule admits. | Mark such pixels invalid or replace them before running. | `prepareExternalToolHandoff` in `ExternalToolHandoff.h` |
| Output loads but looks tiled | The default overlap may be insufficient for this image, or the model may be unsuitable; exact merge behavior is upstream-private. | Undo, keep weights fixed, compare stride 480 with **Conservative (256)**, and inspect the same locations. Do not assume smaller is always better. | Fixed 512-window/stride contract; process presets |
| Output has an unexpected tonal scale after pre-stretch | **Pre-Stretch linear image** was enabled for nonlinear data, or model changes prevented an exact inverse round trip. | Undo, confirm the source is linear, and compare a duplicate with pre-stretch disabled using the same display stretch. | `ExternalToolHandoff.h`; `ExternalToolsWindowTest.ThePreStretchIsInvertedOnTheWayBack` |
| Walking noise or banding remains | Upstream guidance says correlated noise behaves like structure and is a poor model match. | Correct acquisition/calibration/integration issues or use a process designed for the pattern. | DeepSNR input guidance |
| Every frame of the sequence changed, or the run took one program run per frame | A sequence target is denoised frame by frame and replaced as one history step. | Expected. Undo the step, extract the intended frame, and run DeepSNR on it. | `runInPlace` in `GeometryProcessSupport.cpp`; `ExternalToolsWindowTest.ASequenceIsRunFrameByFrame` |
| A busy warning titled `DeepSNR <target>` and nothing runs | Another run already holds the sequence view. | Wait for that run to land, then press **OK** again. | `runInPlace` lock branch |
| Cancel closes the progress window without a result or message | Cancellation kills the subprocess and publishes nothing. | This is expected. Rerun when ready; use a larger stride if runtime prompted cancellation. | `runProgram` stop branch; `ExternalToolsWindowTest.AStoppedRunPublishesNothing` |
| Closing the process window did not stop the run | Closing hides the window until the run lands; only the progress window's **Cancel** stops it. | Use **Cancel** on the progress window. | `ImageProcessControl::closeEvent`; `DeepSNRProcess::finishRun` |

## Related workflow steps

Calibrate and integrate data first with [Image Integration](../stacking-integration/image-integration.md)
or [Image Stacking](../stacking-integration/image-stacking.md). Correct gradients
with [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md)
or [Background Extraction](../background-extraction/background-extraction.md),
and resolve patterned noise before using DeepSNR. Compare native,
parameter-controlled alternatives such as [Wavelet Denoise](wavelet-denoise.md),
[Total Variation Denoise](total-variation-denoise.md),
[NL-Bayes](nl-bayes.md), and [Patch PCA Denoise](patch-pca-denoise.md). After denoising, continue with the
next linear or nonlinear step appropriate to your workflow and save the final
result explicitly.

The DeepSNR executable path is managed in
[Settings > External Tools](../../interface/settings-dialog.md). For process
target, history, and cancellation behavior, see
[Process Windows](../../interface/process-windows.md) and
[Image History](../../interface/image-history.md).

## References and provenance

- [DeepSNR command-line reference](https://starnetastro.com/documentation/deepsnr/command-line-tool/)
  documents the current options, inputs, outputs, model selection, 512-pixel
  minimum, stride range, and package layout.
- [DeepSNR input guidance](https://starnetastro.com/documentation/deepsnr/data-quality/)
  documents camera/channel suitability and the limitation with correlated
  noise such as walking noise.
- [Current DeepSNR CLI releases](https://starnetastro.com/cli-tools/deepsnr/)
  and [CLI installer locations](https://starnetastro.com/documentation/cli-installers/)
  document current platform versions, hashes, runtimes, and installed paths.
- DeepSNR 1.2.3's `LICENSE.txt` is the controlling reviewed license
  artifact. It identifies Mikita (Nikita) Misiura as copyright holder and says
  the architecture is based on NAFNet. Its SHA-256 is
  `7902179E9EF668C314A53882387D0F601E072882B62F2B983F2A95D74D2B3193`.
- The reviewed Windows 1.2.3-0119 archive has SHA-256
  `98B3F366375DA82388850CFB110306EA14FDE90EF3ACA1B4C273978BD3D6AE06`.
  Its file SHA-256 values are: `deepsnr.exe`
  `624975D63A27F96AD174097916F8291C571282263A6242DBCFA7F1FA162DF126`,
  `README.txt`
  `DC32555F75F1D5C6A9D750C75A54725BE4733D669BA8A38BE09A120020D83AF2`,
  model v1 `556B8D9AD9EC53BA5DE3DBE789AD627B0E2A2D777D52B5163ACAAFDE264EAD7C`,
  and model v2
  `CF5A129D235AF02B5C5980680B1A4A35ECBCCB81EAF3DE1AB51C68F92019F5FA`.
- DeepSNR cites the
  [official NAFNet implementation](https://github.com/megvii-research/NAFNet)
  and Chen et al.,
  [Simple Baselines for Image Restoration](https://arxiv.org/abs/2204.04676),
  ECCV 2022. These explain the architecture family, not DeepSNR's private
  weights or training behavior.
- AfterNight integration behavior was verified against the exact source and
  tests. Recommendations are labeled as guidance;
  no unexecuted visual or quantitative result is presented as fact.
