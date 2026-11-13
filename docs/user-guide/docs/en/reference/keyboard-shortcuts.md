---
kind: reference
summary: >-
  Every key AfterNight defines, by the window or the part that must have the keyboard: the main window, an image
  view, a process window and its preview, the Session Console, Image Integration, Blink Comparator and Mask Drawing.
---
# Keyboard Shortcuts

This page lists every key AfterNight defines, grouped by the place where the key works: the main window, an image
view, a process window, the Session Console, Image Integration, Blink Comparator and
[Mask Drawing](../processes/masking/mask-drawing.md). A key runs the same command as its menu entry or button, with
the same questions and the same conditions. The keys are fixed: no setting changes them.

## Where a key works

- **A key of a menu** works in the window that holds the menu, while its entry is enabled. A process window is a
  window of its own, which the main window's keys do not reach. Image Integration's window has menus and keys of its
  own.
- **A key of an image view** works while the view's picture has the keyboard focus. Click the picture first.
- **A key of a list, a table or a chart** works while that control has the focus.
- **A key of Mask Drawing** works while any part of its window has the focus.
- **The Session Console's keys** work in the window that holds the console: the main window while it is docked, its
  own window while it floats.
- **Some controls read the key itself** and not every modifier. An image view does not look at ++alt++. The process
  windows' ++esc++, Image Integration's frame list, chart and issues, and Blink Comparator look at the key alone.
  Press the keys as this page writes them.
- **The keys every window has** are not listed: typing and moving in a text box, ++tab++ between controls, the
  underlined letters of a menu with ++alt++, and in a dialog ++enter++ for the default button and ++esc++ to close
  it, as in [Image History](../interface/image-history.md) and [Settings](../interface/settings-dialog.md).
- **The keyboard layout** decides where a key is: ++ctrl+plus++ can need ++shift++, and ++bracket-left++ and
  ++bracket-right++ can sit elsewhere. A desktop, an input method or another program can take a key such as ++f1++,
  ++f5++, ++f11++ or ++ctrl+space++ before AfterNight receives it. Use the menu entry or the button then.

## The main window

These keys work while the main window is active. [Toolbar & Menus](../interface/toolbar-and-menus.md) describes each
command.

