---
kind: process
stage: noise
menu: Process > Mask Generation > Starnet++ Star Mask
summary: >-
  Opens a star mask made by the separately installed StarNet2 tool beside the target, which stays unchanged.
data_in: linear
derived: true
---
# StarNet++ Star Mask

## Purpose and expected result

StarNet++ Star Mask hands the current image, or every frame of the current
sequence, to a separately installed StarNet2 CLI 2.5.3 or newer and opens the
program's own star mask as a new view beside the target. The target is never
changed. Optional shaping reduces a colour mask to its CIE lightness and then
turns that lightness into a smoothed binary range mask.

Use this process when a learned StarNet2 mask is preferable to AfterNight's
analytical [Star Mask](star-mask.md). Use [Starnet++](starnet.md) instead when
the workflow also needs a starless image or an unscreen star layer.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Mask Generation > Starnet++ Star Mask** |
| Enabled when | The menu action follows the normal single-image launch gates: an image or sequence view is active, no duplicate StarNet++ Star Mask window is attached to it, and no attached process holds exclusive access. The dialog remains reachable without StarNet2; its processing controls and **OK** require a configured `starnet2` executable that passes the 3-second 2.5.3+ probe |
| Inputs | The current grayscale or RGB image in any of its representations (8-, 16- or 32-bit integer, signed or unsigned, 32- or 64-bit floating point); AfterNight hands the program a temporary unsigned 16-bit TIFF. A colour filter array mosaic is refused: demosaic it first |
| Outputs | One new still named `<target> - STARMASK`, or one new sequence of that name over a sequence view; the starless file StarNet2 must also write is not loaded or published |
| Recommended image state | A calibrated, registered, integrated linear master with **Pre-Stretch linear image** on; leave it off for already nonlinear input |
| Preview support | None. **OK** starts the external run directly |
| Sequence support | Every frame is handed to the program in turn, demosaiced when the view demosaics its frames; the masks are written into a new sequence that is published whole and opened beside the target. The progress window counts the frames |
| Mask support | The target's active AfterNight mask is ignored. This process creates a mask image; it does not attach that image to the source |
| External dependencies | Official StarNet2 CLI and model/runtime package, version 2.5.3 or newer |

## Before you start

1. Install an official StarNet2 CLI package and keep its executable, default
   model, runtime files, README, and license in the layout required upstream.
2. In **Edit > Settings > External Tools**, configure `starnet2`. The dialog
   disables processing for an empty, missing, non-executable, too-old, timed-out,
   or unparseable executable.
3. Debayer CFA data first. The run refuses a colour filter array mosaic by name
   and asks for the image to be demosaiced; over a sequence whose view demosaics
   its frames, each frame is demosaiced before it is handed to the program.
4. Use an image at least `512 x 512` pixels in normal mode. StarNet2 publishes
   that minimum, but AfterNight does not prevalidate it.
5. Ensure the temporary-files volume can hold the 16-bit source, starless, and
   mask TIFFs. They are written into a directory of the run's own below the
   session's temporary directory and removed when the run ends.

!!! note "The run reads the dialog once; the progress window's Cancel stops it"
    Pressing **OK** reads every control on the GUI thread, and the run reads
    nothing else: a change made to the target, the options, or the thresholds
    while StarNet2 runs does not reach that run and applies to the next one.
    Stop a run with **Cancel** on the progress window; it kills StarNet2,
    publishes nothing, and reports nothing. Closing the process window during
    a run hides the window until the run lands and does not stop the run.

## Procedure

1. Select the source and open **Process > Mask Generation > Starnet++ Star
   Mask**.
2. Confirm the green readiness message and intended **Target Image**.
3. For linear input, enable **Pre-Stretch linear image**. Leave **Highlight
   protection** on, **Stride** at **Standard (256)**, **Upsample x2** off, and
   **Weights** empty for a first run.
4. Leave **Lightness** off to preserve StarNet2's native mask channels. Enable
   it when a one-channel lightness mask is wanted.
