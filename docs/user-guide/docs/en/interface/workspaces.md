---
kind: interface
summary: >-
  A workspace is a place of the rail that holds open images in tab groups. Make several, name them, move an image
  between them, give one a window of its own, and arrange its images with the layout icons.
menu: Window
---
# Workspaces

A [workspace](../reference/glossary.md#workspace) is where your images are: a place of the [rail](rail-and-places.md) whose page holds the image views in
tab groups, side by side or one above another. Every image you open goes into a workspace, and the
[readout](readout.md) beside it reads the image you work on. There is one workspace, **Workspace 1**, until you make
another.

Arranging the images of a workspace changes only what you see. No image, [history](../reference/glossary.md#history)
or unsaved change moves with it.

```figure
owed: true
image: ../assets/images/interface/workspaces.webp
alt: >-
  The AfterNight main window with Workspace 1 selected in the rail. The top bar reads No project, then Workspace 1 and
  the name of a master flat, the field Search or run a command, and at its right a lock, two arrangement icons, a fit
  icon, Night, help, information and a menu button. The centre holds two images split side by side, each in a tab
  group of its own: the Carina Nebula master at the left and a gray master flat with dust shadows at the right, under
  an AUTOSTRETCH banner. A dark floating toolbar lies over the bottom of the images, and under them a strip reads
  HISTORY and Initial state. The readout column at the right shows the flat's Histogram, Statistics, Astrometry and
  Image cards.
caption: >-
  Workspace 1 with two images split side by side, and the layout icons at the right of the top bar.
pins:
  - {n: 1, x: 1.2, y: 15.3}
  - {n: 2, x: 10.2, y: 5.9}
  - {n: 3, x: 80.6, y: 5.9}
  - {n: 4, x: 3.6, y: 10.2}
  - {n: 5, x: 38.6, y: 45.0}
  - {n: 6, x: 10.6, y: 89.6}
  - {n: 7, x: 3.4, y: 95.1}
```

## What you see

1. **The workspace in the rail.** Its icon, among the places at the top of the [rail](../reference/glossary.md#rail). With several workspaces each
   icon carries its number, and one in a window of its own carries a window mark instead.
2. **Its name in the top bar.** The breadcrumb names the workspace shown, then its
   [active image](../reference/glossary.md#active-image).
3. **The layout icons.** **Lock layout**, **Group All Images**, **Split All Images** and **Fit Images To Window**, at
   the right of the top bar while a workspace is shown. They act on the workspace on screen.
4. **The tab groups.** Each holds one image, or several as tabs. [Docking Images](docking-panels.md) says how to move
   them.
5. **The images.** Here two, split side by side.
6. **The floating toolbar.** The zoom, channel and display mode of the workspace's active image: see
   [Image Viewer](image-viewer.md).
7. **The history strip.** The steps of that image, with its Undo, Redo and Image History: see
   [Image History](image-history.md).

Each workspace has its own floating toolbar and history strip, which show its image that was active last.

## What you can do

### Make another workspace

Choose **Window > New Workspace**, or right-click a workspace's icon in the rail and choose **New Workspace**. The new
workspace is named **Workspace 2**, or the next number that is free, and its page reads "This workspace is empty"
with **Open file...** and "or drag an image here". A file opened from there goes into it. A new workspace starts
locked when **Lock dock layout by default** is on in [Settings](settings-dialog.md).

A workspace with no docked image closes once you leave it, unless it is the only one.

### Go to a workspace

Click its icon in the rail, whose tooltip names it. Its page comes back as you left it. With no image open and one
workspace, the click does nothing: open an image first.

### Open a workspace's menu

Right-click its icon, press and hold it for half a second, or select it and press the Menu key. The menu acts on that
workspace:

- **New Workspace**, **Rename...** and **Move to New Window**;
- **Group All Images**, **Split All Images** and **Fit All Images to Window**;
- **Lock Layout**, checked while the layout is locked;
- **Close Workspace**.

The **Window** menu holds the same commands for the current workspace: the one on screen in the main window, or the
one whose window you used last.

### Rename a workspace

Choose **Rename...** in its menu, or **Window > Rename Workspace...**. Type the name in the **Name** field of
**Rename Workspace** and choose **OK**, which stays disabled while the name is blank. The rail, the top bar and the
workspace's window take the new name.

### Move an image to another workspace

Right-click the image's tab and open **Move to Workspace**. Choose another workspace, or **New Workspace** to make one
for the image. The image goes to the right of what that workspace holds, that workspace is shown, and the one the
image left closes when it has no docked image left.

### Give a workspace a window of its own

Choose **Move to New Window** in its menu, or **Window > Move Workspace to New Window**. The workspace opens in a window
titled with its name, as large as the centre of the main window, with the layout icons in its header. The main window
shows another workspace, or Home when every workspace has a window. A click on the workspace's icon in the rail brings
its window to the front, and files dropped on the window open in that workspace.

To bring it back, close its window and answer "Return this workspace to the main window, or close its images?":
**Return** puts it back, **Close Images** closes it as **Close Workspace** does, and **Cancel** keeps the window.

### Close a workspace

Choose **Close Workspace** in its menu, or **Window > Close Workspace**. Each of its images closes as **Close** closes
it, with its question when it has unsaved changes. Answer **No** for one and the workspace stays open with that image.
The only workspace cannot be closed. When no image is left open, Home is shown.

### Arrange the images

Choose a layout icon, or the same command in the workspace's menu or the **Window** menu. **Group All Images** makes
every image a tab of one group, **Split All Images** gives each a group of its own, **Fit Images To Window** fits
each to its group, and **Lock layout** keeps them where they are. Images that float, and other workspaces, stay as
they are.

### Find the images that float

When the only workspace has no docked image and images float in windows of their own, its page reads "The images of
this workspace are floating" and lists them, each with a small picture, its name and **Dock here**. **Dock all here**,
"or drag a window back here" and **Open file...** follow. **Dock here** docks an image at the right of the
workspace.

### Keep the workspaces with a project

A [project](projects.md) keeps its workspaces when you leave it: each with its name, its lock and its images, and the
one you used last. Opening the [project](../reference/glossary.md#project) puts them back. Without a project, every start has one empty workspace.

## Controls

### Layout icons

```controls
window: Layout icons
controls:
  - name: Lock layout
    kind: toolbutton
    default: ""
    effect: Locks the layout of the workspace shown. Its icon then shows an open lock and its tooltip reads **Unlock layout**, and choosing it again unlocks. **Window > Lock Layout**, **Window > Unlock Layout** and **Lock Layout** in the workspace's menu do the same.
    enabled: While a workspace is shown. Disabled while one of its images is maximized, and **Window > Lock Layout** and **Window > Unlock Layout** offer only the one that changes the state.
    tradeoff: The lock stops dragging the workspace's tabs and groups, dropping an image into it, and the double-click that floats a tab or a group. It does not stop **Group All Images**, **Split All Images**, **Fit Images To Window**, **Float / Detach**, choosing a tab or closing an image.
    scope: The workspace shown, or in a workspace's own window, that workspace. Each workspace has a lock of its own.
    persistence: For the session, and kept by a project while one is open. **Lock dock layout by default**, on the Interface page of Settings and kept as `interface/lockLayoutByDefault`, locks the workspace at the start and every new workspace. Changing that setting does not lock or unlock a workspace that is open.
  - name: Group All Images
    kind: toolbutton
    default: ""
    effect: Puts every image docked in the workspace in one tab group, at its left, and keeps the active image active.
    enabled: With an image open. Disabled while an image of the workspace is maximized.
    tradeoff: No command undoes an arrangement.
    scope: The images docked in the workspace. Images that float and images of other workspaces stay where they are.
  - name: Split All Images
    kind: toolbutton
    default: ""
    effect: Gives every image docked in the workspace a tab group of its own, makes the groups equal and keeps the active image active.
    range: "From one to sixteen images it builds a fixed grid: two side by side, three as two above each other beside one, four as two by two, nine as three by three, sixteen as four by four. Beyond sixteen, every four more images make another column at the right."
    enabled: As **Group All Images**.
    tradeoff: The grid follows the count of images, not what they show, and many images leave each a small group. Many images in sight also cost memory and drawing time; group them into tabs, or maximize one, when the application slows.
    scope: As **Group All Images**.
  - name: Fit Images To Window
    kind: toolbutton
    default: ""
    effect: Fits each image docked in the workspace to its tab group, as each view's own **Fit To Window** would.
    enabled: With an image open, also while an image is maximized.
    tradeoff: A fitted picture is reduced. Look at 100 % before you judge noise, stars or sharpness.
    scope: The images docked in the workspace. It changes the zoom only.
    note: Lock, group, split and fit have no shortcut. ++ctrl+shift+f++ is the active image's own **Fit To Window**.
```

## When it goes wrong

The layout icons are missing from the top bar
:   **Cause.** Home, the Integration place or a [task](../reference/glossary.md#task) is shown, and the icons belong to a workspace.
:   **Fix.** Click a workspace's icon in the rail.

**Close Workspace** is greyed
:   **Cause.** The workspace is the only one.
:   **Fix.** Close its images with **Close** instead, or make another workspace first.

**Move to New Window** is greyed
:   **Cause.** The workspace already has a window of its own.
:   **Fix.** Click its icon in the rail to bring that window to the front.

A workspace is gone from the rail
:   **Cause.** It had no docked image when you left it, so it closed.
:   **Fix.** Make it again with **New Workspace**, and move or open an image in it before you leave it.

**Group All Images** left some images out
:   **Cause.** They float in windows of their own, or are in another workspace.
:   **Fix.** Dock them with **Dock here** or a drag, or bring them with **Move to Workspace**.

The arrangement is gone after a restart
:   **Cause.** Without a project, AfterNight keeps only the main window's size and place.
:   **Fix.** Work in a [project](projects.md), which keeps its workspaces.

## Related

- [Docking Images](docking-panels.md), for the tabs and groups inside a workspace.
- [Image Viewer](image-viewer.md), for the floating toolbar of a workspace.
- [The Rail and Its Places](rail-and-places.md), for the other places beside the workspaces.
- [Top Bar and Menus](top-bar-and-menus.md), for the top bar around the layout icons and the **Window** menu.
- [Projects](projects.md), which keep the workspaces from one session to the next.
- [Settings](settings-dialog.md), for **Lock dock layout by default** and **Remember window size and position**.
