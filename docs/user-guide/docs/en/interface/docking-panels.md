---
kind: interface
summary: >-
  How a workspace holds its images as tabs of tab groups, side by side, one above another or floating, and how to
  move, split, group, float and close them.
---
# Docking Images

Inside a [workspace](workspaces.md), each image or [sequence](../reference/glossary.md#sequence) you open has a tab in
a tab group. A group shows one of its images at a time. Groups sit side by side or one above another, and an image can
float in a window of its own. Arranging them changes only what you see: no image,
[history](../reference/glossary.md#history) or unsaved state changes.

Only images dock in a [workspace](../reference/glossary.md#workspace). A [task](../reference/glossary.md#task) has a page of its own, the Session Console is a drawer of the main window, and
Image History, the header viewers and Settings are windows of their own.

```figure
image: ../assets/images/interface/docking-panels.webp
alt: >-
  The centre of the main window holding one tab group with two tabs, RGB and the name of a master flat. The flat's tab
  is in front, with an accent line under it, and each tab has a cross. At the right end of the strip are a chevron, a
  float button and a cross. The gray master flat with its dark dust shadows sits small in the middle of the group under
  an AUTOSTRETCH banner, and a dark floating toolbar near the bottom starts with the flat's name. Under the group, the
  history strip reads HISTORY and Initial state.
caption: >-
  Two images grouped as tabs of one tab group, the master flat in front.
pins:
  - {n: 1, x: 2.2, y: 2.1}
  - {n: 2, x: 12.9, y: 2.1}
  - {n: 3, x: 44.2, y: 2.1}
  - {n: 4, x: 88.4, y: 2.1}
  - {n: 5, x: 24.6, y: 50.0}
  - {n: 6, x: 3.7, y: 91.7}
```

## What you see

1. **The tabs.** One for each image of the group: an icon for an image or a sequence, its name, an asterisk in the
   accent color while it has unsaved changes, and a cross. Hover over a tab for its full name and the summary of its image.
2. **The tab in front.** Its image is the one the group shows. An accent line runs under it in the group you work in.
3. **The close cross.** It closes the tab's image.
4. **The strip's buttons.** **Tab List**, shown with two tabs or more, then **Float / Detach** and
   **Close Tab Group**.
5. **The image in front.** It fills its group, which is never smaller than 100 by 100 pixels.
6. **The name on the floating toolbar.** With more than one image docked in the workspace, the toolbar starts with
   the name of the image it acts on.

## What you can do

### Know where a new image goes

- A file you open goes at the right of what the workspace on screen holds. The files you choose together in one Open
  are tabs of one group.
- An image made from another - a duplicate, an extracted channel or frame, a converted copy, a snapshot of a task's
  preview - opens as a tab beside it, in its workspace.
- A new image a task makes opens beside its neighbour too. While the task's page is shown, a message such as
  "stars_only opened in Workspace 1" says where, with **Show**.
- Opening an image while another is maximized restores that one first.

Click a tab, or anywhere in an image, to make it the [active image](../reference/glossary.md#active-image). The
[readout](../reference/glossary.md#readout) and the floating toolbar follow it.

### Move an image

With the layout unlocked, drag a tab:

- along its strip to change the order;
- onto another group's strip to join that group;
- over a group or toward an edge of the workspace, where the drop indicators show, to split them;
- out of the main window to float the image.

While you drag, a cross of five square targets stands over the group under the pointer, the middle one for its tabs
and the four others for its sides, and four more stand at the edges of the workspace. The target under the pointer is
drawn in the accent color, and a shaded band shows where the image would land. Release it there.

Drag the empty part of a strip to move the whole group. Drag the bar between two groups to give one more room.

### Float an image

Double-click a tab to float its image in a window of its own, or choose **Float** on the tab's menu. Double-click the
empty part of a strip, or choose **Float / Detach**, to float the whole group. In a floating window the same button
reads **Dock** and docks the group back, and a double-click on a tab docks its image back.

A floating image has a smaller floating toolbar: **Fit To Window**, **Zoom Out**, the zoom, **Zoom In**, the display
mode as one button, and **More**. It has no history strip: make it the active image and use the **Edit** menu. It
cannot be maximized.

### Use a tab's menu

Right-click a tab. Its image becomes the active one, and the menu offers **Close**, **Close All Others** and
**Close Tab Group**, then **Float** and **Move to Workspace**, then **Duplicate**, **Rename** and **Save File...**, and
**Show in File Browser**. [Workspaces](workspaces.md) says where **Move to Workspace** takes the image.

### Arrange the images of the window an image is in

Right-click an image and open **Current Window**. **Group All Images**, **Split All Images** and **Fit All Images to
Window** then arrange the images of the window it is in: those of its floating window alone, or, for a docked image,
those docked in its workspace, as the workspace's layout icons do. Other workspaces stay as they are.

### Lock the layout

Choose **Lock layout** in the top bar, or **Window > Lock Layout**. The workspace's tabs and groups can no longer be
dragged or double-clicked into another place, and no image can be dropped into it. Unlock it the same way.

### Close images

The cross on a tab closes its image, and **Close Tab Group** closes each image of the group in turn. An image with
unsaved changes asks first, and so does one with a task open on it, whose task closes with it. While a run writes an
image, its cross is disabled and its view is dimmed until the run ends.

## Controls

### A tab group

```controls
window: A tab group
controls:
  - name: Tab
    kind: action
    default: ""
    effect: A click makes its image the active image. A drag moves the image, and a double-click floats it or docks it back. A right-click opens the tab's menu.
    enabled: Dragging and double-clicking only while the workspace's layout is unlocked.
    note: A long name is shortened in the middle, never below twelve characters.
  - name: Tab summary
    kind: readout
    default: ""
    effect: The tooltip of a tab gives the image's full name and the summary of the image, the SER sequence or the image list. The title of a floating window shows the summary too.
    range: A video sequence shows no summary, nor does the empty part of the strip. A still image an edit is writing at that moment shows none either; hover again.
  - name: Task mark
    kind: action
    default: ""
    effect: While a task is open on the image, its tab shows the task's mark, whose tooltip reads "<process> is open on this image. Click to go to the task". A click shows the task.
    note: While the task's run writes the image, the tooltip says that the image is locked until the run ends.
  - name: Close cross
    kind: button
    default: ""
    effect: Closes the image, after "The image '...' has unsaved changes, are you sure you want to close it?" when it has some, and after a message titled "Close image with process attached" that names the tasks open on it. A sequence stops playing, and those tasks close.
    enabled: Disabled while a run writes the image.
    tradeoff: Nothing is saved for you. Answer No to keep the image open.
  - name: Tab List
    kind: toolbutton
    default: ""
    effect: Lists the group's tabs by name. Choosing one brings it to the front; the one in front is greyed.
    enabled: Shown while the group has two tabs or more.
  - name: Float / Detach
    kind: toolbutton
    default: ""
    effect: Moves the whole group into a floating window, or docks it back from there, where its tooltip reads **Dock**.
    enabled: Disabled while a run writes the image in front.
    tradeoff: In a floating window, **Maximize image view** and **Image > View > Maximize View** are disabled, the history strip is not shown and the floating toolbar is the small one.
  - name: Close Tab Group
    kind: toolbutton
    default: ""
    effect: Closes each image of the group in turn, each with its own question.
    range: It stops at the first question you cancel, and the images after it stay open.
    enabled: Disabled while a run writes the image in front.
  - name: Bar between groups
    kind: action
    default: ""
    effect: Dragging it moves the border between the two groups.
  - name: Empty part of the strip
    kind: action
    default: ""
    effect: Dragging it moves the whole group. A double-click floats the group, or docks it back.
    enabled: While the workspace's layout is unlocked.
```

### The tab's menu

```controls
window: The tab's menu
controls:
  - name: Close
    kind: action
    default: ""
    effect: Closes the tab's image, with the questions of its close cross. ++ctrl+w++ closes the active image of a workspace.
  - name: Close All Others
    kind: action
    default: ""
    effect: Closes every other open image, each with its own question, and goes on to the next after a No.
  - name: Close Tab Group
    kind: action
    default: ""
    effect: Closes each image of the tab's group, as the strip's **Close Tab Group** does.
  - name: Float
    kind: action
    default: ""
    effect: Floats the tab's image alone in a window of its own.
  - name: Move to Workspace
    kind: action
    default: ""
    effect: Lists the other workspaces, then **New Workspace**. The image moves to the right of what the workspace chosen holds, and that workspace is shown.
  - name: Duplicate
    kind: action
    default: ""
    effect: Opens a copy of the image as a tab beside it.
    tradeoff: The copy takes the memory of a second image.
  - name: Rename
    kind: action
    default: ""
    effect: Gives the image another name, which its tab, the top bar and its history show.
  - name: Save File...
    kind: action
    default: ""
    effect: Saves the image or sequence to a file you name, as **Image > Actions > Save File...** does.
  - name: Show in File Browser
    kind: action
    default: ""
    effect: Shows the image's file in the system's file browser.
    enabled: While the image is a file.
```

### Current Window

```controls
window: Current Window
controls:
  - name: Group All Images
    kind: action
    default: ""
    effect: Puts the images of the window the clicked image is in as tabs of one group, and keeps the clicked image active.
    enabled: In the menu of a right-click on an image. Disabled while that image is maximized.
    scope: The floating window that holds the clicked image, or, for a docked image, the images docked in its workspace. Other workspaces stay as they are.
  - name: Split All Images
    kind: action
    default: ""
    effect: Gives each image of that window a group of its own, in the grid of **Split All Images** among the layout icons.
    enabled: As **Group All Images**. A window with one image is left as it is.
  - name: Fit All Images to Window
    kind: action
    default: ""
    effect: Fits each image of that window to its group.
    enabled: Also while the clicked image is maximized.
```

## When it goes wrong

A tab cannot be dragged, and a double-click does nothing
:   **Cause.** The workspace's layout is locked.
:   **Fix.** Choose **Lock layout** in the top bar again, or **Window > Unlock Layout**.

An image cannot be dropped into a workspace
:   **Cause.** That workspace's layout is locked.
:   **Fix.** Unlock it first, or use **Move to Workspace** on the tab's menu.

The close cross of a tab is greyed
:   **Cause.** A task's run is writing the image.
:   **Fix.** Wait for the run to end, or click the tab's task mark to see it.

A floating image has no history strip and cannot be maximized
:   **Cause.** Those belong to a workspace.
:   **Fix.** Dock it back with a double-click on its tab, **Dock**, or **Dock here** on its workspace's page.

The arrangement is gone after a restart
:   **Cause.** Without a [project](../reference/glossary.md#project), AfterNight keeps only the main window's size and place, with **Remember window size
    and position** (`interface/rememberWindowSize`), which is on unless you turn it off.
:   **Fix.** Work in a [project](projects.md), which keeps its workspaces, or arrange the images again.

## Related

- [Workspaces](workspaces.md), for the layout icons, several workspaces and moving an image between them.
- [Image Viewer](image-viewer.md), for an image's own zoom, its floating toolbar and **Maximize image view**.
- [Tasks](tasks.md), for the tasks that close with their image and the runs that lock it.
- [The Main Window](overview.md), for the [rail](../reference/glossary.md#rail), the top bar and the status line around the workspace.
- [Settings](settings-dialog.md), for **Lock dock layout by default** and **Remember window size and position**.

## Credits

- [KDDockWidgets](https://github.com/KDAB/KDDockWidgets), by Klarälvdalens Datakonsult AB (KDAB), under the GNU
  General Public License version 2.0 or 3.0, draws the areas, the tabs, the floating windows and their gestures.
