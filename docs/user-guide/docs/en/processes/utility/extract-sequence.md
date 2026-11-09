---
kind: process
stage: any
menu: Image > Sequence > Extract Sequence
summary: >-
  Creates a new sequence from a contiguous range of frames of the active sequence.
data_in: either
---
# Extract Sequence

## Purpose and expected result

Extract Sequence creates a new sequence containing one inclusive, contiguous
range from the active sequence. Use it to remove unwanted leading/trailing
frames, isolate a short interval for inspection, or make a smaller input for a
later process. The source sequence remains unchanged.

The result opens as a separate unsaved sequence view beside the source. It is
written in the source's own container: SER produces SER, an Image List produces
an `.anseq` plus its own frame files, AVI produces AVI, MP4 produces MP4 and
another video container produces Matroska (`.mkv`). The frames are copied as
the source recorded them; the new view shows them demosaiced when the source
view does. The process does not rank frames, accept a non-contiguous selection,
debayer the stored frames, align, stack, resample, or export an archival
destination.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Image > Sequence > Extract Sequence**. |
| Enabled when | An active sequence view is available. The whole **Image > Sequence** menu is disabled for a still image, and the window accepts only sequence views. |
| Inputs | One active nonempty SER, Image List, AVI, MP4 or other video sequence; one 1-based inclusive frame range. |
| Outputs | One new temporary sequence view containing `End - Start + 1` frames, in the source's container. The source sequence and its current frame are not modified. |
| Recommended image state | Linear/nonlinear state is not interpreted. Extract before downstream processing when the aim is to reduce frame count without changing pixels. |
| Preview support | No RT or full-image preview. Read-only count and size estimates update immediately. |
| Sequence support | Required. |
| Mask support | Unsupported and ignored; extraction copies whole frames. |
| External dependencies | The engine's SER, image-list, AVI and video readers and writers; bundled FFmpeg libraries for the video containers. No network or catalog. |

## Before you start

- Navigate to the first/last frames of interest in the [sequence workspace](../../interface/sequences.md)
  and note their 1-based player positions.
- Confirm free space in AfterNight's temporary directory for this session.
  **Sequence Size** is a sample-memory estimate, not the encoded size or the
  peak temporary space.
- Keep the source open until the extraction completes. The run holds the source
  view and pauses and disables its player while it reads.
- For Image List input, extraction writes each selected frame into a new FITS
  file. The extracted result is therefore self-contained within its temporary
  `.anseq`/`-frames` pair rather than merely referencing the original image
  paths.

## Procedure

1. Activate the intended sequence view and choose **Image > Sequence > Extract
   Sequence**. Confirm the title identifies that sequence.
2. Set **Start Frame** to the first frame to keep and **End Frame** to the last.
   Both are 1-based and inclusive. If Start is moved beyond End, Start snaps to
   End; if End is moved before Start, End snaps to Start.
3. Check **Selected Frames**. It must equal `End - Start + 1`. Check **Sequence
   Size** as a memory estimate for that count.
4. Select **OK**. The range is read at that moment; later edits of the controls
   do not change the run. The process disables OK/Reset/Close, takes the source
   view (pausing and disabling its player), and shows a progress window titled
   **Extract Sequence** with the text "Extracting new sequence from sequence
   *name*" and a **Cancel** button.
5. On success, the process window closes and a new sequence view opens beside
   the source. It shows the frame the source was showing when that frame is in
   the range, and the range's first frame otherwise. The view takes the source's
   name, with the normal unique-name suffix when needed.
6. If cancelled, nothing is published, no new view opens, nothing is left in
   the temporary directory, and the controls are re-enabled.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Start Frame | Linked integer spin box and slider; frame number | 1 | None | 1 through source frame count; step 1 | Sets the first included frame. Increasing drops frames from the beginning. | Cannot remain above End; it snaps back to the current End. | Smaller ranges reduce read/write time and temp disk. | Execution; whole sequence frames; 1-based inclusive | Process-window lifetime; Reset returns 1. |
