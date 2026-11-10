---
kind: process
stage: stack
menu: Process > Preprocess > Image Integration
summary: >-
  Builds the calibration masters, then calibrates, registers and stacks the light frames of one or many targets in a
  project that remembers its work, and writes one linear master for each group of lights.
data_in: linear
data_out: linear
destructive: false
preview: static
sequences: false
masks: none
external: >-
  Optional: a plate solver set up in the settings, for Auto-solve unsolved frames, Mosaic alignment and Plate-Solve
  Final Master If Needed, and a Gaia catalog for Enrich with Gaia catalog
glance:
  enabled: >-
    Always, from the menu or from the Image Integration card of the Dashboard. The window opens without an image and
    restores its last project. Execute Integration needs a light frame and an output directory it can write to.
  inputs: >-
    Still image files in a format AfterNight reads: light frames, and any bias, dark and flat frames or masters made
    beforehand. No open image, mask, sequence or video is used.
  outputs: >-
    Files in the output directory: the calibration masters, the registered lights, one 32-bit floating-point FITS
    master for each integration group, optional maps, and a folder for each run with its report and log. The frames
    are never changed, and the masters open when the run ends.
  state: >-
    Raw linear frames as the camera wrote them, grayscale or a color-filter mosaic, of one or several targets, filters
    and nights.
derived: true
controls_ignore:
  - {label: Add Bias..., reason: "The same action under Add Folder... (This Folder Only) and Add Folder... (Recursive), which the Add Files... entry describes."}
  - {label: Add Darks..., reason: "The same action under both Add Folder... submenus, which the Add Files... entry describes."}
  - {label: Add Flats..., reason: "The same action under both Add Folder... submenus, which the Add Files... entry describes."}
  - {label: Add Lights..., reason: "The same action under both Add Folder... submenus, which the Add Files... entry describes."}
  - {label: Auto-sort by file type..., reason: "The same action under both Add Folder... submenus, which the Add Files... entry describes."}
  - {label: (No recent projects), reason: "A disabled line of Open Recent while it lists no project; the Import Project... entry describes Open Recent."}
  - {label: No presets saved, reason: "A disabled line of Load Preset... and Delete Preset... while no preset exists; the Save Preset... entry describes both."}
  - {label: Add Files..., reason: "The same button on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: Add Folder, reason: "The same button on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: This Folder Only..., reason: "The same item on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: Recursive..., reason: "The same item on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: Remove Selected, object: actionRemoveSelected, reason: "The same button on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: Remove All, reason: "The same button on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: Darks, object: frameTreeSearch, reason: "The search box of the Darks tab, the same as the Bias tab's."}
  - {label: Flats, object: frameTreeSearch, reason: "The search box of the Flats tab, the same as the Bias tab's."}
  - {label: Lights, object: frameTreeSearch, reason: "The search box of the Lights tab, the same as the Bias tab's."}
  - {label: Display, reason: "The same choice on the Darks, Flats and Lights tabs, which the Bias block describes."}
  - {label: Filter, object: chkFilter, reason: "Hidden on the Bias and Darks tabs, whose frames are not grouped by filter."}
  - {label: Object, object: chkObject, reason: "Hidden on the Bias and Darks tabs, whose frames are not grouped by object."}
  - {label: Session, object: chkSession, reason: "Hidden on the Bias and Darks tabs, whose frames are not grouped by session."}
  - {label: Exposure, object: chkExposure, reason: "Hidden on the Bias and Flats tabs, whose frames are not grouped by exposure."}
  - {label: Tol (s), object: spinExpTolerance, reason: "Hidden with Exposure on the Bias and Flats tabs."}
  - {label: Temperature, object: chkTemperature, reason: "Hidden on every tab but Darks."}
  - {label: Tol (C), object: spinTempTolerance, reason: "Hidden with Temperature on every tab but Darks."}
  - {label: Enable minimum composite score filter, object: chkMinScore, reason: "The check box without text before Score ≥, which switches that filter on; the Score ≥ entry says so."}
  - {label: Score ≥, object: chkMinStars, reason: "The check box without text before Stars ≥, which the inventory names after the label before it; the Stars ≥ entry says so."}
  - {label: Stars ≥, object: chkMinSnr, reason: "The check box without text before SNR ≥, which the inventory names after the label before it; the SNR ≥ entry says so."}
  - {label: Enable maximum FWHM filter, object: chkMaxFwhm, reason: "The check box without text before FWHM ≤, which switches that filter on; the FWHM ≤ entry says so."}
  - {label: FWHM ≤, object: chkMaxEccentricity, reason: "The check box without text before Ecc ≤, which the inventory names after the label before it; the Ecc ≤ entry says so."}
  - {label: Go to, reason: "One button for each issue of the Summary that has a place to fix it; the Issues readout describes it."}
rewritten: true
---
# Image Integration

