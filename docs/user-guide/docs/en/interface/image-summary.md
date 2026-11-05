# Image And Sequence Summary

## Purpose, audience, prerequisites, and expected result

Image and Sequence Summary turns the active view's in-memory metadata into a
readable report. Use it to verify identity, dimensions, channel/pixel format,
file context, acquisition fields, or sequence frame/FPS information and to copy
that report into support or processing notes.

You need an active view. Saved still images, native SER sequences, and image-list
sequences provide the most complete reports. The expected result is a scrollable
set of sections with selectable values and a plain-text clipboard action. The
tool does not inspect pixel statistics or modify the target.

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Path | **Image > Information > Summary**, or **Summary** on the active image-view toolbar. The toolbar advertises `Ctrl+Shift+M` while that view has focus. |
| Enabled/visible when | A view with an image is active. Opening the dialog disables the main-menu **Summary** for that parent view until the menus are next refreshed, as when another view is activated; the view's toolbar **Summary** and `Ctrl+Shift+M` stay enabled, and each opens another Summary window. |
| Supported inputs/state | Still images, native SER sequences, and image-list sequences. Other sequence backends fall through to the current frame's image-metadata report rather than a sequence report. Masks, selections, display mode, and autostretch do not affect fields. |
| Outputs/side effects | A transient structured report and, on request, its plain text in the system clipboard. No image, file, metadata, or history change. |
| Persistence | None. No report settings are stored. Clipboard text remains under normal operating-system clipboard rules. |
| External dependencies | None beyond the platform clipboard. No network, catalog, or external tool is used. |

## Before you start

- Save the image or sequence first if you need meaningful file path, size, codec,
  and filesystem timestamps. Temporary workspace paths are intentionally hidden.
- For sequence-level frame count and FPS, use a native SER or image-list view.
  FFmpeg-backed and raw-AVI views currently receive a current-frame still report.
- Treat acquisition fields as metadata claims from the loaded file. A dash means
  unavailable; it does not infer a default camera value.

## Procedure

1. Activate the target and open **Image > Information > Summary**.
2. Confirm the window title names the intended view. For a still, look for
   **File Info**, **Image General Info**, and **Image Detailed Info**. For SER or
   image list, look for **File Info** and **Sequence General Info**.
3. Verify dimensions, channels, pixel type/format, CFA, and in-memory size before
   checking acquisition or filesystem metadata.
4. For a native sequence, verify **Number of Frames**, **Frames per Second**
   and frame size. **Sequence creation date** is when a SER says its recording
   began, or when an image list was made, and `-` when the file states neither.
5. Select individual field text if you need only one value, or choose **Copy to
   Clipboard** to copy the entire plain-text report. The action shows no success
   notification.
6. Close the dialog when finished. A rename updates both the window title and
   the report's name line.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Summary Report** | Scrollable read-only section view | Generated on open | Parsed report sections, key/value rows, and free-text notes | Presents the report in structured sections. Keys and values are mouse-selectable. | A normal still/SER/image-list generator always returns content. If a supplied report is empty - as when the window opens while an edit holds the image - the view shows **No summary information available.** | Image values reflect the in-memory metadata; the file's size and times are what the file system states when the report is built. The image itself is not re-read from disk. | Active view | Dialog lifetime |