| End Frame | Linked integer spin box and slider; frame number | Source frame count | None | 1 through source frame count; step 1 | Sets the last included frame. Decreasing drops frames from the end. | Cannot remain below Start; it snaps up to the current Start. | Smaller ranges reduce read/write time and temp disk. | Execution; whole sequence frames; 1-based inclusive | Process-window lifetime; Reset returns N. |
| Selected Frames | Read-only integer | `N` | None | Derived as `End - Start + 1` | Reports exact output frame count. | Recomputed after a valid Start/End change. | No processing effect. | Estimate/readout only | Not persisted. |
| Sequence Size | Read-only MB/GB estimate | Size for all `N` frames | None | `frame bytes × selected count`, displayed in MB (MiB) or, above 1024 MB, GB with two decimals | Scales linearly with selected count. | `frame bytes` is width × height × channels × bytes per sample of the image the view holds now - a demosaiced frame when the view demosaics its frames. | Not the encoded file size, free-space requirement, or peak memory guarantee. | Estimate/readout only | Not persisted. |
| OK | Button | Enabled while idle | None | One run at a time | Reads the range, takes the source view, writes the range into a new temporary sequence of the source's container, and opens a new view. | Disabled during execution. Requires an active sequence; another run holding the view stops it with a busy warning. | Full-range read/write; may use substantial IO and temporary space. | Full execution; all channels; mask ignored; sequence only | No parameter persistence; new result remains unsaved. |
| Reset | Button | Enabled while idle | None | Click action | Restores Start 1 and End N, updating count/size. | Disabled during execution. | Discards the edited range only. | Process window | No persistence. |
| Close | Button | Enabled while idle | None | Click action | Closes without extracting. | Disabled during execution. Closing the window another way while a run is in flight hides it until the run has landed. | Discards the edited range only. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. The process reads the source frame count `N` and constrains both controls
   to `1..N`.
2. On OK it reads the range from the controls, on the interface thread; the
   work that follows reads nothing of the window.
3. It takes the source view for the run. This pauses playback and disables the
   player until the run ends. If another run already holds the view, a warning
   titled **Extract Sequence** says "Another process is executing for this
   sequence, please cancel that job first before running this process", and
   nothing starts.
4. It asks the session's temporary-file service for a new path in the source's
   container (`SEQ_SWAP<token>.ser`, `.anseq`, `.avi`, `.mp4` or `.mkv`).
5. On a worker, the frames `Start` to `End` are read in order, as recorded - a
   mosaic frame is not demosaiced - and appended to the new sequence. The
   progress bar advances with each frame written, and Cancel is checked before
   each frame.
6. When the last frame is in, the new sequence is completed and published at
   its path whole; until then nothing exists under that name. The source view is
   released, the progress window closes, and a new sequence view opens beside
   the source. It demosaics its frames for display when the source view does.

### Model and equations

For visible 1-based controls `S` and `E`, the run reads the zero-based frames:

\[
s = S - 1, \qquad e = E - 1
\]

and the included frame count is:

\[
n = e - s + 1 = E - S + 1.
\]

The displayed size estimate is:

\[
B_{estimate} = n \times B_{frame},
\]

where `B_frame` is width × height × channels × bytes per sample of the image
the source view holds. The window divides by `1024²` for MB and by another 1024
for GB. Container headers, compression, Image List XML/filesystem overhead,
decoder buffers, and temporary peak usage are not included.

No interpolation, arithmetic transform, clipping, normalization, channel
selection, mask blend, or demosaic is applied by the extractor. The destination
container's writer still stores a frame as that container can:

- **SER** stores unsigned 8- or 16-bit mono, two-by-two mosaic or RGB frames. A
  SER source's frames already fit, so they are copied unchanged, and each
  frame's exposure start is kept in the new SER's timestamp trailer, as an
  unknown entry for a frame that states none; the new SER states the rate those
  timestamps give.
