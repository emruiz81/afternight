---
kind: interface
summary: >-
  The rail down the left of the main window: the places at its top, an item for each open task, the Session Console and
  Settings at its foot, and what each one shows or does.
---
# The Rail and Its Places

The [rail](../reference/glossary.md#rail) is the narrow column of icons down the left of the main window. An item at its top is a place - Home, a
[workspace](../reference/glossary.md#workspace), the Integration hub - and a click shows it in the centre. Under the places, each open [task](../reference/glossary.md#task) has an item of
its own. At the rail's foot, **Session Console** and **Settings** open over whatever the centre shows.

```figure
image: ../assets/images/interface/rail-and-places.webp
alt: >-
  The main window showing the Histogram Transformation task. Down the left edge, the rail: a house, a picture frame
  and a stack of layers, a short line, then a small square picture of the Carina Nebula with a round mark at its
  corner, which a bar at the rail's edge marks as shown. At the rail's foot, a terminal and a gear. The centre holds the
  task's page: the preview of the nebula, marked PREVIEW, at the left, and the task's column at the right, with the
  preview's readout at its top and Reset, Close and Apply at its foot.
caption: >-
  The rail beside the Histogram Transformation task: the three places at its top, the task's item under them, and the
  Session Console and Settings at its foot.
pins:
  - {n: 1, x: 3.6, y: 10.0}
  - {n: 2, x: 3.6, y: 14.4}
  - {n: 3, x: 3.6, y: 18.7}
  - {n: 4, x: 3.8, y: 24.5}
  - {n: 5, x: 3.6, y: 92.9}
  - {n: 6, x: 3.6, y: 97.2}
```

## What you see

1. **Home.** The place AfterNight starts on, to open files and return to recent work. While a [project](../reference/glossary.md#project) is open, it shows
   the project's own page. See [Home](home.md).
2. **A workspace.** One item for each workspace, named **Workspace 1** and so on, which holds image views. With
   several, each carries its number, and one moved to a window of its own carries a window mark. See
   [Workspaces](workspaces.md).
3. **Integration.** The Integration hub: the ways to stack, the single steps of calibration, registration and
   stacking, and the [integration jobs](../reference/glossary.md#integration-job).
4. **A task.** Under a short line, one item for each open task: the picture of the image it works on, with the
   process's mark at its corner, or the mark alone on a plain tile for a task with no image. A bar at the rail's edge
   marks the place or the task shown. See [Tasks](tasks.md).
5. **Session Console.** Opens or closes the console's drawer above the status line. See
   [Session Console](log-viewer.md).
6. **Settings.** Opens [Settings](settings-dialog.md).

## What you can do

### Change places

Click an item. Its name shows after a moment with the pointer over it; a task's names its process and its target, and
what it is doing. With no image open and one workspace, the workspace's item does nothing: open an image from Home.

### Follow a task's run

- While a task applies, a ring around its picture fills with the run's progress, or turns when the run gives no figure.
  Its name adds what it does and how far it is, such as "applying 40 %" or the step an integration is at.
- A run that ends while another place is shown leaves a green check on the item, or a red cross when it failed, until
  you show the task. Its name adds "applied" or "failed".
- A task floating in a window of its own shows its picture dimmed, with a window mark, and its name adds "in a window".
  A process that brings a window of its own adds "opens in its own window".

### Open an item's menu

Right-click a task or a workspace, hold the mouse button on it for half a second, or press the menu key while the rail
has the keyboard focus.

- A task's menu holds **Show**, **Float as Window** - **Attach** for a task in a window of its own - and **Close**, which
  is greyed while the task runs.
- A workspace's menu holds **New Workspace**, **Rename...**, **Move to New Window**, **Group All Images**, **Split All
  Images**, **Fit All Images to Window**, **Lock Layout** and **Close Workspace**, which Workspaces describes.

### Reach the tasks that do not fit

When more tasks are open than the rail has room for, its last task row reads the number hidden, such as "+3". Click it
for the task list: every task with its picture and what it is doing; choose one to show it. The mouse wheel over the
rail scrolls the tasks it shows.

### Float a task by dragging

Drag a task's item past the edge of the main window: the task floats in a window of its own under the pointer, which
follows it until you release the button. Released inside the main window, the drag changes nothing, and ++esc++
cancels it. Drag a floating task's window back over the rail or the centre to attach it: that zone lights up and the
rail marks where the task goes.

### Use the keyboard

++f6++ moves the keyboard focus to the next region - the rail, the centre, the column - and ++shift+f6++ back. In the
rail, ++up++ and ++down++ move between items, ++home++ and ++end++ to the first and the last, ++enter++ or ++space++
shows the item, and the menu key opens its menu. Moving onto a hidden task scrolls it into view.

## Controls

```controls
window: Rail
controls:
  - name: Home
    kind: button
    default: ""
    effect: Shows Home, or the open project's page while a project is open.
    note: "**View > Home** does the same."
  - name: Workspace 1
    kind: button
    default: ""
    effect: Shows the workspace and its image views, with the readout beside them and the layout icons in the top bar.
    range: One item for each workspace, by its name. With several, each carries its number; one in a window of its own carries a window mark instead.
    enabled: With no image open and one workspace, a click does nothing.
    note: Its menu - a right-click, a long press or the menu key - names, moves, arranges, locks and closes the workspace.
  - name: Integration
    kind: button
    default: ""
    effect: Shows the Integration hub, to start Image Integration or Image Integration Lite, run one step on a list of frames, or reopen an integration job.
    note: "**View > Integration** does the same."
  - name: Session Console
    kind: button
    default: ""
    effect: Opens the Session Console's drawer above the status line, or closes it.
    range: Its count is the warnings and errors of the session you have not looked at yet.
    note: "**View > Session Console** and the status line's **Console** do the same."
  - name: Settings
    kind: button
    default: ""
    effect: Opens Settings, as **Edit > Settings...** and ++ctrl+comma++ do.
```

## When it goes wrong

Clicking the workspace does nothing
:   **Cause.** No image is open and there is only one workspace.
:   **Fix.** Open an image from Home, with **Open file** or ++ctrl+o++.

A task seems to be missing from the rail
:   **Cause.** More tasks are open than the rail has room for, and the last row counts the ones hidden.
:   **Fix.** Click the count for the task list, or scroll over the rail with the mouse wheel.

A task's item carries a red cross
:   **Cause.** Its run failed while another place was shown.
:   **Fix.** Click the item to show the task, and read its message and the Session Console.

A task's **Close** is greyed
:   **Cause.** The task is still running.
:   **Fix.** Wait for the run to end; the item's ring shows how far it is.

Dragging a task's item changed nothing
:   **Cause.** The button was released inside the main window; a task floats only when dragged past its edge.
:   **Fix.** Drag the item out of the window, or choose **Float as Window** in its menu.

## Related

- [The Main Window](overview.md), for the regions around the rail.
- [Home](home.md), the place AfterNight starts on.
- [Workspaces](workspaces.md), for a workspace's menu and the layout icons.
- [Tasks](tasks.md), for a task's page, floating it and attaching it again.
- [Session Console](log-viewer.md), for what the console lists.
- [Keyboard Shortcuts](../reference/keyboard-shortcuts.md), for every key by window.
