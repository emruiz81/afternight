---
kind: tutorial
summary: >-
  Start AfterNight for the first time, find your way around its Dashboard, open one image and look at it, and come
  back to the Dashboard with the image among your recent files.
needs: []
---
# First Launch

You start with AfterNight installed and never started, and one image file of your own. You end with that image open
and visible in AfterNight, then listed among the recent files of the Dashboard, the screen AfterNight starts on.
Opening a file only reads it: your file stays as it is.

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
  The AfterNight main window on its Dashboard. At the top left, the AfterNight logo, the name AfterNight with its
  version, and the tagline "Deep-sky imaging, calibrated and refined". At the top right, three small round buttons: a
  gear, a question mark and an i. Under the name, three blue buttons: Open file, New sequence and Image integration,
  and a button with three dots at the right. Below, a large dark panel reads "Welcome to AfterNight", then "What you
  can do" over six small cards, "Get started" over three large cards named Open Image, New Sequence and Image
  Integration, a dashed box that reads "Drop your images here to get started", and a line of keyboard hints.
caption: >-
  The Dashboard as AfterNight shows it on its first start, before any file was opened.
pins:
  - {n: 2, x: 14.0, y: 12.1}
  - {n: 3, x: 89.8, y: 12.9}
  - {n: 4, x: 3.5, y: 21.0}
  - {n: 8, x: 94.6, y: 9.0}
```

### Start AfterNight

On Windows, choose **AfterNight** in the Start menu. On Linux, start the AppImage. The first time, the window fills
the screen and opens on the Dashboard. No welcome dialog opens and nothing asks you a question.

Starting AfterNight while it is already open brings the open window to the front. It does not open a second one.

!!! see "You should see"
    The Dashboard, in the dark theme, with a large panel that reads "Welcome to AfterNight".

??? wrong "If it goes wrong: the Dashboard shows thumbnails instead of the welcome panel"
    Your user profile already holds recent files, from an earlier installation. AfterNight shows the welcome panel
    only while the list of recent files is empty. Follow the same steps with the **Open file** button.

### Read the Dashboard

The top line names AfterNight and its version. Check that it is the version you installed. The three small buttons
at the right, a gear, a question mark and an i, open **Settings**, this **User Guide** and **About**, as their
tooltips say.

Under the name, **Open file**, [**New sequence**](../reference/glossary.md#sequence) and **Image integration** are
the Dashboard's quick actions. The
button with three dots at the right holds the others: **Extension Manager**, **Clear recents**, **System info** and
**Customize quick actions**, which reorders and hides them. [Dashboard](../interface/dashboard.md) describes them.

The welcome panel holds six cards under "What you can do", which only describe AfterNight. The three cards under
"Get started" do what the three quick actions do. The formats they and the dashed drop box name are a short list,
not every format AfterNight opens.

While the Dashboard shows, the status bar is hidden and the **Window** menu is disabled. Both return when an image is
open.

!!! see "You should see"
    The version after the name, the same as in the name of the file you installed.

??? wrong "If it goes wrong: ++ctrl+n++ does nothing"
    The hint line under the panel names it, but no command has that shortcut. Choose **New sequence**, or **File >
    New Sequence...**, to start a sequence.

### Look at the settings that decide how a file opens

Choose the gear button, or **Edit > Settings...**, or press ++ctrl+comma++. Two pages of [Settings](../interface/settings-dialog.md)
decide how your first image opens:

- **Raw Images** sets how the raw file of a camera is read. A new profile reads it with the **Daylight** white
  balance, and turns it by the orientation the camera recorded in its [EXIF](../reference/glossary.md#exif) record,
  as **Auto-rotate based on EXIF orientation** asks. Its **Demosaic
  Algorithm**, RCD on a new profile, turns any color-filter mosaic you open into color when you ask for it in step 5.
- **General** shows under **Temporary Files** the folder for the intermediate files of each session: an
  `AfterNight` folder in the system's temporary folder. Until Settings stores it, with **OK** or **Apply**,
  AfterNight uses a `temp` folder in its own data folder instead. This page also holds **Check for
  extension updates on startup**, which is on. On a first start that check is the only thing AfterNight does on the
  network by itself; once extensions are installed, preparing their environments at startup may reach a package index
  too. Turn it off if this computer must stay offline.

The other pages, such as **External Tools** and **Catalogs & Data**, are for tools and data that opening an image does
not need. Choose **Cancel** to leave without a change, or **OK** to store what the pages show.

!!! see "You should see"
    The Settings dialog closes, and the Dashboard is as it was.

??? wrong "If it goes wrong: "The temporary files directory is not writable: …""
    The folder named under **Temporary Files** cannot be created or written to, and Settings keeps the dialog open.
    Choose a folder on a disk you can write to, with room for large files, and choose **OK** again.

### Open one image

Choose **Open file**, or the **Open Image** card, or press ++ctrl+o++. The **Open File** dialog opens in your
Pictures folder the first time, and later in the folder you last opened from. Its filter, **All supported formats**,
shows every file AfterNight opens, sequences and videos among them. Select your image and choose **Open**.

You can also drag files from your file manager and drop them anywhere on the Dashboard. AfterNight opens the dropped
files of a kind it opens and ignores the others.

!!! see "You should see"
    The dialog closes and a small window titled **File Open** appears.

??? wrong "If it goes wrong: "Unsupported File Type""
    The box reads "AfterNight can only open supported image, RAW, and video files." One of the files you selected
    is of a kind AfterNight does not open, and the whole selection is refused. Select only files the filter shows.

### Answer the question about the color filter array

This step happens only when the image is a mosaic of a color filter array, or
[CFA](../reference/glossary.md#cfa): the raw file of a color camera, or a file of another format that states a filter
pattern. AfterNight then asks whether to [debayer](../reference/glossary.md#debayer) it. A box titled **Open CFA file** asks "The file appears to have a bayer matrix.
Do you want to automatically debayer it before opening? (If no, you can always debayer it by using the Debayer
process)".

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
files on it counts the share done. When the image is read, the Dashboard gives way to the workspace, and the image opens in its view.

!!! see "You should see"
    A tab named after your file, without its extension, a toolbar above the image, and a status bar at the bottom.
    The status bar gives the image's size and channels, its sample type, the memory it takes and the zoom.

??? wrong "If it goes wrong: a box titled "File open error""
    It reads "Could NOT load file '…'." and gives the reason. A file can carry a name AfterNight knows and still hold
    something it does not read, or be damaged. Read the reason, look the format up in
    [Supported Formats](supported-formats.md), and try another file.

### Look at the image

The view opens in the **Linear** display mode, which shows the samples as they are. A
[linear image](../reference/glossary.md#linear-image), such as a stacked [master](../reference/glossary.md#master) or
a raw frame, then looks nearly black. Choose [**Autostretch**](../reference/glossary.md#autostretch) in the display
mode box of the view's
toolbar to see it. This changes the display only, not the image: the word AUTOSTRETCH in the corner of the view
reminds you of it.

```figure
image: ../assets/images/getting-started/first-launch-image.webp
alt: >-
  The AfterNight main window with one image open in a tab. Above the image, the view's toolbar with zoom buttons, a
  channel box reading RGB and the display mode box reading Autostretch. The image shows the red and white clouds of
  the Carina Nebula across a field of stars, with the word AUTOSTRETCH in its top left corner. At the bottom, the
  status bar gives the image's size, sample type, memory and zoom.
