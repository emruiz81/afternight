---
kind: interface
summary: >-
  A project keeps a body of work in one folder: its images where they are, their undo histories, its workspaces, its
  notes and its integration runs, all put back when you open it again.
---
# Projects

A [project](../reference/glossary.md#project) is a folder that keeps a body of work together. Its images stay where they are on disk: the project refers
to them, and keeps the undo [history](../reference/glossary.md#history) of each, the [workspaces](../reference/glossary.md#workspace) and the images open in
them, your notes and its integration runs. Open it again another night and the window comes back as you left it. A
project is optional: without one, a history lasts only while AfterNight runs.

The folder holds `project.anproj`, which describes the project, the folders `history`, `workspaces`, `integration`
and `thumbnails`, and `notes.md`. While a project is open, **Home** shows its page, Project Home.

```figure
owed: true
image: ../assets/images/interface/projects.webp
alt: >-
  The main window showing Project Home. The top bar reads Carina Nebula beside a folder icon, then Home. Under the
  caps word Project, a small picture of the nebula, the heading Carina Nebula with a tag reading NGC 3372 and the words
  object, as it was named, then the project's folder, Created and the date, and 0.4 GB. A row of buttons: Continue -
  Workspace 1, Add images..., New integration..., Project settings..., and Close project at the far right. Below, on
  the left, a Workspaces card with one tile, a miniature of the nebula, Workspace 1 and the tag last used, with Continue
  and Rename...; under it an Images and sequences card with one row, RGB.fit, FITS, 7246 x 5429, 1 step - 0.4 GB, Open
  in Workspace 1 and a date. On the right, an Integrations card reading No integration in this project yet, a Storage
  card with a bar, a legend of five lines and the buttons Compact history... and a greyed Clean intermediate files...,
  and a Notes card with an empty text box.
caption: >-
  Project Home of a new project, Carina Nebula, made with the image that was open.
pins:
  - {n: 1, x: 3.4, y: 5.9}
  - {n: 2, x: 4.6, y: 14.0}
  - {n: 3, x: 4.6, y: 21.2}
  - {n: 4, x: 4.6, y: 26.0}
  - {n: 5, x: 4.6, y: 46.4}
  - {n: 6, x: 70.6, y: 26.0}
  - {n: 7, x: 70.6, y: 34.3}
  - {n: 8, x: 70.6, y: 61.8}
```

## What you see

1. **The project switcher.** The open project's name, or **No project**. Its menu lists the open project, ticked, the
   projects used last, then **New project...**, **Open project...** and **Close project**.
2. **The header.** The picture of the image opened last, the name, the object, the folder, the day the project was
   made and the room it takes.
3. **The actions.** **Continue**, **Add images...**, **New integration...**, **Project settings...** and **Close
   project**.
4. **Workspaces.** A tile for each workspace, with a miniature of its arrangement.
5. **Images and sequences.** A row for each image: its name, type and size, its history's steps, where it is open
   and when its file last changed.
6. **Integrations.** The project's [integration jobs](../reference/glossary.md#integration-job), each with its status and **Open**.
7. **Storage.** What the folder holds, as a bar and its legend.
8. **Notes.** Your notes, kept in the folder as `notes.md`.

## What you can do

### Make a project

1. Choose **File > New Project...**, **New project** on [Home](home.md), or **New project...** in the switcher's menu.
2. Type a **Name**, which the project's folder takes, and check the folder the line under **Location** shows.
3. Type the **Object** if you like, and choose a **History compression**.
4. With images open, keep **Add the open image to the project** ticked to bring them in, and **Move its undo history
   into the project** to bring their steps. With several images, the boxes say how many.
5. Choose **Create project**. The project opens, and its name shows in the top bar.

The folder must not exist yet, or be empty. Moving a history to another drive copies it, and a message says how far
the move is.

```figure
owed: true
image: ../assets/images/interface/projects-new.webp
alt: >-
  The New Project dialog. Name reads Carina Nebula. Location holds a long folder path with a folder button, and under
  it a line beginning The project folder will be, then the path ending in Carina Nebula. Object (optional) reads NGC
  3372. History compression shows three segments, LZ4 (fast) chosen, None and Zstandard (smallest), over a line saying
  history steps are XISF images that open in other programs. Two ticked boxes read Add the open image to the project
  and Move its undo history into the project, with a size. A framed note says images stay where they are. Cancel and
  Create project close the dialog.
caption: >-
  The New Project dialog filled in for Carina Nebula, with the open image and its undo history added to it.
```

### Work in a project

While a project is open:

- An image you open from a file joins it, and its steps are written into the project's folder, where they stay
  when you close the view. Open the file again in the project, and its history comes back.
- An image you save joins it, so a result that was never a file joins with its first save. Saved under another
  name, it keeps its history.
- A [sequence](../reference/glossary.md#sequence) joins it, and keeps its history with the session.
- An image open before the project opened joins once you save it. A temporary file never joins.
- The [masters](../reference/glossary.md#master) a successful integration run makes join it.

A file changed outside AfterNight is not given its old history: the project sets that history aside and says so.

### Switch or close

Choose another project in the switcher's menu, **File > Switch Project...** to open that menu, or **File > Open
Project...** to choose a project's folder. The open project is left only once the other one is sure to open.

Leaving a project, by **File > Close Project**, its page's **Close project** or another project, keeps its workspaces
and closes its images, each asked about as **File > Close** asks. Keep one open, and the project stays open. Images
that are not the project's stay open.

### Open a project again

Choose it in the switcher's menu or on the **Projects** row of Home, or choose its folder with **File > Open
Project...**. Its workspaces come back with their names, their locks and their images, and the one used last is shown.
**Open the last project on startup**, in [Settings](settings-dialog.md), opens it as AfterNight starts.

An image whose file is not found is left out, and one message names them all. **Locate...** asks where each file is
now, and the image opens where it was. **Remove from the project** forgets them, and leaves their files as they are.

### Free room on disk

Under **Storage**, **Compact history...** drops the older steps of the histories, and **Clean intermediate files...**
the frames a run can make again from the lights. A project whose undo history takes more than **Ask to compact a
history above**, in Settings, offers to compact it once, as it opens.

### Let an integration run make its project

With no project open, an integration run gets a project in its output folder, named after the folder, while **Create
a project for every integration job** is on in Settings. A folder that is a project already is opened instead, and a
message says which. Nothing the run wrote is moved.

### Open a project held by another session

While a project is open its folder holds a lock. A lock that a crashed session left on this computer is taken without
a question. One held from another computer, or by another user, stops the project from opening, and the message names
who holds it. Choose **Open anyway** only when that session is closed.

## Controls

### New Project

```controls
window: New Project
controls:
  - name: Name
    kind: field
    default: ""
    effect: The project's name, which its folder takes.
    range: >-
      Not empty, no blank at either end, no dot at its end, none of `< > : " / \ | ? *`, no name Windows reserves such
      as `CON` or `NUL`, and at most 128 characters. Any other name gives "A project's name is its folder's: it cannot
      hold < > : " / \ | ? * nor end with a dot."
  - name: Location
    kind: field
    default: The default location of Settings > Projects
    effect: >-
      The folder the project's folder is made in. Its folder button opens "Select the Folder the Project Is Made In".
    range: A folder's full path. Any other text gives "The location must be a folder's full path."
  - name: The project folder will be
    kind: readout
    default: ""
    effect: The folder that will be made, the name under the location, cut in its middle when long.
  - name: Object (optional)
    kind: field
    default: ""
    effect: >-
      The object the project is about, such as `NGC 3372`. Project Home shows it as a tag with "object, as it was
      named".
  - name: History compression
    kind: radio
    default: LZ4 (fast)
    options: [LZ4 (fast), None, Zstandard (smallest)]
    effect: >-
      How the steps of the project's histories are stored. Each step is an [XISF](../reference/glossary.md#xisf)
      image that opens in other programs, as the line under it says.
    tradeoff: Zstandard takes the least room and is the slowest to write; None takes the most.
    persistence: It starts at **History compression:** of Settings > Projects.
  - name: Add the open image to the project
    kind: check
    default: "On"
    effect: >-
      Brings the open images that show a file into the project, as images opened in it. It reads "Add the 3 open
      images to the project" for several.
    enabled: >-
      Shown while such images are open. An image of a project open now and a temporary file are not counted.
  - name: Move its undo history into the project
    kind: check
    default: "On"
    effect: >-
      Moves the steps those images have into the project, with their size, such as "(349.3 MB)". Off, the steps stay
      with the session and the project's history of each image starts from its next step.
    enabled: While **Add the open image to the project** is ticked. Unticking that box unticks this one.
    tradeoff: Across drives the steps are copied, which takes a while for a large history.
  - name: Message
    kind: readout
    default: ""
    effect: >-
      A line over the buttons. It reads "This folder holds files and is not a project. Choose another name or another
      location." or the problems above, and "This folder is a project already. You can open it." when it is one.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and makes nothing.
  - name: Create project
    kind: button
    default: ""
    effect: Makes the project's folder and opens the project. It reads **Open project** for a folder that is a project already, and opens it.
    enabled: Once the name and the location are good and the folder is free, empty or a project.
```

### Project Home

```controls
window: Project Home
controls:
  - name: Show in File Browser
    kind: toolbutton
    default: ""
    effect: Shows the project's folder in your system's file browser.
  - name: Size
    kind: readout
    default: Measuring...
    effect: The room the project's folder takes, measured in the background as the page opens.
  - name: Continue
    kind: button
    default: ""
    effect: Shows the workspace used last. It reads its name, such as "Continue · Workspace 1".
  - name: Add images...
    kind: button
    default: ""
    effect: Opens files as **File > Open** does. Each one joins the project.
  - name: New integration...
    kind: button
    default: ""
    effect: >-
      Opens Image Integration. Opened while a project is open, it keeps its sessions in the project's `integration`
      folder.
  - name: Project settings...
    kind: button
    default: ""
    effect: Opens Settings on its Projects page, as **File > Project Settings...** does.
  - name: Close project
    kind: button
    default: ""
    effect: Closes the project and its images, as **File > Close Project** does.
  - name: Open
    object: projectHomeWorkspaceOpen
    kind: button
    default: ""
    effect: Shows the tile's workspace. The tile of the workspace used last reads **Continue**.
    note: The tile's line says its images and arrangement, such as "2 images · 2 × 1 split".
  - name: Rename...
    kind: button
    default: ""
    effect: Renames the tile's workspace.
  - name: Open
    object: projectHomeIntegrationOpen
    kind: button
    default: ""
    effect: Opens Image Integration on that job's session.
    note: >-
      The row reads the job's name, its status tag, and its frames, masters, last run and how long it took, such as
      "718 frames · 4 masters · 2026-07-27 23:54 · 51m 0s".
  - name: Storage
    kind: readout
    default: ""
    effect: >-
      The bar and its legend: **Undo history** with its steps, **Masters and stacked images**, **Calibrated and
      registered frames** with "can be made again from the lights", **Reports, layouts, notes**, and what is free on
      the folder's drive.
    range: The histories set aside count with the undo history.
  - name: Compact history...
    kind: button
    default: ""
    effect: Measures each image's history, then opens **Compact History**.
    enabled: While the project keeps some undo history.
  - name: Clean intermediate files...
    kind: button
    default: ""
    effect: >-
      Asks "Remove the calibrated, corrected and registered frames of this project's integration jobs?", with what it
      frees, and on **Clean** removes them. A message then says "Intermediate files cleaned" and what was freed.
    enabled: >-
      While the storage counts such frames. During an integration run it says "An integration is running" and removes
      nothing.
    tradeoff: The next run that needs those frames makes them again from the lights, which takes time.
  - name: Notes
    kind: field
    default: ""
    effect: Your notes, kept in the project's folder as `notes.md`, which any text editor opens.
    persistence: Written a moment after the last keystroke, and when you leave the project.
```

### Compact History

```controls
window: Compact History
controls:
  - name: Keep
    kind: radio
    default: The last
    options: [The last, Only the current state]
    effect: >-
      **The last** keeps the last steps of each image's history, as many as **Steps** says, and removes the older ones;
      **Only the current state** keeps the state each image shows now and removes every other step.
  - name: Steps
    kind: spinbox
    default: "10"
    widget: {min: 1, max: 999, step: 1}
    effect: How many steps **The last** keeps, with the suffix `steps`.
  - name: Remove the histories set aside too
    kind: check
    default: "Off"
    effect: Also removes the histories set aside when a file changed outside AfterNight, whose size it gives.
    enabled: Shown while the project keeps some.
  - name: It frees
    kind: readout
    default: Measuring what it frees...
    effect: What the choice frees, such as "It frees 1.2 GB.", measured again whenever the choice changes.
  - name: Compact
    kind: button
    default: ""
    effect: >-
      Removes the files of the steps it does not keep and rewrites each history's list of steps. A message then says
      "The undo history was compacted" and what was freed.
    tradeoff: It cannot be undone, as the warning over the buttons says.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the dialog and removes nothing.
```

## When it goes wrong

"The folder '...' holds files and is not a project. Choose another name or another location."
:   **Cause.** The name and the location point at a folder that already holds other files.
:   **Fix.** Choose another name, or another location.

"The project '...' is open in ... on ..."
:   **Cause.** Another session, on that computer or by another user, holds the project's lock.
:   **Fix.** Close the project there. If that session is gone for good, choose **Open anyway**.

"An image of this project was not found and was not opened."
:   **Cause.** Its file was moved or renamed, or its drive is not connected.
:   **Fix.** Connect the drive and open the project again, or choose **Locate...** and select the file.

"This project was written by a newer AfterNight"
:   **Cause.** A later version of AfterNight wrote the project's document.
:   **Fix.** Open the project with that version.

The project stays open after **Close project**
:   **Cause.** You kept one of its images open when asked, or its images are still opening.
:   **Fix.** Save or close that image, or wait for the images, and close the project again.

**Clean intermediate files...** is greyed
:   **Cause.** The project's jobs keep no frame that a run can make again.
:   **Fix.** Nothing to clean. **Compact history...** frees the room an undo history takes.

## Related

- [Home](home.md), for the projects used last and the page Home becomes while one is open.
- [Workspaces](workspaces.md), for the workspaces a project keeps.
- [Image History](image-history.md), for the steps a project keeps of each image.
- [Integration Hub](integration-hub.md), for the integration jobs of a project.
- [Image Integration](../processes/stacking-integration/image-integration.md), whose runs a project records.
- [Settings](settings-dialog.md), for the Projects page.