- **Image List** writes each frame as its own FITS file with the frame's header
  cards, in the first extracted frame's sample type; a later frame of another
  sample type is converted to it to the nearest code.
- **AVI** stores uncompressed 8-bit grey or RGB frames.
- **MP4 and Matroska** encode 8-bit grey or RGB frames again with the video
  encoder, at an even width and height, so a compressed video's frames are not
  copied bit for bit.

The new sequence keeps the source's nominal frame rate, or 1 frame per second
when the source states none.

### Boundary and failure behavior

- Both endpoints are always within `1..N`, and edits cannot leave Start after
  End. A one-frame range is valid.
- Cancellation stops the run before the next frame, publishes nothing, removes
  what was written, and leaves the source unchanged. The process window stays
  open with its controls re-enabled; no message is shown.
- If the source view is already held by another run, the busy warning above is
  shown and no second extraction starts.
- If the application lends no temporary files, the run does not start. A
  message titled **Error running Extract Sequence** says "An error occurred when
  executing the Extract Sequence process:" followed by "the application lends no
  temporary files", and the source view is released.
- A read, write or completion failure shows **Error running Extract Sequence**
  with the same leading sentence and the reason, writes it to the log, releases
  the source view, and removes the incomplete output. The process window stays
  open with its controls re-enabled.
- No partial destination view or temporary sequence is retained on cancellation
  or failure.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Start + End | First identify endpoints with the sequence player, then enter those exact 1-based values. | Extra unwanted frames remain at the beginning/end. | Useful boundary frames are missing. | Set Start, set End, verify count, revisit both boundary frames in the source. |
| Range + downstream test | Extract the smallest interval that still contains representative behavior. | The test range misses transitions/artifacts found elsewhere. | The subset is needlessly large and slow. | Identify purpose, include representative boundaries, extract, then validate downstream. |
| Size estimate + free space | Treat the estimate as a rough indicator and keep additional headroom. | Extraction fails when temp storage fills. | Excessive range consumes avoidable IO/disk. | Check temp location, estimate, actual free space, then reduce range if needed. |

## Inputs, outputs, and metadata

- Input count is one active sequence; output count is one new sequence when the
  run succeeds.
- Output frame count is exactly `End - Start + 1`, preserving selected order.
- The source name is applied to the new sequence and the view manager adds a
  normal uniqueness suffix such as `_1` when needed.
- The new view begins its Image History with **Extract Sequence: Extracted
  frames *S* to *E* from sequence '*name*'**. No step is added to the source's
  history.
- The source sequence and its backing file are not modified.
- Extracted frames keep the sample type, channels and filter pattern the source
  recorded, subject to the destination container's writer described above. An
  Image List's new frame files carry the source frames' header cards.
- The destination path is in the session's temporary directory. Save/export it
  before closing if it must survive cleanup.

## Performance

Runtime scales approximately with selected frame count × pixels × channels. SER
and Image List reads are direct; a video source is decoded frame by frame and,
for MP4 and Matroska, encoded again. Image List output writes one FITS file per
selected frame plus the manifest.

The worker runs off the interface thread and progress is cancellable. The source
player is paused/disabled while the run holds the view. Reduce Start/End before
running when only a diagnostic subset is required, and keep the temporary
directory on a fast volume with sufficient free space.

## Worked example

- Evidence mode: Source-and-test-derived.
- Input condition/provenance and settings: a 3-frame Image List of 32-bit
  floating RGB frames, the second different from the other two, with Start
  Frame 2 and End Frame 3 - the sequence
  `GeometryProcessCase.ExtractSequenceWritesTheRangeAsRecorded` builds.
- Expected checkpoint: the dialog reports **Selected Frames 2**. OK opens one
  new Image List sequence view under the source's name whose history begins
  "Extract Sequence: Extracted frames 2 to 3 from sequence '*name*'", and whose
  two frames are, sample for sample, the source's second and third.
