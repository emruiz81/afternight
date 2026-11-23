---
kind: tutorial
summary: >-
  Start AfterNight for the first time, find your way around its main window, open one image, read it, open a first
  task on it, and come back to Home with the image among your recent files.
needs: []
---
# First Launch

You start with AfterNight installed and never started, and one image file of your own. You end with that image opened
in a [workspace](../reference/glossary.md#workspace) and read by the [readout](../reference/glossary.md#readout), a
first [task](../reference/glossary.md#task) opened on it and closed again, and the image listed on Home, the place
AfterNight starts on. Opening a file only reads it: your file stays as it is.

## What you need

- [ ] AfterNight installed, as [Installation](installation.md) describes
- [ ] One image file AfterNight opens, such as a [FITS](../reference/glossary.md#fits),
  [XISF](../reference/glossary.md#xisf), TIFF, PNG or JPEG file, or a camera's [RAW](../reference/glossary.md#raw)
  file
- [ ] A small file for this first time, on a disk of this computer rather than a network or cloud folder

[Supported Formats](supported-formats.md) lists every kind of file AfterNight opens.

## Steps

```figure
image: ../assets/images/getting-started/first-launch.webp
alt: >-
  The AfterNight main window on Home, the first time it starts. Along the top, the top bar: the AfterNight mark, a
  folder reading No project, the word Home, a wide field reading Search or run a command with the keys Ctrl K, and at
  the right a moon, a question mark, an i and three dots. Down the left edge, a narrow rail of three icons - a house,
  which is selected, a picture frame and a stack of layers - with a terminal and a gear at its foot. In the centre, the
  day's date over the heading Home, and at the right the buttons Open file with Ctrl O, New sequence, Image
  integration, New project and three dots. Below, the AfterNight logo over Welcome to AfterNight and a version, three
  cards under Get started - Open an image, Stack your frames and Read the guide - a line about projects with a New
  project link, a dashed box reading Drop images or sequences here, and six small cards under What you can do. Along
  the bottom, the status line with RA, Dec and a Console button.
caption: >-
  The main window on Home, as AfterNight shows it on its first start, before any file was opened.
pins:
  - {n: 2, x: 1.6, y: 11.1}
  - {n: 3, x: 1.6, y: 97.1}
  - {n: 4, x: 52.0, y: 14.4}
  - {n: 10, x: 92.6, y: 5.8}
owed: true
```

### Start AfterNight

On Windows, choose **AfterNight** in the Start menu. On Linux, start the AppImage. The first time, the window fills
the screen and opens on Home. No welcome dialog opens and nothing asks you a question.

Starting AfterNight while it is already open brings the open window to the front. It does not open a second one.

!!! see "You should see"
    Home, in the dark theme, with a large welcome that reads "Welcome to AfterNight".

??? wrong "If it goes wrong: Home shows pictures of files instead of the welcome"
    Your user profile already holds recent files or [projects](../reference/glossary.md#project), from an earlier
    installation. Home shows the welcome
    only while both lists are empty. Follow the same steps with the **Open file** button.

### Find your way around the window

The main window has five parts, which [The Main Window](../interface/overview.md) describes:

- **The [rail](../reference/glossary.md#rail)**, the column of icons at the left. **Home** is selected. Under it,
  **Workspace 1** holds the images you open, and **Integration** gathers the ways to stack frames. At its foot,
  **Session Console** and **Settings**.
- **The top bar.** **No project** says that no [project](../interface/projects.md) is open, and **Home** where you
  are. In its middle, **Search or run a command** opens the [command palette](../interface/command-palette.md), which
  finds any command by name. At its right, **Night**, **User Guide**, **Information** and **All menus**.
- **The centre**, here Home. Its quick actions are **Open file**, **New sequence**, **Image integration** and **New
  project**; the button with three dots holds **Extension Manager**, **Clear recents** and **System info**.
- **The status line** at the bottom, and its **Console** button.

The menu bar is hidden. Press and release ++alt++ to show it, or open **All menus**: both hold **File**, **Edit**,
**View**, **Image**, **Process** and the other menus. This guide writes a menu path as **File > Open**.

!!! see "You should see"
    The version under "Welcome to AfterNight", the same as in the name of the file you installed. **Information >
    About AfterNight** names it too.

### Look at the settings that decide how a file opens

Choose the gear at the foot of the rail, **Edit > Settings...**, or press ++ctrl+comma++. Two pages of
[Settings](../interface/settings-dialog.md) decide how your first image opens:

- **Raw Images** sets how the raw file of a camera is read. A new profile reads it with the **Daylight** white
  balance, and turns it by the orientation the camera recorded in its [EXIF](../reference/glossary.md#exif) record,
  as **Auto-rotate based on EXIF orientation** asks. Its **Demosaic Algorithm**, RCD on a new profile, turns any
  color-filter mosaic you open into color when you ask for it in step 5.
- **General** shows under **Temporary Files** the folder for the intermediate files of each session: an
  `AfterNight` folder in the system's temporary folder. Until Settings stores it, with **OK** or **Apply**,
  AfterNight uses a `temp` folder in its own data folder instead. This page also holds **Check for extension updates
  on startup**, which is on. On a first start that check is the only thing AfterNight does on the network by itself;
  once extensions are installed, preparing their environments at startup may reach a package index too. Turn it off
  if this computer must stay offline.

The other pages, such as **Projects**, **Interface** and **External Tools**, are for work that opening an image does
not need. Choose **Cancel** to leave without a change, or **OK** to store what the pages show.

!!! see "You should see"
    The Settings dialog closes, and Home is as it was.

??? wrong "If it goes wrong: "The temporary files directory is not writable: …""
    The folder named under **Temporary Files** cannot be created or written to, and Settings keeps the dialog open.
    Choose a folder on a disk you can write to, with room for large files, and choose **OK** again.

### Open one image

Choose **Open file**, or the **Open an image** card, or press ++ctrl+o++. The **Open File** dialog opens in your
Pictures folder the first time, and later in the folder you last opened from. Its filter, **All supported formats**,
shows every file AfterNight opens, [sequences](../reference/glossary.md#sequence) and videos among them. Select your image and choose **Open**.

You can also drag files from your file manager and drop them anywhere on the window. AfterNight opens the dropped
files of a kind it opens and ignores the others.

!!! see "You should see"
    The dialog closes and a small window titled **File Open** appears.

??? wrong "If it goes wrong: "Unsupported File Type""
    The box reads "AfterNight can only open supported image, RAW, and video files." One of the files you selected
    is of a kind AfterNight does not open, and the whole selection is refused. Select only files the filter shows.

### Answer the question about the color filter array

This step happens only when the image is a mosaic of a color filter array, or
[CFA](../reference/glossary.md#cfa): the raw file of a color camera, or a file of another format that states a filter
pattern. AfterNight then asks whether to [debayer](../reference/glossary.md#debayer) it. A box titled **Open CFA
file** asks "The file appears to have a bayer matrix. Do you want to automatically debayer it before opening? (If no,
you can always debayer it by using the Debayer process)".

- **Yes** turns the mosaic into a color image, with the **Demosaic Algorithm** of the **Raw Images** settings. Choose
  it to look at the picture.
- **No** opens the mosaic as it was recorded, as one gray plane. Choose it for a frame you will calibrate, since
  calibration works on the mosaic. The [Debayer](../processes/calibration/debayer.md) process turns it into color
  later, with a method you choose.

!!! see "You should see"
    The box closes and the **File Open** window goes on.

### Wait for the image to open

The **File Open** window reads "Opening file '…'..." with the path of your file, and has a **Cancel** button. Several
files open one after the other, each in a view of its own. For one or two files the bar only moves, and from three
files on it counts the share done. When the image is read, the centre shows **Workspace 1**, with your image in its
view.

!!! see "You should see"
    A tab named after your file, without its extension, the image under it, and the top bar reading **Workspace 1**
    and the image's name. The status line gives the image's size and channels, its sample type, the memory it takes
    and the zoom.

??? wrong "If it goes wrong: a box titled "File open error""
    It reads "Could NOT load file '…'." and gives the reason. A file can carry a name AfterNight knows and still hold
    something it does not read, or be damaged. Read the reason, look the format up in
    [Supported Formats](supported-formats.md), and try another file.

### Look at the image

The view opens in the **Linear** display mode, which shows the samples as they are. A
[linear image](../reference/glossary.md#linear-image), such as a stacked [master](../reference/glossary.md#master) or
a raw frame, then looks nearly black. Choose [**Autostretch**](../reference/glossary.md#autostretch) beside **Linear**
in the floating toolbar near the foot of the view. This changes the display only, not the image: the word AUTOSTRETCH
in the corner of the view reminds you of it.

```figure
image: ../assets/images/getting-started/first-launch-image.webp
alt: >-
  The AfterNight main window with one image open in Workspace 1. The top bar reads No project, Workspace 1 and RGB, and
  holds four layout icons beside the moon and the other buttons. In the centre, a tab named RGB over the red and white
  clouds of the Carina Nebula across a field of stars, with the word AUTOSTRETCH in its top left corner. Near the foot
  of the picture, a floating toolbar of zoom buttons, the zoom in percent, a box reading RGB, the segments Linear and
  Autostretch with Autostretch selected, a gear, a star and three dots. Under the picture, the history strip with
  Initial state. At the right, the Readout column with its Histogram, Statistics, Astrometry and Image cards. Along the
  bottom, the status line with a box reading RGB, the image's size, type and memory, and the zoom.
caption: >-
  A linear master of the Carina Nebula just opened, shown in the Autostretch display mode, with the readout beside it.
pins:
  - {n: 7, x: 41.5, y: 89.6}
  - {n: 8, x: 75.0, y: 10.6}
owed: true
```

[Image Viewer](../interface/image-viewer.md) shows how to zoom, pan and inspect the image.

!!! see "You should see"
    Your image in its view, with stars and structure visible.

### Read the readout

The readout, the column at the right of the workspace, reads the image you look at
in four cards. **Histogram** shows how its samples spread and how much is clipped, **Statistics** their minimum,
maximum, mean, median, spread and [MAD](../reference/glossary.md#mad), **Astrometry** the
[plate solution](../reference/glossary.md#plate-solution) the file carries, if any, and **Image** its size and format,
with buttons to its header and its summary. [Readout](../interface/readout.md) describes each card.

!!! see "You should see"
    The name of your image after READOUT, and the cards filled in a moment after the image opened. "No astrometric
    solution" in **Astrometry** only means that the file carries none.

### Open a first task

Press ++ctrl+k++, type `histogram` and choose **Histogram Transformation** with ++enter++, or choose **Process >
Intensity Transformations > Histogram Transformation**. A process opens as a task: a page of the centre with an item of its own in the rail. Its canvas at the left
shows a preview of the result, and its column at the right holds the process's controls, with **Reset**, **Close** and
**Apply** at its foot. Nothing changes the image until you choose **Apply**. [Tasks](../interface/tasks.md) says what
every task shares.

Leave it without applying: choose **Close**, or press ++esc++.

!!! see "You should see"
    While it is open, the task header reads "Histogram Transformation on" and your image's name, and the rail holds the
    task's item under **Integration**. Once it is closed, the item is gone and the workspace shows your image as it was.

??? wrong "If it goes wrong: a message says the process cannot open"
    No image was active, or the process does not suit it: the message says which. Click your image's view, then open
    the task again.

### Open the user guide

Press ++f1++, or choose **User Guide** in the top bar, the question mark, or **Help > User Guide**. On a task's page,
++f1++ opens the guide at that process's page. The guide opens in your web browser, from the copy installed with
AfterNight.

!!! see "You should see"
    This guide in your browser.

### Close the image

Choose **File > Close**, press ++ctrl+w++, or choose the cross on the view's tab. When the last image closes,
AfterNight returns to Home. The welcome has given way to a **Continue** row, which features your image, and to
**Recent images and sequences**, which shows it as a tile with its name. A click on it opens the image again.

!!! see "You should see"
    Home, with your image under **Recent images and sequences**.

## The result

You have opened an image, read it, opened a task on it and closed both, and AfterNight is ready for the
[Quick Start](quick-start.md).

- **Your file is as it was.** Opening reads a file into memory and never writes it. A raw file is turned into an
  image by the **Raw Images** settings in force when you opened it, and its bytes stay the same.
- **Home remembers it.** Your image is under **Recent images and sequences**, and in **File > Open Recent**, among the
  files you opened last, as many as **Recent Files Limit** in Settings keeps. Only a file that opened is added. From
  now on Home opens with your recent files instead of the welcome. [Home](../interface/home.md) describes them.
- **Without a project, nothing is kept of the work.** The image's [history](../reference/glossary.md#history) lasts
  while its view is open, and AfterNight reopens no image at the next start. A project keeps both: see
  [Projects](../interface/projects.md).
- **AfterNight has a folder of its own** in your user profile: `%LOCALAPPDATA%\AfterNight` on Windows,
  `~/.local/share/AfterNight` on Linux. It holds the settings, the list of recent files and their pictures, and the
  logs in `logs`.
- **Each session has a folder for its intermediate files,** under `temp` in that folder, or in the **Temporary
  Files** location once Settings has stored one. With **Clear temporary files on exit** on, as it is on a new
  profile, the folder goes when AfterNight closes. The next start then removes the folders of sessions that have
  ended, and never the folder of one still running.
- **The window keeps its size.** When you close AfterNight, it remembers the size and place of its window, and the
  next start opens it the same way, as **Remember window size and position** on the Interface page of Settings asks.

**New sequence** opens a dialog of its own, and **Image integration** opens a task. They write files only once you
start what they prepare.

## Next

- [Quick Start](quick-start.md) takes the frames of one night to a stacked master.
- [The Main Window](../interface/overview.md) names every part of the window and what to do first.
- [Home](../interface/home.md) describes the recent files, their search and the quick actions.
- [Image Viewer](../interface/image-viewer.md) shows how to inspect an image and what Autostretch does.
- [Coming from 1.1](coming-from-1-1.md) says where each part of AfterNight 1.1's window went.
- [Settings](../interface/settings-dialog.md) describes every page of the Settings dialog.
