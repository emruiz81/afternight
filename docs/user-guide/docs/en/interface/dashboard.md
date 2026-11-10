---
kind: interface
summary: >-
  The Dashboard fills the main window when AfterNight starts and whenever no image is open: open files, start a
  sequence or an integration, and return to recent work.
rewritten: true
---
# Dashboard

The Dashboard is the first thing AfterNight shows, and it comes back when the last image view and the Session
Console close. From it you open images and [sequences](../reference/glossary.md#sequence), start a new sequence or an image integration, and reopen the
files you worked on, each with a thumbnail. What it keeps is a list of paths, thumbnails and choices: taking a file
off the list never touches the file.

```figure
image: ../assets/images/interface/dashboard.webp
alt: >-
  The Dashboard of a profile with recent files. At the top, the AfterNight icon, name and version, and three round
  buttons on the right. Under them, a wide card shows a picture of the Carina Nebula behind its file name, a line of
  facts from its header, a Continue button and a star. Then a row of three blue buttons, Open file, New sequence and
  Image integration, with a button of three dots at the far right. Below, the Recent heading with its count, a search
  box and a box reading Recent, a line of help, and a grid of cards, each a thumbnail of an astronomical image with its
  size, type, file name and a short line of facts.
caption: >-
  The Dashboard with seven recent files, the linear Carina master featured at the top.
pins:
  - {n: 1, x: 20.0, y: 13.0}
  - {n: 2, x: 8.0, y: 24.3}
  - {n: 3, x: 25.0, y: 36.9}
  - {n: 4, x: 92.0, y: 36.9}
  - {n: 5, x: 6.5, y: 41.7}
  - {n: 6, x: 4.0, y: 57.0}
```

## What you see

1. **The identity bar.** The application's name and version. Its three buttons open **Settings**, the **User Guide**
   and **About**.
2. **The featured file.** The most recent file, or the one you pinned, as a wide picture with what its header says
   and a **Continue** button that opens it.
3. **The quick actions.** **Open file**, **New sequence** and **Image integration**: the three ways to start work.
4. **More actions.** The **···** button. It holds **Extension Manager**, **Clear recents** and **System info**, and the
   menu that arranges the quick actions.
5. **Recent.** The count of recent files, a search box, a sort box, and a line that says how to use the cards.
6. **The recent files.** One card for each file: its thumbnail, its size and type, its name, and the target, filter,
   exposure and camera its header states. Click a card to open the file, and right-click it for more.

## What you can do

### Open files

Choose **Open file** or press ++ctrl+o++, select one or more files and confirm. You can also drop files from your
file manager anywhere on the Dashboard. A **File Open** window follows the reading. Each file that opens gets a view
of its own, and the workspace replaces the Dashboard.

A camera raw file, or a file whose header states a [CFA](../reference/glossary.md#cfa) mosaic, asks "Open CFA file"
first. **Yes** demosaics it with the **Demosaic Algorithm** of the Raw Images settings. **No** opens the mosaic as it
was recorded, to [debayer](../reference/glossary.md#debayer) later with the Debayer process.

A file that cannot be read is reported in a "File open error" message, and the next file opens. Files that opened
before stay open. Try one file first when you open a format you have not used before.

### Start with a fresh profile

On a first start, or after **Clear recents**, a welcome panel stands where the recent files will be. Its **Open
Image**, **New Sequence** and **Image Integration** cards do what the quick actions do, and its drop zone lights up
while you drag files over the Dashboard. The panel goes with the first file you open, and comes back only when the list
is empty again.

```figure
image: ../assets/images/interface/dashboard-first-start.webp
alt: >-
  The Dashboard of a new profile. Under the identity bar and the row of quick actions, a large panel reads Welcome to
  AfterNight, with six small cards under What you can do, three larger cards under Get started - Open Image, New
  Sequence and Image Integration - a dashed drop zone reading Drop your images here to get started, and a line of
  keyboard hints at the bottom.
caption: >-
  The Dashboard on a first start, before any file has been opened.
```

### Return to recent work

Click a card, or **Continue** on the featured file, to open the file again. It moves to the front of the list. The
featured file follows the most recent one unless you pin a file with the featured card's star.

To find a file, type part of its name or path in **Search by name, type, or path...**, which ++ctrl+f++ reaches, and
choose **Recent**, **Name** or **Type** in the sort box. **Pin to top**, in a card's right-click menu, keeps a file
ahead of the others in every order.

A recent file that can no longer be opened - moved, deleted or unreadable - leaves the list after its "File open
error". Open it again from its new place with **Open file**.

### Tidy the list

Choose **Remove from recent files** in a card's menu to drop one file, or **More actions > Clear recents** to drop
them all after a question. Both remove the paths and the thumbnails AfterNight made, never your files. **Recent Files
Limit**, on the General page of [Settings](settings-dialog.md), sets how many files the list keeps.

### Arrange the quick actions

Open **More actions > Customize quick actions**, then the action to move or hide. **Move earlier** and **Move later**
change its place, and **Hide action** takes it off the Dashboard. **Show hidden action** brings one back, and **Reset
quick actions** restores the first order with nothing hidden. **Open file**, **New sequence** and **Image
integration** always stay in the row and the other three in the **···** menu, whatever their order.

## Controls

### The Dashboard

```controls
window: The Dashboard
controls:
  - name: Settings
    kind: button
    default: ""
    effect: Opens Settings.
    enabled: Always. ++ctrl+comma++ does the same.
    note: Settings, User Guide and About are not quick actions, so they cannot be moved or hidden.
  - name: User Guide
    kind: button
    default: ""
    effect: Opens this guide from the copy installed with AfterNight, or online when that copy cannot be opened and the online guide is allowed.
    enabled: Always. ++f1++ does the same.
  - name: About
    kind: button
    default: ""
    effect: Opens About AfterNight.
  - name: Featured file
    kind: readout
    default: ""
    effect: Shows a recent file as a wide picture with its name and what its header states - the object and the night it was taken, then filter, exposure, camera and telescope - followed by its type, its size and when it was last changed. A file whose header states none of these shows only the last three.
    range: The night is the day of `DATE-OBS` or of the camera's original date, else the day the file says it was made, never the day it was written. The camera is the EXIF model or `INSTRUME`, and for a camera raw file the make and model its decoder states. Placeholders such as `-` or `N/A` are left out.
    enabled: Hidden while the list is empty or no listed file can be shown. The search does not filter it.
    tradeoff: The facts are read in the background only while a thread is free, so they can be missing at first. Nothing the Dashboard reads is ever written back to the file.
    note: The pinned file comes first, then the files in the list's order. A file that no longer exists, or whose picture cannot be read, is passed over for the next.
  - name: Continue
    kind: button
    default: ""
    effect: Opens the featured file and moves it to the front of the list, as a click on its card does.
  - name: Pin featured file
    kind: toolbutton
    default: "Off"
    effect: The star on the featured card. On, it keeps this file featured whatever you open next, and its tooltip reads **Unpin featured file**. Off, the featured file follows the most recent one.
    persistence: Kept between sessions as `interface/dashboardPinnedHeroFile`. Dropped when the file leaves the list or no longer exists.
    note: Separate from **Pin to top**. Neither pin sets the other.
  - name: Open file
    kind: button
    default: ""
    effect: Opens one or more image, raw, sequence or video files, each in a view of its own, as **File > Open** does.
    range: In the file window, choosing a file of an extension AfterNight does not open refuses the whole choice with "Unsupported File Type".
    enabled: Always. ++ctrl+o++ does the same.
    tradeoff: Many large views at once cost memory and time. Open large files a few at a time.
    persistence: The folder is remembered for the next open, when the open starts and again when it ends, whatever becomes of it.
  - name: New sequence
    kind: button
    default: ""
    effect: Opens New Sequence, which makes a sequence of files. See Sequences.
  - name: Image integration
    kind: button
    default: ""
    effect: Opens the full Image Integration window, for calibration, registration, stacking and their report.
    note: The Quick Start uses Image Integration Lite instead, from **Process > Preprocess**.
  - name: More actions
    kind: button
    default: ""
    effect: Opens the menu of the other three quick actions and of **Customize quick actions**.
    note: The button itself cannot be moved or hidden.
  - name: Extension Manager
    kind: action
    default: ""
    effect: Opens the Extensions window on its **Browse** tab, to install, update and repair extensions.
    tradeoff: Installing, updating and repairing an extension use the network and disk space.
  - name: Clear recents
    kind: action
    default: ""
    effect: Asks "Are you sure you want to delete the recent opened files?" and, on **Yes**, empties the list, deletes the thumbnails AfterNight made and drops both kinds of pin.
    enabled: With at least one recent file.
    tradeoff: "**Yes** is the default answer, so ++enter++ clears. The question's \"delete\" means the list: your files stay on disk."
    persistence: The search, the sort and the quick actions stay as they were. **File > Open Recent > Clear recent files list** does the same.
  - name: System info
    kind: action
    default: ""
    effect: Opens System Information, the report of the application, its build and the computer.
    tradeoff: The report names details of your system. Read it before you share it.
  - name: Recent
    kind: readout
    default: ""
    effect: Counts the recent files. While a search is typed it reads "Showing 3 of 8", and "No matches in 8 files" when nothing matches.
    range: It counts the paths in the list, a file whose card cannot be shown among them.
    enabled: Hidden while the list is empty.
    tradeoff: A picture of the Dashboard shows the names of your files and folders. Mind it before you share one.
    persistence: The list is the file `recent_files` in AfterNight's data folder, most recent first, up to the **Recent Files Limit** of Settings, which is 20 files unless you choose another number from 1 to 100 and is kept as `general/recentFilesLimit`. Applying a lower limit in Settings drops the oldest files and their thumbnails at once. A file comes first when it opens or is saved. A file on another computer - a Windows network path such as `\\server\share\...`, `//server/share/...` or `\\?\UNC\server\...`, or a file address that names a computer - is never listed, and its folder is not remembered.
    note: Paths are cleaned when the list is read, and a path listed twice is kept once.
  - name: Search by name, type, or path...
    kind: field
    default: ""
    effect: Shows only the files whose name or path holds what you type, in any case. Part of an extension, such as `fit`, finds files by type.
    range: Text only. Neither the file's contents nor its header are searched.
    enabled: Hidden while the list is empty. ++ctrl+f++ puts the cursor in it and selects its text.
    tradeoff: The cards follow 120 ms after the last key.
    persistence: Kept between sessions as `interface/dashboardRecentSearchText`, so a search left typed is in force at the next start. The cross in the box empties it.
  - name: Sort recent files
    kind: combo
    default: Recent
    options: [Recent, Name, Type]
    effect: Orders the cards. **Recent** keeps the order of the list, **Name** sorts by file name, and **Type** groups by extension, then by name.
    range: Names are compared in your language's order, and the folder never decides between two equal names.
    enabled: Hidden while the list is empty.
    persistence: Kept between sessions as `interface/dashboardRecentSortMode`.
    note: Pinned files come first in every order.
  - name: Recent file card
    kind: button
    default: ""
    effect: Opens the file and moves it to the front of the list. The featured card shows it at once.
    range: On the picture, the image's size and type. Under the name, the target, filter, exposure and camera a FITS, XISF, TIFF or raw header states, or else the folder's name. Hover over the card for its path, type, size, file size and date.
    enabled: A card shows only for a file whose thumbnail AfterNight can read.
    tradeoff: A file that fails to open, for any reason but a stop you asked for, leaves the list after "File open error". A stop leaves it.
    persistence: Opening from a card remembers the file's folder for the next open once its view is made.
  - name: Thumbnail
    kind: readout
    default: ""
    effect: The card's picture of the image as it was when it last opened or was saved. It is made in the background, reduced to at most 2,400 pixels on its longer side with a Lanczos-4 kernel, and written as an 8-bit JPEG at quality 95, whole or not at all.
    range: A linear image is shown through the view's Autostretch, measured exactly over the reduced picture, each color channel on its own. An image counts as linear when its header records it so, when it is a stack that counts its frames (`STACKCNT` or `NCOMBINE`, as AfterNight's stacks write), a frame that states its type (`IMAGETYP`), or a camera raw frame. An image recorded as stretched, as every AfterNight stretch records it, or encoded as sRGB or with a gamma, is shown as it is. A mosaic shows the gray picture of its values.
    tradeoff: It changes only when the file opens or is saved in AfterNight again. A change another program makes to the file shows after that.
    persistence: Kept in AfterNight's data folder, its place recorded in its database, and removed with its entry. Of two thumbnails asked for one file, only the last is kept.
    note: Up to four are read at a time. The Dashboard keeps 256 pictures in memory and reads one again when its thumbnail file changes.
  - name: No matches found
    kind: readout
    default: ""
    effect: Stands where the cards were when the search matches no file, with "Try a different search term to see your recent files."
    note: The featured file stays above it.
```

### A recent file's menu

```controls
window: A recent file's menu
controls:
  - name: Open
    kind: action
    default: ""
    effect: Opens the file, as a click on its card does.
  - name: Pin to top
    kind: action
    default: ""
    effect: Keeps the file ahead of the files not pinned, in every order and search. On a pinned card it reads **Unpin from top**, and a pin mark stands in the card's corner.
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
    effect: Takes the file off the list and off **File > Open Recent**, with its thumbnail and its pin.
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
    effect: Takes the action off the Dashboard. On a hidden action it reads **Show action** and brings it back.
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
    note: Dashboard settings written in another format, by another version of AfterNight, are set aside at start, and the Dashboard starts from these defaults. The Dashboard also records the last action you chose, as `interface/dashboardLastAction`, which nothing shows.
```

### The welcome panel

```controls
window: The welcome panel
controls:
  - name: What you can do
    kind: readout
    default: ""
    effect: Six cards that say what AfterNight does - Multi-Format Support, Frame Calibration, Star Alignment, Image Stacking, Background Extraction and Enhancement.
    range: Three columns, two on a narrower Dashboard, one on a narrow one.
    enabled: The whole panel shows only while the list is empty. With recent files at start it never shows, not even for a moment, and a search that matches nothing does not bring it back.
    note: They are no buttons, and do not say that every file suits every process.
  - name: Open Image
    kind: button
    default: ""
    effect: The same as **Open file**.
    note: Its line names FITS, XISF, TIFF and RAW. Supported Formats lists them all.
  - name: New Sequence
    kind: button
    default: ""
    effect: The same as **New sequence**.
  - name: Image Integration
    kind: button
    default: ""
    effect: The same as **Image integration**, the full window and not Lite.
  - name: Drop your images here to get started
    kind: readout
    default: ""
    effect: Lights up while you drag local files over the Dashboard. Files dropped anywhere on the Dashboard open as with **Open file**.
    range: A file from another computer, or of an extension AfterNight does not open, is left out without a message, and the others open. The extension decides; a file whose contents cannot be read still fails at the open.
    persistence: The folder of the last file dropped is remembered for the next open.
    note: Its line names FITS, XISF, TIFF, RAW, JPEG and PNG. The highlight shows only with the panel, and a drop works whatever the Dashboard shows.
  - name: Keyboard hints
    kind: readout
    default: Ctrl+O to open · Ctrl+N for new sequence · F1 for help
    effect: ++ctrl+o++ opens files and ++f1++ the User Guide.
    tradeoff: ++ctrl+n++ is no shortcut of New Sequence and does nothing.
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

"No matches found" just after AfterNight starts
:   **Cause.** A search typed in an earlier session is still in the search box, and it matches no file now.
:   **Fix.** Empty the box with the cross at its right end.

The Recent count is higher than the number of cards
:   **Cause.** A listed file has no thumbnail AfterNight can read: it was never written, or it was removed.
:   **Fix.** Open the file once with **Open file** to make a new thumbnail, or remove the entry.

A card disappears after "File open error"
:   **Cause.** The file was moved, deleted, or could not be read or shown, and a recent file that fails to open
    leaves the list.
:   **Fix.** Find the file and open it with **Open file** from where it is now.

A dropped file is ignored
:   **Cause.** It is not a local file, or its extension is not one AfterNight opens.
:   **Fix.** Drop a local file in a format of [Supported Formats](../getting-started/supported-formats.md), or use
    **Open file**.

++ctrl+n++ does nothing
:   **Cause.** The welcome panel's hint names a shortcut that New Sequence does not have.
:   **Fix.** Choose **New sequence**, its card, or **File > New Sequence...**.

Moving a quick action seems to do nothing
:   **Cause.** It swapped places with an action of the other group, and each group keeps its place.
:   **Fix.** Move it again until it passes an action of its own group, or choose **Reset quick actions**.

## Related

- [Interface Overview](overview.md), for the main window around the Dashboard and the workspace that replaces it.
- [Image Viewer](image-viewer.md), for the view a file opens in.
- [Sequences](sequences.md), for New Sequence and the sequences it makes.
- [Image Integration](../processes/stacking-integration/image-integration.md), the window the Dashboard opens; the
  [Quick Start](../getting-started/quick-start.md) uses Image Integration Lite instead for a first stack.
- [Supported Formats](../getting-started/supported-formats.md), for the files you can open.
- [Settings](settings-dialog.md), for **Recent Files Limit** and the Raw Images settings.
