---
kind: interface
summary: >-
  The main window's regions - the top bar, the rail, the centre and its places, the readout, the status line and the
  Session Console - and what to do first.
---
# The Main Window

AfterNight works in one main window. The top bar runs along its top and the [rail](../reference/glossary.md#rail) down its left. The centre shows one
place at a time: [Home](home.md), a [workspace](../reference/glossary.md#workspace) of images, the Integration hub or a [task](../reference/glossary.md#task). Beside a workspace the [readout](../reference/glossary.md#readout)
reads out the [active image](../reference/glossary.md#active-image), and the status line runs along the bottom.

This page names each region and says what to do first. The page each region links to describes it in full.

```figure
image: ../assets/images/interface/overview.webp
alt: >-
  The AfterNight main window with one image open. Along the top, the top bar: No project, the path Workspace 1 then
  RGB, a wide field reading Search or run a command with the keys Ctrl K, and a row of icons at the right. Down the
  left edge, the rail: a house, a picture frame that is selected and a stack of layers, and at its foot a terminal
  and a gear. The centre holds a tab named RGB over the red and white clouds of the Carina Nebula among many stars,
  the word AUTOSTRETCH at its top left, a floating toolbar near its foot with the zoom, RGB, Linear and Autostretch,
  and a History strip under it holding Initial state. At the right, the Readout column with its Histogram, Statistics
  and Astrometry cards and the top of its Image card. Along the bottom, the status line: a box reading RGB, the
  image's size, type and memory, the zoom, RA and Dec, an icon and a Console button.
caption: >-
  The main window with the linear Carina master open in Workspace 1, shown with Autostretch, and its readout at the
  right.
pins:
  - {n: 1, x: 28.5, y: 4.8}
  - {n: 2, x: 1.8, y: 24.0}
  - {n: 3, x: 40.0, y: 45.0}
  - {n: 4, x: 88.5, y: 9.6}
  - {n: 5, x: 10.7, y: 98.4}
  - {n: 6, x: 94.3, y: 98.4}
```

## What you see

1. **The top bar.** The [project](../reference/glossary.md#project) switcher, which reads **No project** until a project is open, then where you are.
   In its middle, the field that opens the [command palette](command-palette.md). At its right, the layout icons
   while a workspace shows, then **Night**, **User Guide**, **Information** and **All menus**. The menu bar is hidden
   until you press ++alt++. See [Top Bar and Menus](top-bar-and-menus.md).
2. **The rail.** The places at its top - **Home**, each workspace and **Integration** - then an item for each open
   task, and at its foot **Session Console** and **Settings**. See [The Rail and Its Places](rail-and-places.md).
3. **The centre.** The place or the task the rail has selected: Home, a [workspace](workspaces.md) of image views, the
   Integration hub, or a [task](tasks.md)'s page. Each image view has a floating toolbar and a
   [history](../reference/glossary.md#history) strip under it, which [Image Viewer](image-viewer.md) and
   [Image History](image-history.md) describe.
4. **The readout.** Beside a workspace, the Histogram, Statistics, Astrometry and Image cards of the active image. See
   [Readout](readout.md).
5. **The status line.** The box that chooses the active image, the image's facts and zoom, and the sky coordinates
   under the pointer. At its right, the readout's button and **Console**.
6. **The Session Console.** A drawer above the status line that lists what the application did and what went wrong.
   **Console**, the rail's **Session Console** and **View > Session Console** open it. See
   [Session Console](log-viewer.md).

## What you can do

### Start working

1. **Open an image.** Choose **Open file** on Home, press ++ctrl+o++, or drop files on the window. The image opens in
   **Workspace 1**, the centre shows it, and the readout fills in.
2. **Open a process.** Press ++ctrl+k++ and type part of its name, or choose it under **Process** in the menu bar or in
   **All menus**. It opens as a task: a page of the centre, with an item of its own in the rail.
3. **Apply it.** Set the controls in the task's column and choose **Apply**, or press ++ctrl+enter++. A step joins the
   image's history.
4. **Go back to the image.** Choose **Close** in the task's footer, press ++esc++ while it is idle, or click the
   workspace in the rail.

### Move between places

Click an item of the rail. Opening an image shows its workspace, and Home comes back by itself once the last image is
closed. The readout stands beside a workspace only, and the layout icons show in the top bar only then.

AfterNight starts on Home and reopens no image of the session before. **Open the last project on startup**, on the
Projects page of [Settings](settings-dialog.md), opens the last [project](projects.md) instead, with its images. With
**Remember window size and position** on, on the Interface page, the window comes back where it was.

### Choose the image you work on

The active image is the one the menus, the [command palette](../reference/glossary.md#command-palette) and a newly opened task act on. Click its view or its tab,
or choose its name in the status line's box. The menus then offer what suits that image: the color commands for an RGB
image and **Convert to RGB** for a grayscale one, the **Sequence** submenu for a
[sequence](../reference/glossary.md#sequence), and the header viewers only for an image whose file stated
[FITS](../reference/glossary.md#fits) cards or an [EXIF](../reference/glossary.md#exif) record. **Undo** and **Redo**
name the step they would take back or do again.

A process that cannot open on the active image says why when you choose it: no image, an image of the wrong kind, a
task of the same process already open on it, or a task that needs the image to itself. A process an extension adds is
greyed in the **Process** menu instead, with the reason as its tooltip. Image Integration and the other tools of the
Integration place need no image.

### Close images and quit

Choose **File > Close** (++ctrl+w++), **Close All** or **Close All Others**, or the cross on a view's tab. A view with
unsaved changes has an asterisk after its name, and closing it asks first: "The image '...' has unsaved changes, are
you sure you want to close it?" Quitting with such a view open asks too. **Confirm before closing unsaved images**, on
the General page of Settings, turns both questions off.

Without a project, an image's history and its duplicates last only while AfterNight runs. Save what you need to keep,
and keep a copy of a [master](../reference/glossary.md#master) before you try a process you do not know.

## Controls

### The status line

```controls
window: Status line
controls:
  - name: Active image
    kind: combo
    default: The active image
    options: [The active image, Every other open image or sequence]
    effect: Makes the image you choose the active one and brings its view forward. Clicking a view or its tab changes it too.
    enabled: Shown while an image or a sequence is open, and hidden otherwise.
    scope: Every menu, the command palette and every task opened afterwards.
    persistence: The views are not reopened at the next start, so neither is the choice, unless a project opens with its images.
  - name: Image facts
    kind: readout
    default: ""
    effect: The active image's size and color, its sample type and the memory it takes, as `[7246x5429 (RGB)] - [32 bit floating point] - [450.19 MB]`, then its zoom.
    enabled: Empty while no image is open. On a task's page it shows the facts of the task's preview.
    note: Image Viewer says what each field means.
  - name: RA and Dec
    kind: readout
    default: "RA - Dec -"
    effect: The right ascension and declination under the pointer on a plate-solved image. Each value's tooltip gives it in full.
    range: A dash while the pointer is off the image or the image has no plate solution.
  - name: Show or hide the readout
    kind: toolbutton
    default: ""
    effect: Collapses the readout column to its edge, or opens it again, as **View > Readout** and the button in the column's header do.
    enabled: Always. The column itself shows only beside a workspace.
    persistence: Kept between sessions.
  - name: Console
    kind: button
    default: ""
    effect: Opens the Session Console's drawer above the status line.
    range: Its count is the warnings and errors of the session you have not looked at yet. The rail's **Session Console** shows the same count.
    note: The rail's **Session Console** and **View > Session Console** open and close the drawer too. Session Console says what it lists.
```

## When it goes wrong

The readout column is missing
:   **Cause.** Home, the Integration hub or a task fills the centre, and the readout stands beside a workspace only;
    or it is collapsed.
:   **Fix.** Click the workspace in the rail, then **Show or hide the readout** in the status line if the column is
    still folded away.

The menu bar is not there
:   **Cause.** It shows only while you use it, after ++alt++.
:   **Fix.** Press and release ++alt++, or open **All menus** in the top bar. **Always show the menu bar**, on the
    Interface page of Settings, keeps it shown.

The image commands or a process are greyed
:   **Cause.** No image is active, the active image is not of the kind the command needs, or a task already open on the
    image holds it.
:   **Fix.** Click the view of the image you mean, and read the message a process gives, or the tooltip of a process an
    extension adds. When it speaks of another process on the image, close that task first.

A process worked on the wrong image
:   **Cause.** Another image was active when the task opened.
:   **Fix.** Undo the step with ++ctrl+z++ in that view, close the task, click the right view and open the process
    again.

The File Open window closes and no view appears
:   **Cause.** The open was cancelled, the file could not be read, or no view could be made for it; a message titled
    "File open error" says which.
:   **Fix.** Read the message and the Session Console, check the format in
    [Supported Formats](../getting-started/supported-formats.md), and open the file alone.

**FITS Header** or **EXIF Viewer** is greyed
:   **Cause.** The image's file stated no [header cards](../reference/glossary.md#header-card), or no camera record.
:   **Fix.** Open the image's **Summary** instead, or open a file that carries the record.

## Related

- [Home](home.md), to open files and return to recent work.
- [The Rail and Its Places](rail-and-places.md), for the places and the tasks the rail holds.
- [Top Bar and Menus](top-bar-and-menus.md), for the top bar's buttons and every menu.
- [Command Palette](command-palette.md), to find any command, process, image or setting by name.
- [Tasks](tasks.md), for what every task's page shares.
- [Saving And Exporting](saving-and-exporting.md), before you close work you need to keep.
