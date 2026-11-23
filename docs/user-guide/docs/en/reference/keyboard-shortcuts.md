---
kind: reference
summary: >-
  Every key AfterNight defines, by the part that must have the keyboard: the main window, an image view, a task and its
  canvas, the rail and the command palette, the Session Console, Image Integration, Blink Comparator and Mask Drawing.
---
# Keyboard Shortcuts

This page lists every key AfterNight defines, grouped by the place where the key works: the main window, an image
view, a [task](glossary.md#task) and its canvas, the [rail](glossary.md#rail) and the
[command palette](glossary.md#command-palette), the Session Console, Image Integration, Blink Comparator and
[Mask Drawing](../processes/masking/mask-drawing.md). A key runs the same command as its menu entry or button, with
the same questions and the same conditions. The keys are fixed: no setting changes them. Every key of AfterNight 1.1
does what it did; [Coming from 1.1](../getting-started/coming-from-1-1.md) names the keys this version adds.

## Where a key works

- **A key of a menu** works anywhere in the main window while its entry is enabled, with the menu bar hidden too. On
  a task's page, the image commands act on the task's target.
- **A task floated in a window of its own** is a window the main window's keys do not reach. It answers ++esc++,
  ++ctrl+enter++ and ++f1++, and the keys of its own controls.
- **A key of a task's own,** such as those of Image Integration and **Mask Drawing**, works while the keyboard focus is inside
  the task's page, and wins there over the main window's key of the same name. With the focus on the rail or the top
  bar, the main window's key acts.
- **A key of an image view** works while the view's picture has the keyboard focus. Click the picture first.
- **A key of a list, a table or a chart** works while that control has the focus.
- **The Session Console's keys** work while its drawer is open, and in its own window while it floats.
- **Some controls read the key itself** and not every modifier. An image view does not look at ++alt++. A task's
  ++esc++, Image Integration's frame list, chart and issues, and Blink Comparator look at the key alone. Press the keys
  as this page writes them.
- **The keys every window has** are not listed: typing and moving in a text box, ++tab++ between controls, and in a
  dialog ++enter++ for the default button and ++esc++ to close it, as in [Image History](../interface/image-history.md)
  and [Settings](../interface/settings-dialog.md).
- **The keyboard layout** decides where a key is: ++ctrl+plus++ can need ++shift++, and ++bracket-left++ and
  ++bracket-right++ can sit elsewhere. A desktop, an input method or another program can take a key such as ++f1++,
  ++f5++, ++f6++, ++f11++ or ++ctrl+space++ before AfterNight receives it. Use the menu entry or the button then.

## The main window

These keys work while the main window is active. [Top Bar and Menus](../interface/top-bar-and-menus.md) describes each
command.

| Keys | Command | Works when |
| --- | --- | --- |
| ++alt++ | Shows the menu bar and gives it the keyboard. ++alt++ with a menu's underlined letter, such as ++alt+f++, opens that menu. | The menu bar is hidden, as it is unless **Always show the menu bar** is on |
| ++ctrl+k++ | **View > Command Palette...** | Always |
| ++ctrl+o++ | **File > Open** | Always |
| ++ctrl+shift+s++ | **File > Save as...** | An image or [sequence](glossary.md#sequence) view is active |
| ++ctrl+w++ | **File > Close** | An image is active. It is greyed on a task's page. |
| ++ctrl+z++ | **Edit > Undo** | The [active image](glossary.md#active-image) has a step to take back |
| ++ctrl+y++ | **Edit > Redo** | After an Undo |
| ++ctrl+plus++ | **Image > View > Zoom In** | An image is active, or a task's canvas shows |
| ++ctrl+minus++ | **Image > View > Zoom Out** | As ++ctrl+plus++ |
| ++ctrl+0++ | **Image > View > Zoom 100%** | As ++ctrl+plus++ |
| ++ctrl+comma++ | **Edit > Settings...** | Always |
| ++ctrl+shift+n++ | **View > Night**, on or off | Always |
| ++f6++ | **View > Next Region**: moves the keyboard focus to the rail, the centre, then the column | Always. On a task's page, the column is the task's. |
| ++shift+f6++ | **View > Previous Region**, the same the other way | Always |
| ++ctrl+enter++ | **Process > Apply**: the task's **Apply**, or its main button, such as **Run Integration** | A task's page shows and that button is enabled. Either **Enter** key works. |
| ++f1++ | **Help > User Guide**. On a task's page, the guide opens at that process's page. | Always |
| ++ctrl+f++ | Puts the cursor in the search box of [Home](../interface/home.md) and selects its text | Home shows its recent files. The box is hidden while the list is empty. |

**File > New Sequence...** has no key. **Image > View > Fit To Window** and **Maximize View** have none in the menu:
a view's own keys do the same.

## An image view

These keys act on the view whose picture has the keyboard focus, and on its image and
[history](glossary.md#history). Held down, each of them but ++ctrl+z++ and ++ctrl+y++ acts once.
[Image Viewer](../interface/image-viewer.md) describes the floating toolbar, and [Readout](../interface/readout.md)
the cards whose buttons several of these keys press.

| Keys | Same as | Works when |
| --- | --- | --- |
| ++f11++ | **Maximize image view**, and again to restore the view | The view is docked in its [workspace](glossary.md#workspace). Dock a floating view before you press it. |
| ++esc++ | Restores a maximized view, as ++f11++ does | A view of the workspace shown is maximized, and no other control takes the key |
| ++ctrl+shift+f++ | **Fit To Window** | Always |
| ++ctrl+z++ | **Undo** | The image has a step to take back |
| ++ctrl+y++ | **Redo** | After an Undo |
| ++ctrl+shift+z++ | **Image History** | The view holds an image or a sequence |
| ++ctrl+shift+a++ | **Autostretch Settings** | **Display Mode** reads **Autostretch** and no **Autostretch Settings** window of this view is open |
| ++ctrl+shift+x++ | **EXIF Viewer** | Always. Each press opens another window, empty when the file stated no field. |
| ++ctrl+shift+h++ | **FITS Header** | Always. Each press opens another window, empty when the image carries no card. |
| ++ctrl+shift+t++ | **Statistics** | The view holds an image |
| ++ctrl+h++ | **Histogram** | The view holds an image |
| ++ctrl+shift+m++ | **Summary** | The view holds an image |
| ++ctrl+shift+w++ | **Show Astrometry** | A still image with a [plate solution](glossary.md#plate-solution) |
| ++ctrl+1++ | **Equatorial Grid** | A solved still image with **Show Astrometry** on |
| ++ctrl+2++ | **Deep-Sky Annotations** | As ++ctrl+1++ |
| ++ctrl+3++ | **Star Labels** | As ++ctrl+1++ |
| ++ctrl+4++ | **Detected Stars** | As ++ctrl+1++ |
| ++ctrl+5++ | **SIP Distortion Map** | As ++ctrl+1++ |
| ++ctrl+shift+p++ | **Info**, Astrometry Info | A solved still image |
| ++ctrl+shift+l++ | **Annotate...**, Annotate Image | A solved still image |
| ++ctrl+p++ | **Plate Solve** | A still image, not a sequence, that can be solved |

[Astrometry Overlays](../interface/astrometry-overlays.md) describes the five overlays of ++ctrl+1++ to ++ctrl+5++.

## A task and its canvas

[Tasks](../interface/tasks.md) describes the task's page, its canvas and its column.

| Keys | Does | Works when |
| --- | --- | --- |
| ++esc++ | Closes the task as its **Close** does | The focus is inside the task's page, no run is working, and the control with the focus does not use ++esc++ itself |
| ++ctrl+enter++ | Presses **Apply**, or the task's main button | **Apply** is enabled. In a task floated in its own window too. |
| ++space++, held | Shows the other image while you hold it: the original instead of the result, or the result while **Processed** shows the original | The canvas has the focus |
| ++ctrl+shift+o++ | Switches between the result and the original, which stays switched, as **Processed** does | The canvas has the focus |
| ++ctrl+shift+k++ | **Take snapshot** | The canvas has the focus |
| ++ctrl+shift+f++ | **Fit To Window** | The canvas has the focus |
| ++ctrl+shift+a++ | **Autostretch Settings** of the canvas | The canvas's toolbar offers **Display Mode** and it reads **Autostretch** |

- ++space++ switches the image once when you press it and once when you let go, and a held key is not repeated. When
  the focus leaves the canvas before you let go, it stays switched: press ++space++ again, or choose **Processed**.
- A preview has no history of its own: on a task's page ++ctrl+z++ and ++ctrl+y++ act on the target's history, as the
  history strip under the canvas does. ++ctrl+shift+x++ and ++ctrl+shift+h++ open their window over a canvas too.
- A still preview, such as the one beside Image Integration's frame list, takes ++ctrl+shift+f++ and, when it offers
  **Display Mode**, ++ctrl+shift+a++.

## The rail and the command palette

[The Rail and Its Places](../interface/rail-and-places.md) and [Command Palette](../interface/command-palette.md)
describe them.

| Where | Keys | Does |
| --- | --- | --- |
| The rail | ++up++, ++down++ | Moves between the items |
| The rail | ++home++, ++end++ | Moves to the first or the last item |
| The rail | ++enter++, ++space++ | Shows the item: a place or a task |
| The rail | The menu key | Opens the item's menu |
| A task's item, dragged | ++esc++ | Cancels the drag |
| The command palette | ++up++, ++down++, ++page-up++, ++page-down++, ++home++, ++end++ | Moves the selection |
| The command palette | ++tab++, ++shift+tab++ | Goes to the next or the previous group, round from the last to the first |
| The command palette | ++enter++ | Runs the row selected |
| The command palette | ++esc++ | Closes the palette |

## Session Console

These keys work in the window that holds the [Session Console](../interface/log-viewer.md): the main window while its
drawer is open, its own window while it floats.

| Keys | Command | Works when |
| --- | --- | --- |
| ++ctrl+f++ | **Search...**, which opens **Search log** | The console is open and no **Search log** window is |
| ++ctrl+s++ | **Export visible session text** | The console is open |
| ++enter++ | **Find next**, in **Search log** | **Find** holds text |

**Export visible session text** writes the lines the console shows, not the whole [log file](glossary.md#log-file).

## Image Integration

Image Integration's menus are under the button of its task header. Its keys work while the keyboard focus is inside its
page, and there ++ctrl+o++ does what this table says, not what it does elsewhere in the main window.
[Image Integration](../processes/stacking-integration/image-integration.md) describes each command.

| Keys | Command | Works when |
| --- | --- | --- |
| ++ctrl+o++ | **Frames > Add Files... > Auto-sort by file type...** | Always |
| ++ctrl+shift+o++ | **Frames > Add Folder... (Recursive) > Auto-sort by file type...** | Always |
| ++delete++ | **Frames > Remove Selected** | The frame list of the page shown, or of the frame source you used last, has a selection |
| ++ctrl+shift+delete++ | **Frames > Remove All Frames...** | The job holds a frame |
| ++ctrl+g++ | **Frames > Go to Group...** | Always |
| ++ctrl+i++ | **Project > Import Project...** | Always |
| ++ctrl+e++ | **Project > Export Project...** | Always |
| ++ctrl+enter++ | **Run > Execute Integration**, with either **Enter** key | Nothing in the job blocks a run |
| ++ctrl+1++ to ++ctrl+8++ | **Navigate > Bias**, **Darks**, **Flats**, **Lights**, **Calibration**, **Registration**, **Stacking** and **Summary**, the rows of the stage list | Always |
| ++shift+f1++ | **Help > Keyboard Shortcuts**, the task's list of keys | Always |
| ++f5++ | Refreshes the page: its theme, the Registration and Stacking tables, the issue badges and the stages' labels | Always |
| ++ctrl+f++ | Puts the cursor in the search box of the frame source shown and selects its text. A keyboard's Find key does the same. | A frame source is shown: Bias, Darks, Flats or Lights |

- **Edit > Undo** and **Edit > Redo** have no key. The **Keyboard Shortcuts** list names ++ctrl+z++ and ++ctrl+y++,
  and the questions before removing frames end with "You can undo this action with Ctrl+Z.", but both keys do
  nothing on this page. Choose **Edit > Undo**.
- ++ctrl+1++ to ++ctrl+5++ choose stages here. Over an image view they turn overlays on and off.

These keys work while one control of the page has the focus:

| Control | Keys | Does |
| --- | --- | --- |
| The frame list | ++ctrl+a++ | Selects every frame |
| The frame list | ++esc++ | Clears the selection. The task stays open. |
| The frame list | ++home++, ++end++ | Selects the first frame of the first group that has one, or the last of the last. With no frame, the first or the last group. |
| **Registration Groups**, **Integration Groups** | ++enter++, ++num-enter++ | Opens the selected group's settings. Only the table itself takes the key, not a cell being edited. |
| **Frame Quality**, on the Registration stage | ++delete++, and ++ctrl+d++ on Linux | Chooses **Exclude Selected** for the selected lights, when it is enabled |
| The chart under **Frame Quality** | ++left++ or ++up++, ++right++ or ++down++ | Selects the previous or the next light, in the chart's order. With none selected, the previous is the last and the next the first. Neither goes past an end. |
| The chart under **Frame Quality** | ++home++, ++end++ | Selects the first or the last light, in the chart's order |
| A stage's issue badge | ++enter++, ++space++ | Shows the badge's stage |
| An issue on the Summary stage | ++enter++, ++space++ | Opens what the issue points at, as **Go to** does |
| The page, while frames are imported | ++esc++ | Stops the import, as **Cancel** in the band over the stages does |

When **Frame Quality** has the focus and the frame list you used last still holds a selection, **Remove Selected** and
**Exclude Selected** both take ++delete++, and the key runs neither. Clear that selection, or choose
**Exclude Selected**.

## Blink Comparator

These keys work while Blink Comparator is the active window and the control with the focus does not use the key
itself, as its table uses the arrows.

| Keys | Does |
| --- | --- |
| ++space++ | Plays or pauses |
| ++left++, ++right++ | Shows the previous or the next light, from the last back to the first and round again. Opened by **Compare** on two stacked [masters](glossary.md#master), they skip the master held fixed. |
| ++x++ | Excludes the light shown. Pressing it again does not include it again. |
| ++1++ to ++9++ | Gives the light shown that rating, in place of an earlier one. The ratings stay in the comparator. |
| ++esc++ | Closes the comparator. The lights you excluded are then excluded from the job, which **Edit > Undo** takes back. |

## Mask Drawing

These keys work while the keyboard focus is anywhere in the [Mask Drawing](../processes/masking/mask-drawing.md)
task, where you paint a [mask](glossary.md#mask).

| Keys | Does |
| --- | --- |
| ++bracket-left++, ++bracket-right++ | Lowers or raises **Size** by 1, from 1 to 200 |
| ++x++ | Switches between **Brush** and **Eraser**, and chooses **Brush** from any other tool |
| ++f++ | Chooses **Fill** |
| ++g++ | Chooses **Gradient** |
| ++delete++ | Removes the selected shape. Painted strokes stay. |
| ++ctrl+z++ | Takes back the last change to the drawing. ++alt+backspace++ on Windows, ++f14++ on Linux and a keyboard's Undo key do the same. |
| ++ctrl+shift+z++ | Makes again the change taken back. ++ctrl+y++ and ++alt+shift+backspace++ on Windows, and a keyboard's Redo key, do the same. |

These two keys move through the drawing's own steps, not the image's history. In this task ++ctrl+shift+z++
redoes a drawing step: it opens Image History only over an image view.

## Troubleshooting

A key does nothing
:   **Cause.** Another window is active, the control the key belongs to does not have the focus, the command is
    disabled, or the desktop took the key.
:   **Fix.** Click the view, the list or the task's page first, and check that the menu entry or the button is
    enabled. Use the entry or the button when the key still does nothing.

A main window key does nothing in a floating task
:   **Cause.** A task floated in a window of its own answers only ++esc++, ++ctrl+enter++, ++f1++ and its own keys.
:   **Fix.** Click the main window first, or choose **Attach** in the task's header.

A view's key does nothing although its button works
:   **Cause.** The picture does not have the keyboard focus, or **Show Astrometry** is off for ++ctrl+1++ to ++ctrl+5++.
:   **Fix.** Click the picture, then press the key. Turn **Show Astrometry** on before the overlay keys.

++ctrl+o++ adds frames instead of opening an image
:   **Cause.** The focus is inside Image Integration's page, and the key adds files there.
:   **Fix.** Click Home or a workspace in the rail, then press ++ctrl+o++.

++ctrl+1++ to ++ctrl+5++ choose stages instead of overlays
:   **Cause.** The focus is inside Image Integration's page.
:   **Fix.** Click the picture of a solved image's view, then press the key.

++ctrl+f++ searches the wrong place
:   **Cause.** Home, the Session Console and Image Integration each have a search of their own.
:   **Fix.** Click the place, the drawer or the page whose search you want first.

++esc++ does not close a task
:   **Cause.** A run is working, the focus is outside the task's page, or a control inside it uses the key, as Image
    Integration's frame list clears its selection.
:   **Fix.** Wait for the run or choose **Cancel**, click elsewhere in the page and press ++esc++ again, or choose
    **Close**.

++delete++ does nothing in **Frame Quality**
:   **Cause.** The frame list you used last still holds a selection, so two commands share the key.
:   **Fix.** Clear that selection, or choose **Exclude Selected**.

++ctrl+z++ does nothing in Image Integration
:   **Cause.** Its **Undo** has no key, although its list of keys and its questions name one.
:   **Fix.** Choose **Edit > Undo** under the button of its task header.

++ctrl+n++ does nothing
:   **Cause.** **New Sequence** has no key.
:   **Fix.** Choose **File > New Sequence...**, or Home's **New sequence**.

++ctrl+y++ does not redo in Mask Drawing on Linux
:   **Cause.** Linux gives ++ctrl+y++ no redo.
:   **Fix.** Press ++ctrl+shift+z++, or choose **Redo**.

++space++ does something unexpected
:   **Cause.** ++space++ presses a focused button, issue or badge, shows a rail item, holds the original on a task's
    canvas, and plays Blink Comparator.
:   **Fix.** Click the control you mean first, or use its button.

## Credits

- [QKeySequence](https://doc.qt.io/qt-6/qkeysequence.html), [QAction](https://doc.qt.io/qt-6/qaction.html) and
  [QShortcut](https://doc.qt.io/qt-6/qshortcut.html), the Qt 6 documentation of the platform keys of undo, redo,
  delete, find and help, of a shortcut's reach, and of a key two commands share.
