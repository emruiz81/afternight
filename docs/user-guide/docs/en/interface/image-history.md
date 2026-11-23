---
kind: interface
summary: >-
  Every image keeps the steps made to it. The history strip under a view shows them with Undo and Redo, and the Image
  History window previews, restores, clones or removes a step and saves the list as text.
menu: Edit > Image History
---
# Image History

Every image or [sequence](../reference/glossary.md#sequence) view keeps a [history](../reference/glossary.md#history):
the state the view began with, then one step for each change of the image - an Apply, a mirror or a turn, a format
conversion, a [plate solution](../reference/glossary.md#plate-solution) written into it, a rename. The history strip
under the view, and under a [task](../reference/glossary.md#task)'s canvas, shows the steps with **Undo**, **Redo** and **Image History**. The Image
History window lists the steps, shows any of them without changing the image, and restores, clones or removes one.

A history keeps 20 states, the first one included. Without a [project](projects.md) it lasts as long as its view, so
save the states you need to keep. An image of a [project](../reference/glossary.md#project) keeps its history in the project's folder.

```figure
owed: true
image: ../assets/images/interface/image-history.webp
alt: >-
  The centre of the main window with the Carina Nebula master in its view, stretched, its tab named RGB with an
  asterisk, and the floating toolbar near the bottom. Under the view, the history strip reads HISTORY, then three chips
  joined by chevrons: Initial state, Histogram Transformation, and Gamma Correction with a check on an accent ground.
  At its right are the undo, redo and history buttons.
caption: >-
  The history strip of the Carina master after Histogram Transformation and Gamma Correction: the image shows the
  last step.
pins:
  - {n: 1, x: 1.0, y: 97.9}
  - {n: 2, x: 6.9, y: 97.9}
  - {n: 3, x: 34.8, y: 97.9}
  - {n: 4, x: 90.7, y: 97.9}
```

## What you see

1. **HISTORY.** The strip's label, at its left.
2. **The steps.** A chip for each state of the image, the oldest at the left, with chevrons between them. Resting the
   pointer on a chip shows what the step recorded. A step inherited from the image this one was made from is in pale
   text.
3. **The current step.** The chip with a check, in the accent color: the state the image shows.
4. **Undo, Redo and Image History.** At the strip's right. **Undo** and **Redo** are enabled when the history has a
   step for them.

While a task is open on the image, a dashed chip after the current step names its process and "pending": the step it
will make, which is not in the history until Apply. During the run it reads "writing". When the steps do not fit, the
oldest give way to a "..." chip that opens the Image History window.

## What you can do

### Undo and redo a step

Choose **Undo** on the history strip, **Edit > Undo**, or press ++ctrl+z++. The command names the step it takes back,
as `Undo <Color Saturation>`. **Redo**, or ++ctrl+y++, makes the step again. The state is read back in the background
and replaces the image when it is ready. Until then the window stays responsive, and pressing the command again does
nothing. On a task's page, the strip and the commands act on the task's target.

### Go to a step from the strip

Click a chip of the strip. The image takes that state, as **Jump To Step** does in the window below, and the chip
takes the check. The current chip and the inherited ones take no click.

### Look at an earlier state

1. Choose **Image History** on the history strip, **Edit > Image History**, or press ++ctrl+shift+z++ over the view.
2. Click a row once. After a moment its picture, histogram and description appear on the right. The image does not
   change, and the bold row stays where it was.

### Go back to a step

Double-click a row, or right-click it and choose **Jump To Step**. The image takes that state, the row turns bold and
the view becomes the active one. The arrows do the same for the step just before or after the bold row, whichever row
is selected.

The later steps stay until the image changes again. The new step then replaces every step after the bold row, without
a question.

### Keep another version

Right-click a row and choose **Clone From Step**. The state opens as a new image, under the name the image had then,
and the original stays as it is. Clone a later state before you change an image you took back.

### Remove a step

Right-click any row of the view's own but the first and choose **Remove Step**. The step goes at once, without a
question, and cannot be brought back. When it is the bold row, the image first takes the state before it.

### Save the list

Choose the disk, or **Save History Log...** on a row's menu, and name a text file. The file lists the inherited steps,
then the image's own, with the current step marked by `*`. It keeps no pixels and restores nothing.

### See where an image came from

A view made from another one - a duplicate, a clone, a snapshot, the new image of many processes - shows the other's
steps above its own, under `Inherited from "<name>"`, and the strip shows them first, in pale text. This inherited
[lineage](../reference/glossary.md#lineage) can be read and saved, but not selected, restored or cloned. The eye hides
it and shows it again.

### Keep a history with a project

While a project is open, an image opened from a file, or saved, becomes the project's, and its steps are written into
the project's folder. They stay there when the view closes. Opening the file again in the project gives its history
back, the step the file holds being the current one. A file changed since is not given its old history: the project
sets it aside and says so. A sequence keeps its history with the session. [Projects](projects.md) says how a project
keeps, moves and compacts histories.

## Controls

### History strip

```controls
window: History strip
controls:
  - name: Steps
    kind: readout
    default: ""
    effect: A chip for each state, the oldest at the left, the current one with a check. The steps of the image the strip shows - the workspace's active image, or a task's target - and nothing of a floating image, which has no strip.
    range: When the chips do not fit, the oldest give way to a "..." chip, whose tooltip reads "Earlier steps", and which opens the Image History window.
    note: The tooltip of a step gives its description, of the current one "the image shows this step", and of an inherited one "inherited from the image this one was made from". A task open on the image adds its dashed chip, "pending" until Apply and "writing" during the run.
  - name: Undo
    kind: toolbutton
    default: ""
    effect: Restores the state before the bold step and moves the bold row up one. **Edit > Undo** names the step it takes back, as `Undo <Histogram Transformation>`, and so does the button's tooltip.
    enabled: When the image the strip shows has a step before the current one. ++ctrl+z++ and **Edit > Undo** do the same.
    tradeoff: The state is read from its snapshot on disk in the background, and waits there for a snapshot still being written. A large image or slow temporary storage delays it. There is no progress window and no cancel.
    scope: The image or sequence of the active view - its pixels, its header and plate solution, its valid pixels and the name it had - as the step recorded them. A still image keeps the file it was opened from, so saving it writes there.
    persistence: The step after it stays, and **Redo** brings it back until the image changes again.
    note: "One restore runs at a time: pressing it again before the first has landed does nothing. A restore that fails changes nothing and says \"Could NOT undo the last step. Error message: ...\" in a message titled **Image History**. Without an active image it says \"No selected image to undo changes from\"."
  - name: Redo
    kind: toolbutton
    default: ""
    effect: Restores the state after the bold step and moves the bold row down one. **Edit > Redo** names the step, as `Redo <Color Saturation>`, and so does the button's tooltip.
    enabled: After an Undo or a jump back, until the image changes. ++ctrl+y++ and **Edit > Redo** do the same.
    note: "A restore that fails changes nothing and says \"Could NOT redo the next step. Error message: ...\". Without an active image it says \"No selected image to redo changes to\"."
  - name: Image History
    kind: toolbutton
    default: ""
    effect: Opens the Image History window of the image the strip shows.
    enabled: With an image or sequence. ++ctrl+shift+z++ over the view and **Edit > Image History** do the same. A task's preview has no history of its own, and its strip shows the target's.
    persistence: Each choice opens another window, which leaves the rest of the application usable and closes with its view. Nothing of the window is kept when it closes.
    note: Without an active image, **Edit > Image History** says "No selected image to show history from".
```

### The window

```controls
window: Image History
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the view, as `Image History <RGB>`, and follows a rename. The heading inside reads **Image History**, over "Review the sequence of edits applied to the active image and refresh the timeline when needed."
  - name: History Timeline
    kind: readout
    default: ""
    effect: Lists the states, one row each, under **Step**, **Executed** and **Description**. **Initial state** comes first, then each step in the order it was made. The bold row is the current state, and its tooltip reads "Current active step".
    range: At most 20 states, the first row included. A step that would make 21 deletes the first state without a question, and the oldest step, under its own name, becomes the first row, which nothing can go back past.
    tradeoff: A process writes in **Description** what it chose to record. It may leave out the mask, a value or what you would need to run it again, and a step is no preset.
    scope: One view. The rows cannot be sorted or edited, and one row is selected at a time.
    persistence: While the view is open. Saving the image does not store the history, and closing the view deletes it.
    note: The window follows every change of the history and then selects the bold row again.
  - name: Step
    kind: readout
    default: ""
    effect: The name of the state - **Initial state**, the name of a process, **Rename**, or the name of an edit of the view such as **Autostretch**.
  - name: Executed
    kind: readout
    default: ""
    effect: When the step was recorded, in local time to the second, as `2026-11-10 21:04:17`. It is not how long the step took.
  - name: Description
    kind: readout
    default: ""
    effect: What the step recorded. **Initial state** says how the view began, as `Image loaded from file '<path>'`, `Sequence loaded from file '<path>'`, `Cloned from image <view>` for a duplicate, or `Cloned from '<view>' step '<step>'`. A rename records the new name, and a process its values, in its own words. It may be empty.
  - name: Inherited rows
    kind: readout
    default: ""
    effect: "Above the view's own rows, the states of the image it was made from, grouped under `Inherited from \"<name>\"`, then a row **Current Image History** before the view's own."
    enabled: Only in a view made from another view. An opened file has none, and some processes start their new image without them.
    range: The other view's own inherited rows, then every state it kept when this view was made, its later steps included. Each records a name, a time and a description.
    tradeoff: They are in italics and cannot be selected, restored, cloned or removed. Their tooltip reads "Inherited from a previous image (read-only)". A heading row spans the three columns and has no time.
  - name: Selected Step Preview
    kind: readout
    default: ""
    effect: "Shows the selected row: its name on one line, with **(Current)** after the bold row's, then on up to five lines its time, \"Active step in the current image.\" or \"Previewing a historical step. Double-click or use the context menu to jump.\", and its description."
    range: Text that does not fit ends in an ellipsis, and resting the pointer on it shows all of it. So the picture below keeps its place from row to row.
    note: When the state cannot be read, a line reads "Preview unavailable for this step.", the picture is cleared and the histogram hidden.
  - name: Picture
    kind: readout
    default: ""
    effect: Shows the selected state reduced to fit, in 8 bits for each channel - gray, color, or color with alpha.
    range: A state that is linear data is stretched for the screen, each color channel on its own. Linear data is an image recorded linear, a stack whose header counts its frames, a frame whose header records its frame type, or a camera raw frame - unless the image records a stretch, or an sRGB, Rec. 709 or gamma encoding. A state an extension made is judged by the header the extension gave it, so one that kept its source's header and records no stretch is shown stretched. Any other state is shown as it is, at full scale.
    tradeoff: It is a picture to look at, not a measure. It cannot be zoomed, and holding the left button over it shows no lens.
    scope: A sequence step shows the frame it changed. Any other sequence step shows the frame the view shows now, read from the step's sequence with the view's present debayer setting, so it shows the step in today's context, not as the view displayed it then.
    note: The state is read in the background, and waits there for a snapshot still being written or a frame being decoded. A newer selection replaces a picture still on its way.
  - name: Histogram
    kind: readout
    default: ""
    effect: Counts the selected state at full precision and draws each channel as a filled area, with **Clipping Low** and **Clipping High** in its corner.
    enabled: Shown when the state could be read.
    tradeoff: Counting a large state takes time and memory.
    note: It has no toolbar. The wheel with ++ctrl++ held zooms it, as in a task.
  - name: Up arrow
    kind: button
    default: ""
    effect: Restores the state just before the bold row, as **Undo** does. Its tooltip reads `Jump to <step>`, naming the bold step it takes back, as **Undo** does, not the row it moves to. It reads "No earlier step available." on the first row.
    enabled: When the bold row is not the first.
    scope: It moves from the bold row, not from the selected one. It never reorders the rows.
  - name: Down arrow
    kind: button
    default: ""
    effect: Restores the state just after the bold row, as **Redo** does. Its tooltip reads `Jump to <step>`, or "No later step available."
    enabled: When there is a step after the bold row.
  - name: Eye
    kind: toolbutton
    default: "On"
    effect: Hides the inherited rows, or shows them again. Its tooltip reads "Hide inherited processing history from parent images", or "Show inherited processing history from parent images" while they are hidden.
    enabled: Shown only when the view has inherited rows.
    scope: This window only. A saved list includes the inherited rows either way.
    persistence: Every window opens with them shown.
  - name: Disk
    kind: button
    default: ""
    effect: Asks for a file and writes the whole history to it as text. Its tooltip reads "Save the complete history log to a text file", and the name offered is `<view>_history.txt`.
    range: "The inherited steps come first, under `INHERITED PROCESSING HISTORY (from parent images, read-only)`, with a line `--- Inherited from \"<name>\" ---` for each image and the description under each step. Then the view's own, under `CURRENT IMAGE PROCESSING HISTORY`, one line each, as `[2] * 2026-11-10 21:04:17 - Color Saturation (<description>)`. `*` marks the current step, and no mark is written when the first row is current."
    tradeoff: The file holds the names, descriptions and file paths the steps recorded, so read it before you share it. It holds no pixels, no snapshot, no view setting and nothing to run again, and it cannot restore a state.
    persistence: The dialog opens in the folder of the last history you saved, while that folder exists. AfterNight adds no extension to the name you type.
    note: A file that cannot be written gives "Unable to write the history log file." in a message titled **Save History Log**.
  - name: Refresh
    kind: button
    default: ""
    effect: Reads the history again, selects the bold row and shows its picture.
    tradeoff: The window follows the history by itself, so it is rarely needed, and it reads a snapshot again.
  - name: Close
    kind: button
    default: ""
    effect: Closes this window. The history stays as it is. ++enter++ and ++esc++ do the same.
```

### A row's menu

```controls
window: A row's menu
controls:
  - name: Double-click
    kind: action
    default: ""
    effect: Restores the state of the row, as **Jump To Step** does.
    enabled: On a row of the view's own that is not bold. On the bold row or an inherited row it does nothing.
  - name: Jump To Step
    kind: action
    default: ""
    effect: Restores the state of the row. Once it is shown, the row turns bold, the view becomes active and the arrows follow it.
    enabled: On a row of the view's own that is not bold, while no state is being restored.
    tradeoff: The image is replaced. The later steps stay until the next change of the image, which replaces them.
    scope: The pixels, the header, the plate solution, the valid pixels and the name the state had. A name another open view has taken since is made unique, and the step keeps the new one, which later clones and inherited rows show.
    note: "A state that cannot be read changes nothing - not the image, its name or the bold row - and says \"Could NOT jump to the selected history step. Error message: ...\"."
  - name: Clone From Step
    kind: action
    default: ""
    effect: Reads the state of the row and opens it as a new image, under the name the image had then, made unique. Its history starts with `Cloned from '<view>' step '<step>'`.
    enabled: On any row of the view's own, the bold one included, never on an inherited row.
    tradeoff: It reads the state from disk and holds another image in memory.
    scope: A still image opens as one that has never been a file, so it has no file name, size or date, and its header, plate solution and valid pixels are the state's. A sequence step opens as a sequence tied to the step's sequence file, at the frame the step changed or else at the frame the view shows, with the view's debayer setting. The new view takes the original's unsaved mark and none of its detected stars.
    persistence: Its inherited rows are the original's whole list, later steps included, not only the steps up to the row.
    note: A state that cannot be read gives "Unable to clone the selected history step." in a message titled **History Clone**. Selecting another row, or a change of the history, before the new image opens can stop the clone with that message. Closing the window stops it without one.
  - name: Remove Step
    kind: action
    default: ""
    effect: Deletes the row's state at once, with no question and no way back, and frees its snapshot.
    enabled: On any row of the view's own but the first, while no state is being restored.
    tradeoff: Removing the bold row first restores the state before it, and the row goes only when that is shown. Removing an earlier or a later row leaves the image as it is, and a later one can no longer be redone.
    note: "A restore that fails removes nothing and says \"Could NOT remove the selected history step. Error message: ...\"."
  - name: Save History Log...
    kind: action
    default: ""
    effect: Saves the list as text, as the disk does.
    enabled: On any row, inherited and heading rows included.
```

### What a step keeps

```controls
window: What a step keeps
controls:
  - name: A step of a still image
    kind: readout
    default: ""
    effect: A copy of the image taken the moment the step is recorded, which later changes cannot reach. It is written to disk in the background with every sample, the header, the plate solution and the valid pixels, and read back bit for bit - every pixel type, extreme values, infinities and subnormals included.
    range: Up to two copies, and 1 GiB between them, wait for the writer across all open views, and a larger copy waits alone. An edit of a still image - an Apply, a mirror or a turn, a conversion, **Apply Stretch** - waits in the background until the state it replaces is written and its own copy has room, and only then is shown.
    tradeoff: A burst of edits on a large image, on slow storage, takes longer to appear, while the application keeps responding. A wait for the writer has no limit and no cancel, and an edit or a restore that seems stuck is waiting there. Read the Session Console when it lasts. A fast local disk for the temporary folder keeps the waits short, and closing views you no longer need frees the space of their snapshots.
    scope: The snapshots of a view are written in a folder of their own, whose name starts with `AN_HISTORY_`, inside the session's folder in the temporary location of Settings, or in the project's folder for an image of a project. Each is an XISF image whose samples are compressed as **History compression** on the Projects page of Settings says, with a record only AfterNight reads; a state XISF cannot hold is written as a snapshot of AfterNight's own. A step is written whole or not at all. The history reads back only the files it wrote, never the image's own file, whose name it never changes.
    persistence: Closing the view drops the copies still waiting, stops the one being written, closes its history windows and deletes that folder, and nothing else. A project's image keeps its folder in the project.
  - name: A rename
    kind: readout
    default: ""
    effect: A step **Rename** whose description is the new name. It shares the snapshot of the state it renames, so going across it changes the name and no pixel.
  - name: A step of a sequence
    kind: readout
    default: ""
    effect: The sequence file the view showed. A process that writes a new sequence file is recorded at once, by that file, with no copy. Undo and Redo open the file of the step again, at the frame the view shows.
    range: An edit of the frame on screen alone, such as **Apply Stretch** in a sequence view, writes that frame and its number before it is shown, and is refused when the frame cannot be written. Restoring that step moves to the frame and shows the kept one. Leaving it moves to the same frame and shows the sequence's own.
    tradeoff: The step needs its sequence file, and a reader for its format, until the view closes. A temporary sequence a process wrote stays until the application clears its temporary files.
  - name: What no step keeps
    kind: readout
    default: ""
    effect: The view's own settings - its channel, display mode, zoom and position, its mask and the way the mask is shown, the tasks open on it - and its detected stars. A restore leaves the settings as they are, except that a mask that no longer fits is removed and the channels offered follow the restored image. It clears the detected stars and marks the image unsaved.
  - name: A step that was not kept
    kind: readout
    default: ""
    effect: A snapshot that could not be written is reported, and its step stays in the list but cannot be restored. Undo, Redo and a jump to it change nothing and say why.
    range: An edit of the view whose undo cannot be kept is refused before it is shown.
    note: While a state is being restored, the other actions of the history and a new edit of the view are refused. A sequence a process replaces meanwhile cancels the restore.
```

## When it goes wrong

"Preview unavailable for this step."
:   **Cause.** The step's snapshot was not written or cannot be read, or its sequence has no frame there.
:   **Fix.** Keep the view open and its sequence file in place, free space in the temporary folder, read the Session
    Console, then choose **Refresh**.

"Could NOT keep the snapshot of step '...' of '...'; undo and redo cannot restore that step. Error message: ..."
:   **Cause.** The step's snapshot could not be written, most often because the disk of the temporary folder is full.
:   **Fix.** Free space there, and save the image now if that state matters: no undo or jump can reach it.

"Could NOT apply '...', because ..."
:   **Cause.** An edit of the view was refused before anything changed: what it replaces, its result or its frame could
    not be kept, or a state was still being restored.
:   **Fix.** Wait for the restore to land, free space in the temporary folder, and make the edit again.

The later steps are gone
:   **Cause.** The image changed while an earlier row was bold, and the new step replaced every later one.
:   **Fix.** They cannot come back. Next time, choose **Clone From Step** on the later state before you change the
    image.

The oldest steps are gone
:   **Cause.** The history keeps 20 states, and each new step past them drops the oldest.
:   **Fix.** Save or clone a state you may need before you make many more steps.

Undo restored the pixels but not the [mask](../reference/glossary.md#mask), the zoom or the display mode
:   **Cause.** A step keeps the image, not the settings of its view.
:   **Fix.** Set the display mode, the zoom and the mask again in the view.

## Related

- [Image Viewer](image-viewer.md), for the view the history strip stands under.
- [Tasks](tasks.md), for the step an Apply records and the pending chip of an open task.
- [Projects](projects.md), which keep the histories of their images.
- [Saving and Exporting](saving-and-exporting.md), to keep a state as a file.
- [Session Console](log-viewer.md), for what a failed snapshot, restore or save wrote.
- [Settings](settings-dialog.md), for the temporary folder and the compression of the snapshots.
