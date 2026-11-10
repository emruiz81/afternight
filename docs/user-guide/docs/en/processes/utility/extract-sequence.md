---
kind: process
stage: any
menu: Image > Sequence > Extract Sequence
summary: >-
  Copies a range of consecutive frames of the active sequence, as they were recorded, into a new sequence of the same
  container, which opens beside the source.
data_in: either
data_out: either
destructive: false
preview: none
sequences: true
masks: none
external: none
glance:
  enabled: >-
    A sequence view is active. The Image > Sequence menu is disabled for a still image, and the window opens only on a
    sequence.
  inputs: One SER, image list, AVI, MP4 or other video sequence that holds at least one frame, and a range of its frames.
  outputs: >-
    A new, unsaved sequence view of the frames in the range, in the source's container, kept in the session's temporary
    folder. The source, its current frame and its history are not changed.
  state: >-
    Any sequence, linear or not, before its frames are processed or stacked, when only part of it is wanted.
derived: true
controls_ignore:
  - label: Start Frame
    reason: >-
      The window's inventory was recorded on a still image, where the window holds no frames; the block The frame
      range describes the box on a sequence.
  - label: End Frame
    reason: >-
      The window's inventory was recorded on a still image, where the window holds no frames; the block The frame
      range describes the box on a sequence.
rewritten: true
---
# Extract Sequence

