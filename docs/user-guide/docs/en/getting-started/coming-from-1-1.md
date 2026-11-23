---
kind: reference
summary: >-
  Where each part of AfterNight 1.1's window went: the dashboard, the toolbars, the status bar, the process windows,
  Image Integration's tabs and Processing window, the menus, the Session Console, Settings and Help.
---
# Coming from 1.1

AfterNight 1.1 had a dashboard, images docked with toolbars of their own, and a window for each process. This version
has one main window: a [rail](../reference/glossary.md#rail) of places down its left, a top bar, a centre that shows
one place or one [task](../reference/glossary.md#task) at a time, a [readout](../reference/glossary.md#readout) beside
the images and a status line along the bottom. Every command, process and setting of 1.1 is still there, under its
name and in its menu, and every key of 1.1 does what it did.

The tables below follow the parts of the 1.1 window in the order you met them. Each row says what the part was, where
it is now, and the page that describes it. [The Main Window](../interface/overview.md) names every part of the new
window.

## Your settings and files

Installing this version over 1.1 keeps your settings, your recent files and the folders AfterNight uses, and
AfterNight starts in the new main window. Your theme and contrast are kept as you chose them. Your accent and surface
tint are kept too, and the Interface page of [Settings](../interface/settings-dialog.md) shows them as the colour
preset they make, or as **Custom**. The density, which 1.1 did not have, starts at **Compact**.
[Appearance](../interface/appearance.md) describes these choices.

## The main window

| In 1.1 | Now | Page |
| --- | --- | --- |
| The dashboard, shown while no image was open | Home, the first place of the rail and the one AfterNight starts on. It has the same quick actions, with **New project** added, the recent files with their search, sort and menu, and a **Continue** row for the image you used last. A new profile sees a welcome in their place. | [Home](../interface/home.md) |
| The dashboard's Settings, User Guide and About buttons | **Settings** at the foot of the rail, and **User Guide** and **Information** at the right of the top bar | [Top Bar and Menus](../interface/top-bar-and-menus.md) |
| The menu bar | The same menus, hidden until you press ++alt++, and all of them under **All menus** in the top bar. **Always show the menu bar** keeps the bar shown. | [Top Bar and Menus](../interface/top-bar-and-menus.md) |
| The main toolbar: Open, Save as, Close | **Open file** on Home, a tab's right-click menu, the **File** menu, and the [command palette](../reference/glossary.md#command-palette), which finds every command by name | [Command Palette](../interface/command-palette.md) |
| The dock toolbar: Lock, Group All, Split All, Fit | The same four buttons, the layout icons, at the right of the top bar while a [workspace](../reference/glossary.md#workspace) shows | [Workspaces](../interface/workspaces.md) |
| The status bar | The status line: the box that chooses the [active image](../reference/glossary.md#active-image), the image's facts and zoom, the sky coordinates, and the **Console** button | [The Main Window](../interface/overview.md) |
| The window's size and place, kept between sessions | Kept the same way. A [project](../reference/glossary.md#project) also keeps its workspaces and the images open in them. | [Projects](../interface/projects.md) |
| The Session Console, a dock with its toolbar | A drawer above the status line, with the same toolbar, which can float in a window of its own. **Session Console** in the rail and **Console** in the status line open it. | [Session Console](../interface/log-viewer.md) |
| Settings, nine pages | The same dialog, from the rail's **Settings**, with a tenth page, **Projects**. The Interface page adds colour presets, tint strength, density and Night. | [Settings](../interface/settings-dialog.md) |
| Help and About | **User Guide** and **Information** in the top bar, and the **Help** and **About** menus. ++f1++ opens this guide, at a process's page on its task. | [Top Bar and Menus](../interface/top-bar-and-menus.md) |

## Images and their views

| In 1.1 | Now | Page |
| --- | --- | --- |
| The dock area that held the image views | A workspace, a place of the rail. There can be several, and one can move to a window of its own. | [Workspaces](../interface/workspaces.md) |
| A view's tab, its buttons, floating and splitting | The same, inside a workspace, with a menu on the tab | [Docking Images](../interface/docking-panels.md) |
| Every image floating, and an empty dock area | A list of the floating images, each with **Dock here** | [Workspaces](../interface/workspaces.md) |
| A view's toolbar: zoom, channel, display mode, [Autostretch](../reference/glossary.md#autostretch) and its settings | The floating toolbar near the foot of the view, with **Linear** and **Autostretch** as two segments | [Image Viewer](../interface/image-viewer.md) |
| A view's **Undo**, **Redo** and **Image History** | The [history](../reference/glossary.md#history) strip under the view, which also shows the steps | [Image History](../interface/image-history.md) |
| A view's Statistics, Histogram, [EXIF](../reference/glossary.md#exif), [FITS](../reference/glossary.md#fits) Header and Summary | The readout's cards, at the right of the workspace, which show the figures at once and open each window | [Readout](../interface/readout.md) |
| A view's astrometry buttons: Show Astrometry, its settings, Astrometry Info, Annotate Image, Plate Solve | **Show Astrometry** on the floating toolbar, and the readout's Astrometry card with its overlays, **Plate Solve**, **Annotate...** and **Info** | [Readout](../interface/readout.md) |
| Maximize View, ++f11++ | The same button on the floating toolbar and the same key. ++esc++ restores the view too. | [Image Viewer](../interface/image-viewer.md) |

## Processes

| In 1.1 | Now | Page |
| --- | --- | --- |
| A process window | A task: a page of the centre with an item in the rail. It can float in a window of its own, from **Float as window** or by dragging its item, and be attached again. | [Tasks](../interface/tasks.md) |
| The window's title, description and message strip | The task header, which names the process and its target, the column's header, and the message strip over the controls | [Tasks](../interface/tasks.md) |
| The controls at the left, the preview at the right | The preview in the canvas at the left, the controls in the column at the right | [Tasks](../interface/tasks.md) |
| The preview's toolbar | The canvas's floating toolbar, and the column's toggles row: **Preview**, **Preview area** and **Processed** | [Tasks](../interface/tasks.md) |
| **Reset**, **Close** and **Apply** | The column's footer, always in view. ++ctrl+enter++ applies, and ++esc++ closes a task that is not running. | [Tasks](../interface/tasks.md) |
| A run's progress and its **Cancel** | A row of the task's column that says how far the run has come, **Cancel** in the footer, and a ring around the task's item in the rail | [Tasks](../interface/tasks.md) |
| Several process windows, each on its image | Several tasks, each with its item in the rail | [The Rail and Its Places](../interface/rail-and-places.md) |
| A view locked while a run writes it | The view names the task that writes it, with **Show** to go to the task | [Tasks](../interface/tasks.md) |
| The Process menu, Favorite Processes and Recent Processes | The same menus, and the command palette, which lists the favourites first | [Command Palette](../interface/command-palette.md) |
| An extension's process with a window of its own | A task too: one that describes its settings shows them in a column, and one that brings its own window keeps it, with an item in the rail | [Tasks Reference](../interface/tasks-reference.md) |

## Image Integration

| In 1.1 | Now | Page |
| --- | --- | --- |
| A window of its own | A task whose page fills the centre, opened from the Integration place, Home's **Image integration** or **Process > Preprocess** | [Image Integration](../processes/stacking-integration/image-integration.md) |
| The Frame Sources and Workflow tabs | One stage list at the page's left, under **Run sheet**: **Bias**, **Darks**, **Flats**, **Lights**, then **Calibration**, **Registration**, **Stacking** and **Summary** | [Image Integration](../processes/stacking-integration/image-integration.md) |
| Its six menus: Frames, Project, Edit, Run, Navigate, Help | The same menus, under the button of the task header, and the buttons at the foot of the stage list | [Image Integration](../processes/stacking-integration/image-integration.md) |
| The Importing files window | A band over the stages, with the same **Pause**, **Resume** and **Cancel** | [Image Integration](../processes/stacking-integration/image-integration.md) |
| The Image Integration - Processing window | The run's view, which takes the place of the stage's page until you close it. The task's item in the rail shows the run's progress, and so does the Integration place. | [Image Integration](../processes/stacking-integration/image-integration.md) |
| A saved session, reopened with the window | An [integration job](../reference/glossary.md#integration-job), listed in the Integration place with how its last run ended | [Integration Hub](../interface/integration-hub.md) |
| Image Integration Lite, Image Calibration, Image Registration, Image Stacking and Image List Analyzer | Tasks whose pages fill the centre, opened from the Integration place or from **Process > Preprocess** | [Integration Hub](../interface/integration-hub.md) |
| Blink Comparator, the registration points and comet tracking | The same windows, from the same buttons | [Image Integration](../processes/stacking-integration/image-integration.md) |

## The keyboard

Every key of 1.1 does what it did, and **Help > User Guide** keeps ++f1++. This version adds ++ctrl+k++ for the command
palette, ++ctrl+shift+n++ for Night, ++ctrl+w++ for **File > Close**, ++ctrl+enter++ and ++esc++ on a task, ++f6++ and
++shift+f6++ to move between the rail, the centre and the column, and ++alt++ for the menu bar.
[Keyboard Shortcuts](../reference/keyboard-shortcuts.md) lists them all.

## What is new

| Part | What it does | Page |
| --- | --- | --- |
| The command palette | Finds every command, process, image and setting by name, from ++ctrl+k++ or the top bar's field | [Command Palette](../interface/command-palette.md) |
| The readout | Reads the active image beside the workspace: its histogram, statistics, astrometry and facts | [Readout](../interface/readout.md) |
| The history strip | Shows an image's steps under its view, and a task's step as pending | [Image History](../interface/image-history.md) |
| Tasks | Keep several processes open at once, each with its item in the rail | [Tasks](../interface/tasks.md) |
| Workspaces | Hold different sets of images, each a place of the rail | [Workspaces](../interface/workspaces.md) |
| Projects | Keep a body of work in a folder: its images' histories, its workspaces, its notes and its integration jobs | [Projects](../interface/projects.md) |
| The Integration place | Gathers the ways to stack, the single steps and the integration jobs | [Integration Hub](../interface/integration-hub.md) |
| Presets, tint strength, density and Night | Change how the window looks, and draw it in dim red for an observing night | [Appearance](../interface/appearance.md) |
