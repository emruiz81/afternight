---
kind: process
stage: register
menu: Process > Preprocess > Image Registration
summary: >-
  Matches the stars of a set of calibrated frames with a reference frame, records each frame's transform and figures,
  and writes every included frame resampled onto one shared grid, ready for stacking.
data_in: linear
data_out: linear
destructive: false
preview: none
sequences: false
masks: none
external: >-
  Optional: a configured plate solver for Auto-solve unsolved frames, and a Gaia catalog installed in the settings for
  Enrich with Gaia catalog
glance:
  enabled: >-
    Always. The window opens without an image and chooses its frames itself. Register needs two frames and an output
    directory. Align needs a registration that answers the frames and settings as they are, and two included,
    registered frames.
  inputs: >-
    Still image files of one width, height, pixel type and channel count: calibrated mono, color-filter or RGB lights.
    No mask, and no sequence or video: extract its frames to files first.
  outputs: >-
    The file registration_metrics.db in the output directory, with every frame's stars, figures and transform, and on
    Align one 32-bit floating-point FITS file for each included, registered frame, named after it with _reg added. The
    source files are not changed.
  state: Calibrated linear lights of one target, before normalization and stacking.
derived: true
---
# Image Registration

Image Registration finds where each frame of a set lies on a
[reference frame](../../reference/glossary.md#reference-frame), then resamples the frames onto one grid so that a
stack can add them. **Register** detects the stars of every frame, pairs them with the reference's stars and fits a
transform to the pairs. It records the transforms and each frame's figures in the output directory and lists them in
the table. **Align** writes each included, registered frame onto the reference's grid as a new 32-bit floating-point
[FITS](../../reference/glossary.md#fits) file. The source files are never changed. Use the window to choose the
reference and the transform yourself, look at each frame's figures and leave out weak frames, without the whole
Image Integration workflow.

```figure
image: ../../assets/images/processes/registration-alignment/image-registration.webp
alt: >-
  The Image Registration window. On the left, a preview of one frame above the Frame List, a table of the 35 frames, five of them in view, with the
  columns File, Filter, Exposure, Status, Stars, FWHM, SNR, Ecc., Zero Point and RMS, and under it a row of buttons
  from Add to Clear All with the Current, Registered and Reference counts. On the right, the Registration card with
  Method at Star Matching, Transformation at Homography, Reference at Manual and 1, Min Star Pairs, Max Stars per
  Frame, three check boxes, Skip already-registered frames ticked and the Register button; the Alignment card with
  Standard chosen, Interpolation at Auto and the Align button; Extended Settings and the Output Directory with its
  path. Reset and Close run along the bottom.
caption: >-
  Image Registration after Register and Align on 35 calibrated lights of NGC 253, every setting at its default: the
  table holds each frame's figures, and the first frame is the reference.
pins:
  - {n: 2, x: 9.6, y: 62.8}
  - {n: 3, x: 70.9, y: 55.8}
  - {n: 4, x: 69.1, y: 10.3}
  - {n: 5, x: 71.1, y: 19.8}
  - {n: 6, x: 68.2, y: 51.7}
  - {n: 7, x: 69.8, y: 29.7}
  - {n: 8, x: 58.5, y: 82.7}
  - {n: 9, x: 68.4, y: 35.8}
  - {n: 10, x: 69.8, y: 45.8}
```

## Before you start

- Calibrate the frames first and keep them linear: this window applies no bias, dark, flat, cosmetic correction or
  normalization. Leave color-filter frames as mosaics. The window
  [debayers](../../reference/glossary.md#debayer) each one as it reads it, the aligned files are RGB, and the mosaics
  stay on disk for any step that needs them.
- Every frame must have the width, height, [pixel type](../../reference/glossary.md#pixel-type) and channel count of
  the others. Gain, offset, exposure, filter, camera, [CFA](../../reference/glossary.md#cfa) pattern and session are
  not compared.
- Give each set an empty output directory of its own: it holds the registration and the aligned files.
- For a mosaic, choose **Mosaic** in **Extended Settings...** before you register. The window's own **Alignment** card
  stays disabled until a registration is recorded, and a switch to or from **Mosaic** needs a new one.
- **Auto-solve unsolved frames** needs a plate solver set up in [Settings](../../interface/settings-dialog.md), and
  **Enrich with Gaia catalog** an installed [Gaia](../../reference/glossary.md#gaia) catalog. A frame's
  [plate solution](../../reference/glossary.md#plate-solution) gives it catalog figures, never its transform.
- The aligned files carry no [WCS](../../reference/glossary.md#wcs) cards, which the warp would make wrong, and of the
  source's other cards only its observing context. Plate solve the stack, or an aligned frame, when you need sky
  coordinates.

## Steps

1. **Open the window.** Choose **Process > Preprocess > Image Registration**. No image needs to be open. The window
   opens with an empty **Frame List**, and its **Registration** card stays disabled until it lists two frames.
2. **Add the frames.** Choose **Add** and pick the calibrated lights, or **Add Folder** to take a folder's images. The
   table lists each file with its filter and exposure, **Status** reads "Not registered", and an arrow marks the first
   row as the reference.
3. **Choose the output directory.** Choose **Browse** and pick an empty folder of its own for this set. When the
   folder already holds a registration of these frames with these settings, the table shows it at once.
4. **Choose how the frames are matched.** Leave **Method** at **Star Matching** and **Transformation** at
   **Homography** for most deep-sky data. Choose **Similarity** for frames that only shift and turn, and a polynomial
   for a wide field whose corners do not line up.
5. **Choose the reference.** Type the row number of a sharp, well-guided frame in the **Reference** box, or
   right-click its row and choose **Set as reference**. The arrow moves to that row.
6. **Check the extended settings.** For color-filter frames, open **Extended Settings... > Advanced Settings...** and
   check **CFA Pattern** and **Method**. For a mosaic, choose **Mosaic** there now. Choose **OK** in each dialog to
   keep the values.
7. **Register.** Choose **Register**. A progress window follows the frames, then each row reads **Registered** or
   **Failed** with its **Stars**, **FWHM**, **SNR**, **Ecc.** and **RMS**, and the line under the output directory
   says how many registered.
8. **Look at the frames.** Sort by **File**, **Filter** or **Exposure**, step through failures with **Next Failed**,
   and select two rows or more for **Blink Compare...**. Exclude a weak frame from its row's menu, or set limits under
   **Quality Thresholds** and choose **Apply Thresholds**. Excluded rows are marked, and need no new registration.
9. **Choose the canvas.** Under **Alignment**, keep **Standard**, or choose **Intersection** to keep only the part
   every frame covers. Leave **Interpolation** at **Auto**. Neither choice needs a new registration.
10. **Align.** Choose **Align**. A progress window follows the frames. The output directory then holds one
    `<name>_reg.fits` for each included, registered frame, and the line under it reads "Registration and alignment
    completed successfully".

## Controls

The preview pane's zoom, channel and display mode work as in [Process Windows](../../interface/process-windows.md),
which also says what ++esc++, the window's kept size and its kept values do. This window has no **Apply**:
**Register** and **Align** are its runs, and each works on files.

### Image Registration

```controls
window: Image Registration
controls:
  - name: Method
    kind: combo
    default: Star Matching
    options: [Star Matching, Phase Correlation, Comet / Asteroid Tracking]
    effect: >-
      Chooses how a frame is placed on the reference. **Star Matching** pairs its stars with the reference's and fits
      **Transformation** to the pairs. **Phase Correlation** measures a shift alone, from the whole picture - the green channel of a color frame. **Comet /
      Asteroid Tracking** follows a moving object you mark in **Track Comet**, so that the object stays still and the
      stars trail.
    enabled: >-
      **Phase Correlation** and **Comet / Asteroid Tracking** set **Reference** to **Manual** and disable it.
      **Phase Correlation** also disables **Transformation**.
    tradeoff: >-
      Star matching follows a shift, a turn, a change of scale and what else **Transformation** allows. Phase
      correlation suits frames that differ by a shift only, and judges no answer, so any shift it measures is taken.
      Comet tracking needs a `DATE-OBS` card on every frame and two marked positions of the object.
    scope: The whole list. Phase correlation still detects every frame's stars first, though its shift does not use them.
    persistence: Kept for the next window. **Reset** returns **Star Matching**.
  - name: Transformation
    kind: combo
    default: Homography
    options: [Similarity, Affine, Homography, Polynomial 2, Polynomial 3, Polynomial 4, Auto]
    effect: >-
      Chooses what a frame's transform may do. **Similarity** shifts, turns and scales, **Affine** also shears and
      scales each axis apart, and **Homography** also follows perspective. **Polynomial 2** to **Polynomial 4** refine
      a homography with a correction for the optics' distortion. **Auto** keeps the homography unless a polynomial is
      clearly better.
    range: >-
      A polynomial order needs as many star pairs as it has terms, 6, 10 and 15 for orders 2, 3 and 4. A frame matched
      by fewer keeps the linear transform its stars matched, and the log file says so.
    enabled: >-
      With **Star Matching**. **Phase Correlation** disables it. It stays enabled for **Comet / Asteroid Tracking**,
      which does not read it.
    tradeoff: More freedom needs more well-spread pairs, and on few or poor pairs it can bend the frame where it should not.
    persistence: Kept for the next window. **Reset** returns **Homography**.
  - name: Reference
    object: referenceModeSelector
    kind: combo
    default: Manual
    options: [Manual, Auto]
    effect: >-
      **Manual** takes the frame whose row number the box beside it holds. **Auto** chooses the frame with the best
      star figures, from more stars, a higher SNR, a smaller FWHM and rounder stars, before the frames are matched.
    enabled: Disabled, at **Manual**, for **Phase Correlation** and **Comet / Asteroid Tracking**.
    tradeoff: >-
      Every transform is measured against the reference, so a soft or trailed reference weakens them all. **Auto**
      leaves out excluded frames and, with several named filters, compares each frame with the others of its filter.
    scope: >-
      Choosing **Auto** picks a frame at once from the figures the table holds, and **Register** picks again from the
      figures of its own detection. The row it chose is the reference afterwards.
    persistence: Kept for the next window. **Reset** returns **Manual**.
  - name: Reference
    object: referenceImageValue
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 100000, step: 1}
    effect: Makes the frame of that row the reference. An arrow marks its row, and the **Reference** count shows the number.
    range: From 1 to the number of listed frames. The reference stays the same file when the rows are sorted or moved, and its number follows it.
    enabled: With **Reference** at **Manual**.
    tradeoff: >-
      The reference fixes the grid **Standard** writes onto, its framing and its size. A frame near the middle of the
      session and free of trailing serves best.
    persistence: The reference file is kept for the next window and recorded with the registration. Choosing another makes a recorded registration out of date.
  - name: Min Star Pairs
    kind: spinbox
    default: "10"
    widget: {min: 5, max: 10000, step: 1}
    effect: >-
      Sets the fewest matched star pairs a frame needs, with the reference or with a frame registered near it, however
      small a part of its stars they are. A frame matched by fewer is marked **Failed**.
    range: >-
      A match of fewer than ten candidate pairs also needs four fifths of them to fit its transform, unless either
      frame holds 1,000 stars or more.
    enabled: Read by **Star Matching**. It stays enabled for the other methods, which do not read it.
    tradeoff: Higher refuses sparse fields and narrowband frames. Lower accepts weaker matches.
    persistence: Kept for the next window. **Reset** returns 10.
  - name: Max Stars per Frame
    kind: spinbox
    default: "2000"
    widget: {min: 10, max: 20000, step: 1}
    effect: >-
      Sets how many stars are detected in each frame and offered for matching, the brightest first. A changed value
      detects the frames again.
    tradeoff: Higher helps a crowded field and costs detection and matching time. Lower can leave parts of the field without stars.
    scope: Every method's detection, though only **Star Matching** pairs the stars.
    persistence: Kept for the next window. **Reset** returns 2000.
  - name: Auto-solve unsolved frames
    kind: check
    default: "Off"
    effect: >-
      Before the stars are detected, sends each included frame whose header holds no plate solution, and that you did
      not solve with **Solve...**, to the plate solver set up in the settings, one frame at a time.
    enabled: >-
      When ASTAP, a local or online astrometry.net, or the internal Gaia solver is set up. Without one the box shows
      off, and the choice you made is kept for when one is.
    tradeoff: >-
      Each solve adds time, and an online solver needs the network. A frame that fails to solve is logged as a warning,
      and the registration goes on.
    scope: A solution serves **Enrich with Gaia catalog** and the arcsecond **RMS** only. It never places a frame, and it is not written to the file.
    persistence: Kept for the next window. **Reset** returns off. The solver's own settings are the application's.
  - name: Enrich with Gaia catalog
    kind: check
    default: "Off"
    effect: >-
      After matching, pairs the stars of each frame that has a plate solution with the Gaia catalog, and fills the
      **Zero Point** column. The frame's limiting magnitude, the scatter of its zero point and its count of catalog
      stars are recorded too, and its **RMS** is given in arcseconds.
    enabled: >-
      When a Gaia catalog is installed and ready. Without one the box shows off, and the choice you made is kept for
      when one is.
    tradeoff: Adds the catalog's work for each solved frame. A frame without a solution gets no catalog figures.
    scope: >-
      The frames whose stars this run detected. A frame whose stars are reused but has no catalog figures is detected
      again. Stars are placed in the catalog at the frame's date of observation.
    persistence: Kept for the next window. **Reset** turns it on, shown ticked while a catalog is ready.
  - name: Chromatic aberration correction
    kind: check
    default: "Off"
    effect: >-
      Before detection, moves the red and blue channels of a color frame onto the green one, with the settings of
      **Extended Settings...**. The frame is changed in memory only, and no file is written.
    enabled: Always. A grayscale frame is left as it is, and a channel whose correction fails is left uncorrected, with a warning in the log file.
    tradeoff: >-
      Stars are detected in the green channel, which the correction does not move, and **Align** reads the original
      files. So the correction changes neither the transforms nor the aligned files, and only adds time.
    persistence: Kept for the next window with its settings. **Reset** returns off.
  - name: Skip already-registered frames
    kind: check
    default: "On"
    effect: >-
      Reuses what the output directory holds of an earlier registration of these frames. With the same frames and
      settings, the whole registration is reused. Otherwise the stars it found are, where the detection settings and
      the files have not changed. Off detects and matches every frame again.
    tradeoff: On saves most of the time of a second run. A record that does not answer the frames and settings is not used.
    scope: The registration database in the output directory. Excluded frames that failed do not stand in the way of a reuse.
    persistence: Kept for the next window. **Reset** turns it on.
  - name: Clear Registered Data
    kind: button
    default: ""
    effect: Deletes this set's registration from the database in the output directory, without asking, and clears the figures from the table.
    enabled: When the table holds a registration and no run works.
    scope: The records of these frames only. The aligned files and the comet files stay.
  - name: Register
    kind: button
    default: ""
    effect: >-
      Detects the stars of every frame, solves and corrects frames where those options ask, chooses the reference,
      matches each frame with it, fits its transform and records the result in `registration_metrics.db` in the output
      directory. The table then shows what was recorded.
    enabled: >-
      With two frames or more and no run working. Without an output directory the run stops at once with an error.
    tradeoff: >-
      Each frame is read whole and debayered when it is a mosaic, which costs time and memory. Cancel stops the run,
      and a stopped run records nothing.
    scope: Every listed frame, excluded ones too. With **Comet / Asteroid Tracking**, it also writes the comet-aligned files.
    persistence: >-
      The record replaces any earlier one of these frames. Changing **Method**, **Transformation**, **Reference** or the
      reference, **Min Star Pairs**, **Max Stars per Frame**, **Auto-solve unsolved frames**, **Enrich with Gaia
      catalog**, **Chromatic aberration correction**, a detection setting, the frames, or a switch to or from
      **Mosaic** makes it out of date, and **Align** waits for a new one.
  - name: Standard / Mosaic / Intersection
    kind: radio
    default: Standard
    options: [Standard, Mosaic, Intersection]
    effect: >-
      Chooses the canvas the aligned frames share. **Standard** is the reference's own frame. **Mosaic** is the
      smallest rectangle that holds every frame. **Intersection** is the rectangle every frame covers.
    range: >-
      **Standard** and **Mosaic** round the canvas up to whole pixels and **Intersection** rounds it down. An
      intersection the frames do not share is refused.
    enabled: With a registration that answers the frames and settings, and two included, registered frames.
    tradeoff: >-
      **Standard** gives a predictable size, and a frame's shifted part falls outside it. **Mosaic** keeps everything
      at the largest size, time and disk. **Intersection** gives clean edges and drops the strips some frames miss.
    persistence: >-
      Kept for the next window. **Reset** returns **Standard**. A change between **Standard** and **Intersection** keeps
      the registration. A switch to or from **Mosaic** makes it out of date, and disables this card until you register
      again.
  - name: Interpolation
    kind: combo
    default: Auto
    options: [Auto, Nearest Neighbor, Bilinear, Bicubic, Pixel Area Relation, Lanczos4, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: Chooses how each aligned pixel is sampled from the frame. **Auto** is **Lanczos4**.
    enabled: With the canvas choice.
    tradeoff: >-
      **Nearest Neighbor** is blocky, and **Bilinear** fast and slightly soft. **Bicubic** and **Lanczos4** keep fine
      detail and can ring beside bright stars. **Pixel Area Relation** averages, and the two **Bit Exact** choices give
      the same result on every computer.
    scope: Every channel of every aligned frame. A change keeps the registration.
    persistence: Kept for the next window. **Reset** returns **Auto**.
  - name: Align
    kind: button
    default: ""
    effect: >-
      Plans the canvas, then reads each included, registered frame, maps every pixel of the canvas back into it through
      its transform and writes the result as `<name>_reg.fits` in the output directory, in 32-bit floating point.
    enabled: >-
      With an output directory, a registration that answers the frames and settings, every frame registered or failed,
      two included, registered frames, and no run working.
    tradeoff: >-
      Time and memory grow with the canvas and the interpolation. Each file holds three channels for a color or
      debayered frame. A stopped or failed run keeps the files it finished, and the frame it was writing leaves no file.
    scope: The included, registered frames, in the list's order. Failed and excluded frames are left out.
    persistence: Replaces a file of the same name without asking. Nothing of the window's settings changes.
  - name: Extended Settings...
    kind: button
    default: ""
    effect: Opens **Registration Settings**, the full settings of the registration and the alignment, and the detection, color-filter and comet settings behind them.
    enabled: With a frame in the list and no run working.
    persistence: The values you accept with **OK** replace the window's.
  - name: Output Directory
    kind: readout
    default: ""
    effect: Shows the folder that holds the registration database and the aligned files. You cannot type in it.
    range: A folder that no longer exists is made again by the next **Register** or **Align**.
    persistence: Kept for the next window. **Reset** leaves it.
  - name: Browse
    kind: button
    default: ""
    effect: >-
      Opens a folder dialog and makes the folder you choose the output directory. A registration of these frames and
      settings found there is read and shown.
    range: >-
      The dialog starts at the present folder, else at the last folder chosen, else at the first frame's folder. It
      offers existing folders.
    tradeoff: A folder shared by two sets mixes their files, and two sources of one name write one aligned file.
  - name: Status line
    kind: readout
    default: ""
    effect: >-
      Says where the set stands, under the output directory. "Registration needs update", "Select at least two
      included, successfully registered frames to align", "Images ready to align", "Registration completed. N images
      registered." with "M failed." after it when some failed, and "Registration and alignment completed successfully".
    enabled: Empty while the list is.
  - name: Frame List
    kind: readout
    default: ""
    effect: >-
      Lists each frame with its **File**, **Filter**, **Exposure** in seconds and **Status**, and after **Register**
      its **Stars**, **FWHM** in pixels, **SNR**, **Ecc.**, **Zero Point** and **RMS**. A figure the frame does not have
      reads "-".
    range: >-
      **Status** reads "Not registered", "Registered", "Failed", "Excluded manually" or "Excluded by quality
      thresholds". An arrow marks the reference, a red mark a failed row and a struck-through circle an excluded one.
      **RMS** is in pixels, or in arcseconds with a catalog match, and the reference's reads "-". In pixels, compare it
      between frames as how well each transform fits.
    enabled: >-
      Selecting a row shows its frame in the preview. ++ctrl++ and ++shift++ select several. Clicking **File**,
      **Filter** or **Exposure** sorts the list by it in descending order, and a second click reverses the order. The
      other columns do not sort.
    persistence: The frames and their order are kept for the next window, which reads their headers again. The figures come from the database.
    note: Hover over a file name to see its full path.
  - name: Add
    kind: button
    default: ""
    effect: >-
      Opens a file dialog and adds the image files you choose. Their headers are read away from the window, which stays
      responsive.
    range: A file already listed is passed over. A message says how many were added, passed over and could not be read, when any were not added.
    enabled: While no run works.
    persistence: The dialog remembers its folder, shared with **Add Folder**.
  - name: Add Folder
    kind: button
    default: ""
    effect: Opens a folder dialog, then **Add Folder Options**, and adds the image files of the folder that pass those options.
    tradeoff: A scan of subfolders reads every file's header and can take long on a large tree.
    enabled: While no run works.
  - name: Remove
    kind: button
    default: ""
    effect: Takes the selected rows out of the list. The files stay on disk.
    enabled: With a frame in the list. It does nothing while a run works.
    scope: Removing a frame changes the set, so a recorded registration is out of date.
  - name: Remove Failed
    kind: button
    default: ""
    effect: Takes every failed row out of the list, and says how many with "N failed images have been removed from the table (not from the disk)".
    enabled: When a frame failed and no run works.
    tradeoff: The list is then another set, so **Register** runs again, and reuses the stars it found.
  - name: Move frame up
    kind: button
    default: ""
    effect: Moves the selected row up one place.
    enabled: When the row can move.
    scope: The list's order, which **Align** follows. The column sorting is cleared, and the reference keeps its file.
  - name: Move frame down
    kind: button
    default: ""
    effect: Moves the selected row down one place.
    enabled: When the row can move.
  - name: Next Failed
    kind: button
    default: ""
    effect: Selects the next failed row after the selected one, from the top again after the last, and shows its frame.
    enabled: When a frame failed.
  - name: Blink Compare...
    kind: button
    default: ""
    effect: Opens the **Blink Comparator** over the selected frames.
    enabled: With two rows or more selected.
  - name: Current, Registered and Reference
    kind: readout
    default: 0, 0/0, 0
    effect: >-
      Give the selected row's number, the registered frames out of the listed ones with how many failed and how many
      are excluded, and the reference's row number.
  - name: Clear All
    kind: button
    default: ""
    effect: >-
      Empties the list, the preview and the counts, stops the files still being read, and forgets the exclusions and
      the registration the window holds.
    enabled: With a frame in the list. It does nothing while a run works.
    scope: The window only. No file is deleted, and a registration recorded in the output directory is read back when you list the frames again.
```

```controls
window: Quality Thresholds
controls:
  - name: Min Stars
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 100000, step: 1}
    effect: Excludes every frame with fewer stars, the reference too. 0 sets no minimum.
    enabled: >-
      When every listed frame has a current registration. The **Quality Thresholds** section opens and closes with its
      title, and opens closed.
    tradeoff: High values drop sparse fields and narrowband frames that registered well.
    persistence: Kept for the next window, and applied when it opens.
  - name: Max FWHM
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 999, step: 0.1, decimals: 3}
    effect: Excludes every frame whose FWHM, in pixels, is larger, the reference too. 0 sets no limit.
    tradeoff: Lower keeps sharper frames, and can drop the frames of another filter or a night of other seeing.
    persistence: Kept for the next window, and applied when it opens.
  - name: Max Eccentricity
    kind: spinbox
    default: "0.000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Excludes every frame whose **Ecc.** is larger, the reference too. 0 sets no limit.
    tradeoff: Lower keeps rounder stars and can exclude more frames than you meant.
    persistence: Kept for the next window, and applied when it opens.
  - name: Apply Thresholds
    kind: button
    default: ""
    effect: >-
      Excludes the frames that fail the three values, marked "Excluded by quality thresholds". While any value is set,
      a failed frame is counted among them.
    tradeoff: Fewer than two included, registered frames disables **Align**.
    scope: What **Align** takes and what **Auto** may choose. No registration is needed again.
    note: A frame a threshold excludes stays excluded while it fails the thresholds, even after **Include selected frames**.
  - name: Reset
    kind: button
    default: ""
    effect: Sets the three values to 0 and includes the frames the thresholds excluded.
    scope: Frames you excluded yourself stay excluded.
```

```controls
window: Reset and Close
controls:
  - name: Reset
    kind: button
    default: ""
    effect: >-
      Returns **Star Matching**, **Homography**, **Manual**, 10 and 2000, **Skip already-registered frames** on,
      **Auto-solve unsolved frames** and **Chromatic aberration correction** off, **Enrich with Gaia catalog** on,
      **Standard** and **Auto**. Every value of **Extended Settings...** returns to its default, the comet's marked
      positions are cleared, and the quality thresholds are cleared.
    range: It leaves the list, the reference, the output directory and the frames you excluded yourself.
    enabled: While no run works.
    persistence: The new values are kept for the next window.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window at once. A run that works is stopped, and the window does not wait for it.
    enabled: Also while a run works. ++esc++ does the same.
    tradeoff: A stopped registration records nothing. A stopped alignment keeps the files it finished, and the frame it was writing leaves no file.
    persistence: The window's values, its list and its output directory are kept for the next window.
```

```controls
window: The preview
controls:
  - name: Preview
    kind: readout
    default: ""
    effect: >-
      Shows the selected row's file as it is stored, a mosaic debayered for display. Nothing of the registration or
      the alignment is shown in it.
  - name: Show statistics
    kind: toolbutton
    default: "On"
    effect: >-
      Shows or hides a card over the preview with the frame's **Min**, **Max**, **Mean**, **Median** and **StdDev**, to
      four decimals. For a color frame they are the means over its channels, and **Min** and **Max** the extremes.
    scope: The preview only.
```

```controls
window: The frame list's menu
controls:
  - name: Set as reference
    kind: action
    default: ""
    effect: Makes the row you right-clicked the reference.
    enabled: On any row but the reference's.
    persistence: A recorded registration is then out of date.
  - name: Exclude selected frames
    kind: action
    default: ""
    effect: >-
      Marks the selected rows "Excluded manually". **Align** leaves them out, and **Auto** does not choose them. The
      **Blink Comparator** excludes the same way.
    scope: The window only. No registration is needed again, and the files stay.
    persistence: Until the window closes. Exclusions are not kept for the next window.
  - name: Include selected frames
    kind: action
    default: ""
    effect: Takes back the exclusion of the selected rows, yours and the thresholds'.
    enabled: When a selected row is excluded.
  - name: Solve...
    kind: action
    default: ""
    effect: >-
      Shows the frame in the preview and opens Plate Solving over it, which runs the solver set up in the settings.
      When that window closes, the solution it applied is kept for this frame.
    scope: The solution serves the catalog figures only. The file is not changed.
    persistence: Until this window closes.
  - name: Blink Compare...
    kind: action
    default: ""
    effect: As the button of that name.
    enabled: With two rows or more selected.
  - name: Move Up
    kind: action
    default: ""
    effect: As **Move frame up**.
  - name: Move Down
    kind: action
    default: ""
    effect: As **Move frame down**.
  - name: Remove
    kind: action
    default: ""
    effect: As the **Remove** button.
  - name: Delete From Disk
    kind: action
    default: ""
    effect: >-
      Asks "This will delete the selected file from the disk permanently. Are you sure you want to proceed?", then
      takes the row you right-clicked out of the list and deletes its file.
    tradeoff: The file cannot be brought back. A file that cannot be deleted is reported as "Could not delete '<path>' from the disk."
    note: The menu does not open while a run works.
```

```controls
window: Progress
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: >-
      Follows a run from 0 to 100 percent, in a window titled "Registration" or "Image Alignment". It names what is
      being done, as `Plate-solving <file>`, `Detecting stars on <file>`, `Computing transformations...` and `Aligning
      image 3/35: <file>`.
    range: In a registration, detection fills the bar to 75 percent and matching the rest.
    enabled: >-
      While **Register** or **Align** works. The cards, **Add**, **Add Folder**, **Extended Settings...** and **Reset**
      are disabled meanwhile, and the list cannot be changed.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop. The window closes when the run reaches a point where it can stop.
    tradeoff: A cancelled registration records nothing and leaves an earlier record in place. A cancelled alignment keeps the files it finished.
```

### Registration Settings

**Extended Settings...** opens this dialog. It shows the window's values and the ones only it holds. **OK** gives
them to the window, and **Cancel** discards them.

```controls
window: Registration Settings
controls:
  - name: Auto-solve unsolved frames during Registration
    kind: check
    default: "Off"
    effect: The window's **Auto-solve unsolved frames**.
    enabled: When a plate solver is set up.
    note: >-
      Here **Mosaic** does not turn it on. This window solves a frame only while it is on, and the **Mosaic** tooltip
      says so.
  - name: Enrich with Gaia catalog
    kind: check
    default: "On"
    effect: The window's **Enrich with Gaia catalog**.
    enabled: Always, so you can turn it off without a catalog. The window's own box shows it off while no catalog is ready.
  - name: Minimum Star Pairs
    kind: spinbox
    default: "10"
    widget: {min: 5, max: 10000, step: 1}
    effect: The window's **Min Star Pairs**.
    enabled: Shown for **Star Matching** only.
  - name: Maximum Stars per Frame
    kind: spinbox
    default: "2000"
    widget: {min: 10, max: 20000, step: 1}
    effect: The window's **Max Stars per Frame**.
    enabled: Shown for **Star Matching** only.
  - name: Reference Image
    kind: combo
    default: The window's reference
    options: [The window's reference, Every listed frame by its full path]
    effect: Chooses the reference by its file. You can type part of a path, and the list offers the frames that hold it.
    enabled: While **Auto-select** is off.
  - name: Auto-select
    kind: check
    default: "Off"
    effect: The window's **Reference** at **Auto**.
    enabled: Disabled for **Phase Correlation** and **Comet Tracking**.
  - name: Best
    kind: button
    default: ""
    effect: >-
      Chooses the reference as **Auto** would and puts it in **Reference Image**. It reads the figures the window
      holds, and detects the stars of any frame without them, away from the dialog.
    enabled: While **Auto-select** is off, and not for **Comet Tracking**.
    tradeoff: Nothing is recorded. When no frame can be chosen, the box stays as it was and nothing says why.
  - name: Registration Method
    kind: combo
    default: Star Matching
    options: [Star Matching, Phase Correlation, Comet Tracking]
    effect: The window's **Method**. Each method shows only the settings it reads.
    note: Image Integration's **Manual Points** is not offered here.
  - name: Transform Model
    kind: combo
    default: Homography
    options: [Similarity, Affine, Homography, Polynomial 2, Polynomial 3, Polynomial 4, Auto]
    effect: The window's **Transformation**. A line under it says what the choice does.
    enabled: Shown for **Star Matching** only.
  - name: Comet Output
    kind: combo
    default: Comet only
    options: [Comet only]
    effect: Aligns the frames on the comet, so that the stars trail.
    enabled: Shown for **Comet Tracking** only.
  - name: Track Comet...
    kind: button
    default: ""
    effect: Opens **Track Comet**, where you mark the comet's position in the frames. A line under it counts the positions and states the tracking options.
    enabled: Shown for **Comet Tracking** only.
  - name: Correct chromatic aberration before registration
    kind: check
    default: "Off"
    effect: The window's **Chromatic aberration correction**, and enables the four values under it. A line under them says what the correction will do.
  - name: Edge Fraction
    kind: spinbox
    default: "0.55"
    widget: {min: 0.3, max: 0.8, step: 0.05, decimals: 2}
    effect: Uses only the stars farther from the center than this share of half the frame's diagonal, where the color fringes show.
    tradeoff: Higher keeps fewer stars, nearer the corners.
  - name: CA Fit Type
    kind: combo
    default: Similarity
    options: [Similarity, Affine]
    effect: Chooses the model that moves red and blue onto green. **Affine** also allows shear and a different scale on each axis.
    tradeoff: Affine needs more and better matches.
  - name: Min Matched Stars
    kind: spinbox
    default: "6"
    widget: {min: 3, max: 50, step: 1}
    effect: >-
      Sets the matches a channel needs before it is moved, never fewer than five. A match of fewer than ten candidate
      pairs also needs three fifths of them to fit.
    tradeoff: Higher is safer and leaves sparse channels uncorrected.
  - name: CA Interpolation
    kind: combo
    default: Lanczos4
    options: [Bilinear, Bicubic, Lanczos4, Pixel Area Relation, Nearest Neighbor, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: Chooses how a moved channel is resampled. Where it does not reach, the pixel or two at the edge it moved off, the channel keeps its own samples.
  - name: Standard / Mosaic / Intersection
    kind: radio
    default: Standard
    options: [Standard, Mosaic, Intersection]
    effect: The window's canvas choice. Here it can be changed before a registration, when the window's card is disabled.
  - name: Interpolation
    kind: combo
    default: Auto
    options: [Auto, Nearest Neighbor, Bilinear, Bicubic, Pixel Area Relation, Lanczos4, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: The window's **Interpolation**.
  - name: Advanced Settings...
    kind: button
    default: ""
    effect: Opens **Advanced Registration Settings**, the star detection and the color-filter settings.
  - name: Reset to Defaults
    kind: button
    default: ""
    effect: Returns the dialog's values to their defaults. The dialog stays open, and nothing changes until **OK**.
    note: >-
      It sets **Transform Model** to **Auto** and ticks **Auto-select**, where the window's **Reset** returns
      **Homography** and **Manual**.
  - name: Apply to All Groups
    kind: button
    default: ""
    effect: Does nothing in this window. It belongs to Image Integration, which has several groups.
    enabled: Disabled for **Comet Tracking**.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and keeps the window's values.
  - name: OK
    kind: button
    default: ""
    effect: Gives the dialog's values to the window and closes it.
```

### Advanced Registration Settings

**Advanced Settings...** opens this dialog over **Registration Settings**. **OK** keeps its values for that dialog,
and **Cancel** returns them to what they were when it opened.

```controls
window: Advanced Registration Settings
controls:
  - name: Radius
    kind: spinbox
    default: "5"
    widget: {min: 3, max: 32, step: 1}
    effect: Sets the size, in pixels, of the box a star is searched and fitted in. A slider beside it moves it too.
    tradeoff: Too small cuts broad stars short. Too large merges close stars and costs time.
    enabled: With **Star Matching**, as every setting of **Star Detection** is.
    note: >-
      A stored value above 32 runs at 32, and the log file says so. **Threshold**, **Roundness Threshold** and the
      limits of the two ranges have sliders too.
  - name: Threshold
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 15, step: 0.05, decimals: 2}
    effect: Sets how far above the local background, in units of its noise, a peak must rise to count as a star.
    tradeoff: Low finds noise and hot pixels as stars. High misses faint stars.
  - name: Enable Amplitude Range
    kind: check
    default: "Off"
    effect: Keeps only the stars whose fitted peak lies between **Minimum** and **Maximum**, as fractions of full scale.
    tradeoff: A narrow range drops saturated or faint stars and can leave too few to match.
  - name: Minimum
    object: thresholdMinimumSpinBox
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 0.99, step: 0.05, decimals: 2}
    effect: The smallest peak kept, while the range is enabled.
  - name: Maximum
    object: thresholdMaximumSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: The largest peak kept. Lower drops saturated stars.
  - name: Roundness Threshold
    kind: spinbox
    default: "0.50"
    widget: {min: 0.01, max: 0.95, step: 0.05, decimals: 2}
    effect: Sets the least roundness a star must have, from 0 for a line to 1 for a circle.
    tradeoff: Higher drops trailed stars and the stretched stars of the corners.
    note: A stored value of 0 or below runs at 0.01, and the log file says so.
  - name: Enable Roundness Range
    kind: check
    default: "Off"
    effect: Adds the upper limit **Maximum** to the roundness.
  - name: Maximum
    object: roundnessThresholdMaximumSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: The roundest a star may be, while the range is enabled.
  - name: Convergence
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 3, step: 1}
    effect: Sets how many fitting passes each star gets.
    tradeoff: More passes steady the fit on noisy data and cost time.
  - name: Relax PSF Checks
    kind: check
    default: "Off"
    effect: Accepts stars whose profile fails the stricter checks.
    tradeoff: Helps distorted or faint stars, and lets more false stars through.
  - name: Profile Type
    kind: combo
    default: Gaussian
    options: [Gaussian, Moffat]
    effect: Chooses the profile fitted to each star. **Moffat** follows broad wings.
    tradeoff: Gaussian is fast and steady. Moffat fits real stars better and is more sensitive.
  - name: Minimum Beta
    kind: spinbox
    default: "1.50"
    widget: {min: 0.05, max: 10, step: 0.05, decimals: 2}
    effect: Sets the smallest exponent a Moffat fit may take.
    note: The detector checks it under either profile. A stored value of 0 or below runs at 0.05.
  - name: CFA Pattern
    kind: combo
    default: Auto
    options: [Auto, RGGB, BGGR, GBRG, GRBG]
    effect: Chooses how a color-filter mosaic is read. **Auto** reads the pattern the file states, and another choice overrides it.
    tradeoff: A wrong pattern gives wrong colors and wrong star measurements.
    scope: The detection, the alignment and the comet's alignment of a mosaic frame. Other frames are read as they are.
  - name: Method
    kind: combo
    default: Default
    options: [Default, Bilinear, VNG4, AHD, AMaZE, LMMSE, RCD]
    effect: >-
      Chooses the debayer method for a mosaic frame. **Default** is the method of the **Raw Images** page of the
      settings, which the item names in brackets.
    scope: The detection and the aligned files of a mosaic frame.
  - name: OK
    kind: button
    default: ""
    effect: Keeps the values for **Registration Settings**.
  - name: Cancel
    kind: button
    default: ""
    effect: Returns the values to what they were when this dialog opened.
```

### Track Comet

**Track Comet...** in **Registration Settings** opens this dialog. In this window you mark the comet by hand: the
automatic tools stay disabled.

```controls
window: Track Comet
controls:
  - name: Frame
    kind: readout
    default: The first frame
    effect: Lists the frames. Selecting one reads it and shows it in the preview beside the list.
  - name: Comet preview
    kind: readout
    default: ""
    effect: Shows the selected frame. Zoom, and click the comet's core to place a pending point.
    tradeoff: The fit follows the points, so a misplaced point pulls the track off.
  - name: Mark
    kind: button
    default: ""
    effect: Adds the pending point as an observation of the selected frame.
    enabled: With a pending point on the selected frame.
  - name: Clear Point
    kind: button
    default: ""
    effect: Removes the pending point.
    enabled: With a pending point.
  - name: Refine
    kind: button
    default: ""
    effect: Does nothing in this window. It stays disabled.
  - name: Auto Track
    kind: button
    default: ""
    effect: Disabled in this window, which offers no automatic tracking.
  - name: Find Moving Object
    kind: button
    default: ""
    effect: Disabled in this window, which offers no search.
  - name: Observations
    kind: readout
    default: ""
    effect: >-
      Lists each observation with **Use**, **Frame**, **X**, **Y** and **Origin**. **Use** is ticked for a new mark,
      and unticking it leaves the mark out of the fit.
    tradeoff: Leaving out a poor mark can steady the fit, and too few marks cannot fit a track.
  - name: Diagnostics
    kind: readout
    default: ""
    effect: Lists candidate positions with **Accept** to take one. In this window nothing fills it.
  - name: Clear
    kind: button
    default: ""
    effect: Removes every observation.
  - name: Search radius
    kind: spinbox
    default: "32"
    widget: {min: 1, max: 64, step: 1}
    effect: Measures the nucleus within this many pixels of where the track places it, in **Tracking and Fit Options**.
    tradeoff: Larger tolerates a poorer track and can take a star beside the nucleus for it.
  - name: Minimum SNR
    kind: spinbox
    default: "3.0"
    widget: {min: 0.1, max: 100, step: 0.5, decimals: 1}
    effect: Refuses a nucleus fainter than this many times the noise around it. Its frame is then aligned where the track places it.
  - name: Bootstrap frames
    kind: spinbox
    default: "8"
    widget: {min: 0, max: 4096, step: 1}
    effect: Serves **Auto Track** only, which this window does not offer.
  - name: Track model
    kind: combo
    default: Linear
    options: [Linear, Automatic, Prefer quadratic]
    effect: Chooses how the path is fitted. **Linear** is a constant speed. **Automatic** adds an acceleration when it fits clearly better, and **Prefer quadratic** whenever enough marks remain.
  - name: Reject outlier observations
    kind: check
    default: "On"
    effect: Leaves out of the fit a mark farther from the track than **Outlier sigma** allows.
  - name: Outlier sigma
    kind: spinbox
    default: "3.0"
    widget: {min: 1, max: 100, step: 0.5, decimals: 1}
    effect: Sets how many times the fit's residual a mark may lie from the track.
    enabled: While **Reject outlier observations** is on.
  - name: OK
    kind: button
    default: ""
    effect: Keeps the observations and the options for the registration.
    enabled: >-
      It needs two enabled observations. With fewer it says "Add or accept a second comet observation before
      submitting." and stays open.
    note: The positions and the frames' times are checked again when you register.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and keeps the observations as they were.
```

### Add Folder Options and Blink Comparator

```controls
window: Add Folder Options
controls:
  - name: Include subfolders (recursive)
    kind: check
    default: "On"
    effect: Adds the images of every folder under the chosen one too.
    tradeoff: A broad scan can bring in frames of other sessions and other kinds.
  - name: Frame Type
    kind: combo
    default: All
    options: [All, Light, Dark, Flat, Bias, Dark Flat]
    effect: >-
      Keeps only the files whose frame type matches. **Bias** also takes `OFFSET`, **Dark** leaves out dark flats,
      **Flat** leaves out dark flats, and **Dark Flat** needs both words.
    range: A file whose header states no frame type counts as a light.
  - name: Filter Name
    kind: field
    default: ""
    effect: Keeps only the files whose filter is exactly this text, without regard to case. Empty keeps every filter.
    tradeoff: A file with a misspelled filter, or none, is left out.
  - name: OK
    kind: button
    default: ""
    effect: Scans the folder and adds what passes. The choices last for this scan only.
```

```controls
window: Blink Comparator
controls:
  - name: Blink
    kind: readout
    default: ""
    effect: >-
      Shows the selected frames one after another. ++space++ plays and pauses, ++left++ and ++right++ step, and ++x++
      excludes the frame shown.
    tradeoff: Each frame is read whole, which costs time and memory.
    persistence: >-
      When it closes, the frames you excluded are excluded in the window. Ratings you give with ++1++ to ++9++ stay in
      the comparator. It opens at the size you left it.
```

## How it works

1. **Register** reads every frame as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale)
   in 32-bit floating point, debayering a mosaic with **CFA Pattern** and **Method**. It detects up to **Max Stars per
   Frame** stars in the green channel of a color frame, or the one channel of a grayscale frame, and measures them.
2. **Star Matching** pairs each frame's stars with the reference's through triangles of stars, keeps the pairs that
   agree within 3 pixels by [RANSAC](../../reference/glossary.md#ransac), and fits **Transformation** to them. A frame
   the reference does not match is tried through the frames registered nearest it. When a frame's own header states a
   [SIP](../../reference/glossary.md#sip) distortion, its stars are corrected by it first, and **Align** undoes it.
3. The transforms and figures are written to `registration_metrics.db` in the output directory, read back and shown
   in the table. A later run reuses them while the frames and settings still match.
4. Exclusions and thresholds choose the frames **Align** takes, and change no transform.
5. **Align** plans one canvas, maps each of its pixels back into every frame through its transform, samples the frame
   there with **Interpolation**, and writes one file per frame.

### Model

A star at \((x, y)\) in a frame is placed on the reference by the transform \(H\), a 3 × 3 matrix, at \((x', y')\):

\[ \begin{pmatrix} u \\ v \\ w \end{pmatrix} = H \begin{pmatrix} x \\ y \\ 1 \end{pmatrix}, \qquad x' = \frac{u}{w}, \quad y' = \frac{v}{w} \tag{1} \]

**Similarity** leaves 4 numbers of \(H\) free, a shift, a turn and one scale, and **Affine** 6. For both, the last
row of \(H\) is \((0, 0, 1)\), so \(w = 1\). A [homography](../../reference/glossary.md#homography) leaves 8 free. A
polynomial of order \(n\) then adds to each axis a correction with

\[ T_n = \frac{(n + 1)(n + 2)}{2} \tag{2} \]

terms, 6, 10 and 15 for orders 2, 3 and 4, in the frame's position measured from its center in half-widths.

With \(P\) the value of **Min Star Pairs**, \(n_c\) the candidate pairs the triangles propose, and \(n_i\) the pairs
that fit one transform within 3 pixels, a match is accepted when

\[ n_i \ge \max(P, 5) \quad \text{and, when } n_c < 10, \quad \frac{n_i}{n_c} \ge 0.8 \tag{3} \]

The second condition is waived when either frame holds 1,000 stars or more. With \(d_k\) the distance, in pixels,
between the \(k\)-th of the \(n_i\) stars placed by equation (1) and its partner on the reference, the **RMS** column
shows the root mean square residual:

\[ \mathrm{RMS} = \sqrt{\frac{1}{n_i} \sum_{k=1}^{n_i} d_k^{2}} \tag{4} \]

With a catalog match, it is multiplied by \(s\), the mean of the two
[pixel scales](../../reference/glossary.md#pixel-scale) the frame's plate solution states, and shown in arcseconds.
With \(r\) the median roundness of the frame's stars, from 0 for a line to 1 for a circle, **Ecc.** is

\[ e = 1 - \min\bigl(1, \max(0, r)\bigr) \tag{5} \]

The [Image Registration Reference](image-registration-reference.md) gives the polynomial's trimming, the choice of
**Auto**, and the score **Auto** uses to choose the reference.

### Boundaries

- **Register** refuses frames that differ in width, height, pixel type or channel count, and a reference whose stars
  cannot be matched. A frame that cannot be read or matched is marked **Failed**, and the run goes on.
- A sample that is not a number, which calibration writes where a flat is zero, is left out of star detection, and
  the frame's stars are still found. **Phase Correlation** refuses such a frame, and frames under 8 × 8 pixels.
- A polynomial order the pairs cannot carry gives way to the linear fit over them, and the
  [log file](../../reference/glossary.md#log-file) says so. The frame fails only when that fit is refused too.
- A stopped registration records nothing and leaves an earlier record as it was. A stopped alignment keeps the files
  it finished, and the frame it was writing leaves no file.
- Canvas pixels a frame does not reach are 0. Near its own edge a frame is sampled from its edge pixels rather than
  from a black border, so **Bicubic** and **Lanczos4** do not ring there.

### Worked example

The data are 35 lights of NGC 253, 180 seconds each, from a QHY183C color camera on a 432 mm refractor, with 2.4 µm
pixels. The worked example of [Image Calibration](../calibration/image-calibration.md) wrote them into a folder
`lights_cal` as `<name>_cal.fits`: 5544 × 3684 RGGB mosaics in 32-bit floating point, about 82 MB each. The mount
crossed the meridian after the 14th frame, so the last 21 are turned half a turn from the first 14.

Add the 35 files in name order, choose an empty folder `lights_reg` as the output directory, and leave every other
control at its default. Then choose **Register**. Each frame is read, debayered, and up to 2,000 of its stars are detected in its green channel.
Triangles of stars keep their shape when a frame turns, so star matching treats the 21 frames after the flip like the
others, and their transforms hold the half turn. Each row then shows its figures. **RMS** is in pixels,
since the frames hold no plate solution.

Then choose **Align**. On the reference's grid each file holds three channels of 5544 × 3684 samples of 4 bytes,
\(5544 \times 3684 \times 3 \times 4 = 245{,}089{,}152\) bytes, about 245 MB. The 35 files take about 8.6 GB. Open two
of them, one from each side of the flip, at 100 %, and check that their stars lie on the same pixels. Read one in
[FITS Header, EXIF, And Astrometry Metadata](../../interface/fits-header-viewer.md): it keeps `EXPTIME` and the
frame's observing cards, and has no WCS cards.

At 2.4 µm and 432 mm, one pixel covers \(206.265 \times 2.4 / 432 = 1.146\) arcseconds, so an **RMS** of 0.40 pixels
would read 0.46″ with a catalog match.

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| Dithered lights of one night and one camera | **Star Matching**, **Homography**, a sharp frame from the middle of the session as reference, **Standard** | **Similarity** for a plain dither. A higher **Max Stars per Frame** for a crowded field. |
| A wide field whose corners do not line up | **Polynomial 2** | A higher order or **Auto** only while the corners stay off. Improve detection and the reference first. |
| Frames that differ by a shift only, with structure and few stars | **Phase Correlation**, with a fixed reference | **Star Matching** when the frames also turn or change scale. |
| Panels of a mosaic | **Mosaic** in **Extended Settings...**, before **Register** | **Min Star Pairs** low enough for the panels' overlap, and room for a large canvas. |

## When it goes wrong

!!! danger "Align replaces files without asking"
    Each output is named after its source with `_reg.fits`, and an existing file of that name is replaced. Two
    sources of one name in different folders write one file. Give each set its own output directory.

!!! danger "Delete From Disk deletes the source file"
    **Delete From Disk** in the frame list's menu removes the file for good after one question. **Remove** only takes
    the row out of the list.

!!! warning "Chromatic aberration correction changes no output"
    Stars are detected in the green channel, which the correction does not move, and **Align** reads the original
    files. The option only adds time. Leave it off.

"Could not build registration persistence context: Registration output directory is empty."
:   **Cause.** **Register** was chosen before an output directory.
:   **Fix.** Choose **Browse**, pick a folder, and register again.

"Some images in the list have different size or color depth"
:   **Cause.** A frame differs from the others in width, height, pixel type or channel count.
:   **Fix.** Register each kind of frame as a set of its own, or bring them to one size and pixel type first.

"The reference frame holds too few stars to match"
:   **Cause.** The reference's stars cannot form the triangles the matching needs.
:   **Fix.** Choose another reference, check **CFA Pattern** for a mosaic, or lower **Threshold** in **Advanced
    Registration Settings**.

Rows read **Failed** though their frames show plenty of stars
:   **Cause.** Fewer pairs than **Min Star Pairs** matched the reference and every frame registered near it, or a small
    match failed equation (3).
:   **Fix.** Choose a sharper reference, lower **Min Star Pairs**, or try a simpler **Transformation**. Step through
    the failures with **Next Failed** and **Blink Compare...**.

The status line reads "Registration needs update" and **Align** is disabled
:   **Cause.** The frames, the reference or a setting the registration depends on changed since **Register**.
:   **Fix.** Choose **Register** again. With **Skip already-registered frames** on, it reuses the stars it found.

**Standard**, **Mosaic**, **Intersection** and **Interpolation** cannot be changed
:   **Cause.** The **Alignment** card waits for a registration that answers the frames and settings, and a switch to
    **Mosaic** makes the registration out of date.
:   **Fix.** Choose the canvas under **Alignment** in **Extended Settings...**, then register.

"The registration could not be recorded in '...registration_metrics.db': ..."
:   **Cause.** The database in the output directory cannot be written: a read-only folder, a database another program
    holds open, a file of that name that is no database, or one written in another format.
:   **Fix.** Choose a writable folder of its own, or close what holds the file, and register again.

**Zero Point** reads "-" and **RMS** is in pixels
:   **Cause.** No Gaia catalog is ready, the frame has no plate solution, or the catalog matched none of its stars.
:   **Fix.** Install a catalog, then turn on **Auto-solve unsolved frames** or solve the frame with **Solve...**, and
    register again.

**Auto-solve unsolved frames** or **Enrich with Gaia catalog** is disabled
:   **Cause.** No plate solver is set up, or no Gaia catalog is ready.
:   **Fix.** Set them up in [Settings](../../interface/settings-dialog.md). Your choice comes back when they are ready.

The aligned frames have black edges
:   **Cause.** **Standard** keeps the reference's frame, and a shifted or turned frame does not fill it. **Mosaic**
    leaves black wherever no frame reaches.
:   **Fix.** Choose **Intersection** and align again, or crop the stack.

"Comet tracking requires accepted observations, finite timestamps, and an explicit reference frame."
:   **Cause.** **Track Comet** holds fewer than two enabled observations, or no reference is set.
:   **Fix.** Mark the comet in two frames or more, set a reference, and check that every frame has a `DATE-OBS` card.

**Register** detects every frame again
:   **Cause.** **Skip already-registered frames** is off, the output directory is another, or a detection setting or
    a file changed.
:   **Fix.** Keep one output directory per set, and leave the detection settings as they were.

## Related

- [Image Calibration](../calibration/image-calibration.md), which makes the calibrated lights this window takes.
- [Image Stacking](../stacking-integration/image-stacking.md), which stacks the aligned files and reads the database
  this window writes.
- [Image Integration](../stacking-integration/image-integration.md), which calibrates, registers and stacks in one
  workflow, and shares the registration database.
- [Plate Solving](plate-solving.md), to solve a frame for **Solve...**, or the stack for sky coordinates.
- [Image Registration Reference](image-registration-reference.md), for the matching, the database, the files Align
  writes, the comet run and every message.
- [Process Windows](../../interface/process-windows.md), for the preview pane and what every process window shares.

## Credits

- The star detection and its point-spread-function fit are adapted from the star finder and the profile fit of
  [Siril](https://gitlab.com/free-astro/siril), Copyright (C) 2005-2011 Francois Meyer and Copyright (C) 2012-2026
  team free-astro, under GPL-3.0-or-later. **About > About AfterNight** credits it.
- Color-filter frames are debayered by methods adapted from [RawTherapee](https://github.com/Beep6581/RawTherapee)'s
  demosaic code as [librtprocess](https://github.com/CarVac/librtprocess) packages it, under GPL-3.0-or-later.
  **About > About AfterNight** credits it.
- [Phase correlation](../../reference/glossary.md#phase-correlation) computes its transforms with
  [FFTW](https://www.fftw.org), by Matteo Frigo and Steven G. Johnson, under GPL-2.0-or-later.
- [Fischler and Bolles, "Random Sample Consensus", 1981](https://doi.org/10.1145/358669.358692) is the primary
  reference for RANSAC. AfterNight's thresholds and acceptance rules are its own.
- The [OpenCV geometric-transform and robust-estimation documentation](https://docs.opencv.org/4.x/d9/d0c/group__calib3d.html)
  is background for affine and homography estimation. AfterNight's matcher and estimator are its own.
- The [OpenCV motion-analysis documentation](https://docs.opencv.org/4.x/d7/df3/group__imgproc__motion.html) is
  background for phase correlation. Its sub-pixel peak is the same weighted centroid. The Hann window and the rest of
  the measurement are AfterNight's.
- [The SIP Convention for Representing Distortion in FITS Image Headers](https://fits.gsfc.nasa.gov/registry/sip/SIP_distortion_v1_0.pdf)
  defines the distortion terms a frame's own header may state.
- The [Gaia DR3 documentation](https://gea.esac.esa.int/archive/documentation/GDR3/) describes the catalog the
  enrichment reads.