| Keys | Command | Works when |
| --- | --- | --- |
| ++ctrl+o++ | **File > Open** | Always |
| ++ctrl+shift+s++ | **File > Save as...** | An image or [sequence](glossary.md#sequence) view is active |
| ++ctrl+z++ | **Edit > Undo** | The [active image](glossary.md#active-image) has a step to take back |
| ++ctrl+y++ | **Edit > Redo** | After an Undo |
| ++ctrl+plus++ | **Image > View > Zoom In** | An image is active |
| ++ctrl+minus++ | **Image > View > Zoom Out** | An image is active |
| ++ctrl+0++ | **Image > View > Zoom 100%** | An image is active |
| ++ctrl+comma++ | **Edit > Settings...** | Always |
| ++f1++ | **Help > User Guide**. In a process window, the guide opens at that process's page. | Always |
| ++ctrl+f++ | Puts the cursor in the [Dashboard](../interface/dashboard.md)'s search box and selects its text | The Dashboard shows its recent files. The box is hidden while the list is empty. |

**File > New Sequence...** has no key. **Image > View > Fit To Window** and **Maximize View** have none in the menu:
a view's own keys do the same.

## An image view

These keys act on the view whose picture has the keyboard focus, and on its image and
[history](glossary.md#history). Held down, each of them but ++ctrl+z++ and ++ctrl+y++ acts once.
[Image Viewer](../interface/image-viewer.md) describes each button.

| Keys | Same as | Works when |
| --- | --- | --- |
| ++f11++ | **Maximize image view**, and again to restore the view | The view is docked. Dock a floating view before you press it. |
| ++ctrl+shift+f++ | **Fit To Window** | Always |
| ++ctrl+z++ | **Undo** | The image has a step to take back |
| ++ctrl+y++ | **Redo** | After an Undo |
| ++ctrl+shift+z++ | **Image History** | The view holds an image or a sequence |
| ++ctrl+shift+a++ | **Autostretch Settings** | **Display Mode** reads **Autostretch** and no **Autostretch Settings** window of this view is open |
| ++ctrl+shift+x++ | **EXIF Viewer** | Always. Each press opens another window, empty when the file stated no field. |
| ++ctrl+shift+h++ | **FITS Header** | Always. Each press opens another window, empty when the image carries no card. |
| ++ctrl+shift+t++ | **Statistics** | Its button is on the toolbar and enabled |
| ++ctrl+h++ | **Histogram** | Its button is on the toolbar and enabled |
| ++ctrl+shift+m++ | **Summary** | Its button is on the toolbar and enabled |
| ++ctrl+shift+w++ | **Show Astrometry** | Its button is on the toolbar: a still image with a [plate solution](glossary.md#plate-solution) |
| ++ctrl+1++ | **Equatorial Grid** | **Astrometry Settings** is on the toolbar and enabled, which needs **Show Astrometry** on |
| ++ctrl+2++ | **Deep-Sky Annotations** | As ++ctrl+1++ |
| ++ctrl+3++ | **Star Labels** | As ++ctrl+1++ |
| ++ctrl+4++ | **Detected Stars** | As ++ctrl+1++ |
| ++ctrl+5++ | **SIP Distortion Map** | As ++ctrl+1++ |
| ++ctrl+shift+p++ | **Astrometry Info** | Its button is on the toolbar and enabled: a solved still image |
| ++ctrl+shift+l++ | **Annotate Image** | Its button is on the toolbar: a solved still image |
| ++ctrl+p++ | **Plate Solve** | Its button is on the toolbar: a still image, not a sequence |

!!! warning "A key whose button sits in More does nothing"
    The keys whose condition is a button on the toolbar act through that button: ++ctrl+shift+t++, ++ctrl+h++,
    ++ctrl+shift+m++, ++ctrl+shift+w++, ++ctrl+1++ to ++ctrl+5++, ++ctrl+shift+p++, ++ctrl+shift+l++ and
    ++ctrl+p++. When the view is too narrow, its toolbar moves buttons into its **More** menu, and their keys do
    nothing until the view is wide enough to show the button again. Widen the view, or choose the button in **More**.

[Astrometry Overlays](../interface/astrometry-overlays.md) describes the five overlays of ++ctrl+1++ to ++ctrl+5++.

## A process window and its preview

[Process Windows](../interface/process-windows.md) describes the window, its preview and its buttons.

| Keys | Does | Works when |
| --- | --- | --- |
| ++esc++ | Closes the window as its own close button does, also while a run works | In a process window, unless the control with the focus uses ++esc++ itself |
| ++space++, held | Shows the other image while you hold it: the original instead of the result, or the result while **Switch image** shows the original | The preview's picture has the focus |
| ++ctrl+shift+o++ | **Switch image**, which stays switched | The preview's picture has the focus |
| ++ctrl+shift+k++ | **Take snapshot** | The preview's picture has the focus |
| ++ctrl+shift+f++ | **Fit To Window** | The preview's picture has the focus |
| ++ctrl+shift+a++ | **Autostretch Settings** of the preview | The window offers **Display Mode** and it reads **Autostretch** |

- ++space++ switches the image once when you press it and once when you let go, and a held key is not repeated. When
  the focus leaves the preview before you let go, the pane stays switched: press ++space++ again, or choose
  **Switch image**.
- A preview has no history, so ++ctrl+z++, ++ctrl+y++ and ++ctrl+shift+z++ do nothing over it.
  ++ctrl+shift+x++ and ++ctrl+shift+h++ open their window over a preview too.
- A still preview, such as the one beside Image Integration's frame list, takes ++ctrl+shift+f++ and, when it offers
  **Display Mode**, ++ctrl+shift+a++.

## Session Console

These keys work in the window that holds the [Session Console](../interface/log-viewer.md): the main window while it
is docked, its own window while it floats.

| Keys | Command | Works when |
| --- | --- | --- |
| ++ctrl+f++ | **Search...**, which opens **Search log** | The console is open and no **Search log** window is |
| ++ctrl+s++ | **Export visible session text** | The console is open |
| ++enter++ | **Find next**, in **Search log** | **Find** holds text |

**Export visible session text** writes the lines the console shows, not the whole [log file](glossary.md#log-file).

## Image Integration

Image Integration's window has its own menus. Its keys work while its window is active, and there ++ctrl+o++
does what this table says, not what it does in the main window.
[Image Integration](../processes/stacking-integration/image-integration.md) describes each command.

| Keys | Command | Works when |
| --- | --- | --- |
| ++ctrl+o++ | **Frames > Add Files... > Auto-sort by file type...** | Always |
| ++ctrl+shift+o++ | **Frames > Add Folder... (Recursive) > Auto-sort by file type...** | Always |
| ++delete++ | **Frames > Remove Selected** | The frame list of the tab shown, or of the frame tab you used last, has a selection |
| ++ctrl+shift+delete++ | **Frames > Remove All Frames...** | The project holds a frame |
| ++ctrl+g++ | **Frames > Go to Group...** | Always |
| ++ctrl+i++ | **Project > Import Project...** | Always |
| ++ctrl+e++ | **Project > Export Project...** | Always |
| ++ctrl+num-enter++ | **Run > Execute Integration** | Nothing in the project blocks a run |
| ++ctrl+1++ to ++ctrl+8++ | **Navigate > Bias**, **Darks**, **Flats**, **Lights**, **Calibration**, **Registration**, **Stacking** and **Summary** | Always |
| ++shift+f1++ | **Help > Keyboard Shortcuts**, the window's list of keys | Always |
| ++f5++ | Refreshes the window: its theme, the Registration and Stacking tables, the issue badges and the tabs' labels | Always |
| ++ctrl+f++ | Puts the cursor in the search box of the frame tab shown and selects its text. A keyboard's Find key does the same. | A frame tab is shown: Bias, Darks, Flats or Lights |

- **Execute Integration** takes ++ctrl++ with the **Enter** key of the numeric keypad. ++ctrl++ with the main
  **Enter** key does nothing.
- **Edit > Undo** and **Edit > Redo** have no key. The **Keyboard Shortcuts** list names ++ctrl+z++ and ++ctrl+y++,
  and the questions before removing frames end with "You can undo this action with Ctrl+Z.", but both keys do
  nothing in this window. Choose **Edit > Undo**.
- ++ctrl+1++ to ++ctrl+5++ choose tabs here. Over an image view they turn overlays on and off.

These keys work while one control of the window has the focus:

| Control | Keys | Does |
| --- | --- | --- |
| The frame list | ++ctrl+a++ | Selects every frame |
| The frame list | ++esc++ | Clears the selection. The window stays open. |
| The frame list | ++home++, ++end++ | Selects the first frame of the first group that has one, or the last of the last. With no frame, the first or the last group. |
| **Registration Groups**, **Integration Groups** | ++enter++, ++num-enter++ | Opens the selected group's settings. Only the table itself takes the key, not a cell being edited. |
| **Frame Quality**, on the Registration tab | ++delete++, and ++ctrl+d++ on Linux | Chooses **Exclude Selected** for the selected lights, when it is enabled |
| The chart under **Frame Quality** | ++left++ or ++up++, ++right++ or ++down++ | Selects the previous or the next light, in the chart's order. With none selected, the previous is the last and the next the first. Neither goes past an end. |
| The chart under **Frame Quality** | ++home++, ++end++ | Selects the first or the last light, in the chart's order |
| A tab's issue badge | ++enter++, ++space++ | Shows the badge's tab |
| An issue on the Summary tab | ++enter++, ++space++ | Opens what the issue points at, as **Go to** does |

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
| ++esc++ | Closes the comparator. The lights you excluded are then excluded from the project, which **Edit > Undo** takes back. |

## Mask Drawing

These keys work while any part of the [Mask Drawing](../processes/masking/mask-drawing.md) window has the focus,
where you paint a [mask](glossary.md#mask).

| Keys | Does |
| --- | --- |
| ++bracket-left++, ++bracket-right++ | Lowers or raises **Size** by 1, from 1 to 200 |
| ++x++ | Switches between **Brush** and **Eraser**, and chooses **Brush** from any other tool |
| ++f++ | Chooses **Fill** |
| ++g++ | Chooses **Gradient** |
| ++delete++ | Removes the selected shape. Painted strokes stay. |
| ++ctrl+z++ | Takes back the last change to the drawing. ++alt+backspace++ on Windows, ++f14++ on Linux and a keyboard's Undo key do the same. |
| ++ctrl+shift+z++ | Makes again the change taken back. ++ctrl+y++ and ++alt+shift+backspace++ on Windows, and a keyboard's Redo key, do the same. |

These two keys move through the drawing's own steps, not the image's history. In this window ++ctrl+shift+z++
redoes a drawing step: it opens Image History only over an image view.

## Troubleshooting

A key does nothing
:   **Cause.** Another window is active, the control the key belongs to does not have the focus, the command is
    disabled, or the desktop took the key.
:   **Fix.** Click the view, the list or the window first, and check that the menu entry or the button is enabled. Use
    the entry or the button when the key still does nothing.

A view's key does nothing although its button works
:   **Cause.** The view is narrow and the button sits in the toolbar's **More** menu, or **Show Astrometry** is off for
    ++ctrl+1++ to ++ctrl+5++.
:   **Fix.** Widen the view, or choose the button in **More**. Turn **Show Astrometry** on before the overlay keys.

++ctrl+o++ adds frames instead of opening an image
:   **Cause.** Image Integration's window is active, and the key adds files there.
:   **Fix.** Click the main window, then press ++ctrl+o++.

++ctrl+1++ to ++ctrl+5++ switch tabs instead of overlays
:   **Cause.** Image Integration's window is active.
:   **Fix.** Click the picture of a solved image's view, then press the key.

++ctrl+f++ searches the wrong place
:   **Cause.** The Dashboard, the Session Console and Image Integration each have a search of their own.
:   **Fix.** Click the window or the tab whose search you want first.

++delete++ does nothing in **Frame Quality**
:   **Cause.** The frame list you used last still holds a selection, so two commands share the key.
:   **Fix.** Clear that selection, or choose **Exclude Selected**.

++esc++ does not close Image Integration
:   **Cause.** The frame list has the focus and clears its selection.
:   **Fix.** Click elsewhere in the window and press ++esc++ again, or choose **Close**.

++ctrl+enter++ does not start Image Integration
:   **Cause.** The key needs the **Enter** of the numeric keypad.
:   **Fix.** Press ++ctrl+num-enter++, or choose **Run > Execute Integration**.

++ctrl+z++ does nothing in Image Integration
:   **Cause.** Its **Undo** has no key, although its list of keys and its questions name one.
:   **Fix.** Choose **Edit > Undo** in its window.

++ctrl+n++ does nothing
:   **Cause.** The Dashboard's hint names it, but **New Sequence** has no key.
:   **Fix.** Choose **File > New Sequence...**, or the Dashboard's **New Sequence** card.

++ctrl+y++ does not redo in Mask Drawing on Linux
:   **Cause.** Linux gives ++ctrl+y++ no redo.
:   **Fix.** Press ++ctrl+shift+z++, or choose **Redo**.

++space++ does something unexpected
:   **Cause.** ++space++ presses a focused button, issue or badge, holds the original in a preview, and plays Blink
    Comparator.
:   **Fix.** Click the control you mean first, or use its button.

## Credits

- [QKeySequence](https://doc.qt.io/qt-6/qkeysequence.html), [QAction](https://doc.qt.io/qt-6/qaction.html) and
  [QShortcut](https://doc.qt.io/qt-6/qshortcut.html), the Qt 6 documentation of the platform keys of undo, redo,
  delete, find and help, of a shortcut's reach, and of a key two commands share.