caption: >-
  A linear master of the Carina Nebula just opened, shown in the Autostretch display mode.
pins:
  - {n: 7, x: 24.0, y: 12.6}
```

[Image Viewer](../interface/image-viewer.md) shows how to zoom, pan and inspect the image.

!!! see "You should see"
    Your image in its view, with stars and structure visible.

### Open the user guide

Press ++f1++, or choose **Help > User Guide**. On the Dashboard, the small button with the question mark does the
same. The guide opens in your web browser, from the copy installed with AfterNight.

!!! see "You should see"
    This guide in your browser.

### Close the image

Choose **File > Close**, or the cross on the view's tab. When the last view closes, AfterNight returns to the
Dashboard. The welcome panel has given way to **Recent**, which shows your image as a thumbnail with its name. A click
on it opens the image again.

!!! see "You should see"
    The Dashboard, with your image under **Recent**.

??? wrong "If it goes wrong: the Dashboard does not come back"
    The Session Console is open, and AfterNight stays in the workspace while it is. Close it, or choose **View >
    Session Console** again, and the Dashboard returns.

## The result

You have opened an image, looked at it and closed it, and AfterNight is ready for the [Quick Start](quick-start.md).

- **Your file is as it was.** Opening reads a file into memory and never writes it. A raw file is turned into an
  image by the **Raw Images** settings in force when you opened it, and its bytes stay the same.
- **The Dashboard remembers it.** Your image is under **Recent**, and in **File > Open Recent**, among the files you
  opened last, as many as **Recent Files Limit** in Settings keeps. Only a file that opened is added. From now on
  the Dashboard opens with your recent files instead of the welcome panel.
- **AfterNight has a folder of its own** in your user profile: `%LOCALAPPDATA%\AfterNight` on Windows,
  `~/.local/share/AfterNight` on Linux. It holds the settings, the list of recent files and their thumbnails, and the
  logs in `logs`.
- **Each session has a folder for its intermediate files,** under `temp` in that folder, or in the **Temporary
  Files** location once Settings has stored one. With **Clear temporary files on exit** on, as it is on a new
  profile, the folder goes when AfterNight closes. The next start then removes the folders of sessions that have
  ended, and never the folder of one still running.
- **The window keeps its size.** When you close AfterNight, it remembers the size and place of its window, and the
  next start opens it the same way.

**New sequence** and **Image integration** open windows of their own. They write files only once you start what they
prepare.

## Next

- [Quick Start](quick-start.md) takes the frames of one night to a stacked master.
- [Dashboard](../interface/dashboard.md) describes the recent files, their search and the quick actions.
- [Image Viewer](../interface/image-viewer.md) shows how to inspect an image and what Autostretch does.
- [Supported Formats](supported-formats.md) lists the files AfterNight opens and writes.
- [Settings](../interface/settings-dialog.md) describes every page of the Settings dialog.