| Scroll | Scroll-area interaction | Top of report | Normal platform scrolling | Navigates long reports. | Available when content exceeds the viewport. | Presentation only. | Report view | Dialog lifetime |
| **Copy to Clipboard** | Push button | Available | Click | Copies the entire report, as last shown, as plain text with HTML formatting removed. | Does nothing if the platform clipboard object is unavailable; no confirmation is shown. | Replaces existing clipboard text and may expose file/acquisition metadata when pasted elsewhere. | Entire current report | Platform clipboard lifetime |
| **Close** | Push button/default dialog action | Available | Click, Enter when default, or close window | Closes the dialog and re-enables Summary for its parent view. | Always available. | None. | This dialog | None |
| Image/sequence load or update | Automatic action | N/A | Listener notification | Rebuilds and rerenders the report. | Sequence frame updates can trigger regeneration; native sequence-level fields normally remain unchanged. | No disk verification occurs. | Parent view | None |
| View rename | Automatic title and report update | N/A | Rename notification | Updates the dialog window title and rebuilds the report, so **Image Name**/**Sequence Name** follows the new view name. | None. | None. | Parent view | None |

## Behavior and state model

The dialog selects one of three report paths:

1. A native SER view uses the SER sequence report.
2. An image-list view uses the image-list sequence report.
3. Every other view uses the current image's metadata report. This includes
   normal stills and currently includes sequence backends not recognized by the
   first two checks.

The generator emits small HTML sections. The dialog converts headings and
colon-separated lines into section titles and key/value rows, preserves lines
without a colon as notes, and falls back to a plain-text **Summary** section if
the expected heading structure is absent. It also stores an HTML-stripped copy
for **Copy to Clipboard**.

Temporary-file service paths are cleared before display. When the resulting
path is empty, file name, size, filesystem timestamps, and codec are rendered as
`-`. For a saved path, the file size and the creation and modification times are
what the file system states for that path when the report is built, and `-` for
a path that names no file; the codec is the format the file's extension names.
The report is not a save verification: it does not reopen the file or compare
disk pixels.

Load, update and rename notifications regenerate the body on the GUI thread,
from the view's document read under a lease that never waits: asked while an
edit holds the image, the window keeps the report it shows and rebuilds it at
the next notification. No manual refresh button exists.

The report reads typed values rather than a card's text. A number is written in
its fewest digits, to six significant ones (`300 sec` for a FITS card spelled
`300.000`), and an exposure under a second that is a whole fraction of one as a
camera writes it (`1/250 sec`). A fact nothing states reads `-`, a sequence's
size and frame rate included.

## Inputs, outputs, and metadata

### Still-image report fields

| Section | Fields and source |
| --- | --- |
| Overview | **Image Name** from the view's unique name passed to the report. |
| File Info | **File Name**; **File Size** in MB; filesystem **File Creation Time** and **File Modification Time** in local time with milliseconds; **File Codec**, the format the file's extension names. Unavailable or temporary-backed values become `-`. |
| Image General Info | **Image Resolution (Width x Height)**; **Image Channels**; **Image Format**, the sample type, such as `16 bits (unsigned integer)` or `32 bits (float)`; **Image Pixel Format**, what the channels are: `Grayscale`, `RGB`, `RGBA` or the colour space they state; **CFA pattern**, the four filters at the image's top-left corner such as `RGGB`, `X-Trans`, or `-` for an image that is not a mosaic; calculated **Image Size (in memory)** in MB. |
| Image Detailed Info | **Camera Model**; **ISO Speed**, or **Gain** when the file states a gain setting and no ISO speed; **Offset**; **Image Creation Date**, the date the file states - for a camera file its `Exif.Photo.DateTimeDigitized` - or else the exposure start, in UTC to the second; **Exposure Time** in seconds, a whole fraction of a second written as `1/N sec`; **Sensor Temperature** in C; **Software Used**. The acquisition facts are read from the image's acquisition record and the engine's reading of its header cards; a missing value renders as `-`. |

The summary does not list statistics, histogram bins, processing history, WCS,
SIP coefficients, or all FITS/EXIF keys. Use the dedicated tools for those.

### Native SER sequence fields

| Section | Fields and source |
| --- | --- |
| Overview | **Sequence Name** from the view's unique name. |
| File Info | **File Name**; **File Size** in MB or GB; filesystem creation/modification timestamps; **Sequence Type: SER sequence**. |
| Sequence General Info | **Resolution (Width x Height)**; **Channels**; **CFA pattern**; **Number of Frames**; **Frames per Second** to two decimals; **Pixel Format**, the sample type as in the still report; **Frame size (in memory)** in MB/GB; **Sequence creation date**, the SER header's UTC start time, or its first frame's timestamp when the header states none, in UTC; `-` when neither is stated. |

### Image-list sequence fields

The image-list report has the same general fields as SER, but File Info labels
the size **Total files size** and the type **Image list sequence**. The size
shown is the sum of the sizes of every file the list names, measured when the
list was opened, and `-` when one of them cannot be measured; the creation and
modification times are the list (`.anseq`) file's own. **Number of Frames** is
the number of frames the list names and **Frames per Second** the rate it
states. For a list opened from its file, **Resolution**, **Channels**, **CFA
pattern**, **Pixel Format** and **Frame size (in memory)** describe its first
frame, read when the list was opened; a list AfterNight has just written states
the description of the frames it wrote. **Sequence creation date** is the
creation time the list's manifest states, and `-` when it states none. The
report does not enumerate member file names or per-frame metadata. The view's
dock tooltip shows the same report.

The only durable output is optional plain text placed on the clipboard. No
metadata/history field is modified and no report file is created.

## Performance and responsiveness

Report generation is metadata-only and normally small: it formats a fixed set
of fields and rebuilds a modest widget tree. A still with recorded file size
zero may issue a filesystem size query. No pixel scan, sequence-frame iteration,
catalog lookup, or network request occurs.

Repeated sequence-frame updates can rebuild the same native sequence report, so
pause playback if the dialog is not needed continuously. Clipboard cost is
proportional to the short report text.

## Worked example

- Evidence mode: Source-and-test-derived.
- Starting state, actions, settings, checkpoints, result, and limitations: For a
  saved SER sequence, the selected source path produces **SER sequence** plus
  common resolution, channels, CFA, frame count, FPS, pixel type and per-frame
  memory size. `ImageReportTest` holds a 640 x 480 16-bit SER of 1000 frames at
  30 frames per second to **640x480**, **RGGB**, **Number of Frames: 1000**,
  **Frames per Second: 30.00**, **16 bits (unsigned integer)** and **0.59 MB**
  per frame. For an image-list sequence the report substitutes **Image list
  sequence** and **Total files size**. `InspectorWindowsTest` copies a report
  to the clipboard and renames the view, but no project-data field value is
  claimed.

## Warnings and limitations

- Summary reports metadata; it does not validate pixels, statistical quality,
  file integrity, or whether the file can be reopened.
- Temporary workspace paths are deliberately hidden and their file fields show
  dashes.
- FFmpeg-backed and raw-AVI sequences do not enter the two sequence-report
  branches; they show current-frame image metadata.
- Image-list summary does not enumerate files or aggregate per-frame acquisition
  fields; its frame fields describe the first frame, and its **Total files
  size** is measured when the list is opened, not when the report is built.
- **Sequence creation date** shows `-` for a SER whose header and trailer state
  no time, and for a list whose manifest states none.
- The toolbar **Summary** action and `Ctrl+Shift+M` are not disabled by an open
  Summary window, so repeated use opens more than one.
- Copy replaces clipboard text, provides no confirmation, and may reveal local
  file names or acquisition metadata when pasted.
- WCS/SIP, full FITS/EXIF tables, history, and numerical image statistics belong
  to separate tools.

## Troubleshooting

| Symptom or exact error | Verified cause/enablement path | Recovery | Evidence source |
| --- | --- | --- | --- |
| Summary is disabled | No active image exists, or this parent view already has an open Summary dialog. | Activate a loaded view, or close the existing Summary dialog. | `ActionStateCoordinator.cpp`; `ImageSummaryViewerControl.cpp` |
| File fields show `-` | The image is unsaved/temporary-backed, or metadata/filesystem information is unavailable. | Save to a durable path, reopen if disk verification matters, and open Summary again. | `common.h` |
| A video/raw-AVI sequence shows image fields | Only SER and image-list sequence checks select sequence reports; other backends use current-frame image metadata. | Use the sequence workspace/status for backend/frame context; do not infer sequence totals from this summary. | `ImageInspectorContent.cpp` |
| The report did not change while an edit ran | The window never waits for an edit; it keeps the report it showed until the edit lands. | Wait for the edit to finish; the report rebuilds on the image's update. | `ImageSummaryViewerControl.cpp`; `ImageInspectorContent.cpp` |
| Copy produces no visible feedback | The action writes plain text without a success notification. | Paste into a trusted text field to verify, while considering metadata privacy. | `ImageSummaryViewerControl.cpp` |
| Expected FITS/EXIF/WCS fields are missing | Summary intentionally formats only its fixed common-field set. | Open [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md). | `common.h`; `ImageSummaryViewerControl.cpp` |

## Related workflow steps

- Use [Image Statistics](image-statistics.md) for per-channel level, spread,
  robust estimators, and background-noise measurements.
- Use [Histogram Analysis](image-histogram.md) for distributions and endpoint
  clipping.
- Use [FITS Header, EXIF, And Astrometry Metadata](fits-header-viewer.md) for
  detailed metadata and WCS/SIP inspection.
- Use [Sequences](sequences.md) for player state, extraction, conversion, and
  backend behavior.
- Use [Saving And Exporting](saving-and-exporting.md) to create a durable file,
  then reopen it when the summary is part of output verification.

## References and provenance

- `common.h` is authoritative for every still/SER/image-list field, unit,
  placeholder, timestamp format, and temporary-path rule.
- `ImageSummaryViewerControl.cpp` defines report parsing, refresh,
  rename, empty/fallback behavior, and clipboard conversion.
- `ImageInspectorContent.cpp` reads the view's document under a lease that never
  waits and chooses the report; the engine's `SequenceDescriptor.h` and
  `CodecMetadataProjection.h` define the sequence description and the
  acquisition facts the report reads.
- `ImageReportTest.cpp` in `Test_ControlsServices` holds every report's fields,
  labels and placeholders; `InspectorContentTest.cpp` and
  `InspectorWindowsTest.cpp` in `Test_ControlsDialogs` hold the dispatch, the
  window, its clipboard copy and its rename; `test_image_metadata_viewers_ui.cpp`
  verifies the structured summary view, scroll/content widgets, and primary close
  action. Live layout was not inspected.