This process copies a range of consecutive frames of a [sequence](../../reference/glossary.md#sequence) into a new
sequence. Use it to drop frames at the start or the end of a capture, to keep a short part of it to look at, or to
make a smaller input for a later process. The frames are copied as the file recorded them: nothing is debayered,
aligned, stacked, resampled or ranked.

**OK** opens the new sequence in a view of its own beside the source, in the source's container and not yet saved. The
source and its [history](../../reference/glossary.md#history) are not changed.

```figure
image: ../../assets/images/processes/utility/extract-sequence.webp
alt: >-
  The Extract Sequence window. Under the title, the process name and the line Create a new sequence from a selected
  frame range. Below, Start Frame at 11 and End Frame at 60, each with a box and a slider, then Selected Frames at 50
  and Sequence Size in megabytes. Along the bottom, Reset at the left, and OK and Close at the right.
caption: >-
  Extract Sequence on a planetary video with Start Frame at 11 and End Frame at 60, which keeps 50 frames.
pins:
  - {n: 2, x: 25.5, y: 34.3}
  - {n: 3, x: 25.5, y: 45.6}
  - {n: 4, x: 36.0, y: 54.4}
  - {n: 5, x: 49.9, y: 91.1}
```

## Before you start

- Find the first and the last frame to keep in the player of the sequence's view, and note their numbers. They count
  from 1, as the boxes do: type them as they are.
- Check the free space of the session's temporary folder, best on a fast disk. **Sequence Size** is a guide to it, not
  the space the files take.
- Keep the source open until the run ends. The run holds its view, and pauses and disables its player.
- From an image list, each frame is written again as a [FITS](../../reference/glossary.md#fits) file of its own. The
  new sequence then stands alone, and can take much more temporary disk than the list it came from.
- The frames are copied as recorded. A mosaic stays a mosaic: [debayer](../../reference/glossary.md#debayer), align or
  stack the frames afterwards with the processes made for that.

## Steps

1. **Open the window.** Make the sequence's view active and choose **Image > Sequence > Extract Sequence**. The title
   names the sequence, **End Frame** shows its last frame and **Selected Frames** counts all of them.
2. **Set the first frame.** Type the number of the first frame to keep in **Start Frame**.
3. **Set the last frame.** Type the number of the last frame to keep in **End Frame**. Both frames are kept.
4. **Check the count.** **Selected Frames** shows how many frames the new sequence will hold, and **Sequence Size**
   about how much memory their samples take.
5. **Extract.** Choose **OK**. A progress window follows the frames, then the window closes and a new sequence view
   opens beside the source, under its name.

## Controls

The header of the window works as in every process window: see [Process Windows](../../interface/process-windows.md).
The window has no preview and no **Target Image**: it works on the sequence it opened on.

```controls
window: The frame range
controls:
  - name: Start Frame
    kind: spinbox
    default: "1"
    widget: {min: 1, step: 1}
    effect: Sets the first frame the new sequence keeps. Higher drops more frames from the start.
    range: From 1 to the sequence's frame count, counted as the player counts them, and kept itself. The slider beside it moves the same value. Raised above **End Frame**, it goes back to **End Frame**.
    tradeoff: A shorter range takes less time to read and write, and less temporary disk.
    scope: Whole frames with every channel, read when you press **OK**. A change made during the run does not reach it.
    persistence: Until the window closes. **Reset** returns it to 1.
  - name: End Frame
    kind: spinbox
    default: The sequence's frame count
    widget: {min: 1, step: 1}
    effect: Sets the last frame the new sequence keeps. Lower drops more frames from the end.
    range: From 1 to the frame count, counted as the player counts them, and kept itself. The slider beside it moves the same value. Lowered below **Start Frame**, it goes back to **Start Frame**.
    tradeoff: A shorter range takes less time to read and write, and less temporary disk.
    scope: Whole frames with every channel, read when you press **OK**.
    persistence: Until the window closes. **Reset** returns it to the last frame.
  - name: Selected Frames
    kind: readout
    default: The sequence's frame count
    effect: Shows how many frames the new sequence will hold, which is **End Frame** minus **Start Frame**, plus one.
    enabled: Counted again after each change of either box.
    scope: The window only.
  - name: Sequence Size
    kind: readout
    default: The size of every frame
    effect: Shows how much memory the samples of the selected frames take, in MB, or in GB above 1,024 MB, with two decimals.
    range: The bytes of one frame as the view holds it, times **Selected Frames**. A frame's bytes are its width, height, channels and bytes per sample after the view's demosaic, when the view demosaics its frames.
    tradeoff: It counts the samples alone. It is not the size of the files, not the free disk space the run needs and not its peak memory. When the view demosaics a mosaic, it counts three channels where the file stores one.
    scope: The window only.
```

```controls
window: Extract Sequence
controls:
  - name: OK
    kind: button
    default: ""
    effect: Reads the range, holds the source's view, copies the frames into a new sequence in the source's container, and opens it in a new view beside the source. The window then closes.
    enabled: Disabled while the run works, with **Close** and **Reset**. While another run holds the source's view, a box titled **Extract Sequence** shows "Another process is executing for this sequence, please cancel that job first before running this process", and nothing starts.
    tradeoff: The whole range is read and written once, which can take time and much temporary disk. The time grows with the frames, their pixels and their channels. A video is decoded frame by frame, and MP4 and Matroska are encoded again. The source's player is paused and disabled while the run holds the view.
    scope: Whole frames and every channel. No mask is read, and nothing is blended.
    persistence: "The new sequence is unsaved, in the session's temporary folder. Its history starts with `Extract Sequence: Extracted frames 11 to 60 from sequence '<name>'`, with the range and the source's name. The source's history gets no step."
  - name: Close
    kind: button
    default: ""
    effect: Closes the window without extracting. The range is discarded.
    enabled: Disabled while the run works. Closing the window another way during the run hides it until the run ends, and does not stop the run.
  - name: Reset
    kind: button
    default: ""
    effect: Returns **Start Frame** to 1 and **End Frame** to the last frame, and counts the two readouts again.
    enabled: Disabled while the run works.
```

```controls
window: Extraction progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Shows "Extracting new sequence from sequence <name>" and the share of the frames written, from 0 to 100 percent, in a window titled **Extract Sequence**.
    enabled: Shown while the run works.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the run before the next frame. Nothing is published, what was written is removed, and the source is unchanged.
    persistence: The Extract Sequence window stays open with its controls enabled, and no message is shown.
```

## How it works

1. The window reads the sequence's frame count and keeps both boxes between 1 and it.
2. **OK** reads the range, holds the source's view and asks for a new file in the session's temporary folder, in the
   source's container: `SEQ_SWAP<token>.ser`, `.anseq`, `.avi`, `.mp4`, or `.mkv` for any other video container.
3. The frames from **Start Frame** to **End Frame** are read in order, as the file recorded them, and appended. A
   mosaic is not demosaiced. The progress bar moves with each frame written.
4. When the last frame is in, the new sequence is completed and appears under its file name whole. Until then
   nothing exists there.
5. The source's view is released and a new view opens beside it, under the source's name, made unique when that name
   is taken. It shows the frame the source showed when that frame is in the range, and the first frame otherwise. It
   demosaics its frames for display when the source's view does.

### Model

With \(S\) the value of **Start Frame** and \(E\) that of **End Frame**, the new sequence holds the frames \(S\) to
\(E\) of the source, in order, and **Selected Frames** is \(n = E - S + 1\). With \(w\), \(h\), \(c\) and \(b\) the
width, height, channels and bytes per sample of the frame the view holds, **Sequence Size** is

\[ B = n\,w\,h\,c\,b \tag{1} \]

divided by 1,024 twice for MB, and once more for GB. It leaves out the container's headers, compression, an image
list's own files, the decoder's buffers and the run's peak use of the temporary folder.

Nothing is interpolated, clipped, normalized, masked or demosaiced. The writer of the container still stores a frame
as the container can:

- **SER** stores unsigned 8-bit or 16-bit frames of mono, a two-by-two mosaic or RGB. A
  [SER](../../reference/glossary.md#ser) source's frames already fit and are copied unchanged. Each frame's exposure
  start is kept in the new file's timestamp trailer, as unknown for a frame that states none, and the new file is read
  at the rate those timestamps give.
- **An image list** writes each frame as a FITS file of its own, with the frame's
  [header cards](../../reference/glossary.md#header-card), beside a new `.anseq` list. Every frame takes the first
  frame's [pixel type](../../reference/glossary.md#pixel-type), so a later frame of a deeper type is converted to the
  nearest code.
- **AVI** stores uncompressed 8-bit grayscale or RGB frames.
- **MP4 and Matroska** encode 8-bit grayscale or RGB frames again, at an even width and height, so an odd last column
  or row is cut. A compressed video's frames are not copied bit for bit, and no sound is kept.

The new sequence keeps the source's frame rate, or 1 frame per second when the source states none.

### Boundaries

- Both boxes stay between 1 and the frame count, and **Start Frame** never ends after **End Frame**. A range of one
  frame is valid.
- The range is one run of consecutive frames. No frame inside it can be skipped, and nothing ranks or rejects frames.
- **Cancel** stops the run before the next frame and leaves nothing in the temporary folder. A failed or cancelled run
  releases the source's view and enables its player again.
- When the application lends no temporary files, the run does not start. A box titled **Error running Extract
  Sequence** says "An error occurred when executing the Extract Sequence process:" and "the application lends no
  temporary files".
- A read, write or completion that fails shows the same box with its reason and writes it to the log. The incomplete
  output is removed, and the window stays open with its controls enabled.

### Worked example

The sequence is a planetary video of Jupiter, an 8-bit SER file. With **Start Frame** at 11 and **End Frame** at 60,
**Selected Frames** reads \(60 - 11 + 1 = 50\), and by equation (1) **Sequence Size** reads 50 times the bytes of one
frame as the view holds it. **OK** opens a new SER sequence of 50 frames under the video's
name. Its first frame is the video's eleventh, and its history starts with "Extract Sequence: Extracted frames 11 to
60 from sequence" and the video's name.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Frames to drop at the start or the end | The numbers of the first and last frames to keep, found in the player | Check **Selected Frames**, then look at both end frames in the new view. |
| A short range to test a later process | The shortest range that still shows what you test | Widen it when the test misses something that happens elsewhere in the capture. |
| Little free disk | **Sequence Size**, with room to spare | Shorten the range when the temporary folder fills. |

## When it goes wrong

!!! warning "The new sequence is temporary"
    It is written to the session's temporary folder and is not saved. Save or export it from its view before you end
    the session if you need it, in the container you want to keep.

**Extract Sequence** is disabled in the menu
:   **Cause.** No view is active, or the active view holds a still image.
:   **Fix.** Make the sequence's view active, then choose the command again.

**Start Frame** or **End Frame** jumps back
:   **Cause.** The window keeps the range in order: a box moved past the other goes back to it.
:   **Fix.** Move the other box first: raise **End Frame** before **Start Frame**, or lower **Start Frame** before
    **End Frame**.

The file on disk is larger or smaller than **Sequence Size**
:   **Cause.** The readout counts the samples of the frames as the view holds them, not the encoded file.
:   **Fix.** Use it as a rough guide, and look at the size of the file once it is written.

"Another process is executing for this sequence, please cancel that job first before running this process"
:   **Cause.** Another run holds the source's view.
:   **Fix.** Wait for that run to end, or cancel it, then choose **OK** again.

"An error occurred when executing the Extract Sequence process:" and a reason
:   **Cause.** No temporary files were lent, or a read, a write or the completion failed, for the reason given.
:   **Fix.** Check that the source can be read, the free space and permissions of the temporary folder, the log, and
    the video's codec. Keep the original and try a smaller range.

**Cancel** left no new sequence
:   **Cause.** A cancelled run publishes nothing, on purpose.
:   **Fix.** Choose **OK** again and let the run finish.

A video result differs in size or look from its source
:   **Cause.** MP4 and Matroska frames are decoded and encoded again, at an even width and height.
:   **Fix.** Keep SER or an image list when the samples must stay as recorded, and compare the decoded frames before
    you rely on a video.

## Related

- [Sequences](../../interface/sequences.md), to find the frames in the player and to save or export the new sequence.
- [Non-Interactive Processes](non-interactive-processes.md), whose **Extract Frame** takes one frame as an image.
- [Image Stacking](../stacking-integration/image-stacking.md), to combine the frames you kept.
- [Image List Analyzer](../stacking-integration/image-list-analyzer.md), to inspect and filter a list of still files.
- [Planetary Imaging](../../workflows/planetary-imaging.md), for the workflow of planetary videos.
- [File Formats](../../reference/file-formats.md) and [Saving and Exporting](../../interface/saving-and-exporting.md),
  before you choose the container to keep the result in.

## Credits

- [FFmpeg](https://ffmpeg.org), by the FFmpeg developers, under the GNU LGPL 2.1 or later and, as built with its GPL
  encoders, the GNU GPL 2 or later. It decodes AVI, MP4 and the other video containers, and encodes MP4 and Matroska.
- [FFmpeg Formats Documentation](https://ffmpeg.org/ffmpeg-formats.html), for the video containers.
- [SER format description, version 3b](https://free-astro.org/images/5/51/SER_Doc_V3b.pdf), for the SER container and
  its timestamp trailer.