Image Integration turns the frames of one night, or of many, into stacked
[masters](../../reference/glossary.md#master). You add [light frames](../../reference/glossary.md#light-frame) and
any [bias frames](../../reference/glossary.md#bias-frame), [dark frames](../../reference/glossary.md#dark-frame) and
[flat frames](../../reference/glossary.md#flat-frame), and the window sorts them into groups by what their headers
say. It matches calibration masters to each group of lights, and lets you choose the registration and the stacking of
each group.

**Execute Integration** stacks the bias, dark and flat masters, calibrates and registers the lights, and stacks each
[integration group](../../reference/glossary.md#integration-group) into one linear 32-bit floating-point
[FITS](../../reference/glossary.md#fits) master. Everything is written into the output directory and your frames are
never changed. The window keeps the project for next time, and a later run reuses whatever still matches.

```figure
image: ../../assets/images/processes/stacking-integration/image-integration-stacking.png
alt: >-
  The Image Integration window on its Stacking tab. Above it, the menus Frames, Project, Edit, Run, Navigate and Help,
  the tabs Frame Sources and Workflow, and the tabs Calibration, Registration, Stacking and Summary. The Integration
  Groups table has two rows, one for the object NGC253 and one for the object Target, with their frame counts, total
  time, Average, Winsorized Sigma Clipping, Global Normalization and the name of each output file. Under it, the
  Frame Quality panel shows the head of its table for the first group, with the columns Frame, Score, Stars, FWHM,
  SNR, Eccentricity and Weight, then the Execution Options. The Output Directory field, Reset and Close run along the
  bottom.
caption: >-
  The Stacking tab after a run on the NGC 253 set with every setting at its default, the window opened again: one
  integration group for each object name the lights carry, and the measured lights of the first group below.
pins:
  - {n: 2, x: 30.0, y: 12.7}
  - {n: 5, x: 5.0, y: 85.8}
  - {n: 6, x: 12.0, y: 19.0}
  - {n: 7, x: 24.0, y: 19.0}
  - {n: 8, x: 35.5, y: 19.0}
  - {n: 9, x: 47.5, y: 19.0}
```

## Before you start

- Keep the frames where they are. The project records their paths, not their images, and a run reads them again.
- Add the files as the camera wrote them, in a format [Supported Formats](../../getting-started/supported-formats.md)
  lists, and do not [debayer](../../reference/glossary.md#debayer) a color camera's frames: they are calibrated as a
  mosaic. A video, a [SER](../../reference/glossary.md#ser) file or a
  [sequence](../../reference/glossary.md#sequence) is not taken: extract its frames first with
  [Extract Sequence](../utility/extract-sequence.md).
- The headers decide the groups. Frames whose object, filter, exposure or date are written differently land in
  different groups, and each group of lights gives a master of its own.
- Use a new, empty output directory for each data set, on a disk with room for several times your lights. A
  registered light of a color camera holds three 32-bit channels.
- **Auto-solve unsolved frames during Registration**, **Mosaic** alignment and **Plate-Solve Final Master If Needed**
  need a plate solver set up in [Settings](../../interface/settings-dialog.md). **Enrich with Gaia catalog** needs an
  installed [Gaia](../../reference/glossary.md#gaia) catalog.
- For a first run with fewer choices, see [Image Integration Lite](image-integration-lite.md) and the
  [Quick Start](../../getting-started/quick-start.md): they run the same workflow.

## Steps

1. **Open the window.** Choose **Process > Preprocess > Image Integration**, or the **Image Integration** card of the
   Dashboard. No image needs to be open, and the window shows the project you left.
2. **Add the frames.** On each tab of **Frame Sources**, choose **Add Folder** or **Add Files...** for frames of that
   type. **Frames > Add Files... > Auto-sort by file type...** sorts mixed files by their headers instead. A progress
   window reads the headers, and the files appear in the list.
3. **Check the groups.** Choose **Hierarchical** under the list. Each group node names what its frames share, and
   **Group By** under the preview chooses what splits them.
4. **Correct the headers' mistakes.** Right-click frames or a group to change their type, to mark a master or, under
   **Metadata Overrides**, to give them another object or session. Your files are not changed, and the groups are
   formed again.
5. **Choose the output directory.** Type a path in **Output Directory**, or choose **...** beside it. The Summary
   stops reporting the output directory.
6. **Check the calibration.** On **Workflow > Calibration**, read the **Bias**, **Dark** and **Flat** cells of each
   light group. A green check is an exact match, and **Calibration Settings...** sets cosmetic correction and the
   dark's scale.
7. **Choose the registration.** On **Registration**, select a group and choose **Registration Settings**. To see
   each light's figures before the stack, choose **Run Registration**, then leave out poor frames with **Exclude
   Selected**.
8. **Choose the stacking.** On **Stacking**, select each row of **Integration Groups** and choose **Stacking
   Settings...**. Each row names the master it will write.
9. **Read the Summary.** On **Summary**, the card reads **Ready to Execute**, **Ready with Warnings** or **Cannot
   Execute**. **Go to** beside an issue opens the place to fix it.
10. **Execute.** Choose **Execute Integration** or press ++ctrl+enter++. Read the **Execution Plan**, then choose
    **Run**.
11. **Follow the run.** The progress window names each stage and file, and **Cancel** stops at the next safe point.
    A box titled **Complete** reads "Workflow completed successfully." at the end.
12. **Look at the masters.** With **Open integrated files** on, each master opens in its own view, nearly black until
    you choose [Autostretch](../../reference/glossary.md#autostretch). **Open Report** shows the run's report.

## Controls

The window has no preview of a result, no **Apply** and no histogram, and it works on files, never on an open image.
Its menus act on the whole project. **Frame Sources** holds the frames by type, and **Workflow** the settings of the
run, a tab for each stage. The preview beside each frame list is the image viewer: see
[Image Viewer](../../interface/image-viewer.md). Settings that belong to a group are chosen with the group selected,
and the dialogs say which group they edit.

### The menu bar

```controls
window: Add Files...
controls:
  - name: Add Bias...
    kind: action
    default: ""
    effect: Adds the files you pick as bias frames, whatever their headers say. **Add Darks...**, **Add Flats...** and **Add Lights...** do the same for their types.
    scope: The same four items under **Frames > Add Folder... (This Folder Only)** add every supported file of the folder you pick, and under **Frames > Add Folder... (Recursive)** of that folder and every folder under it.
    tradeoff: A wrong type makes wrong groups. Correct it with **Change Type** in the list's menu.
  - name: Add Darks...
    kind: action
    default: ""
    effect: Adds the files you pick as dark frames, whatever their headers say.
  - name: Add Flats...
    kind: action
    default: ""
    effect: Adds the files you pick as flat frames, whatever their headers say.
  - name: Add Lights...
    kind: action
    default: ""
    effect: Adds the files you pick as light frames, whatever their headers say.
  - name: Auto-sort by file type...
    kind: action
    default: ""
    effect: >-
      Adds the files you pick, each with the type its header states. A file whose header states none takes the type
      a folder on its path names, and else the type of the tab shown.
    range: >-
      The header's `IMAGETYP` card decides first, then INDI's `FRAME` card, or an XISF image's type. A value holding
      `OFFSET` names a bias. Folders named `bias`, `biases`, `offset`, `offsets`, `dark`, `darks`, `flat`, `flats`,
      `light` or `lights`, in any case, name a type, the nearest one to the file deciding. A light frame in a folder
      named `darks` stays a light.
    note: ++ctrl+o++ adds files this way, and ++ctrl+shift+o++ a folder and its subfolders. File names are not read.
```

```controls
window: Frames
controls:
  - name: Remove Selected
    kind: action
    default: ""
    effect: Removes the selected frames from the project. The files stay on disk.
    range: Removing more than ten frames asks first, with **Don't ask again for this session**.
    note: ++delete++ does the same. **Edit > Undo** brings them back.
  - name: Remove All Frames...
    kind: action
    default: ""
    effect: Removes every frame of the four tabs from the project, after the question "Are you sure you want to remove all frames?". The files stay on disk.
    note: ++ctrl+shift+delete++ does the same. It can be undone.
  - name: Go to Group...
    kind: action
    default: ""
    effect: Asks for "group name or filter text" and selects the first group of the tab shown whose name holds it.
    note: ++ctrl+g++ does the same. With no match it says "No group matching" and the text.
```

```controls
window: Project
controls:
  - name: Import Project...
    kind: action
    default: ""
    effect: Replaces the project with one saved by **Export Project...**, frames, settings and output directory.
    range: >-
      It reads a JSON project whole. Any other file, or a project holding a value no dialog writes, is refused with
      "The project file could not be read, and nothing was loaded." and the reason, and nothing changes.
    persistence: >-
      **Open Recent** under the same menu lists the last ten projects you imported or exported, kept in the settings
      file, and **Clear Recent Projects** empties it. A project no longer on disk gives "Project file not found" and
      leaves the list.
    note: ++ctrl+i++ does the same.
  - name: Export Project...
    kind: action
    default: ""
    effect: Writes the whole project to a JSON file you name, frames by their paths, every setting and the output directory.
    tradeoff: The project points at your files and does not hold them. Moving the frames breaks it.
    note: ++ctrl+e++ does the same. The file is written whole or not at all, and a failure says "Export Failed".
  - name: Save Preset...
    kind: action
    default: ""
    effect: >-
      Saves the settings under a name you give: the grouping, the matching policy and the calibration chosen by hand,
      the light calibration, registration and stacking settings, and whether calibrated frames go to disk. Frames and
      the output directory are not saved.
    range: >-
      Up to 100 characters, without `< > : " / \ | ? *`. A name already used asks before it overwrites.
    persistence: >-
      **Load Preset...** applies a preset to the project, and can be undone. **Delete Preset...** removes one after a
      question. A preset holding a value no dialog writes, or written in a format this version does not read, is
      refused and left as it is.
    note: The presets are files in the `presets` folder of AfterNight's local data, shared by every project.
  - name: Reset Settings...
    kind: action
    default: ""
    effect: Does what the window's **Reset** does.
```

```controls
window: Edit
controls:
  - name: Undo
    kind: action
    default: ""
    effect: Takes back the last change to the project, and names it.
    range: Up to 100 changes, kept until the window closes. Files a run wrote are not taken back.
    note: The **Keyboard Shortcuts** list names ++ctrl+z++ for it.
  - name: Redo
    kind: action
    default: ""
    effect: Makes again the change **Undo** took back.
    note: The **Keyboard Shortcuts** list names ++ctrl+y++ for it.
```

```controls
window: Run
controls:
  - name: Execute Integration
    kind: action
    default: ""
    effect: Prepares the run and shows its **Execution Plan**, as the Summary's button does.
    enabled: When nothing in the project blocks a run.
    note: ++ctrl+enter++ does the same.
```

```controls
window: Navigate
controls:
  - name: Bias
    kind: action
    default: ""
    effect: Shows the **Bias** tab. ++ctrl+1++ does the same.
  - name: Darks
    kind: action
    default: ""
    effect: Shows the **Darks** tab. ++ctrl+2++ does the same.
  - name: Flats
    kind: action
    default: ""
    effect: Shows the **Flats** tab. ++ctrl+3++ does the same.
  - name: Lights
    kind: action
    default: ""
    effect: Shows the **Lights** tab. ++ctrl+4++ does the same.
  - name: Calibration
    kind: action
    default: ""
    effect: Shows the **Calibration** tab. ++ctrl+5++ does the same.
  - name: Registration
    kind: action
    default: ""
    effect: Shows the **Registration** tab. ++ctrl+6++ does the same.
  - name: Stacking
    kind: action
    default: ""
    effect: Shows the **Stacking** tab. ++ctrl+7++ does the same.
  - name: Summary
    kind: action
    default: ""
    effect: Shows the **Summary** tab. ++ctrl+8++ does the same.
```

```controls
window: Help
controls:
  - name: Keyboard Shortcuts
    kind: action
    default: ""
    effect: Lists the window's keys.
    note: >-
      ++f1++ opens it. Besides the keys of the menus, it lists ++f5++ to refresh the view, ++ctrl+f++ for the search
      box, ++ctrl+a++ to select every frame, ++esc++ to clear the selection, and ++home++ and ++end++ for the first
      and last frame.
```

### Frame Sources

The four tabs share their toolbar, their search box and **Display**, which the **Bias** block describes. A red badge
on a tab counts the issues found there, and its tooltip lists them.

```controls
window: Bias
controls:
  - name: Add Files...
    kind: toolbutton
    default: ""
    effect: Adds the files you pick as frames of this tab's type, whatever their headers say.
    range: >-
      A file already in the project is not added twice. A bias, dark or flat frame whose name, or one of its three
      nearest folders, holds the word "master" is marked as a master, with the source **Auto Detected**.
    note: Dropping files or folders on the list adds them too.
  - name: Add Folder
    kind: toolbutton
    default: ""
    effect: Adds every supported file of the folder you pick, as frames of this tab's type. The arrow beside it offers the two items below.
    note: A folder without one supported file says "No supported image files found in the selected directory."
  - name: This Folder Only...
    kind: action
    default: ""
    effect: Adds the folder's files, as the button does.
  - name: Recursive...
    kind: action
    default: ""
    effect: Adds the files of the folder and of every folder under it.
    tradeoff: A broad scan can bring in frames of other nights and other kinds.
  - name: Remove Selected
    kind: toolbutton
    default: ""
    effect: Removes the selected frames of this tab from the project, as **Frames > Remove Selected** does.
  - name: Remove All
    kind: toolbutton
    default: ""
    effect: Removes every frame of this tab, after a question. The files stay on disk, and **Edit > Undo** brings them back.
  - name: Bias
    object: frameTreeSearch
    kind: field
    default: ""
    effect: >-
      Shows only the frames whose file name, path, filter, object, session, resolution, tag, exposure, gain, offset
      or temperature holds what you type, without regard to case. The box reads **Search frames...** while empty.
    range: Part of a word is enough, as `m83`, `ha`, `-10`, `3008x` or `60s`. A count beside it reads "Showing 12 of 35 frames".
    scope: The list only. A hidden frame is still processed.
    note: ++ctrl+f++ puts the cursor in it.
  - name: Display
    kind: radio
    default: Flat
    options: [Hierarchical, Flat]
    effect: >-
      **Flat** lists every file. **Hierarchical** lists the groups, each with its frames under it, and the frames can
      be dragged from one group to another.
    persistence: Each tab keeps its own choice in the settings file, for the next window too.
  - name: ISO / Gain
    kind: check
    default: "Off"
    effect: Splits the groups by the camera's ISO or gain.
    tradeoff: Safer matching, and more groups with fewer frames each.
    scope: This tab's groups. Each tab has a **Group By** box of its own under the preview, and resolution always splits the groups.
  - name: Offset
    kind: check
    default: "On"
    effect: Splits the groups by the camera's offset too.
    enabled: It counts only while **ISO / Gain** is on.
  - name: Custom Tag
    kind: check
    default: "Off"
    effect: Splits the groups by the tag you give frames with **Metadata Overrides > Set Custom Tag...**, for a difference no header records, such as another optical train.
    persistence: >-
      The grouping is part of the project and of a preset. A change makes the groups again after a moment, and the
      tab may say "Grouping changed for" the type and "per-group settings were reset.", so check the groups' settings
      and assignments.
```

```controls
window: Darks
controls:
  - name: ISO / Gain
    kind: check
    default: "Off"
    effect: Splits the dark groups by ISO or gain.
  - name: Offset
    kind: check
    default: "On"
    effect: Splits them by offset too, while **ISO / Gain** is on.
  - name: Exposure
    kind: check
    default: "On"
    effect: Splits the dark groups by exposure, so each light finds a dark of its own exposure.
    tradeoff: Off stacks the darks of every exposure into one master, which every light then takes.
    note: The matching policy's dark exposure fallback works only while this is on.
  - name: Tol (s)
    kind: spinbox
    default: "3.0"
    widget: {min: 0, max: 36000, step: 0.1, decimals: 1}
    effect: Puts exposures closer than this in one group. 0 groups only equal exposures.
    enabled: While **Exposure** is on.
    tradeoff: Wider groups have more frames, and can mix darks of different thermal signal.
  - name: Custom Tag
    kind: check
    default: "Off"
    effect: Splits the dark groups by tag.
  - name: Temperature
    kind: check
    default: "Off"
    effect: Splits the dark groups by the sensor's temperature.
    note: It forms groups only. The matching of a dark to a light prefers the nearest temperature, and sets no limit on it.
  - name: Tol (C)
    kind: spinbox
    default: "0.0"
    widget: {min: 0, max: 10, step: 0.1, decimals: 1}
    effect: Puts temperatures closer than this many degrees in one group. 0 groups only equal readings.
    enabled: While **Temperature** is on.
```

```controls
window: Flats
controls:
  - name: Filter
    kind: check
    default: "On"
    effect: Splits the flat groups by filter, so no flat corrects a light of another filter.
  - name: Object
    kind: check
    default: "Off"
    effect: Splits the flat groups by object name, and a light group then takes only a flat of its own object.
    tradeoff: Off suits flats taken once for every target of the night.
  - name: Session
    kind: check
    default: "On"
    effect: Splits the flat groups by night, the date of each frame's exposure.
    note: The matching policy's flat session fallback works only while this is on.
  - name: ISO / Gain
    kind: check
    default: "Off"
    effect: Splits the flat groups by ISO or gain.
  - name: Offset
    kind: check
    default: "On"
    effect: Splits them by offset too, while **ISO / Gain** is on.
  - name: Custom Tag
    kind: check
    default: "Off"
    effect: Splits the flat groups by tag.
```

```controls
window: Lights
controls:
  - name: Coverage
    kind: check
    default: "Off"
    effect: Shows only the lights that seem to lack a calibration match.
    scope: The list only.
    persistence: Kept in the settings file, for the next window too.
  - name: Filter
    kind: check
    default: "On"
    effect: Splits the light groups by filter.
    note: The stack always splits its integration groups by resolution, filter and object, whatever this says.
  - name: Object
    kind: check
    default: "On"
    effect: Splits the light groups by object name.
    note: The registration and the stack always split by object, whatever this says. To join two names, give the lights one with **Set Object...**.
  - name: Session
    kind: check
    default: "On"
    effect: Splits the light groups by night, so each night's lights can take that night's flats.
  - name: ISO / Gain
    kind: check
    default: "Off"
    effect: Splits the light groups by ISO or gain.
  - name: Offset
    kind: check
    default: "On"
    effect: Splits them by offset too, while **ISO / Gain** is on.
  - name: Exposure
    kind: check
    default: "Off"
    effect: Splits the light groups by exposure.
  - name: Tol (s)
    kind: spinbox
    default: "0.0"
    widget: {min: 0, max: 36000, step: 0.1, decimals: 1}
    effect: Puts exposures closer than this in one light group.
    enabled: While **Exposure** is on.
  - name: Custom Tag
    kind: check
    default: "Off"
    effect: Splits the light groups by tag.
  - name: Frame Statistics
    kind: readout
    default: No lights loaded.
    effect: Counts the lights - **Total**, **Included**, **Excluded**, **Groups**, **Exposure** and **Size** - around a chart of the included share.
    note: Its tooltip describes each group's filter, object, session, exposure, size, gain, offset, temperature and tag.
  - name: Issues
    kind: readout
    default: No issues detected.
    effect: >-
      Lists up to five kinds of problem, with **Issues (n)** over them: light groups missing a dark, bias or flat
      match, groups using a fallback or a calibration you turned off, groups of one frame, groups of mixed sizes,
      groups with more than half their frames excluded, and how many lights are excluded. A last line counts the kinds
      not shown.
    note: Clicking an issue that names a group shows it in the list. Each issue's tooltip says what it means.
  - name: Include All
    kind: button
    default: ""
    effect: Includes every light in the run.
  - name: Exclude All
    kind: button
    default: ""
    effect: Excludes every light, which leaves nothing to stack.
  - name: Clear Tags
    kind: button
    default: ""
    effect: Removes the custom tags of every light, after a warning. It can be undone.
```

```controls
window: The frame list
controls:
  - name: Frame check box
    kind: check
    default: "On"
    effect: >-
      Includes the frame in the run. A frame you untick is left out of the calibration, the registration and every
      stack, and stays in the project. A group's box ticks or unticks all its frames.
    tradeoff: Fewer frames cost less time and weaken the rejection and the registration.
  - name: Columns
    kind: readout
    default: ""
    effect: Show each frame's **Resolution**, **Filter**, **Object**, **Session**, **ISO/Gain**, **Exposure** and **Tag**, as the headers and your overrides give them.
  - name: Frame menu
    kind: readout
    default: ""
    effect: >-
      A right click on frames offers **Copy File Path**, **Include in Processing** or **Exclude from Processing**,
      **Show in File Browser**, **Show in Calibration Table**, **Blink Selected...** for two frames or more,
      **Remove Selected**, **Change Type** and **Metadata Overrides**. A light adds **Show in Registration Table**,
      **Show in Integration Table** and **Set as Registration Reference**, and a bias, dark or flat **Set as Master**
      or **Unset as Master**.
    note: >-
      **Set as Master** makes the group use that file as its master instead of stacking one. **Change Type** asks
      first, under **Calibration Conflicts**, when the frames are masters that groups rely on.
  - name: Group menu
    kind: readout
    default: ""
    effect: A right click on a group offers **Show in Calibration Table**, **Expand All**, **Collapse All**, including or excluding the group, **Remove Group** and **Metadata Overrides**.
  - name: Metadata Overrides
    kind: readout
    default: ""
    effect: >-
      **Set Session...**, **Set Object...** and **Set Custom Tag...** give the selected frames a value of your own,
      which the groups use instead of the header's. Each has a **Reset** item, and **Reset Overrides for All Frames...**
      clears every session and object override after a question.
    range: An empty session takes the capture date again, and an empty object the header's name.
    persistence: Part of the project. The files are not changed. A change can be undone.
  - name: Drag and drop
    kind: readout
    default: ""
    effect: Dragging frames onto another group in **Hierarchical** gives them that group's object and session, after **Confirm Move** lists the changes.
```

```controls
window: The frame preview
controls:
  - name: Preview
    kind: readout
    default: ""
    effect: >-
      Shows the selected frame. Flats and lights open stretched by Autostretch, bias and darks in Linear, and a
      mosaic is debayered for the display.
    scope: The display only. It never changes what the run computes.
  - name: Calibrated
    kind: check
    default: "Off"
    effect: Shows the selected light calibrated with the masters matched to it and its group's calibration settings. The file is not changed.
    enabled: >-
      On the **Lights** tab, with a light selected whose group is not **Pre-Calibrated**, when at least one bias, dark
      or flat master is matched to it and every one it uses exists on disk. CCD mode also needs a bias beside the
      dark. Otherwise its tooltip says why, as "Missing calibration masters" with what to build.
    tradeoff: The preview can be partial, with the masters that exist. It shows what calibration does, not the run's exact result.
    persistence: Off in every new window.
    note: Its tooltip names the masters used. With a dark, the bias is left out, as in the run.
```

```controls
window: Importing files...
controls:
  - name: Import Progress
    kind: readout
    default: ""
    effect: Shows the file being read, the files done, the time left and "Issues detected" with the last ones.
  - name: Pause
    kind: button
    default: ""
    effect: Pauses the reading, and reads **Resume** until pressed again.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the import. Nothing of it is added.
    note: >-
      At the end, **Import Issues** lists the files that could not be read, and a note says what was added, as
      "Imported: 35 Lights". A second import while one runs says "Another import is already running."
```

### Calibration

Each table holds one row for each group of its type. Selecting a row highlights the rows it takes masters from or
gives them to, in the other tables. A double click on a bias, dark or flat row opens its stacking settings.

```controls
window: BIAS Calibration Groups
controls:
  - name: Bias table
    kind: readout
    default: No bias groups available.
    effect: Lists each bias group with **Frames**, **Master**, **Source**, **Stacking** and **Normalization**.
    range: >-
      **Source** reads **Will Generate** before the run, **Auto Generated** once the run made the master, **Auto
      Detected** for a master found on import and **User Defined** for one you set. A group of one frame shows `-`
      for its stacking.
  - name: Stacking Settings...
    object: btnBiasStackingSettings
    kind: button
    default: ""
    effect: Opens **Stacking Settings** for the selected bias group, with the controls a master uses.
    enabled: With a group selected whose master is made by the run, not one found or set.
  - name: + Add Master
    kind: button
    default: ""
    effect: Adds a bias master made beforehand, a FITS or XISF file, as a frame marked **User Defined**, grouped by its own header.
    note: A file that cannot be read is left out, and nothing says so.
```

```controls
window: DARK Calibration Groups
controls:
  - name: Dark table
    kind: readout
    default: No dark groups available.
    effect: Lists each dark group with **Frames**, **Bias Master**, **Master**, **Source**, **Stacking** and **Normalization**.
    note: >-
      **Bias Master** is always `-`: a master dark is stacked from the darks as they are, and holds the bias. In CCD
      mode the bias is taken from the master dark once, when a light is calibrated.
  - name: Stacking Settings...
    object: btnDarkStackingSettings
    kind: button
    default: ""
    effect: Opens **Stacking Settings** for the selected dark group.
    enabled: With a group selected whose master is made by the run.
  - name: + Add Master
    kind: button
    default: ""
    effect: Adds a dark master made beforehand, as **+ Add Master** of the bias does.
```

```controls
window: FLAT Calibration Groups
controls:
  - name: Flat table
    kind: readout
    default: No flat groups available.
    effect: Lists each flat group with **Frames**, **Bias**, **Dark**, **Master**, **Source**, **Stacking** and **Normalization**.
    range: >-
      A flat takes the dark matched to its own exposure, and then no bias, since the dark holds it. With no such
      dark, its **Dark** cell shows `-` with a warning sign and the bias calibrates it.
    note: A right click on the **Bias** or **Dark** cell chooses the master by hand, as in the light table.
  - name: Stacking Settings...
    object: btnFlatStackingSettings
    kind: button
    default: ""
    effect: Opens **Stacking Settings** for the selected flat group.
    enabled: With a group selected whose master is made by the run.
  - name: + Add Master
    kind: button
    default: ""
    effect: Adds a flat master made beforehand, as **+ Add Master** of the bias does.
```

```controls
window: Light Frame Calibration
controls:
  - name: Light table
    kind: readout
    default: No light groups available.
    effect: >-
      Lists each light group with the **Bias**, **Dark** and **Flat** master it takes, its **Cosmetic**, **Dark Opt**,
      **Flat Norm** and **Pedestal** settings, and **Pre-Calibrated**.
    range: >-
      A green check is an exact match, a blue pencil a master you chose, a yellow sign a fallback or a calibration you
      turned off, and a red mark a missing master or several equal candidates. With a dark, the **Bias** cell shows
      `-`: the dark holds the bias.
    note: Hover over a cell for the master's path, the match and the difference in exposure or days.
  - name: Matching
    kind: toolbutton
    default: ""
    effect: Opens the menu below, the matching policy and the overrides in one place.
  - name: Allow dark exposure fallback
    kind: action
    default: "Off"
    effect: The policy's **Allow dark exposure fallback**.
    note: Each time the menu opens, its items show the policy as it stands.
  - name: Dark exposure tolerance...
    kind: action
    default: ""
    effect: Asks for the policy's **Exposure tolerance**, from 0 to 100 percent. The item shows the value, as "(20%)".
    enabled: The two dark items need the **Exposure** grouping of the darks.
  - name: "Advanced: Subtract bias from darks (CCD sensors)"
    kind: action
    default: "Off"
    effect: The policy's **Subtract bias from dark (CCD mode)**.
  - name: Allow flat session fallback
    kind: action
    default: "Off"
    effect: The policy's **Allow flat session fallback**.
    enabled: The three flat items need the **Session** grouping of the flats.
  - name: Max flat session days apart...
    kind: action
    default: ""
    effect: Asks for the policy's **Max session gap**, from 0 to 365 days. The item shows the value, as "(30)".
  - name: Prefer newer flats
    kind: action
    default: "Off"
    effect: The policy's **Prefer newer flats**.
  - name: Warn on fallback matches
    kind: action
    default: "Off"
    effect: The policy's **Warn on fallback matches**.
  - name: Reset matching policy to defaults
    kind: action
    default: ""
    effect: Returns the policy to its defaults and clears every master you chose by hand, after a question.
  - name: Clear all calibration overrides
    kind: action
    default: ""
    effect: Clears every master you chose by hand for the light groups, after a question.
  - name: Smart Match
    kind: button
    default: ""
    effect: Counts the cells that match well, use a fallback, need a check, are missing or are chosen by hand, selects the first that needs you, and says so. It changes no match.
  - name: Calibration Settings...
    kind: button
    default: ""
    effect: Opens **Calibration Settings** for the selected light group.
    enabled: With a light group selected.
```

```controls
window: The light table's cells
controls:
  - name: Pre-Calibrated
    kind: check
    default: "Off"
    effect: >-
      Marks the group's lights as calibrated already. No master, cosmetic correction, dark scaling, flat
      normalization or pedestal is applied, and no calibration warning is given for it.
    tradeoff: On for raw lights leaves every sensor defect in the master.
    note: The run's report lists the group under **Calibration Summary**, so it is not read as one no master matched.
  - name: Bias, Dark or Flat cell menu
    kind: readout
    default: ""
    effect: >-
      A right click offers **Auto-detect**, which returns to the policy's match, **Browse file...** for a FITS master,
      **Do not use**, which turns that calibration off for the group, and every group of that type with the same
      resolution. **Batch Actions** assigns a master to every group, or to every group of one filter, or clears that
      type's choices.
    tradeoff: The file you browse to is not checked. A wrong master calibrates every frame of the group wrongly.
    note: A double click on the cell opens **Browse file...**. A choice holds for lights with the same header facts, and is part of the project and of a preset.
```

```controls
window: Matching Policy
controls:
  - name: Calibration Coverage
    kind: readout
    default: No calibration frames loaded.
    effect: >-
      Counts the light groups as **Complete**, **Warning**, **Override** and **Incomplete**, around the share that is
      not incomplete. A group needs a flat, and a bias or a dark, or both in CCD mode.
    range: >-
      A fallback is a warning while **Warn on fallback matches** is on, and **Do not use** is always one. A group with
      a master chosen by hand counts as an override. **Pre-Calibrated** groups are not counted.
  - name: Frame Summary
    kind: readout
    default: ""
    effect: Counts the **Bias**, **Darks**, **Flats** and **Lights**, their **Total** and the **Light Groups**.
  - name: Issues
    kind: readout
    default: No calibration issues detected.
    effect: Lists up to five kinds of calibration warning, each with the number of groups it concerns, which its tooltip names.
    note: No warning stops a run. Several equal candidates are marked in the table only.
  - name: Allow flat session fallback
    kind: check
    default: "On"
    effect: Lets a light group take the flat of the nearest night when no flat of its own night exists.
    tradeoff: The nearer night may have other dust or another rotation.
  - name: Max session gap
    kind: spinbox
    default: "30"
    widget: {min: 0, max: 365, step: 1}
    effect: Sets how many days apart that flat may be.
    enabled: While **Allow flat session fallback** is on.
  - name: Prefer newer flats
    kind: check
    default: "Off"
    effect: Takes the newer of two flats equally far away. Off takes the older.
    enabled: While **Allow flat session fallback** is on.
  - name: Allow dark exposure fallback
    kind: check
    default: "On"
    effect: Lets a light group take the dark of the nearest exposure when no dark of its exposure exists.
    tradeoff: The dark's thermal signal then differs from the light's.
  - name: Exposure tolerance
    kind: spinbox
    default: "20"
    widget: {min: 0, max: 100, step: 1}
    effect: Sets how far, as a share of the light's exposure, that dark's exposure may be.
    enabled: While **Allow dark exposure fallback** is on.
  - name: Warn on fallback matches
    kind: check
    default: "On"
    effect: Shows each fallback as a warning. Off still uses the fallback, and marks it as a match.
  - name: Subtract bias from dark (CCD mode)
    kind: check
    default: "Off"
    effect: >-
      Calibrates each light with both its bias and its dark: the bias is taken out of the master dark once, so that
      the dark's scale applies to the thermal signal alone.
    tradeoff: For a sensor whose bias level stays put, usually a CCD. Each light group then needs a bias as well.
    scope: Every light group. A change is applied at once and can be undone, as every policy change can.
```

```controls
window: Quick Actions
controls:
  - name: Clear All Overrides
    kind: button
    default: ""
    effect: Clears every master chosen by hand, for light and flat groups alike, at once.
    enabled: While a master is chosen by hand.
  - name: Reset Matching Policy
    kind: button
    default: ""
    effect: Returns the policy to its defaults and keeps the masters chosen by hand.
  - name: Disable All Fallbacks
    kind: button
    default: ""
    effect: Turns off both fallbacks and their warnings, so a group without an exact match is reported missing.
    note: The three ask nothing, and can be undone.
```

### Calibration Settings

**Calibration Settings...** opens this dialog for one light group. Its title line names the group.

```controls
window: Calibration Settings
controls:
  - name: Enable cosmetic correction
    kind: check
    default: "On"
    effect: >-
      Finds the hot and cold pixels of the master dark, and replaces
      them in each calibrated light with the median of the pixels around them, of the same color in a mosaic.
    tradeoff: A light with no dark has nothing corrected, and the run's warnings say so.
  - name: Cold Sigma (σ)
    kind: spinbox
    default: "3.0"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 1}
    effect: A dark sample this many deviations or more below its channel's median is cold. Lower finds more pixels.
    enabled: While cosmetic correction is on.
    tradeoff: Too low replaces real faint detail. Too high leaves defects.
  - name: Hot Sigma (σ)
    kind: spinbox
    default: "3.0"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 1}
    effect: A dark sample this many deviations or more above its channel's median is hot. Lower finds more pixels.
    enabled: While cosmetic correction is on.
    tradeoff: Too low can touch bright star cores. Too high leaves hot pixels.
  - name: Enable dark optimization
    kind: check
    default: "Off"
    effect: Scales the dark for each light by the factor, from 0 to 2, that leaves the least noise over the light's central 512 × 512 square.
    range: Only a one-channel light of at least 512 × 512 pixels, whose dark varies over that square, is optimized.
    tradeoff: It makes up for a moderate difference of temperature or exposure, and costs time for each light.
  - name: Multiplication Factor
    kind: spinbox
    default: "1.00"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 2}
    effect: Multiplies the dark before it is subtracted, whenever the dark is not optimized.
    range: With optimization on, it scales only the lights the optimization cannot measure, and the run's warnings name their dark once.
  - name: Enable flat auto-level
    kind: check
    default: "On"
    effect: >-
      Divides each channel of the master flat, or each color of a mosaic, by its mean over the central third of the
      frame, so that the light keeps its level and its color balance there.
    tradeoff: Off only for a master flat already normalized to 1.
  - name: Pedestal Value
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 10000, step: 1}
    effect: Adds this many 16-bit codes, divided by 65,535, to every sample of the calibrated light, whatever the light's bit depth.
    tradeoff: It keeps samples off zero after the dark is subtracted, and changes the base level. Nothing is clamped.
  - name: Reset to Defaults
    kind: button
    default: ""
    effect: Returns the dialog's values to their defaults.
  - name: Apply to All Groups
    kind: button
    default: ""
    effect: Gives these values to every light group after a question, closes the dialog, and keeps each group's **Pre-Calibrated**.
  - name: OK
    kind: button
    default: ""
    effect: Gives the values to the group. **Cancel** discards them.
```

### Registration

```controls
window: Registration
controls:
  - name: Registration Groups
    kind: readout
    default: No registration groups available.
    effect: >-
      Lists each registration group with **Frames** as included of
      total, **Filters**, **Exposures**, **Alignment**, **Algorithm**, **Reference** and **Status**.
    range: >-
      A registration group holds the lights of one resolution and object, whatever their filter, so the masters of
      their filters line up. **Status** reads **Complete**, **Partial** or **Pending**.
    note: A double click on a row, or ++enter++, opens its settings. A right click offers **Run Registration** and **Select All Frames in Group**.
  - name: Registration Settings
    kind: button
    default: ""
    effect: Opens **Registration Settings** for the selected group.
  - name: Run Registration
    kind: button
    default: ""
    effect: Registers the selected group alone, after an **Execution Plan**, with the masters it needs. Its lights' figures then fill the table.
    enabled: With a group selected and an output directory set, when the group has work to do. The tooltip says why it is enabled or not.
    note: '**Reprocess existing** on the **Stacking** tab counts here too.'
  - name: Clear Registration...
    kind: button
    default: ""
    effect: >-
      Deletes the group's records from `registration_metrics.db`, its detected stars, figures, transforms and comet
      records, after a question. The registered files are kept.
    enabled: While the database holds something for the group.
    tradeoff: The next run detects and matches every light again. The deletion cannot be undone.
  - name: Frame Quality
    kind: readout
    default: Select a registration group to view frame quality.
    effect: >-
      Lists the group's lights with **Status**, **Frame**, **Filter**, **Stars**, **FWHM** in pixels, **SNR**,
      **Ecc**, a combined **Score** and a **Quality** mark from the quartiles. A star marks the reference, a cross an
      excluded light.
    range: Before a registration the figures read `-`, and a banner says why.
    note: >-
      A right click offers **Show in Lights Tree**, **Set as Registration Reference**, excluding or including, and
      **Preview in Blink Comparator...**. ++delete++ excludes the selected lights.
  - name: Set as Registration Reference
    kind: button
    default: ""
    effect: Stores the selected light as the group's reference.
    enabled: With one included light selected.
    tradeoff: >-
      While **Auto-select** is on in the group's settings, the run picks its own reference and stores it over yours.
      Turn **Auto-select** off to keep yours.
  - name: Exclude Selected
    kind: button
    default: ""
    effect: Excludes the selected lights from the project, as unticking them in the frame list does.
    scope: Every stage and every stack.
  - name: Include Selected
    kind: button
    default: ""
    effect: Includes the selected lights again.
  - name: Reset Excluded...
    kind: button
    default: ""
    effect: Includes every excluded light of the group again, after a question.
  - name: Score ≥
    object: spinMinScore
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1000, step: 1, decimals: 2}
    effect: Fails the lights whose **Score** is lower.
    enabled: With the check box before it ticked. The five filters are under **Quality Filters**, which opens folded.
    persistence: The filters are not saved. A line beside them says how many lights would be excluded.
  - name: Stars ≥
    object: spinMinStars
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 100000, step: 1}
    effect: Fails the lights with fewer stars detected.
    enabled: With the check box before it ticked.
    tradeoff: Can fail narrowband or hazy lights.
  - name: SNR ≥
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 100000, step: 1, decimals: 2}
    effect: Fails the lights whose median star SNR is lower.
    enabled: With the check box before it ticked.
    tradeoff: Can favor the lights of a brighter background or filter.
  - name: FWHM ≤
    object: spinMaxFwhm
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 100, step: 0.1, decimals: 2}
    effect: Fails the lights whose stars are broader, in pixels.
    enabled: With the check box before it ticked.
    tradeoff: A tight limit throws away exposure.
  - name: Ecc ≤
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 1, step: 0.05, decimals: 2}
    effect: Fails the lights whose stars are more elongated.
    enabled: With the check box before it ticked.
    tradeoff: A tight limit can fail lights for the tracking or the optics rather than the sky.
  - name: Suggest Filters...
    kind: button
    default: ""
    effect: >-
      Proposes limits from the group's figures, one and a half interquartile ranges past the quartiles, and asks
      whether to set them. A second question asks whether to exclude the lights that fail.
  - name: Exclude Outliers...
    kind: button
    default: ""
    effect: Excludes at once, without a question, every light with figures that fails a ticked filter, and says how many.
    note: It can be undone. With no filter ticked it says "Please enable at least one quality filter."
  - name: Metric
    kind: combo
    default: Score
    options: [Score, Stars, FWHM, SNR, Eccentricity]
    effect: Chooses the figure the chart under the table plots for each light with figures.
    note: Clicking a point selects its light, and a double click opens it in **Blink Comparator**. The chart's choices are not saved.
  - name: Plot
    kind: combo
    default: Points
    options: [Points, Line, Line + Points, Area, Bars]
    effect: Chooses how the chart draws the lights.
  - name: Sort
    kind: combo
    default: By Index
    options: [By Index, By Value (Asc), By Value (Desc), By Capture Time]
    effect: Chooses the order of the lights along the chart.
  - name: Norm
    kind: combo
    default: "Off"
    options: ["Off", By Filter]
    effect: '**By Filter** plots each light against its own filter''s median, in deviations, so filters can be compared.'
  - name: Percentile band
    kind: check
    default: "On"
    effect: Shades the band between the first and third quartiles.
  - name: Median lines
    kind: check
    default: "On"
    effect: Draws a dashed line at the median.
  - name: Selection guides
    kind: check
    default: "On"
    effect: Draws guide lines at the selected light.
  - name: Export...
    kind: toolbutton
    default: ""
    effect: Opens the three items below.
  - name: Copy Chart Image
    kind: action
    default: ""
    effect: Copies the chart as a picture.
  - name: Save Chart as Image...
    kind: action
    default: ""
    effect: Saves the chart as a PNG or JPEG file.
  - name: Export Data to CSV...
    kind: action
    default: ""
    effect: Saves each light's frame, group, filter, value and whether it is the reference, excluded or failing a filter, as a CSV file.
  - name: Group by filter
    kind: check
    default: "Off"
    effect: Draws one series for each filter.
    enabled: With two filters or more in the group.
  - name: Drop to baseline
    kind: check
    default: "Off"
    effect: Draws each series' ends down to the baseline, so no line jumps across missing lights.
    enabled: While **Group by filter** is on.
  - name: Run Registration (All Groups)
    kind: button
    default: ""
    effect: Registers every group that has work to do, after an **Execution Plan**. With none, it says "No registration updates were detected for any group."
    enabled: With an output directory set and a group of lights.
```

### Registration Settings

**Registration Settings** opens this dialog for one
[registration group](../../reference/glossary.md#registration-group), named under its title. **OK** keeps its
values, which can be undone, and **Cancel** discards them.

```controls
window: Registration Settings
controls:
  - name: Auto-solve unsolved frames during Registration
    kind: check
    default: "Off"
    effect: Solves the lights whose header holds no sky solution before they are registered, with the configured plate solver.
    enabled: >-
      When a solver is set up: a ready Gaia catalog, an ASTAP or local astrometry.net program, or an online key.
      Without one it shows off, nothing is solved, and the group keeps your choice for later.
    note: >-
      Under **Mosaic** it shows on and disabled, with a note: **Mosaic** sends every panel the stars cannot place to
      the solver anyway, and the reference too when its header holds no solution.
  - name: Enrich with Gaia catalog
    kind: check
    default: "On"
    effect: >-
      After the registration, matches each solved light's stars with the Gaia catalog for its photometric zero point,
      limiting magnitude and matched star count. **Photometric** weighting and Spectrophotometric Color Calibration
      read them.
    tradeoff: It reads the catalog for each light. Off leaves **Photometric** weighting without zero points.
    note: It is offered without a catalog too. A group with a solved light then enriches nothing, and says so once, naming the catalog folder.
  - name: Minimum Star Pairs
    kind: spinbox
    default: "10"
    widget: {min: 5, max: 10000, step: 1}
    effect: Sets how many star pairs a light must share with the reference, or with a light already registered near it.
    enabled: For **Star Matching**.
    range: >-
      A match of fewer than ten candidate pairs also needs four fifths of them to agree. A reference with fewer stars
      lowers the minimum to its own count for the run, never below five, and the log says so.
  - name: Maximum Stars per Frame
    kind: spinbox
    default: "2000"
    widget: {min: 10, max: 20000, step: 1}
    effect: Caps the stars detected in each light, which are the candidates for matching.
    enabled: For **Star Matching**.
    tradeoff: More stars steady the match and cost time. A changed cap detects the group's stars again.
  - name: Reference Image
    kind: combo
    default: The group's reference, or its first light
    options: ["The group's reference, or its first light", Every included light of the group by its full path]
    effect: Chooses the reference by its file. You can type part of a path.
    enabled: While **Auto-select** is off.
  - name: Auto-select
    kind: check
    default: "On"
    effect: Analyzes the lights first and takes the best one as the reference, which the group's record then names.
    enabled: Off and disabled for **Phase Correlation**, **Manual Points** and **Comet Tracking**, which need a fixed reference.
  - name: Best
    kind: button
    default: ""
    effect: Picks the best reference from the group's figures and puts it in **Reference Image**.
    enabled: While **Auto-select** is off, and not for **Manual Points** or **Comet Tracking**.
    range: >-
      With several filters it asks which one to judge by, or **All Filters (filter-normalized score)**. Without
      figures it offers to register the group first.
  - name: Registration Method
    kind: combo
    default: Star Matching
    options: [Star Matching, Phase Correlation, Manual Points, Comet Tracking]
    effect: >-
      **Star Matching** pairs stars. **Phase Correlation** finds a shift alone. **Manual Points** fits the point pairs
      you place, and **Comet Tracking** aligns the lights on a moving comet.
    tradeoff: Phase Correlation cannot follow a rotation or a change of scale.
  - name: Transform Model
    kind: combo
    default: Auto
    options: [Similarity, Affine, Homography, Polynomial 2, Polynomial 3, Polynomial 4, Auto]
    effect: >-
      Chooses how much a light may be bent onto the reference: rotation, shift and scale, then shear, then
      perspective, then curvature. **Auto** fits a homography and refines
      it with a polynomial where that is clearly better. A line under it says what the choice does.
    enabled: For **Star Matching** and **Manual Points**.
    range: >-
      A polynomial needs 6, 10 or 15 star pairs for orders 2, 3 and 4. A light with fewer keeps the linear transform
      its stars matched, and the log says so.
    tradeoff: A higher order follows lens distortion and can follow bad matches too.
  - name: Edit Points...
    kind: button
    default: ""
    effect: Opens **Manual Registration**. A line beside it counts the pairs and the lights that have them.
    enabled: Shown for **Manual Points**. It needs two lights and a reference.
  - name: Comet Output
    kind: combo
    default: Comet only
    options: [Comet only]
    effect: Aligns the lights on the comet, so that the stars trail. Every stack of this registration group uses the comet-aligned lights.
    enabled: Shown for **Comet Tracking**.
  - name: Track Comet...
    kind: button
    default: ""
    effect: Opens **Track Comet**. A line under it counts the observations and states the tracking options the registration uses.
    enabled: Shown for **Comet Tracking**.
  - name: Correct chromatic aberration before registration
    kind: check
    default: "Off"
    effect: Moves the red and blue channels of each color light onto its green one, matched on stars near the edges, before the registration.
    scope: >-
      Color lights, and a mosaic, which the registration reads in color. A group whose lights all have one channel is
      registered as it is and says so once. The corrected copies go to `ca_corrected_lights`.
  - name: Edge Fraction
    kind: spinbox
    default: "0.55"
    widget: {min: 0.3, max: 0.8, step: 0.05, decimals: 2}
    effect: Uses the stars farther from the center than this share of half the diagonal, where the color fringes show.
    enabled: While the correction is on, as are the three below.
  - name: CA Fit Type
    kind: combo
    default: Similarity
    options: [Similarity, Affine]
    effect: Chooses the model that moves red and blue. **Affine** also allows shear and a different scale on each axis.
  - name: Min Matched Stars
    kind: spinbox
    default: "6"
    widget: {min: 3, max: 50, step: 1}
    effect: Sets the matches a channel needs before it is moved, never fewer than five. A match of fewer than ten pairs also needs three fifths of them to fit.
  - name: CA Interpolation
    kind: combo
    default: Lanczos4
    options: [Bilinear, Bicubic, Lanczos4, Pixel Area Relation, Nearest Neighbor, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: Chooses how a moved channel is resampled.
  - name: Standard / Mosaic / Intersection
    kind: radio
    default: Standard
    options: [Standard, Mosaic, Intersection]
    effect: >-
      **Standard** aligns every light on the reference's frame. **Mosaic** makes a canvas holding every light, and
      places a panel its stars cannot place by its sky solution. **Intersection** keeps only the area every light
      covers.
    tradeoff: Mosaic needs more memory and disk, and may send panels to an online solver. Intersection loses the borders.
    note: When no area is common to every light, **Intersection** uses the **Standard** canvas and says so.
  - name: Interpolation
    kind: combo
    default: Auto
    options: [Auto, Nearest Neighbor, Bilinear, Bicubic, Pixel Area Relation, Lanczos4, Bit Exact Bilinear, Bit Exact Nearest Neighbor]
    effect: Chooses how each light is resampled onto the canvas. **Auto** is Lanczos4, and the log names it.
    tradeoff: Sharper kernels cost more. **Nearest Neighbor** is fast and blocky.
  - name: Advanced Settings...
    kind: button
    default: ""
    effect: Opens **Advanced Registration Settings**, the star detection and the reading of a mosaic.
  - name: Reset to Defaults
    kind: button
    default: ""
    effect: Returns the dialog's values to their defaults, and clears the point pairs and comet observations it holds.
  - name: Apply to All Groups
    kind: button
    default: ""
    effect: >-
      Writes these settings at once into every registration group except a comet group, as one change that can be
      undone. The reference, the point pairs and the comet settings are not copied.
    enabled: Disabled for **Comet Tracking**.
    note: The dialog stays open, and its **Cancel** does not take this back.
```

### Advanced Registration Settings

**Advanced Settings...** opens this dialog. **OK** keeps its values for **Registration Settings**, and **Cancel**
returns them to what they were when it opened.

```controls
window: Advanced Registration Settings
controls:
  - name: Radius
    kind: spinbox
    default: "5"
    widget: {min: 3, max: 32, step: 1}
    effect: Sets the size, in pixels, of the box a star is searched and fitted in.
    enabled: With **Star Matching**, as every setting of **Star Detection** is.
    tradeoff: Too small cuts broad stars short. Too large merges close stars and costs time.
    note: A stored value the detector refuses runs at the nearest one it takes, and the log says so once for each group.
  - name: Threshold
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 15, step: 0.05, decimals: 2}
    effect: Sets how far above the local background, in units of its noise, a peak must rise to count as a star.
    tradeoff: Low finds noise and hot pixels. High misses faint stars.
  - name: Enable Amplitude Range
    kind: check
    default: "Off"
    effect: Keeps only the stars whose peak lies between **Minimum** and **Maximum**, as fractions of full scale.
    tradeoff: A narrow range can leave too few stars to match.
  - name: Minimum
    object: thresholdMinimumSpinBox
    kind: spinbox
    default: "0.00"
    widget: {min: 0, max: 0.99, step: 0.05, decimals: 2}
    effect: The smallest peak kept, while the range is on.
  - name: Maximum
    object: thresholdMaximumSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: The largest peak kept. Lower drops saturated stars.
  - name: Roundness Threshold
    kind: spinbox
    default: "0.50"
    widget: {min: 0.01, max: 0.95, step: 0.05, decimals: 2}
    effect: Sets the least roundness a star must have, from 0 for a line to 1 for a circle.
    tradeoff: Higher drops trailed stars and the stretched stars of the corners.
  - name: Enable Roundness Range
    kind: check
    default: "Off"
    effect: Adds the upper limit **Maximum** to the roundness.
  - name: Maximum
    object: roundnessThresholdMaximumSpinBox
    kind: spinbox
    default: "1.00"
    widget: {min: 0.01, max: 1, step: 0.05, decimals: 2}
    effect: The roundest a star may be, while the range is on.
  - name: Convergence
    kind: spinbox
    default: "1"
    widget: {min: 1, max: 3, step: 1}
    effect: Sets how many fitting passes each star gets.
  - name: Relax PSF Checks
    kind: check
    default: "Off"
    effect: Accepts stars whose profile fails the stricter checks.
    tradeoff: Helps distorted or faint stars, and lets more false stars through.
  - name: Profile Type
    kind: combo
    default: Gaussian
    options: [Gaussian, Moffat]
    effect: Chooses the profile fitted to each star. **Moffat** follows broad wings.
  - name: Minimum Beta
    kind: spinbox
    default: "1.50"
    widget: {min: 0.05, max: 10, step: 0.05, decimals: 2}
    effect: Sets the smallest exponent a Moffat fit may take.
    note: The detector checks it under either profile, and the log mentions it under Moffat only.
  - name: CFA Pattern
    kind: combo
    default: Auto
    options: [Auto, RGGB, BGGR, GBRG, GRBG]
    effect: Chooses how a color-filter mosaic is read. **Auto** reads the pattern the file states.
    tradeoff: A wrong pattern gives wrong colors and wrong star figures.
  - name: Method
    kind: combo
    default: Default
    options: [Default, Bilinear, VNG4, AHD, AMaZE, LMMSE, RCD]
    effect: Chooses the debayer method of a mosaic. **Default** is the method of the **Raw Images** page of the settings, which the item names.
    tradeoff: The finer methods cost more time.
```

### Manual Registration, Track Comet and Blink Comparator

```controls
window: Manual Registration
controls:
  - name: Target frame
    kind: combo
    default: The first light after the reference
    options: [The first light after the reference, Every other light of the group]
    effect: Chooses the light whose points you place. Changing it drops a reference point still waiting for its pair.
  - name: Reference and target previews
    kind: readout
    default: ""
    effect: Click a star in the reference, then the same star in the target, to store a pair. Markers are numbered, and the waiting one reads "Next".
    tradeoff: Pairs spread over the frame give a steady fit. One wrong pair bends it.
  - name: Remove Selected
    kind: button
    default: ""
    effect: Removes the pairs selected in the table below the buttons, which lists **#**, **Ref X**, **Ref Y**, **Target X** and **Target Y**.
  - name: Clear Target Points
    kind: button
    default: ""
    effect: Removes every pair of the target.
  - name: Clear Pending Reference
    kind: button
    default: ""
    effect: Drops the reference point still waiting for its pair.
  - name: OK
    kind: button
    default: ""
    effect: >-
      Keeps the pairs. It checks no count: the run needs 3 pairs for **Similarity** or **Affine**, 4 for a homography
      or **Auto**, and 6, 10 or 15 for a polynomial of order 2, 3 or 4.
    note: A target without enough pairs fails with "Manual registration failed for" and its name.
```

```controls
window: Track Comet
controls:
  - name: Frame
    kind: readout
    default: The first light
    effect: >-
      Lists the group's lights. Selecting one shows it in the preview beside the list, debayered with the method of
      the **Raw Images** settings and stretched channel by channel.
  - name: Comet preview
    kind: readout
    default: ""
    effect: Shows the selected light. Zoom, and click the comet's core to place a pending point.
  - name: Mark
    kind: button
    default: ""
    effect: Adds the pending point as an observation of the light.
    enabled: With a pending point on the selected light.
  - name: Clear Point
    kind: button
    default: ""
    effect: Removes the pending point.
  - name: Refine
    kind: button
    default: ""
    effect: Stays disabled.
  - name: Auto Track
    kind: button
    default: ""
    effect: Follows the comet through the lights from the observations you made, and lists each position it finds under the diagnostics, with its confidence.
    enabled: With one observation or more, while nothing runs.
  - name: Find Moving Object
    kind: button
    default: ""
    effect: Searches the lights for a moving object, and lists what it finds as candidates. Nothing is used until you accept it.
    range: It reads the lights as fractions of full scale, aligned on their stars, with a residual threshold of one 16-bit code.
  - name: Observations
    kind: readout
    default: ""
    effect: Lists each observation with **Use**, **Frame**, **X**, **Y** and **Origin**. Unticking **Use** leaves it out of the fit.
  - name: Diagnostics
    kind: readout
    default: ""
    effect: Lists the positions the two tools found, with **Frame**, **X**, **Y**, **Confidence** and **State**.
  - name: Clear
    kind: button
    default: ""
    effect: Removes every observation.
  - name: Accept
    kind: button
    default: ""
    effect: Takes the selected candidate as an observation.
    enabled: With a row in the **Candidate** state selected.
  - name: Search radius
    kind: spinbox
    default: "32"
    widget: {min: 1, max: 64, step: 1}
    effect: >-
      Measures the nucleus within this many pixels of where the track places it, or of the marked point when **Auto
      Track** starts from one observation. It is under **Tracking and Fit Options**, which opens folded.
    tradeoff: Larger tolerates a poorer track, and can take a star beside the nucleus for it.
    note: A value stored more finely than a box shows, such as a radius between whole pixels, is kept while the box shows what it runs as.
  - name: Minimum SNR
    kind: spinbox
    default: "3.0"
    widget: {min: 0.1, max: 100, step: 0.5, decimals: 1}
    effect: Refuses a nucleus fainter than this many times the noise around it. Its light is then aligned where the track places it.
  - name: Bootstrap frames
    kind: spinbox
    default: "8"
    widget: {min: 0, max: 4096, step: 1}
    effect: With one observation, **Auto Track** searches at most this many lights. With two, it does nothing.
  - name: Track model
    kind: combo
    default: Linear
    options: [Linear, Automatic, Prefer quadratic]
    effect: >-
      **Linear** fits a constant speed. **Automatic** adds an acceleration when six observations or more remain and it
      lowers the residual by 30 % or more, and **Prefer quadratic** whenever six or more remain.
  - name: Reject outlier observations
    kind: check
    default: "On"
    effect: Leaves out an observation farther from the track than **Outlier sigma** times the residual, when that at least halves the residual, never one within 3 pixels.
  - name: Outlier sigma
    kind: spinbox
    default: "3.0"
    widget: {min: 1, max: 100, step: 0.5, decimals: 1}
    effect: Sets how many times the fit's residual an observation may lie from the track.
    enabled: While **Reject outlier observations** is on.
  - name: OK
    kind: button
    default: ""
    effect: Keeps the observations and the options for the group. A changed option registers the group again.
    enabled: It needs two enabled observations. With fewer it says "Add or accept a second comet observation before submitting."
    note: '**Auto Track** runs with the values shown, before **OK**.'
```

```controls
window: Blink Comparator
controls:
  - name: Blink
    kind: readout
    default: ""
    effect: >-
      Shows the chosen lights one after another, with **Previous**, **Play**, **Next** and a speed slider from **Slow**
      to **Fast**, half a second a frame at first. ++space++ plays, ++left++ and ++right++ step, and ++x++ excludes the
      light shown.
    range: >-
      A table lists each file's **Filter**, **Resolution**, **Exposure**, **Temp**, **Object**, **Camera** and
      **Path**, read from its header. A card that does not read as a number shows nothing.
    persistence: >-
      When it closes, the lights you excluded are excluded from the project, which can be undone. Ratings with ++1++
      to ++9++ stay in the comparator. It opens at the size you left it.
    note: '**Compare** in the run history opens it on two stacked masters side by side.'
```

### Stacking

```controls
window: Stacking
controls:
  - name: Integration Groups
    kind: readout
    default: No integration groups available.
    effect: >-
      Lists each integration group with **Frames**, **Calib Groups**, **Total Time**, **Stack**, **Reject**,
      **Normalize**, **Weighting**, **Drizzle** and the **Output** file it writes.
    range: An integration group holds the included lights of one resolution, filter and object, and of whatever the three boxes below add.
    note: >-
      The cells show the stored settings, before the stack lets go of what it cannot use. A double click, or
      ++enter++, opens the row's settings. Sorting changes the view only.
  - name: Group by Session
    kind: check
    default: "Off"
    effect: Makes a master of each night.
    scope: The final stacks only, not the calibration matching. Each box makes more masters of fewer lights.
  - name: Group by Custom Tag
    kind: check
    default: "Off"
    effect: Makes a master of each custom tag.
  - name: Group by Exposure
    object: chkGroupByExposure
    kind: check
    default: "Off"
    effect: Makes a master of each exposure.
  - name: Group by Exposure
    object: spinExposureTolerance
    kind: spinbox
    default: "3.00"
    widget: {min: 0, max: 1000, step: 0.1, decimals: 2}
    effect: Puts exposures closer than this many seconds in one master.
    enabled: While **Group by Exposure** is on.
  - name: Stacking Settings...
    kind: button
    default: ""
    effect: Opens **Stacking Settings** for the selected group.
  - name: Run This Group
    kind: button
    default: ""
    effect: Runs what the selected group needs, after an **Execution Plan**, and always stacks it again.
    enabled: With a group selected.
  - name: Frame Quality
    kind: readout
    default: Select an integration group to view frame quality.
    effect: Lists the selected group's lights with **Score**, **Stars**, **FWHM**, **SNR**, **Eccentricity** and the **Weight** the stack would give them.
    range: >-
      The weights are the stack's, with a mean of one, and a histogram shows them. **Effective exposure** counts each
      light's exposure in proportion to its weight, the heaviest light's whole. A weight you type is a preview only.
    note: >-
      Where the stack would weigh the lights equally, or not at all, a banner says why. Weights that **Number of
      images** reads from headers are kept until the dashboard is refreshed.
  - name: Outlier sensitivity (σ)
    kind: spinbox
    default: "3.0"
    widget: {min: 1, max: 5, step: 0.5, decimals: 1}
    effect: Sets how far below the others, in deviations, a light's score must lie to be found. Lower finds more.
  - name: Analyze → Detect Outliers
    kind: button
    default: ""
    effect: Selects the lights whose score lies that far below.
    tradeoff: A low score is a reason to look, not proof of a bad light.
  - name: Exclude from This Stack
    kind: button
    default: ""
    effect: Leaves the selected lights out of this group's stack only. The registration and other stacks keep them.
    persistence: Part of the project.
  - name: Include Selected in Stack
    kind: button
    default: ""
    effect: Returns the selected lights to this stack.
  - name: Generate 64-bit images
    kind: check
    default: "Off"
    effect: Changes nothing. No stage reads it, and every master is 32-bit floating point.
  - name: Reprocess existing
    kind: check
    default: "Off"
    effect: Computes everything again instead of reusing earlier work, and replaces the outputs.
    tradeoff: The whole run's time and disk. The replaced outputs go to the run's archive.
    persistence: Until the window closes.
  - name: Open integrated files
    kind: check
    default: "On"
    effect: Opens each new master in its own view when a run ends well.
    note: The maps a group makes open whatever this says. Nothing opens after a failed or cancelled run, or a re-run from the history.
  - name: Combine RGB images
    kind: check
    default: "Off"
    effect: Combines the red, green and blue masters of one registration group into one color master, `stacked_RGB_<group>.fit`.
    enabled: When one registration group has exactly one integration group whose filter name holds "red", one "green" and one "blue". A filter named `R` does not count.
    note: The channels are matched by the ratio of their robust scales and the offset of their robust levels, over the samples between 0.0000001 and 0.875 of full scale.
  - name: Save calibrated frames to disk (for debugging)
    kind: check
    default: "Off"
    effect: >-
      Writes each calibrated light to `calibrated_lights` as a FITS file ending in `_cal`, and the registration reads
      those. Off calibrates the lights in memory as they are read.
    tradeoff: One more file of each light's size, and the time to write it. The calibration can then be looked at.
    note: With less than 2 GiB of memory free, a run writes them anyway. A light with no master and no pedestal gets no copy.
  - name: Limit memory usage
    object: chkLimitMemory
    kind: check
    default: "Off"
    effect: Stacks in bands of rows that fit the limit.
    scope: >-
      The light groups whose **Stacking Settings** were accepted at least once. A group never accepted there stacks
      without a limit, and the calibration masters are never limited.
  - name: Limit memory usage
    object: spinMaxMemory
    kind: spinbox
    default: "2048"
    widget: {min: 512, max: 1048576, step: 1}
    effect: Sets the limit in megabytes.
    enabled: While **Limit memory usage** is on.
    tradeoff: >-
      A lower limit costs more reading for lights calibrated in memory, warped as they are read or read from camera
      files, which are read again for each band.
  - name: CPU Threads
    kind: spinbox
    default: The computer's thread count
    widget: {min: 1, step: 1}
    effect: Caps the threads each step of a run uses.
    range: Up to the threads the computer offers.
    tradeoff: Fewer leave more of the computer to other work, and the run takes longer.
    persistence: Until the window closes.
  - name: Run Integration (All Groups)
    kind: button
    default: ""
    effect: Prepares the whole run and shows its **Execution Plan**, as **Execute Integration** does.
    enabled: With a group selected. Whether the project is ready is checked when you press it.
```

### Stacking Settings

**Stacking Settings...** opens this dialog for one group: "LIGHT Integration Stacking Settings", or "BIAS", "DARK" or
"FLAT Stacking Settings" for a calibration group. A calibration group sees only **Stack Method**, **Normalization**,
**Scale Estimator**, the [rejection](../../reference/glossary.md#rejection) and its maps. **OK** keeps the values as
one change that can be undone, and **Cancel** discards them.

```controls
window: Stacking Settings
controls:
  - name: Stack Method
    kind: combo
    default: Average
    options: [Average, Sum, Maximum, Minimum, Median, Auto-Adaptive Weighted Average]
    effect: Chooses how the lights' samples become one. **Auto-Adaptive Weighted Average** is a robust mean that needs no rejection.
    range: The rejection and the weighting need **Average**.
    tradeoff: '**Sum** changes the scale of the result.'
  - name: Enable HDR Exposure Merge
    kind: check
    default: "Off"
    effect: Stacks the lights of each exposure apart, then blends the highlights of the shorter exposures into the long master.
    enabled: For light groups.
    tradeoff: >-
      It turns off normalization, local normalization, the weighting, drizzle, every map and the spatial analysis. A
      weighting chosen before is let go of when you choose **OK**.
  - name: Normalization
    kind: combo
    default: Additive With Scaling
    options: [None, Additive, Additive With Scaling, Multiplicative, Multiplicative With Scaling]
    effect: Matches each light's level, and with **Scaling** its spread, to the reference's before the lights are combined.
    range: >-
      Flats open at **Multiplicative**. Bias and dark groups are fixed at **None**. While local normalization is on, it
      shows **Additive With Scaling (set by local normalization)**, disabled, and your choice returns when it is off.
    enabled: Disabled by the HDR merge.
  - name: Scale Estimator
    kind: combo
    default: IKSS
    options: [IKSS, MAD]
    effect: Chooses how each light's spread is measured, the iterative k-sigma estimate or the median absolute deviation.
    enabled: For the two **Scaling** choices, and with local normalization.
    tradeoff: IKSS is the more efficient robust estimate. MAD costs less.
  - name: Reference Selection
    kind: combo
    default: Automatic (best quality)
    options: [Automatic (best quality), Manual]
    effect: Chooses the light the others are matched to.
    enabled: For light groups with a normalization, and with local normalization only under its **Explicit Reference** strategy.
  - name: Reference Frame
    kind: combo
    default: The best light of the group
    options: [The best light of the group, Every light of the group]
    effect: Chooses the reference under **Manual**. **Best** beside it picks the best light and sets **Manual**.
    tradeoff: A reference the group no longer holds stops the run with "Normalization reference frame is not part of the integration group inputs."
  - name: Enable Overlap Normalization
    kind: check
    default: "Off"
    effect: Matches the lights over the areas they share and solves every match together, so the panels of a mosaic agree where they meet.
    enabled: For light groups with a global normalization.
    tradeoff: Reads every light once more. Lights read as they are, or whose overlaps do not connect, fall back to the usual match with a warning.
  - name: Min Overlap Pixels
    kind: spinbox
    default: "1000"
    widget: {min: 0, max: 100000000, step: 100}
    effect: Leaves out a pair of lights that share fewer pixels than this.
    enabled: While overlap normalization is on.
  - name: Min Overlap Fraction
    kind: spinbox
    default: "0.010"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Leaves out a pair that shares less than this share of the smaller light's area.
    enabled: While overlap normalization is on.
  - name: Enable Local Normalization
    kind: check
    default: "Off"
    effect: Fits each light's background as a surface over a grid, and matches it to the reference's, then matches the levels.
    tradeoff: Follows gradients that differ from light to light, costs time and memory, and can take real faint structure for background.
    note: '**Configure...** beside it opens **Local Normalization Settings**, and the label beside it names the preset.'
  - name: Rejection Method
    kind: combo
    default: Winsorized Sigma Clipping
    options: [None, Sigma Clipping, MAD Clipping, Median Sigma, Winsorized Sigma Clipping, Percentile, GESDT, Linear Fit]
    effect: Chooses how samples unlike the others at a pixel, a satellite or a hot pixel, are left out.
    range: Calibration groups open at **Sigma Clipping**. A group of fewer than three frames is combined without rejection, and the warnings say so.
    enabled: With **Average**, as the whole rejection section is.
    tradeoff: The stronger methods need more frames and cost more time.
  - name: Sigma Low
    kind: spinbox
    default: "3.00"
    widget: {min: 0.01, max: 10, step: 0.1, decimals: 2}
    effect: Rejects samples this many deviations below the others. Lower rejects more.
    range: >-
      Choosing a method sets the pair: **Linear Low** and **Linear High** at 5 and 4 for **Linear Fit**, **ESD
      Outliers** and **ESD Significance** at 0.30 and 0.05 for **GESDT**, **Low Fraction of Median** and **High
      Fraction of Median** at 0.2 and 0.1 for **Percentile**, and 3 and 3 otherwise.
    tradeoff: Too low removes signal. Too high leaves trails and defects.
  - name: Sigma High
    kind: spinbox
    default: "3.00"
    widget: {min: 0.01, max: 10, step: 0.1, decimals: 2}
    effect: Rejects samples this many deviations above the others.
    note: '**Percentile** rejects a sample further from the median than its fraction of the median, not a rank.'
  - name: Weighting
    kind: combo
    default: None
    options: [None, Number of stars, Weighted FWHM, Noise, Number of images, Photometric]
    effect: Gives each light a weight from its figures, so better lights count more.
    range: >-
      **Weighted FWHM** weighs the inverse square of the star width, made broader for the stars a light lacks.
      **Number of images** reads `STACKCNT` or `NCOMBINE` from each light. **Photometric** needs the zero points of
      two lights or more.
    enabled: For light groups with **Average**, without the HDR merge or a comet stack.
    note: '**Weighting Options...** beside it opens the details.'
  - name: Create Rejection Maps
    kind: check
    default: "Off"
    effect: Writes the low and the high rejection maps beside the master.
    enabled: With a rejection.
  - name: Merge Rejection Maps
    kind: check
    default: "Off"
    effect: Writes one map for both.
    enabled: With **Create Rejection Maps**.
  - name: Analyze Spatial Rejection Clusters
    kind: check
    default: "Off"
    effect: Measures where rejected samples gather, and names the lights with the most in the report.
    enabled: For light groups with a rejection.
  - name: Re-Stack Excluding Flagged Frames
    kind: check
    default: "Off"
    effect: Stacks the group again without the lights the analysis flagged, with the same rejection.
    enabled: With the analysis, and without drizzle or the HDR merge.
    range: A second stack that would keep fewer than three lights is not made, and the warnings say so.
  - name: Create Quality Maps
    kind: check
    default: "Off"
    effect: Writes coverage, variance and SNR-gain maps, and a weight-sum map when the stack is weighted.
    enabled: For light groups, without the HDR merge.
  - name: Allow Registered Images to Be Discarded
    kind: check
    default: "Off"
    effect: Writes no registered lights. The stack warps each light as it reads it, on every run.
    tradeoff: Saves a registered file of each light, and leaves nothing to inspect or to resume from.
    scope: A drizzled group, a comet group and a run with **Reprocess existing** keep their registered lights anyway.
  - name: Plate-Solve Final Master If Needed
    kind: check
    default: "Off"
    effect: Gives the master the reference's sky solution, and solves a copy of the master when there is none to give.
    tradeoff: The solver's time, and the network for an online solver.
    note: The report says where each master's solution came from, or why it has none.
  - name: Enable Drizzle
    kind: check
    default: "Off"
    effect: >-
      Resamples each light's pixels onto a finer grid through its registration, instead of aligning it, under
      **Drizzle**, which opens folded. A line under the section says what the choice gives.
    tradeoff: Needs dithered lights, ten or more, and costs memory and disk as the square of **Scale**.
  - name: Scale
    kind: combo
    default: 2.0× (recommended)
    options: [1.0× (no upscaling), 1.5×, 2.0× (recommended), 3.0×]
    effect: Sets how much larger the master is than a light, in width and height.
  - name: Kernel
    kind: combo
    default: Square (recommended)
    options: [Square (recommended), Point, Turbo, Gaussian, Lanczos2, Lanczos3]
    effect: Chooses the footprint each input pixel spreads over.
  - name: Pixel Fraction
    kind: spinbox
    default: "0.90"
    widget: {min: 0.1, max: 1, step: 0.05, decimals: 2}
    effect: Shrinks each input pixel before it is dropped. Lower is sharper and needs more lights.
    note: '**Scale**, **Kernel** and **Pixel Fraction** stay enabled with drizzle off.'
  - name: Recommended Settings...
    kind: button
    default: ""
    effect: Opens **Recommended Stacking Settings** for the group.
  - name: Reset to Defaults
    kind: button
    default: ""
    effect: Returns the dialog's values to its type's defaults.
  - name: Apply to All Groups
    kind: button
    default: ""
    effect: Gives these values to every group of this type and closes the dialog.
    tradeoff: A **Manual** reference is copied too, and a group that does not hold it then refuses the run.
  - name: OK
    kind: button
    default: ""
    effect: Keeps the values. A value stored more finely than its box shows is kept unless you change that box.
```

```controls
window: Weighting Options
controls:
  - name: Star Count Scaling
    kind: combo
    default: Quadratic
    options: [Linear, Quadratic, Exponential, Roundness Weighted]
    effect: Chooses how the star count becomes a weight. **Roundness Weighted** also reads the stars' roundness.
    enabled: For **Number of stars**.
  - name: Exponential Alpha
    kind: spinbox
    default: "2.00"
    widget: {min: 0.1, max: 10, step: 0.1, decimals: 2}
    effect: Sets the steepness of **Exponential**.
    enabled: For **Number of stars** with **Exponential**.
  - name: Min Weight Floor
    kind: spinbox
    default: "0.010"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Raises the weakest lights to this share of the best light's weight, so none is left out.
    enabled: For **Number of stars**, **Weighted FWHM** and **Photometric**, where it is a floor on the model's raw weight.
  - name: Enable Per-Pixel Weighting
    kind: check
    default: "Off"
    effect: Weighs each light differently across the frame, by its local signal-to-noise ratio.
    tradeoff: Fine grids cost time, memory and disk, and a strong exponent gives a few lights most of the weight.
  - name: Window Size
    kind: spinbox
    default: "5"
    widget: {min: 3, max: 31, step: 2}
    effect: Sets the odd width, in pixels, of the box the local ratio is measured in.
    enabled: With per-pixel weighting, as the two below are.
  - name: Grid Spacing
    kind: spinbox
    default: "64"
    widget: {min: 4, max: 512, step: 4}
    effect: Sets the spacing, in pixels, of the grid the weights are computed on. Smaller follows finer changes.
  - name: SNR Exponent
    kind: spinbox
    default: "2.00"
    widget: {min: 0.1, max: 8, step: 0.1, decimals: 2}
    effect: Sets how strongly the local ratio sets the weight.
```

```controls
window: Local Normalization Settings
controls:
  - name: Preset
    kind: combo
    default: Standard (Grid 4)
    options: [Quick (Grid 2), Standard (Grid 4), Fine (Grid 8), Custom]
    effect: Sets the grid and the reference strategy at once. A change of your own selects **Custom**.
  - name: Strategy
    kind: combo
    default: Integrate Best Frames
    options: [Explicit Reference, Single Best Frame, Integrate Best Frames]
    effect: >-
      Builds the reference from the group's normalization reference, from its best light, or from the best lights
      combined. **Single Best Frame** makes that light the stack's reference too.
  - name: Max Frames
    kind: spinbox
    default: "16"
    widget: {min: 1, max: 256, step: 1}
    effect: Sets how many lights are combined.
    enabled: For **Integrate Best Frames**.
  - name: Grid Size
    kind: spinbox
    default: "4"
    widget: {min: 1, max: 32, step: 1}
    effect: Divides the frame into this many cells a side. Finer follows smaller gradients and can fit real structure.
  - name: Interpolation
    kind: combo
    default: Bicubic
    options: [Bilinear, Bicubic]
    effect: Chooses how the surface is drawn between the grid's nodes.
  - name: BG Smoothing
    kind: spinbox
    default: "1"
    widget: {min: 0, max: 10, step: 1}
    effect: Smooths the grid's nodes over this radius. 0 does not smooth.
  - name: Method
    kind: combo
    default: PSF Flux
    options: [PSF Flux, Residual Ratio]
    effect: Chooses how each light's scale is measured, under **Scale Evaluation**.
  - name: Mode
    kind: combo
    default: Linked RGB
    options: [Linked RGB, Per-Channel]
    effect: Gives the three channels one scale, or each its own.
    tradeoff: Per-Channel can correct a color change, and can shift the color balance.
  - name: Low Clip
    kind: spinbox
    default: "0.000045"
    widget: {min: 0, max: 1, step: 0.00001, decimals: 6}
    effect: Leaves samples below this level out of the measurement.
  - name: High Clip
    kind: spinbox
    default: "0.850"
    widget: {min: 0, max: 1, step: 0.01, decimals: 3}
    effect: Leaves samples above this level out of the measurement.
    tradeoff: Too narrow a range leaves too few samples.
  - name: Enable Advanced Options
    kind: check
    default: "Off"
    effect: Enables the controls below.
    persistence: Off each time the dialog opens. The values below are kept.
  - name: Evaluation
    kind: combo
    default: PSF Signal Weight
    options: [PSF Signal Weight, Quality Score, PSF SNR, M* (Background), Median, Star Count]
    effect: >-
      Ranks the lights that may become the reference by the engine's summary of their stars: **PSF Signal Weight** is
      its quality, **PSF SNR** divides by the channel's median absolute deviation, and **Quality Score** reads its
      medians.
    enabled: Not for **Explicit Reference**, which ranks nothing.
    tradeoff: Each figure favors other lights.
  - name: Estimator
    kind: combo
    default: Sigma Clipped Median
    options: [Percentile, Sigma Clipped Median, Sigma Clipped Mode]
    effect: Chooses how each cell's background is estimated, with **Percentile**, **Sigma Clip K** and **Sigma Iterations** as it needs them.
    note: '**Percentile** opens at 0.25, **Sigma Clip K** at 3.0 and **Sigma Iterations** at 3, from 1 to 20.'
  - name: PSF Type
    kind: combo
    default: Auto
    options: [Auto, Gaussian, Moffat]
    effect: >-
      Chooses the profile fitted for **PSF Flux**, with **Moffat Beta** at 4.0 for Moffat, **Max Stars** at 24,576,
      **Min SNR** at 40 and **Include clustered sources** on.
    note: The scale is the median ratio of the paired stars' fitted volumes. **Min SNR** leaves faint stars out of it, not out of the detection.
  - name: Hot Pixel Radius
    kind: spinbox
    default: "0"
    widget: {min: 0, max: 10, step: 1}
    effect: Sets a radius over which hot pixels are suppressed before the measurement. **Noise Reduction** beside it, also at 0, sets a radius of smoothing.
    tradeoff: Both can blur the small structure the measurement reads.
  - name: Use local scale k(x,y)
    kind: check
    default: "Off"
    effect: Lets the scale vary across the frame too, smoothed over **Smoothing**. It is experimental.
  - name: OK
    kind: button
    default: ""
    effect: Gives the values to **Stacking Settings**, whose **OK** keeps them. **Reset to Defaults** and **Cancel** are beside it.
```

```controls
window: Recommended Stacking Settings
controls:
  - name: Parameter comparison
    kind: readout
    default: ""
    effect: >-
      Shows **Current Value** and **Recommended** for **Stack Method**, **Normalization**, **Rejection Method** and,
      for lights, **Weighting** and **Pixel Fraction**. A change is green, the current value struck out.
    range: The advice follows the group's type and number of frames, and the reasons are written under it.
  - name: Drizzle Viability Analysis
    kind: readout
    default: UNAVAILABLE
    effect: >-
      Rates drizzle **RECOMMENDED**, **POSSIBLE**, **NOT RECOMMENDED** or **UNAVAILABLE** from the lights'
      transforms, with a grid of the sub-pixel positions they cover and a pixel fraction.
    range: Drizzle needs a transform for every light and ten lights or more.
  - name: Apply Recommended Settings
    kind: button
    default: ""
    effect: Puts the advice into **Stacking Settings**, drizzle and local normalization included.
    enabled: While something differs.
  - name: Apply Alternative
    kind: button
    default: ""
    effect: Puts the alternative the card names into **Stacking Settings**.
    tradeoff: The advice is a place to start from, not a measured result.
```

### Summary

```controls
window: Summary
controls:
  - name: Status card
    kind: readout
    default: Ready to Execute
    effect: >-
      Reads **Ready to Execute**, **Ready with Warnings** or **Cannot Execute**, with the issues to resolve and a line
      such as "35 light frames → 2 stacked image(s)".
    range: An error blocks the run, and a warning lets it go.
  - name: Issues
    kind: readout
    default: ""
    effect: Lists the errors, then the warnings, each with its group. Its tooltip gives the details, the severity and a suggestion.
    note: >-
      **Go to** beside an issue, or a click on it, opens what to fix: the output directory, a frame tab or a workflow
      tab, with the group selected.
  - name: Integration Summary
    kind: readout
    default: ""
    effect: Gives the included lights' **Total Exposure**, the time **By Filter**, the **Capture Dates**, the **Targets**, the **Source Directories** and the **Camera / Setup**.
  - name: Session Notes
    kind: field
    default: ""
    effect: Holds your notes on this data set. The report includes them.
    enabled: With an output directory set.
    persistence: >-
      Saved a moment after you type into `session_notes.txt` under `.afternight\image_integration` in the output
      directory, and read again when the directory changes.
  - name: Run History
    kind: readout
    default: ""
    effect: >-
      Lists each run kept under `history`, newest first, with its date, mode, masters and time, and each group's
      master with a thumbnail. Hover a row for its details.
    range: >-
      A run offers **Open All**, **Report**, **Log**, **Re-run** and **Delete**, and a master **Open** and **Compare**.
      **Re-run** runs the archived settings in the window's output directory without a plan, and opens nothing.
      **Delete** removes the run's folder for good, after a question.
    note: '**Report** writes the report again from the run''s record, then opens it.'
  - name: Disk space
    kind: readout
    default: "Disk: --"
    effect: Reads the free space and an estimate of what the run needs. Its tooltip divides the estimate into masters, maps, calibrated and registered lights and drizzle data.
    tradeoff: The estimate leaves out the quality maps and never stops a run.
  - name: Fix Issues First
    kind: button
    default: ""
    effect: Reads **Execute Integration** once nothing blocks the run, and prepares it and shows its **Execution Plan**.
    enabled: While no error stands.
```

```controls
window: Execution Plan
controls:
  - name: Preparing Execution Plan
    kind: readout
    default: ""
    effect: Shows the steps of the preparation, ending with "Execution plan ready.". ++esc++ cancels it.
  - name: Plan
    kind: readout
    default: ""
    effect: >-
      Lists each stage of each group with **Stage**, **Group**, **Decision**, **Reason**, **Dependencies**, **ETA** and
      **Outputs**, under "Full re-run" or a count of the groups an incremental run does.
    range: >-
      **Recompute** does the work, **Reuse** keeps earlier work and **Skip** marks a stage the group does not need. The
      **ETA** is an estimate.
  - name: Run
    kind: button
    default: ""
    effect: Starts the run as planned. **Cancel** closes the plan and runs nothing.
```

```controls
window: Image Integration - Processing
controls:
  - name: Stage timeline
    kind: readout
    default: ""
    effect: Shows **Bias**, **Dark**, **Flat**, **Calibration**, **Registration**, **Alignment**, **Normalization**, **Drizzle** and **Stacking**, each pending, running, done, skipped or failed.
  - name: Progress
    kind: readout
    default: Initializing...
    effect: Shows the group, the operation and the step, a bar that never goes back within a stage, the **Current File** and the elapsed time.
  - name: Process Output
    kind: readout
    default: ""
    effect: Logs each stage, file and warning with its time.
  - name: Cancel
    kind: button
    default: ""
    effect: Asks the run to stop at its next safe point, and reads **Cancelling...** meanwhile. Closing the window asks the same.
    tradeoff: Groups finished by then keep their masters.
  - name: Open Report
    kind: button
    default: ""
    effect: Opens `workflow_report.html` in your browser.
    enabled: At the end, when a report was written. **Close** appears then.
    note: >-
      At the end a box reads "Workflow completed successfully.", "Workflow was cancelled.", "Workflow failed:" with
      the reason, or, under **Completed with Errors**, the frames and groups that failed.
```

### The window

```controls
window: Image Integration
controls:
  - name: Output Directory
    kind: field
    default: ""
    effect: Names the folder every output, record and run history goes into. It opens with the project's folder, and empty in a new project.
    range: >-
      An existing folder you can write to, or one that can be made in a folder you can write to. An empty, unusable
      or file path blocks the run, as the Summary says.
    tradeoff: Every stage writes here, so a slow or full disk slows or stops them all.
    persistence: Part of the project, not of a preset.
  - name: ...
    kind: button
    default: ""
    effect: Opens a folder dialog, **Select Output Directory**, for the output directory.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. The project is saved first, and the next window opens with it.
    note: >-
      The window saves the project every five seconds while it changes, so a crash loses little. Its size is kept, as
      every process window's is.
  - name: Reset
    kind: button
    default: ""
    effect: >-
      Returns every setting to its default after a question: the grouping, the calibration, the registration, the
      stacking and the masters chosen by hand. The frames and the output directory stay.
    note: It also clears **Undo** and shows the first tab. **Project > Reset Settings...** does the same.
```

## How it works

1. The headers of the added files give each frame a type, a size, a filter, an object, an exposure, a temperature,
   a gain and a session, the UTC date of its exposure. Each tab groups its frames by the criteria of **Group By**,
   and your overrides take the headers' place.
2. Each group of lights is matched to a bias, dark and flat group, by your choice or by the policy, and the run
   stacks a master for every calibration group a light or a flat uses.
3. Each light is calibrated as it is read, and registered on its group's reference. Unless its group uses
   [drizzle](../../reference/glossary.md#drizzle) or lets its registered images be discarded, it is written aligned,
   in 32-bit floating point, under `registered_lights`.
4. The lights of each integration group are brought to the reference's level, weighed, cleared of rejected samples
   and combined into the master, which is written with its maps and its sky solution.
5. Each run records what it read and wrote. The next run compares the project with that record, and does again
   only what changed.

### Model

With \(L\) a light's sample, \(P\) the [pedestal](../../reference/glossary.md#pedestal), \(B\) the bias master,
\(D\) the dark master, \(k\) the dark's factor and \(F_n\) the flat master, divided by its mean over the central third
while **Enable flat auto-level** is on, the calibrated sample is

\[ L_c = \frac{L + P - B - k\,D}{F_n} \tag{1} \]

A term without a master is left out. With a dark, \(B\) is left out too, since the dark holds the bias. CCD mode uses
both, and takes \(B\) out of \(D\) once. Every sample is a
[fraction of full scale](../../reference/glossary.md#fraction-of-full-scale): an integer file's codes are divided by
their largest value, and \(P\) is the pedestal's codes divided by 65,535.

Each light \(i\) is then matched to the reference \(r\). With \(\ell\) the level and \(s\) the spread that **Scale
Estimator** measures, **Additive With Scaling** writes

\[ x' = \frac{s_r}{s_i}\,(x - \ell_i) + \ell_r \tag{2} \]

**Additive** adds the difference of the medians, and **Multiplicative** multiplies by their ratio. With
\(A_p\) the samples at pixel \(p\) that the rejection keeps, and \(w_i\) the weights, **Average** writes

\[ I(p) = \frac{\sum_{i \in A_p} w_i\, x'_i(p)}{\sum_{i \in A_p} w_i} \tag{3} \]

With equal weights and nothing rejected, the noise of \(N\) lights falls to \(1/\sqrt{N}\) of one light's. The
rejection and the weighting choose \(A_p\) and \(w_i\). Nothing clamps the master to 0 to 1.

### Boundaries

- A light integration group needs two usable lights. One with fewer, or left with fewer when lights have no
  registered file, fails while the other groups go on.
- An unreadable file, or a [calibration frame](../../reference/glossary.md#calibration-frame) of another size,
  channel count, bit depth or pattern than its group's first, is skipped and named. A light the stack cannot read
  once it has started fails its group.
- A sample that is not a number, where a flat was zero, is left out of every measurement and of the stack. A
  pixel no light covers is written as zero, so look at the edges of a mosaic or a rotated set.
- **Cancel** is noticed between safe units of work. Groups finished by then keep their masters, a group in progress
  gets its old files back, and the run is archived with what it finished.
- A run writes each image under a temporary name and renames it when complete. A stopped run leaves no half-written
  file, and the next run removes what a killed one left.

### Worked example

The NGC 253 set comes from a QHY183C color camera: 35 lights of 180 s, 20 darks of 180 s, 40 flats and 30 bias
frames, all 5544 × 3684 FITS files with the pattern RGGB, in the folders `lights`, `darks`, `flats` and `biases`.

On the **Bias** tab choose **Add Folder** and pick `biases`, then do the same on **Darks**, **Flats** and **Lights**
with their folders. Choose a new, empty folder in **Output Directory**, leave every setting at its default, and
choose **Execute Integration** on **Summary**, then **Run** under the plan.

The tabs show one bias group, one dark group of 180 s and one flat group: flats are not split by exposure, so the
flats of 4.35 s and of 0.39 s of the same night make one master. The Lights tab shows two groups. The capture program
wrote `NGC253` as the object of the first 18 lights and `Target` in the other 17. Each light group takes the master
dark and the master flat, and its **Bias** cell shows `-`. The flat group finds no dark of its exposures, so its
**Dark** cell shows a warning sign and the bias master calibrates the flats.

The registration and the stack split by object too, so the run writes `master_bias_5544x3684.fit`,
`master_dark_5544x3684_~180s.fit`, `master_flat_5544x3684_2023-10-10.fit` and two masters:
`stacked_5544x3684_NGC253.fit` and `stacked_5544x3684_Target.fit`. The first one's header reads `STACKCNT` 18 and
`EXPTIME` 3240, the 18 exposures added. By equation (3), with little rejected, its noise is close to
\(1/\sqrt{18} = 0.24\) of one light's. Each registered light is a color image of 245 MB, 8.6 GB for the 35.

To stack all 35 lights in one master, choose **Hierarchical** on the **Lights** tab, right-click the **Target**
group and choose **Metadata Overrides > Set Object...**, type `NGC253`, then execute again. The plan reuses the
calibration masters, registers the one group of 35 and writes `stacked_5544x3684_NGC253.fit` again.

```figure
images:
  - image: ../../assets/images/processes/stacking-integration/image-integration-before.webp
    alt: >-
      A 100 % crop of one registered light: the middle of the galaxy NGC 253, its dust lanes lost in a coarse color
      grain over a gray background, with a few stars.
    label: One light
  - image: ../../assets/images/processes/stacking-integration/image-integration-after.webp
    alt: >-
      The same crop of the master of 18 lights: the galaxy's dust lanes and arms smooth and distinct, the background
      even, the same stars.
    label: Master of 18
caption: >-
  The middle of NGC 253 in one registered light and in the master of 18, at 100 % in the Linear display. Each was
  given Background Neutralization at its defaults, then one Histogram Transformation at Shadows 0.0200, Midtones
  0.25 and Highlights 0.1100.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| One night, one target, with darks and flats | Every setting at its default | Read each light group's cells on **Calibration**, and the Summary's warnings. |
| Several nights with flats of each night | **Session** on for lights and flats | **Max session gap** only for a night without flats. |
| Many dithered, undersampled lights | **Recommended Settings...**, then **Enable Drizzle** at 2× | **Pixel Fraction** lower only with many lights, and check the coverage map. |
| A mosaic of panels | **Mosaic** and **Enable Overlap Normalization** | A plate solver for the panels the stars cannot place. |

## When it goes wrong

!!! danger "A floating-point light must hold fractions of full scale"
    A floating-point light is calibrated as its samples are, while integer masters and the pedestal are divided to
    fractions. A light written in [ADU](../../reference/glossary.md#adu), with samples up to 65,535, is calibrated
    65,535 times off, and nothing warns. Convert it to fractions first, or give it only masters made from such
    frames, and no pedestal.

!!! warning "Generate 64-bit images changes nothing"
    No stage reads the check box. Every master is a 32-bit floating-point file whatever it says.

!!! warning "The run history is not a backup"
    `history` keeps the newest runs until both limits hold: 20 runs and 5 GiB. Copy a run you want to keep.

The Summary reads **Cannot Execute** and the button reads **Fix Issues First**
:   **Cause.** No light is included, or the output directory is empty, not a folder, or cannot be written to.
:   **Fix.** Choose **Go to** beside each issue, add lights, and choose a folder you can write to.

The run writes two masters where you expected one
:   **Cause.** The lights' headers name two objects, two filters or, with the boxes on, two nights or exposures.
:   **Fix.** Give the lights one object with **Set Object...**, or turn off the **Group by** box that splits them.

"Missing flat calibration match", or a red mark in the light table
:   **Cause.** No flat group matches the light group's size, filter, night or settings, or you chose **Do not use**.
:   **Fix.** Add the flats of that night and filter, allow the session fallback, or choose a master with a right
    click.

"Using fallback dark from nearby session"
:   **Cause.** No dark has the light's exposure, and one within **Exposure tolerance** was taken.
:   **Fix.** Add darks of the lights' exposure, or accept the fallback after looking at a calibrated light.

**Calibrated** stays disabled
:   **Cause.** The light's masters do not exist yet, its group is **Pre-Calibrated**, or CCD mode lacks a bias.
:   **Fix.** Read its tooltip, then run once to build the masters or choose existing ones.

A box titled **Registration Records**, and the run does not start
:   **Cause.** The output directory's `registration_metrics.db` was written in a format this version does not read.
    It is left unchanged.
:   **Fix.** Choose another output directory.

"Nothing to Process" with "No changes detected since the last successful run."
:   **Cause.** The project and every output match the last run, so there is nothing to do.
:   **Fix.** Use the masters you have, or turn on **Reprocess existing** to compute everything again.

The log says a light "has no world-coordinate solution and no plate solver is configured"
:   **Cause.** Its stars did not match, and only a plate solve could place it.
:   **Fix.** Set up a solver on the **Plate Solving** page of the settings, or improve the star match.

The log says "star pairs matched" and how many were needed, and lights stay unregistered
:   **Cause.** Too few stars matched the reference or a light near it.
:   **Fix.** Choose a reference with more stars, lower **Threshold**, or raise **Maximum Stars per Frame**.

An **Execution Error** box that reads "Unknown", a setting and "in group", and the run does not start
:   **Cause.** A project edited by hand, or written by another version, stores a method this version does not know.
:   **Fix.** Open the group's **Stacking Settings...**, choose the setting again and choose **OK**.

"Not enough registered frames to integrate after skipping missing outputs."
:   **Cause.** The group has fewer than two lights with a registered file.
:   **Fix.** Include more lights, or register the group again with **Run Registration**.

"The last Image Integration session could not be restored", and the window opens empty
:   **Cause.** The saved project holds a value no dialog writes. It is kept as it is, and the window saves nothing
    over it.
:   **Fix.** Import an exported project, or add the frames again, and export the project before you close the
    window.

## Related

- [Image Integration Lite](image-integration-lite.md) and the [Quick Start](../../getting-started/quick-start.md),
  the same workflow with its choices made for you.
- [Image Integration Reference](image-integration-reference.md), for the matching rules, the files a run writes, how
  a run decides what to reuse, and every message.
- [Image Calibration](../calibration/image-calibration.md), [Image Registration](../registration-alignment/image-registration.md)
  and [Image Stacking](image-stacking.md), one step at a time on files you list.
- [Plate Solving](../registration-alignment/plate-solving.md), to set up the solver some options need.
- [Image Viewer](../../interface/image-viewer.md), for the frame preview and to look at a master with Autostretch.
- [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md),
  [Spectrophotometric Color Calibration](../color-calibration/spectrophotometric-cc.md) and
  [Histogram Transformation](../stretching/histogram-transformation.md), the steps that follow on a linear master.

## Credits

- Color-filter frames are debayered by methods adapted from RawTherapee's demosaic code as
  [librtprocess](https://github.com/CarVac/librtprocess) packages it, under GPL-3.0-or-later.
- The star detection and its point-spread-function fit are adapted from
  [Siril](https://gitlab.com/free-astro/siril)'s star finder and [PSF](../../reference/glossary.md#psf) fit, by
  Francois Meyer and team free-astro, under
  GPL-3.0-or-later.
- The drizzle corner interpolation is adapted from Siril's routine, which follows the
  [STScI drizzle library](https://github.com/spacetelescope/drizzle)'s, under the BSD 3-Clause notice of the
  Association of Universities for Research in Astronomy (AURA).
- The auto-adaptive weighted mean is adapted from [DeepSkyStacker](https://github.com/deepskystacker/DSS)'s routine,
  by David C. Partridge with Michael A. Covington and Simon C. Smith, under the BSD 3-Clause License of Luc Coiffier
  and the DeepSkyStacker contributors.
- The median selection networks of the normalization analysis are
  [RawTherapee](https://github.com/Beep6581/RawTherapee)'s, by Flössie and Ingo Weyrich, under GPL-3.0-or-later.
- **About > About AfterNight** credits these works and reproduces the BSD notices.
