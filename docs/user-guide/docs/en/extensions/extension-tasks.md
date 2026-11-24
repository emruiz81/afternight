---
kind: interface
summary: >-
  How an extension's process shows in the main window: a task AfterNight draws, or a window of the extension's own,
  where its processes are listed, what it may do to the image's view, and how it follows AfterNight's look.
---
# Extensions in the Window

An extension's process opens as a [task](../reference/glossary.md#task), as AfterNight's own processes do, while its
code runs in its [extension host](../reference/glossary.md#extension-host). When an extension describes its controls,
AfterNight draws them as the task's page: a hosted task. An extension that brings a window of its own has an item in
the [rail](../reference/glossary.md#rail) that brings that window forward. [Using Extensions](using-extensions.md)
says whether an extension can run.

```figure
image: ../assets/images/extensions/extension-tasks.webp
alt: >-
  The main window showing HDR Multiscale, an extension's process, as a task. In the rail at the left edge, the task's
  item under Home, the workspace and the Integration place: a small picture of the image with the process's mark. The
  task header reads HDR Multiscale on RGB, preview up to date, with Float as window and Close at its right. Most of
  the window is the canvas, labelled PREVIEW, with the dark preview of the Carina Nebula. At the right, the column:
  the compact readout, Adjustments over HDR Multiscale and the small tag extension, Target Image, a note on the
  process, the Parameters card the extension describes, and Reset, Close and Apply at the foot.
caption: >-
  HDR Multiscale, an extension's process, as a task AfterNight draws, at its defaults on the linear master: the
  preview in the canvas at the left, the extension's controls in the column at the right.
pins:
  - {n: 1, x: 0.8, y: 24.6}
  - {n: 2, x: 3.8, y: 9.3}
  - {n: 3, x: 5.0, y: 16.0}
  - {n: 4, x: 58.9, y: 30.4}
  - {n: 5, x: 58.9, y: 55.0}
  - {n: 6, x: 59.6, y: 94.3}
```

## What you see

1. **The task in the rail.** The target's thumbnail with the process's mark, its run and how it ended.
2. **The task header.** The process "on" its image, where the task stands, **Float as window** and **Close**.
3. **The canvas.** The extension's preview, or the image itself when the extension does not preview, under the shapes
   the extension draws.
4. **The column's header.** The menu the process sits in, over its name and the tag **extension**, whose tooltip
   reads "Runs in its own process". The name's tooltip is what the process does, with the credits the extension
   gives.
5. **The extension's controls.** What the extension describes, drawn by AfterNight.
6. **The footer.** **Reset**, **Close** and **Apply**, as in every task.

## What you can do

### Find an extension's processes

An extension's process carries the tag **extension** wherever it is listed:

- In the **Process** menu, after a line at the end of the category its package names. A process that runs with no
  window has no category there.
- In the extension's own menu under **Extensions**, with **Manage...** and, when the extension names them, its
  **Homepage**, **Repository** and **Support**.
- In the [command palette](../reference/glossary.md#command-palette), under the extension's name.

Once started, it is among **Process > Recent Processes**, and the palette's star makes it a favourite. A process that
cannot run now is greyed in the menus and dimmed in the palette, with the reason.

### Work in a hosted task

1. Make the image the [active image](../reference/glossary.md#active-image) and choose the process. The task opens at
   once, and its column says **Starting** until the extension's host is ready.
2. Change the controls. The preview follows, as in any task.
3. Choose **Apply**, or press ++ctrl+enter++. The result becomes one step of the image's
   [history](../reference/glossary.md#history), and the image is locked during the run.
4. A task the extension keeps open returns its controls to their defaults for another try. Any other closes once the
   result is shown.

The rest is as [Tasks](../interface/tasks.md) says. An extension can also put its task away, which leaves it in the
rail, or close it. ++f1++ opens this guide's home page, since an extension's process has no page in it.

### Use an extension with a window of its own

An extension may show a window of its own: the Full host's process window, or one it builds with a toolkit of its
own. Its item in the rail adds "opens in its own window" to its name. A click brings the window forward, and its menu
holds **Show** and **Close**: it neither floats nor attaches. Its run turns a ring around the item and holds no lock
on the image. When its host fails, the item stays, marked failed, until you close it. An extension that shows no
window is listed the same way, with its progress.

### Use the processes an extension added

An extension that keeps a catalogue of its own, such as scripts or models you enable in it, can add processes while it
runs. Each is listed in the extension's menu under **Extensions**, after its own entries, in a submenu for its group
when it names one. It is in the **Process** menu when it names a category, and in the palette under the extension's
name and its group. AfterNight keeps the list for the next start, and each can join the recent and favourite
processes. **Processes It Added**, in the package's details on the **Browse** tab, counts them.

### Know what an extension does to the image's view

An extension's session is bound to the task's target. Beyond reading it and handing back a result, it may:

- **Draw shapes** over the view, lines and outlines with labels. They follow the zoom and the scroll and take no
  click.
- **Turn on the selection area** of the view, or let your next drag draw it. It reads the area when you let go.
- **Set how the view is shown**: its zoom, a point to centre, fit to window, **Linear** or **Autostretch**.
- **Ask for Undo or Redo**, as the history strip's buttons do.

In a hosted task, the shapes and the display are the canvas's. An extension may not act on another view or clear a
history. Its shapes are in no saved image or [project](../reference/glossary.md#project), and what it left goes when
its session ends.

### See an extension in AfterNight's look

A Full host's window is drawn as AfterNight's own windows are, in the theme, accent, surface tint, density and Night in
force, and follows each change you make in [Appearance](../interface/appearance.md) at once. An extension that draws
its window with another toolkit can read the same colours and sizes, and uses them as its author chose.

### Reach the Extensions window and the settings

- **Extensions > Manage Extensions...**, its row in the palette and **More actions > Extension Manager** on
  [Home](../interface/home.md) open the Extensions window on **Browse**, and **Manage...** in an extension's menu on
  **Installed**.
- An extension's own settings open from **Settings** on its card of the **Installed** tab.
- **Check for extension updates on startup** is on the **General** page of [Settings](../interface/settings-dialog.md),
  and the preview's rate on **Processing**.

## Controls

```controls
window: Starting
controls:
  - name: Starting
    kind: readout
    default: ""
    effect: >-
      "Starting" and the process's name beside a turning arc, then a line a step: what the preparation of the
      extension's environment does on a first start, "Its host has started" and "The extension is loaded".
    enabled: >-
      Heads the column's body of a hosted task until its host is ready. The controls AfterNight kept from its last
      reading of the extension are drawn at once, when it has them, and stay disabled with **Reset** and **Apply** until
      then.
    note: Closing the task while a first start prepares the environment stops the preparation.
```

```controls
window: A host that stopped
controls:
  - name: The extension stopped responding
    kind: readout
    default: ""
    effect: >-
      In the task's message strip, in the error tone, with the reason, once its host crashed or stopped answering. The
      controls are disabled and the run is marked failed, which the task's item in the rail shows while another place
      is shown.
  - name: Restart
    kind: button
    default: ""
    effect: >-
      Starts the extension's host again in the same task, which says **Starting** until it is ready. When it cannot,
      the strip gives the reason, or "The extension could not be started again."
  - name: Close
    kind: button
    default: ""
    effect: Closes the task.
```

```controls
window: A window of its own, in the rail
controls:
  - name: Show
    kind: action
    default: ""
    effect: Asks the extension's host to bring its window in front. A click on the item does the same.
    enabled: >-
      Once the host is ready, when it offers to raise its window. Greyed for an extension that shows no window, and
      once its host failed.
  - name: Close
    kind: action
    default: ""
    effect: >-
      Ends the extension's session: its host is told to close, and the item goes. A failed item is taken away.
    enabled: Greyed while a run in the window goes on. Closing the window itself ends the session too.
```

## When it goes wrong

A process of an extension is greyed in the menus, or dimmed in the palette
:   **Cause.** It is still loading, its package cannot run, or it does not fit the active image.
:   **Fix.** Read its tooltip or the reason beside it, and the package's card in
    [Using Extensions](using-extensions.md).

The task says **Starting** for a long time
:   **Cause.** A first start prepares the extension's environment, which can take minutes.
:   **Fix.** Wait, and read the lines under **Starting**. Closing the task stops the preparation.

"The extension stopped responding"
:   **Cause.** Its host crashed or stopped answering, and AfterNight stopped it.
:   **Fix.** Choose **Restart**. When it happens again, read **Last Crash / Termination** on the **Installed** tab.

Clicking an extension's item in the rail does nothing
:   **Cause.** Its host is still starting, the extension shows no window, or its host failed.
:   **Fix.** Wait for the host, or close a failed item from its menu.

The task closed after **Apply**
:   **Cause.** The extension does not keep its task open after a run.
:   **Fix.** Choose the process again for another try.

An extension cannot turn on the selection area
:   **Cause.** Another process holds the view's selection area, such as Crop.
:   **Fix.** Close that task, then try again in the extension.

## Related

- [Tasks](../interface/tasks.md), for what every task shares: the canvas, the column, floating and closing.
- [Using Extensions](using-extensions.md), to check that an extension can run, and for its settings and environment.
- [Using Extensions Reference](using-extensions-reference.md), for who draws a process, what an extension may ask of
  its view, and the processes it adds.
- [The Rail and Its Places](../interface/rail-and-places.md), for a task's item and its menu.
- [Top Bar and Menus](../interface/top-bar-and-menus.md), for the **Extensions** and **Process** menus.
- [Command Palette](../interface/command-palette.md), for the tag and the favourites.