- Inspect at 100%/statistics/history: verify the new frame count and container
  in the status bar and, with real data, compare both new boundary frames to
  the recorded source positions.
- Limitations: the case proves Image List range, order, naming, history and
  sample identity. It does not prove SER timestamps, every video codec, header
  cards, large-sequence performance, or visual quality.

## Warnings and limitations

- Endpoints are inclusive and 1-based. Do not subtract one yourself.
- Only one contiguous range is supported. There is no per-frame checkbox,
  quality score, rejection criterion, or disjoint selection.
- **Sequence Size** is not actual output size or a disk-space preflight.
- The extractor does not debayer stored frames. A mosaic is written as a
  mosaic; the new view demosaics it for display only when the source view
  does. Data extraction and display debayering are distinct.
- Image List extraction writes each frame as a new FITS file and can use much
  more temporary disk than the source `.anseq` XML list. Every frame is written
  in the first frame's sample type, so a frame of a deeper one is narrowed.
- MP4 and Matroska extraction decodes and re-encodes; it is not a lossless
  packet trim and does not preserve audio.
- A failed or cancelled extraction releases the source view and re-enables
  the player and process actions.
- The result is temporary/unsaved. Extraction is not an archival save/export
  operation.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Extract Sequence is unavailable | The active view is not a sequence, or no view is active. | Activate/open the intended sequence. | `ExtractSequenceProcess::isViewControlSupported()`; registry. |
| Start will not move past End | The window enforces an ordered inclusive range. | Move End later first, then increase Start. | `on_startFrameChanged()`. |
| End will not move before Start | The window enforces an ordered inclusive range. | Move Start earlier first, then decrease End. | `on_endFrameChanged()`. |
| Size estimate differs from the file on disk | The readout is sample bytes of the image the view holds, excluding encoding and filesystem overhead. | Use it only for relative planning; inspect actual temp/output size after completion. | `updateSizeAndFrameCount()`; `imageBytesOf()`. |
| "Another process is executing for this sequence, please cancel that job first before running this process" | Another run holds the source view. | Finish or cancel the other process, then retry. | `runToNewViews()`; `GeometryProcessCase` cases. |
| **Error running Extract Sequence** | No temporary files are lent, or a read, write or completion failed; the message names the reason. The source view is released and partial output is removed. | Check source readability, temp free space/permissions, logs, and codec support; preserve the original and retry a smaller range. | `runToNewViews()`; `reportRunFailure()`; `GeometryProcessCase.ASequenceRunThatCannotFinishLeavesNothingBehind`. |
| Cancelling creates no result | Cancellation intentionally publishes nothing. | Run the intended range again. | `runOverFrames()` cancellation path. |
| Video result differs in size or appearance | MP4 and Matroska extraction decodes and re-encodes frames, at an even width and height. | Use SER or an Image List when scientific sample preservation matters; compare decoded frames before relying on video output. | `SequenceContainers.h`. |

## Related workflow steps

- Use [Sequences](../../interface/sequences.md) first to navigate and record the
  desired boundary frame numbers.
- Continue with [Image List Analyzer](../stacking-integration/image-list-analyzer.md)
  for sequence-like inspection/filtering of still-file lists, or [Image
  Stacking](../stacking-integration/image-stacking.md) when the selected frames
  should be combined.
- See [Planetary Imaging](../../workflows/planetary-imaging.md) for the supported
  sequence workflow and its current lucky-imaging limitations.
- Consult [File Formats](../../reference/file-formats.md) before choosing an
  archival representation and [Saving And Exporting](../../interface/saving-and-exporting.md)
  for the complete save/export and verification behavior.

## References and provenance

- [FFmpeg Formats Documentation](https://ffmpeg.org/ffmpeg-formats.html), current
  upstream libavformat documentation, accessed 2026-07-11.
- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf),
  primary format description, accessed 2026-07-11.
- AfterNight sources/tests.