5. To threshold a multichannel mask, enable **Lightness**, then **Binary Mask**.
   Set the inclusive **Lower limit**/**Upper limit** interval and use
   **Smoothness** only when hard threshold edges need softening.
6. Click **OK**. A progress window titled **Starnet++ Star Mask `<target>`**
   shows the run's stages and the program's own progress; its **Cancel** kills
   StarNet2. Over a sequence it counts the frames.
7. Inspect `<target> - STARMASK` at 100% with a consistent display stretch.
   Look for missed stars, included nebula detail, bright-star residuals, seams,
   and halos before attaching it as a processing mask.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Pre-Stretch linear image | Checkbox | Off | None | Off/On | Applies one linked automatic midtones transfer, derived from the channels' exact medians and median absolute deviations, before the 16-bit hand-off | Disabled until CLI readiness passes | Helps linear data meet model expectations; the mask is generated from the stretched input and is never unstretched, unlike the starless image of Starnet++ | External input bridge; all channels; each still or frame | Dialog lifetime |
| Highlight protection | Checkbox | On | None | On/Off | On keeps StarNet2's default protection; Off adds `--disable-highlights-protection` | Disabled until readiness passes | Off is an intentional bright-region comparison and can worsen residuals | Native inference; each still or frame | Dialog lifetime |
| Stride | Selector, pixels | **Standard (256)** | None | **Standard (256)**, **Wide-field (384)**, **Custom** | Sets the CLI processing-window stride | **Custom** enables its value; disabled until readiness | Upstream recommends 256 generally and 384 for wide fields with small stars; smaller strides usually cost more | Native inference | Dialog lifetime |
| Custom stride value | Integer pixels | `256`, disabled | None | Even `2..512`; step `2`; odd programmatic values normalize downward after clamping | Chooses exact `--stride` | Enabled only for **Custom** and readiness | Extreme values can increase work or change overlap behavior; exact blending is upstream-private | Native inference | Dialog lifetime |
| Weights | Path | Empty; placeholder **Default StarNet2_weights.onnx** | None | Empty or model path | Empty uses the bundled model; nonempty passes trimmed `--weights` | Disabled until readiness | A custom model can change quality, compatibility, runtime, and licensing; AfterNight does not prevalidate it | Native model; a relative path resolves from the program's own directory, where it is started | Dialog lifetime |
| Browse | File action | No selection | Remembers the last browser folder context | `*.onnx` plus all files | Writes the chosen path into **Weights** | Disabled until readiness | File selection is not a model compatibility test | Configuration only | Folder remembered; chosen model remains dialog-only |
| Upsample x2 | Checkbox | Off | None | Off/On | Adds `--upsample` | Disabled until readiness | Can help tight/tiny stars and radiating artifacts; upstream estimates roughly four times the compute/runtime and warns about very large stars | Native inference | Dialog lifetime |
| Readiness message | Status text | Current probe result | Executable path comes from global Settings | Ready or detailed failure | Reports whether controls and **OK** are usable | Rechecked at open and after **Configure**; the run probes the program again before it starts | Each probe asks `starnet2 --machine-info`, then `--version` when that is not answered, within a 3-second budget | Configuration only | Reflects global path |
| Configure | Settings action | Enabled | Global StarNet2 path persists | Opens **Settings > External Tools** | Reloads path and repeats readiness detection after Settings closes | Always enabled | Does not run inference or validate the default model | Configuration only | Global Settings |
| Lightness | Checkbox | Off | None | Off/On | For a colour (RGB) native mask, reduces it to one grey plane holding its CIE `L*` lightness in the unit interval | Disabled until readiness; controls **Binary Mask** | Removes mask color differences; has no effect when StarNet2 returns one channel | Postprocessing; new mask still or sequence | Dialog lifetime |
| Binary Mask | Checkbox | Off, disabled | None | Off/On | Applies an inclusive range selection to the lightness, then optional smoothing | Enabled only with **Lightness** and readiness | Produces a hard one/zero selection before optional smoothing. It is bypassed for a one-channel native mask | Postprocessing only | Dialog lifetime |
| Lower limit | Normalized value | `0.1` | None | `0..1`; spin step `0.01` | Raises/lowers the faintest selected lightness; moving it above Upper pulls Upper up to match | Enabled only with **Lightness + Binary Mask** | Higher values exclude faint mask signal; equal endpoints select only that exact level before smoothing | Inclusive binary range | Dialog lifetime |
| Upper limit | Normalized value | `1.0` | None | `0..1`; spin step `0.05` | Lowers/raises the brightest selected lightness; moving it below Lower pulls Lower down to match | Enabled only with **Lightness + Binary Mask** | Lower values exclude bright mask signal | Inclusive binary range | Dialog lifetime |
| Smoothness | Gaussian sigma, pixels | `0` | None | `0..100`; spin step `1` | Applies an exact Gaussian blur of this sigma to the binary result; `0` applies none | Enabled only with **Lightness + Binary Mask** | Softens threshold edges and spreads transitions; a larger sigma costs more and can merge protection regions | Post-threshold mask | Dialog lifetime |
| Target Image | Image/sequence selector | Attached active target | None | Open eligible view | Chooses the image or sequence the run reads and the output's parent and name | Active target required; read when **OK** is pressed | A change made during a run applies to the next run only | Full image; active mask ignored; a sequence is run frame by frame | Target lifetime |
| OK | Execution action | Enabled only when ready | None | Click | Reads the dialog, opens the progress window, and starts the run on a worker | Disabled while a run is in flight and enabled again when it lands | Writes temporary source/starless/mask TIFFs and publishes one new mask still or sequence on success | No preview; new still or sequence | One run; dialog remains open |
| Close | Dialog action | Enabled | None | Click | Closes an idle dialog; during a run hides the window until the run lands, then closes it | Remains enabled while running | Does not stop the run; use the progress window's **Cancel** for that | Current window | Discards dialog options |
| Progress / Cancel | Progress action | `0..100`: preparing `1`, executing `10`, the program's progress mapped to `10..90`, processing output `95`, done `100` | None | Cooperative Cancel | Shows the program's printed or machine-readable progress and kills StarNet2 on Cancel | Exists only after **OK**; over a sequence it counts the frames | The stop is seen within the program's 300 ms read wait, once the handed file is written and before the program starts, and before each answer is adopted | Current external run | Per run |

## How the algorithm works

### Conceptual pipeline

1. When **OK** is pressed, the dialog is read into one settings value on the
   GUI thread, the progress window opens, and the run starts on a worker that
   reads nothing of the window. The worker refuses a colour filter array mosaic
   and an image that is not one or three channels, probes the program again,
   and creates a `starnet2` directory of its own below the session's temporary
   directory.
2. The engine prepares the hand-off: an integer image is read as its fraction
   of full scale; a floating-point image is mapped from the unit interval
   widened to its smallest and largest finite valid samples, so samples below
   `0` or above `1` reach the program whole rather than clipped. A pixel the
   image's validity rejects is handed as zero and left out of the statistics; a
   non-finite sample in a valid pixel refuses the run. With **Pre-Stretch
   linear image**, one linked automatic midtones transfer is derived from the
   exact medians and median absolute deviations of the mapped channels (shadows
   clipping `-2.8` deviations, target background `0.25`) and applied to every
   channel. The result is written as unsigned 16-bit codes, rounded to nearest
   with ties to even, into an interleaved `srcFile.tiff`, whole or not at all.
3. StarNet2 is started in its own directory with `--input`, `--output`,
   `--mask`, and `--stride`, plus the selected `--weights`, `--upsample`, and
   `--disable-highlights-protection`, and `--machine-progress` when the probe
   reported that the program prints machine-readable progress. Its output is
   read every 300 ms; progress lines move the progress window, other lines go
   to the log, and a stop request kills the program. StarNet2 must create both
   the starless and the mask files; AfterNight discards the starless file.
4. The mask is read through the engine's codecs, held to the source's size and
   channel count, and narrowed to the target's representation to the nearest
   value, saturating. It is the program's own answer: it is never unstretched
   and never mapped back.
5. If **Lightness** is on and the mask has three channels, its CIE `L*`
   lightness is extracted as one grey plane in the unit interval. Only inside
   that branch, **Binary Mask** selects the lightness between the limits and
   then smooths it. The result goes back to the mask's own representation.
6. The mask is published with derived metadata (below) and opened as a new view
   beside the target. The temporary directory and its three TIFFs are removed.
   Over a sequence, steps 2 to 5 run once per frame and the masks are written
   into a new sequence that is published whole and opened once every frame has
   landed.

### Model and equations

StarNet2's inference and native subtractive-mask equation are not published in
the reviewed upstream material, so AfterNight does not infer them.

For an RGB mask, **Lightness** uses the engine's CIE lightness extraction and
retains only normalized `L*`. With **Binary Mask**, a sample `x` becomes

`B(x) = 1` when `lower <= x <= upper`, otherwise `B(x) = 0`.

The endpoints are inclusive and the selection has no fuzziness, so there is no
threshold ramp. **Smoothness > 0** then applies an exact Gaussian blur with that
sigma, in pixels, to `B` - the blur the Gaussian Blur process runs, at every
sigma the dialog offers and on any image; it does not change the selection
thresholds. The order is the one the Range Selection process uses: select, then
smooth.

### Boundary and failure behavior

- Missing/old/unparseable StarNet2 disables **OK**; the run probes the program
  again before it starts and reports the probe's detail if it fails.
- A colour filter array mosaic is refused with a request to demosaic the image
  first; an image of two or four channels is refused as grayscale/RGB only.
  Both are refused before anything is written.
- A program that could not start, crashed, or exited with a nonzero code
  publishes nothing and is reported under **Error running StarNet2**; the
  nonzero-exit text includes what the program printed.
- A mask file the program did not write, or one of another size or channel
  count than the image, is refused by name and nothing is published.
- **Cancel** on the progress window kills the program; a stop is also honoured
  while the handed file is prepared, once it is written and before the program
  starts, and before each answer is adopted. A stopped run publishes nothing
  and reports nothing.
- The temporary directory is removed on success, failure, and cancellation.
- Every failure is reported in a message titled **Error running StarNet2** and
  in the log; over a sequence a failure in any frame publishes no sequence.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Linear input bridge | **Pre-Stretch** on; default model, stride 256, highlight protection on | Missed faint/tight stars or incomplete native mask | Nebula detail or compact structures included in mask | Inspect native result; compare color input, upsample, then stride/model only with a reason |
| Lightness cleanup | **Lightness** on, Binary off | Color-channel mask differences remain when one grayscale mask is wanted | Important chromatic mask distinction is discarded | Compare native versus lightness output first |
| Binary cleanup | Lower `0.1`, Upper `1.0`, Smoothness `0` | Unwanted low-level mask signal remains | Faint star support disappears or regions merge after smoothing | Set interval first, then add the smallest useful smoothing |

## Inputs, outputs, and metadata

The process accepts the current grayscale or RGB image in any representation and
publishes one still, or one sequence, in the target's representation. The
mask's channel count comes from the StarNet2 mask: a three-channel mask stays
three channels unless **Lightness** makes it one, and a native one-channel mask
remains one channel. The source is unchanged and no history step is added to
it; the new view's history begins with `StarNet2: Starmask image created from
'<target>'`.

The output name is `<target> - STARMASK`. Its metadata is derived from the
target's: the acquisition record, the plate solution (the pixel grid is the
target's, so the solution still describes it), and the lineage are kept; the
processing-state cards mark it nonlinear under `starnet-star-mask`; no other
FITS card of the target is carried. The lineage records the program's name and
version, the exact arguments it was started with, that the image was handed as
16-bit codes, and whether it was pre-stretched; when **Lightness** reduces a
colour mask it also records the lightness extraction, whatever the mask's
representation, and when **Binary Mask** is applied the selection's limits and
the smoothness.

## Performance

StarNet2 inference dominates and depends on dimensions, backend, model, stride,
and **Upsample x2**. The bridge holds the handed copy and the answer in memory
and writes three 16-bit TIFFs per image. Lightness extraction and binary shaping
add full-image passes; positive Smoothness adds an exact Gaussian pass whose cost
grows with the sigma. A sequence repeats all of it per frame. No production
timing, GPU, or peak-memory measurement was made for this revision.

## Worked example

- Input/settings: a compatible current master, **Pre-Stretch** chosen according
  to image state, default model, highlight protection on, stride 256, upsample
  off, and native mask cleanup off.
- Expected checkpoint: one new `<target> - STARMASK` still appears only after a
  normal program exit and a successful mask read; the target's history is
  unchanged.
- Inspect at 100%: compare against the source for missed stars, included
  nonstellar detail, seams, bright cores, and halos with one display stretch.
- Limitation: this proves control/data flow, not output quality or compatibility
  with a particular non-default model.

## Warnings and limitations

- This is an external learned-model result, not an analytical star detector.
- The active AfterNight mask and the display autostretch are ignored.
- The mask is generated from the 16-bit, optionally pre-stretched input and is
  intentionally never unstretched or mapped back; a floating-point target's
  wider range reaches the program whole but its mask is a fraction of full scale.
- A run over a sequence processes every frame; extract a frame first when only
  one is wanted.
- **Binary Mask** is bypassed when the native StarNet2 mask has one channel,
  even though its label describes a general mask operation.
- AfterNight does not validate a custom model before launch or prevalidate
  StarNet2's published minimum dimensions.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Processing controls and OK are disabled | Executable path/version/readiness probe failed | Use **Configure** and select official StarNet2 2.5.3+ |
| Binary controls remain disabled | **Lightness** is off | Enable **Lightness**, then **Binary Mask** |
| Binary settings do not change a grayscale result | Native mask has one channel, so the entire Lightness/Binary branch is skipped | Use the native grayscale mask as-is or apply a separate range/mask process afterward |
| `StarNet2 takes a grey or colour picture, not a colour filter array mosaic; demosaic the image first` | The target is a CFA mosaic | [Debayer](../calibration/debayer.md) first, or enable demosaicing on the sequence view |
| `StarNet2 supports grayscale and RGB images only.` | The target has two or four channels | Extract or compose a one- or three-channel image first |
| `Could not start the StarNet2 process` | The configured executable could not be started | Recheck executable path, permissions, and package files |
| `StarNet2 exited with code ...` | CLI returned nonzero; captured output is included | Read the detail; check dimensions, input/model/runtime compatibility, and disk space |
| `the tool wrote no mask file` or `the tool's mask is ... where the image is ...` | StarNet2 ended normally without a mask, or wrote one of another size | Check the package's runtime files and model, and that the input size is supported |
| Run stops silently with no new view | **Cancel** was pressed on the progress window | Run again; a stop publishes nothing by design |
| Result contains nebula filaments or misses stars | Learned inference included structure or missed stellar signal | Compare color input, inspect stretch, then test upsample or justified stride changes |

## Related workflow steps

Start from calibrated/integrated data, and [Debayer](../calibration/debayer.md)
CFA input first. Use [Masks](../../interface/masks.md)
to attach and configure the result. Use [Starnet++](starnet.md) when starless or
unscreen outputs are also needed, or [Star Mask](star-mask.md) for an analytical
detector. A verified mask can then constrain [Star Reduction](star-reduction.md)
or another local operation.

## Credits

- [StarNet command-line tool reference](https://starnetastro.com/documentation/starnet/command-line-tool/)
  documents CLI 2.5.3 options, supported integer grayscale/RGB input, 16-bit
  mask output, even `2..512` stride, model override, upsampling, highlight
  protection, and the `512 x 512` normal-mode minimum.
- [StarNet tips and tricks](https://starnetastro.com/documentation/starnet/tips-and-tricks/)
  supplies current upstream guidance for color input, 256/384 stride, 2x
  upsampling, and inspecting masks for accidentally removed structure.
- [Current command-line releases](https://starnetastro.com/cli-tools/) records
  the current 2.5.3 platform packages and packaged runtime behavior.
