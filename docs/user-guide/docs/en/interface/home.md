---
kind: interface
summary: >-
  Home is the place AfterNight starts on: open files, start a sequence, an integration or a project, and return to
  recent images, sequences and projects.
---
# Home

Home is the place AfterNight starts on, and the one the [rail](../reference/glossary.md#rail)'s **Home** shows. From it you open images and
[sequences](../reference/glossary.md#sequence), start a new sequence, an image integration or a [project](../reference/glossary.md#project), and reopen
what you worked on, each with its picture. While a project is open, **Home** shows the project's own page instead,
which [Projects](projects.md) describes.

What Home keeps is a list of paths, pictures and choices: taking a file off the list never touches the file.

```figure
image: ../assets/images/interface/home.webp
alt: >-
  Home in the main window. At the top left, the day's date in small capitals over the heading Home; at the right, a row
  of buttons: Open file with the keys Ctrl O, New sequence, Image integration, New project and a button of three dots.
  Under them a wide card fills the width: the grey picture of a flat frame behind its file name, a line of facts from
  its header, a Continue button and a star. Below, the heading Recent images and sequences with the count 2 images and
  a line of help, then a search box with the keys Ctrl F, a box reading Recent beside the word Sort, and two icons for
  a grid and a list. Under them two tiles, a grey flat frame and the Carina Nebula, each tagged FITS, with its file
  name, its size and the day it was changed.
caption: >-
  Home with two recent images, the Carina master and a blue flat, the flat featured in the Continue row.
pins:
  - {n: 1, x: 3.9, y: 13.3}
  - {n: 2, x: 52.3, y: 14.4}
  - {n: 3, x: 96.9, y: 17.9}
  - {n: 4, x: 3.9, y: 22.4}
  - {n: 5, x: 94.3, y: 22.3}
  - {n: 6, x: 3.9, y: 48.9}
  - {n: 7, x: 62.9, y: 48.9}
  - {n: 8, x: 3.9, y: 58.0}
owed: true
```

## What you see

1. **The heading.** The day's date over **Home**.
2. **The quick actions.** **Open file**, **New sequence** and **Image integration**, the three ways to start work, and
   **New project**.
3. **More actions.** The button of three dots: **Extension Manager**, **Clear recents**, **System info**, and the menu
   that arranges the quick actions.
4. **The Continue row.** The image you used last, or the one you pinned, as a wide picture with what its header says and
   **Continue**. Beside it, the last project and the last sequence you used, when there are.
5. **The star.** Pins the featured image, so that it stays whatever you open next.
6. **Recent images and sequences.** How many images and sequences the list holds, and a line that says how to use the
   tiles.
7. **Search, sort and arrangement.** The search box, the **Sort** box, and the icons that show the files as a grid or a
   list.
8. **The recent files.** A tile for each file: its picture, its type, its name, its size and when it was changed. Click
   a tile to open the file, and right-click it for more.

## What you can do

### Open files

Choose **Open file** or press ++ctrl+o++, select one or more files and confirm. You can also drop files from your file
manager anywhere on the window. A **File Open** window follows the reading. Each file that opens gets a view of its
own in a [workspace](../reference/glossary.md#workspace), and the centre shows it.

A camera raw file, or a file whose header states a [CFA](../reference/glossary.md#cfa) mosaic, asks "Open CFA file"
first. **Yes** demosaics it with the **Demosaic Algorithm** of the Raw Images settings. **No** opens the mosaic as it
was recorded, to [debayer](../reference/glossary.md#debayer) later with the Debayer process.

A file that cannot be read is reported in a "File open error" message, and the next file opens. Files that opened
before stay open. Try one file first when you open a format you have not used before.

### Start with a fresh profile

While no recent file and no recent project are listed, Home shows a welcome where its rows will be. Its **Open an
image**, **Stack your frames** and **Read the guide** cards open files, open Image Integration and open this guide.
**New project...** starts a project, and the drop zone lights up while you drag files over the welcome. The welcome
goes with the first file you open, and comes back only when the lists are empty again.

```figure
image: ../assets/images/interface/home-first-start.webp
alt: >-
  Home on a new profile. Under the date, the heading Home and the row of quick actions, the AfterNight logo over the
  words Welcome to AfterNight, the line Astrophotography processing suite, a sentence and the version. Under Get
  started, three cards: Open an image, Stack your frames and Read the guide. A line says projects are optional, with a
  New project link. A dashed drop zone reads Drop images or sequences here, with the formats and an Open file button.
  At the foot, six small cards under What you can do.
caption: >-
  Home on the first start of a new profile, before any file has been opened.
owed: true
```

### Return to recent work

Click a tile, **Continue** on the featured image, or **Resume** on the last sequence, to open the file again. It moves
to the front of the list. The featured image follows the image opened last unless you pin one with the star; a
sequence is never featured, and the last one has its own card.

To find a file, type part of its name or path in the search box, which ++ctrl+f++ reaches, and choose **Recent**,
**Name** or **Type** in **Sort**. **Pin to top**, in a tile's right-click menu, keeps a file ahead of the others in
every order. The two icons at the right show the files as a grid or a list.

A recent file that can no longer be opened - moved, deleted or unreadable - leaves the list after its "File open
error". Open it again from its new place with **Open file**.

### Tidy the list

Choose **Remove from recent files** in a tile's menu to drop one file, or **More actions > Clear recents** to drop
them all after a question. Both remove the paths and the pictures AfterNight made, never your files. **Recent Files
Limit**, on the General page of [Settings](settings-dialog.md), sets how many files the list keeps.

### Arrange the quick actions

Open **More actions > Customize quick actions**, or right-click the row of quick actions, then the action to move or
hide. **Move earlier** and **Move later** change its place, and **Hide action** takes it off Home. **Show hidden
action** brings one back, and **Reset quick actions** restores the first order with nothing hidden. **Open file**,
**New sequence** and **Image integration** always stay in the row and the other three in the **More actions** menu,
whatever their order. **New project** stays at the end of the row.

### Open a project

A project is optional: a folder that keeps the [history](../reference/glossary.md#history), the workspaces and the
[integration jobs](../reference/glossary.md#integration-job) of a body of work. Once you have used one, the **Projects** row lists those used last and its **Open
project...** tile opens another. The **Last project** card of the Continue row reopens the last one.

## Controls

### Home

```controls
window: Home
controls:
  - name: Open file
    kind: button
    default: ""
    effect: Opens one or more image, raw, sequence or video files, each in a view of its own, as **File > Open** does.
    range: In the file window, choosing a file of an extension AfterNight does not open refuses the whole choice with "Unsupported File Type".
    enabled: Always. ++ctrl+o++ does the same, as its key hint says.
    tradeoff: Many large views at once cost memory and time. Open large files a few at a time.
    persistence: The folder is remembered for the next open, when the open starts and again when it ends, whatever becomes of it.
  - name: New sequence
    kind: button
    default: ""
    effect: Opens New Sequence, which makes a sequence of files. See Sequences.
  - name: Image integration
    kind: button
    default: ""
    effect: Opens Image Integration as a task, for calibration, registration, stacking and their report.
    note: The Quick Start uses Image Integration Lite instead, from the Integration place or **Process > Preprocess**.
  - name: New project
    kind: button
    default: ""
    effect: Makes a project, as **File > New Project...** does.
    note: It is no quick action of the customizing menu, so it cannot be moved or hidden.
  - name: More actions
    kind: button
    default: ""
    effect: Opens the menu of the other three quick actions and of **Customize quick actions**.
    note: The button itself cannot be moved or hidden.
  - name: Extension Manager
    kind: action
    default: ""
    effect: Opens the Extensions window on its Browse tab, to install, update and repair extensions.
    tradeoff: Installing, updating and repairing an extension use the network and disk space.
  - name: Clear recents
    kind: action
    default: ""
    effect: Asks "Are you sure you want to delete the recent opened files?" and, on **Yes**, empties the list, deletes the pictures AfterNight made and drops both kinds of pin.
    enabled: With at least one recent file.
    tradeoff: "**Yes** is the default answer, so ++enter++ clears. The question's \"delete\" means the list: your files stay on disk."
    persistence: The search, the sort and the quick actions stay as they were. **File > Open Recent > Clear recent files list** does the same.
  - name: System info
    kind: action
    default: ""
    effect: Opens System Information, the report of the application, its build and the computer.
    tradeoff: The report names details of your system. Read it before you share it.
  - name: Featured image
    kind: readout
    default: ""
    effect: Shows a recent image as a wide picture with its name and what its header states - the object and the night it was taken, then filter, exposure, camera and telescope - followed by its type, its size and when it was last changed. A file whose header states none of these shows only the last three.
    range: The night is the day of `DATE-OBS` or of the camera's original date, else the day the file says it was made, never the day it was written. The camera is the EXIF model or `INSTRUME`, and for a camera raw file the make and model its decoder states. Placeholders such as `-` or `N/A` are left out.
    enabled: Hidden while no image is listed or no listed image can be shown. Sequences are never featured. The search does not filter it.
    tradeoff: The facts are read in the background only while a thread is free, so they can be missing at first. Nothing Home reads is ever written back to the file.
    note: The pinned image comes first, then the images in the list's order. A file that no longer exists, or whose picture cannot be read, is passed over for the next.
  - name: Continue
    kind: button
    default: ""
    effect: Opens the featured image and moves it to the front of the list, as a click on its tile does.
  - name: Pin featured file
    kind: toolbutton
    default: "Off"
    effect: The star on the featured card. On, it keeps this image featured whatever you open next, and its tooltip reads **Unpin featured file**. Off, the featured image follows the most recent one.
    persistence: Kept between sessions as `interface/dashboardPinnedHeroFile`. Dropped when the file leaves the list or no longer exists.
    note: Separate from **Pin to top**. Neither pin sets the other.
  - name: Last project
    kind: readout
    default: ""
    effect: The project used last - its name, its object, the picture of the image opened in it last, its images, integrations and history steps, its workspaces, the room it takes and when it was opened - with **Open project**, which opens it.
    enabled: Shown once a project has been used, while none is open.
  - name: Last sequence
    kind: readout
    default: ""
    effect: The sequence opened last - its name, its picture, its type, frame size and number of frames, and when it was changed - with **Resume**, which opens it again.
    enabled: Shown while a sequence is among the recent files.
  - name: Projects
    kind: readout
    default: ""
    effect: Up to four projects used last, each a card with its name, its object, its folder, what it holds and when it was opened, then the **Open project...** tile. Click a card to open its project.
    range: A card whose folder is gone says "Its folder is not there". A project an integration run made carries the tag "from an integration job".
    enabled: Shown once a project has been used, while none is open.
  - name: Recent images and sequences
    kind: readout
    default: ""
    effect: Counts the images and the sequences of the list, as "2 images" or "1 image · 1 sequence". While a search is typed, the line beside it reads "Showing 1 of 2".
    enabled: Hidden while the list is empty.
    tradeoff: A picture of Home shows the names of your files and folders. Mind it before you share one.
    persistence: The list is the file `recent_files` in AfterNight's data folder, most recent first, up to the **Recent Files Limit** of Settings, which is 20 files unless you choose another number from 1 to 100 and is kept as `general/recentFilesLimit`. Applying a lower limit in Settings drops the oldest files and their pictures at once. A file comes first when it opens or is saved. A file on another computer - a Windows network path such as `\\server\share\...`, `//server/share/...` or `\\?\UNC\server\...`, or a file address that names a computer - is never listed, and its folder is not remembered.
    note: Paths are cleaned when the list is read, and a path listed twice is kept once.
  - name: Search by name, type, or path...
    kind: field
    default: ""
    effect: Shows only the files whose name or path holds what you type, in any case. Part of an extension, such as `fit`, finds files by type.
    range: Text only. Neither the file's contents nor its header are searched.
    enabled: Hidden while the list is empty. ++ctrl+f++ puts the cursor in it and selects its text.
    tradeoff: The tiles follow 120 ms after the last key.
    persistence: Kept between sessions as `interface/dashboardRecentSearchText`, so a search left typed is in force at the next start. The cross in the box empties it.
  - name: Sort
    kind: combo
    default: Recent
    options: [Recent, Name, Type]
    effect: Orders the tiles. **Recent** keeps the order of the list, **Name** sorts by file name, and **Type** groups by extension, then by name.
    range: Names are compared in your language's order, and the folder never decides between two equal names.
    enabled: Hidden while the list is empty.
    persistence: Kept between sessions as `interface/dashboardRecentSortMode`.
    note: Pinned files come first in every order.
  - name: Show as a grid
    kind: toolbutton
    default: "On"
    effect: Shows the files as tiles, as many to a row as fit, each with its picture above its name and facts.
    persistence: The choice of grid or list is kept between sessions as `interface/dashboardRecentArrangement`.
  - name: Show as a list
    kind: toolbutton
    default: "Off"
    effect: Shows the files one to a row, a small picture at the left and the type at the right.
  - name: Recent file tile
    kind: button
    default: ""
    effect: Opens the file and moves it to the front of the list. ++enter++ opens the tile that has the keyboard focus.
    range: The type on its picture - FITS, TIFF, JPEG, LIST for an image list, with a film mark for a sequence - then its name, and under it the frame size, the number of frames of a sequence and when the file was changed. Hover over the tile for its path. A pinned file has a dot at its picture's upper right.
    tradeoff: A file that fails to open, for any reason but a stop you asked for, leaves the list after "File open error". A stop leaves it.
    persistence: Opening from a tile remembers the file's folder for the next open once its view is made.
  - name: Picture
    kind: readout
    default: ""
    effect: The tile's picture of the image as it was when it last opened or was saved. It is made in the background, reduced to at most 2,400 pixels on its longer side with a Lanczos-4 kernel, and written as an 8-bit JPEG at quality 95, whole or not at all.
    range: A linear image is shown through the view's Autostretch, measured exactly over the reduced picture, each color channel on its own. An image counts as linear when its header records it so, when it is a stack that counts its frames (`STACKCNT` or `NCOMBINE`, as AfterNight's stacks write), a frame that states its type (`IMAGETYP`), or a camera raw frame. An image recorded as stretched, as every AfterNight stretch records it, or encoded as sRGB or with a gamma, is shown as it is. A mosaic shows the gray picture of its values.
    tradeoff: It changes only when the file opens or is saved in AfterNight again. A change another program makes to the file shows after that.
    persistence: Kept in AfterNight's data folder, its place recorded in its database, and removed with its entry. Of two pictures asked for one file, only the last is kept.
    note: Home keeps 256 pictures in memory and reads one again when its file changes. A file AfterNight has no picture of shows an empty tile.
  - name: No recent files match the search.
    kind: readout
    default: ""
    effect: Stands where the tiles were when the search matches no file.
    note: The featured image stays above it.
```

### A recent file's menu

```controls
window: A recent file's menu
controls:
  - name: Open
    kind: action
    default: ""
    effect: Opens the file, as a click on its tile does.
  - name: Pin to top
    kind: action
    default: ""
    effect: Keeps the file ahead of the files not pinned, in every order and search. On a pinned tile it reads **Unpin from top**.
    persistence: Kept between sessions as `interface/dashboardPinnedRecents`. Dropped when the file leaves the list.
    note: It does not make the file featured.
  - name: Show in File Browser
    kind: action
    default: ""
    effect: Opens your file manager at the file.
    range: A file that is gone gives "File Not Found" with "The file does not exist on disk:" and its path.
  - name: Copy path
    kind: action
    default: ""
    effect: Copies the file's full path to the clipboard.
    tradeoff: A path can name private folders. Look at it before you paste it anywhere public.
  - name: Remove from recent files
    kind: action
    default: ""
    effect: Takes the file off the list and off **File > Open Recent**, with its picture and its pin.
    note: The file stays where it is.
```

### Customize quick actions

```controls
window: Customize quick actions
controls:
  - name: Move earlier
    kind: action
    default: ""
    effect: Swaps the action with the one before it in the order of the six.
    enabled: Not for the first action.
    tradeoff: A swap across the two groups changes the order and nothing on the screen, since the three start actions always stay in the row and the other three in the menu.
    persistence: Kept between sessions, as every choice of this menu is.
  - name: Move later
    kind: action
    default: ""
    effect: Swaps the action with the one after it.
    enabled: Not for the last action.
    tradeoff: The same as **Move earlier** across the two groups.
  - name: Hide action
    kind: action
    default: ""
    effect: Takes the action off Home. On a hidden action it reads **Show action** and brings it back.
    enabled: Not for the last action still shown. One stays visible.
    note: A hidden action stays where the menus offer it, such as **File > Open**.
  - name: Show hidden action
    kind: action
    default: ""
    effect: Lists the hidden actions. Choose one to show it again in its place.
    enabled: While an action is hidden.
  - name: Reset quick actions
    kind: action
    default: ""
    effect: Restores **Open file**, **New sequence** and **Image integration**, then **Extension Manager**, **Clear recents** and **System info**, with nothing hidden.
    note: Home's settings written in another format, by another version of AfterNight, are set aside at start, and Home starts from these defaults. Home also records the last action you chose, as `interface/dashboardLastAction`, which nothing shows.
```

### The welcome

```controls
window: The welcome
controls:
  - name: Open an image
    kind: button
    default: ""
    effect: The same as **Open file**.
    note: Its line names FITS, XISF, TIFF and RAW. Supported Formats lists them all.
  - name: Stack your frames
    kind: button
    default: ""
    effect: The same as **Image integration**, the full Image Integration and not Lite.
  - name: Read the guide
    kind: button
    default: ""
    effect: Opens this guide, as **User Guide** does.
  - name: New project...
    kind: button
    default: ""
    effect: The same as **New project**. The line before it says that projects are optional.
  - name: Drop images or sequences here
    kind: readout
    default: ""
    effect: Lights up while you drag local files over the welcome. Files dropped anywhere on the window open as with **Open file**.
    range: A file from another computer, or of an extension AfterNight does not open, is left out without a message, and the others open. The extension decides; a file whose contents cannot be read still fails at the open.
    persistence: The folder of the last file dropped is remembered for the next open.
    note: Its line names FITS, XISF, TIFF, RAW, JPEG, PNG, SER and video. Its **Open file...** button is the same as **Open file**.
  - name: What you can do
    kind: readout
    default: ""
    effect: Six cards that say what AfterNight does - Multi-Format Support, Frame Calibration, Star Alignment, Image Stacking, Background Extraction and Enhancement.
    enabled: The whole welcome shows only while no recent file and no recent project are listed. A search that matches nothing does not bring it back.
    note: They are no buttons, and do not say that every file suits every process.
```

### File Open

```controls
window: File Open
controls:
  - name: Progress
    kind: readout
    default: ""
    effect: Names the file being read. One or two files show a moving bar, three or more a percentage.
    range: Reopening a recent file always shows a moving bar.
    note: A file that cannot be read is reported and the open goes on. A file whose view cannot be made is reported and stops the files after it.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the read, the sequence open or the demosaic in progress, and reads no further file.
    tradeoff: Files already open stay open and in the list.
  - name: "Open CFA file: Yes"
    kind: button
    default: ""
    effect: Demosaics the mosaic with the **Demosaic Algorithm** of **Settings > Raw Images** before its view opens.
  - name: "Open CFA file: No"
    kind: button
    default: ""
    effect: Opens the mosaic as it was recorded. The Debayer process can demosaic it later.
```

## When it goes wrong

"No recent files match the search." just after AfterNight starts
:   **Cause.** A search typed in an earlier session is still in the search box, and it matches no file now.
:   **Fix.** Empty the box with the cross at its right end.

A tile shows no picture
:   **Cause.** AfterNight has no picture of the file: it was never written, or it was removed.
:   **Fix.** Open the file once with **Open file** to make a new picture, or remove the entry.

A tile disappears after "File open error"
:   **Cause.** The file was moved, deleted, or could not be read or shown, and a recent file that fails to open leaves
    the list.
:   **Fix.** Find the file and open it with **Open file** from where it is now.

A dropped file is ignored
:   **Cause.** It is not a local file, or its extension is not one AfterNight opens.
:   **Fix.** Drop a local file in a format of [Supported Formats](../getting-started/supported-formats.md), or use
    **Open file**.

The Continue row has no featured image
:   **Cause.** No image is among the recent files - only sequences - or no listed image can be shown.
:   **Fix.** Open an image; the last sequence keeps its own card with **Resume**.

Moving a quick action seems to do nothing
:   **Cause.** It swapped places with an action of the other group, and each group keeps its place.
:   **Fix.** Move it again until it passes an action of its own group, or choose **Reset quick actions**.

## Related

- [The Main Window](overview.md), for the regions around Home and the places the rail switches to.
- [Projects](projects.md), for what a project keeps and the page Home becomes while one is open.
- [Sequences](sequences.md), for New Sequence and the sequences it makes.
- [Image Integration](../processes/stacking-integration/image-integration.md), the [task](../reference/glossary.md#task) **Image integration** opens; the
  [Quick Start](../getting-started/quick-start.md) uses Image Integration Lite instead for a first stack.
- [Supported Formats](../getting-started/supported-formats.md), for the files you can open.
- [Settings](settings-dialog.md), for **Recent Files Limit** and the Raw Images settings.
