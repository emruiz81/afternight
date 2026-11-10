---
kind: interface
summary: >-
  How the workspace holds image views and the Session Console in areas of tabs, side by side or floating, and how to
  group, split, fit, maximize and lock them.
menu: Window
rewritten: true
---
# Docking Panels

The [dock workspace](../reference/glossary.md#dock-workspace) holds every image or
[sequence](../reference/glossary.md#sequence) view, and the [Session Console](log-viewer.md), in areas. An area shows
its views as tabs. Areas sit side by side or one above another, and an area can float in a window of its own.
Arranging them changes only what you see: no image, [history](../reference/glossary.md#history) or unsaved state
changes.

Only image views and the Session Console dock. Process windows, Image History, the header viewers and Settings are
windows of their own. AfterNight does not keep the arrangement: every start opens on the [Dashboard](dashboard.md).

```figure
owed: true
image: ../assets/images/interface/docking-panels.webp
alt: >-
  The AfterNight main window with three image views side by side, each in an area of its own with a tab, a toolbar
  and its picture: the red and white clouds of the Carina Nebula on the left, two gray pictures of the same field in
  the middle and on the right. The Window menu is open under the menu bar and lists Lock Layout, a greyed Unlock
  Layout, Group All Images, which is highlighted, Split All Images and Fit Images To Window. Four layout buttons
  follow the Open, Save as and Close buttons on the toolbar.
caption: >-
  Three linear masters opened one after another, each in an area of its own, with the Window menu open on Group All
  Images.
pins:
  - {n: 1, x: 32.5, y: 16.0}
  - {n: 2, x: 15.0, y: 6.7}
  - {n: 3, x: 42.0, y: 9.8}
  - {n: 4, x: 63.2, y: 9.8}
  - {n: 5, x: 34.5, y: 85.0}
```

## What you see

1. **The Window menu.** **Lock Layout** and **Unlock Layout**, then the three commands that arrange every view at
   once: **Group All Images**, **Split All Images** and **Fit Images To Window**.
2. **The layout buttons.** The same lock, group, split and fit, on the toolbar after **Open**, **Save as...** and
   **Close**.
3. **The tabs.** One for each view of the area: its name, an icon for an image or a sequence, and a cross that
   closes it. The tab of the view you work in carries an accent line along its top. Hover over a tab for the
   summary of its image.
4. **The area's buttons.** **Float / Detach** and **Close Tab Group**, at the right end of the tab bar.
5. **The areas.** Each holds one or more views, at least 100 by 100 pixels. Drag the bar between two areas to give
   one more room.

## What you can do

### Know where a new view goes

- Each **File > Open** puts its views in a new area at the right of the workspace. The files you choose together in
  one Open are tabs of one area.
- A view made from an image opens as a tab beside it: a duplicate, an extracted channel or frame, a converted copy,
  a snapshot of a process window.
- **View > Session Console** docks the console across the bottom.
- Opening a view while another is maximized restores that one first.

Click a tab, or anywhere in an area, to make its image the [active image](../reference/glossary.md#active-image), or
choose its name in the status bar's box. Clicking the Session Console leaves the active image as it was.

### Arrange every view at once

1. Choose **Window > Group All Images**. Every view becomes a tab of one area, and the active image stays in front.
2. Choose **Window > Split All Images**. Every view gets an area of its own, in areas of equal size.
3. Choose **Window > Fit Images To Window**. Every view fits its image to its area.

```figure
owed: true
image: ../assets/images/interface/docking-panels-grouped.webp
alt: >-
  The same main window after Group All Images: one area fills the workspace, with three tabs along its top, and the
  Carina Nebula master in front.
caption: >-
  The three masters after Window > Group All Images: one area with three tabs.
```

To arrange only the views of one window, right-click a view and open **Current Window**. Its commands leave the
views of the other windows where they are.

### Move a view

With the layout unlocked, drag a tab:

- along its tab bar to change the order;
- onto another area's tab bar to join it;
- to an edge of an area, where a marker shows, to split that area;
- out of the main window to float it.

Double-click a tab to float its view in a window of its own, and again to dock it back. Double-click the empty part
of a tab bar to do the same with the whole area. **Float / Detach** floats the area, and docks it back from a
floating window.

### Give one view the whole workspace

Choose **Maximize image view** on the view's toolbar, **Image > View > Maximize View** or ++f11++. The view fills the
workspace and the other areas, the Session Console among them, are hidden. Choose it again, now **Restore View**, or
press ++f11++ to put the view back in its area.

### Lock the layout

Choose **Window > Lock Layout** or the lock button. With an image open, a right-click on the main window outside the
views offers both entries too. Tabs and areas can no longer be dragged or double-clicked into another place. Unlock
the same way.

### Close views

The cross on a tab closes its view, and **Close Tab Group** closes every view of the area. A view with unsaved
changes asks first, and the process windows open on its image close with it. Closing the last view brings the
Dashboard back unless the Session Console is still open.

## Controls

### The Window menu and the layout buttons

```controls
window: The Window menu and the layout buttons
controls:
  - name: Lock layout
    kind: toolbutton
    default: "Off"
    effect: The first layout button. It locks the layout, and its tooltip then reads **Unlock layout** and it unlocks it. **Window > Lock Layout** and **Window > Unlock Layout** do the same, and the one that matches the present state is greyed.
    enabled: In the workspace. Disabled on the Dashboard, where the whole toolbar group and the **Window** menu are, and while a view is maximized, when both menu entries are greyed too.
    tradeoff: The lock stops dragging, the drop markers and the double-click gestures. It does not stop **Group All Images**, **Split All Images**, **Fit Images To Window**, **Float / Detach**, choosing a tab or closing a view.
    persistence: For the session. **Lock dock layout by default**, on the Interface page of Settings and kept as `interface/lockLayoutByDefault`, locks it at every start. Changing that setting does not lock or unlock the running workspace.
    note: After a view maximized while the layout was already locked is restored, both menu entries stay greyed. The first press of the button locks again and makes **Unlock Layout** available, and a second press unlocks.
  - name: Group All Images
    kind: toolbutton
    default: ""
    effect: Puts every image and sequence view in one area as tabs, at the left of the main window, and keeps the active image active.
    enabled: With an image open. Disabled while a view is maximized.
    tradeoff: Views in floating windows come back into the main window. No command undoes an arrangement.
    scope: Every view, never the Session Console.
  - name: Split All Images
    kind: toolbutton
    default: ""
    effect: Gives every image and sequence view an area of its own, makes the areas equal and keeps the active image active.
    range: "From one to sixteen views it builds a fixed grid: two side by side, three as two above each other beside one, four as two by two, nine as three by three, sixteen as four by four. Beyond sixteen, every four more views make another column at the right."
    enabled: As **Group All Images**.
    tradeoff: The grid follows the count of views, not what they show, and many views leave each a small area. Many views in sight also cost memory and drawing time; group them into tabs, or maximize one, when the application slows. Floating views come back into the main window.
    scope: Every view, never the Session Console.
  - name: Fit Images To Window
    kind: toolbutton
    default: ""
    effect: Fits each view's image to its area, as each view's own **Fit To Window** would.
    enabled: With an image open, and also while a view is maximized.
    tradeoff: A fitted picture is reduced. Look at 100 % before you judge noise, stars or sharpness.
    scope: Every image view, floating ones included. It changes the zoom only.
    note: Lock, group, split and fit have no shortcut. ++ctrl+shift+f++ is the active view's own **Fit To Window**.
```

### An area

```controls
window: An area
controls:
  - name: Tab
    kind: action
    default: ""
    effect: A click makes its view the active image. A drag moves the view, and a double-click floats it or docks it back.
    enabled: Dragging and double-clicking only while the layout is unlocked.
    note: Right-click the tab bar for a menu of the area's tabs.
  - name: Tab summary
    kind: readout
    default: ""
    effect: The tooltip of a tab, and of a floating window's title, shows the summary of the image, the SER sequence or the image list.
    range: A video sequence shows none, nor does the empty part of the tab bar. A still image an edit is writing at that moment shows none either; hover again.
  - name: Close cross
    kind: button
    default: ""
    effect: Closes the view, after "The image '...' has unsaved changes, are you sure you want to close it?" when it has some, and after a message titled "Close image with process attached" that names the process windows open on it. A sequence stops playing, and those windows close.
    tradeoff: Nothing is saved for you. Answer No to keep the view open.
  - name: Float / Detach
    kind: toolbutton
    default: ""
    effect: Moves the whole area into a floating window with a title bar of its own, or docks it back from there.
    tradeoff: In a floating window, **Maximize image view** and **Image > View > Maximize View** are disabled, and the **Current Window** commands act on that window alone.
  - name: Close Tab Group
    kind: toolbutton
    default: ""
    effect: Closes each view of the area in turn, each with its own question.
    range: It stops at the first question you cancel, and the views after it stay open.
  - name: Bar between areas
    kind: action
    default: ""
    effect: Dragging it moves the border between the two areas.
  - name: Empty part of the tab bar
    kind: action
    default: ""
    effect: Dragging it moves the whole area. A double-click floats the area, or docks it back.
    enabled: While the layout is unlocked.
```

### Current Window

```controls
window: Current Window
controls:
  - name: Group All Images
    kind: action
    default: ""
    effect: Puts the views of the window the clicked view is in as tabs of one area, and keeps the clicked view active.
    enabled: In the menu of a right-click on a view. Disabled while that view is maximized.
    scope: The main window or the one floating window that holds the clicked view.
  - name: Split All Images
    kind: action
    default: ""
    effect: Gives each view of that window an area of its own, in the same grid as **Window > Split All Images**.
    enabled: As **Group All Images**. A window with one view is left as it is.
  - name: Fit All Images to Window
    kind: action
    default: ""
    effect: Fits each view of that window to its area.
    enabled: Also while the clicked view is maximized.
```

### Maximize View

```controls
window: Maximize View
controls:
  - name: Maximize View
    kind: action
    default: "Off"
    effect: "**Image > View > Maximize View**, **Maximize image view** on the view's toolbar and ++f11++ give the active view the whole workspace. The entry then reads **Restore View**, the button **Restore image view**, and both put the view back in its area."
    enabled: For a docked view. Disabled for a view in a floating window, but ++f11++ is not refused there; dock the view before you press it.
    tradeoff: While a view is maximized, **Lock layout**, **Group All Images** and **Split All Images** are disabled, and the other views and the Session Console are hidden. **Fit Images To Window** stays enabled.
    scope: The view only. Nothing is copied or written, and the image, its history and its header stay as they were.
    persistence: An unlocked layout is locked while the view is maximized and unlocked when it is restored. A locked layout stays locked.
    note: Choosing another image in the status bar's box maximizes that one in its place. ++f11++ works while the view has the keyboard focus.
```

## When it goes wrong

The **Window** menu or the layout buttons are greyed
:   **Cause.** The Dashboard is showing, or a view is maximized.
:   **Fix.** Open an image, or press ++f11++ on the maximized view to restore it.

A tab cannot be dragged, and a double-click does nothing
:   **Cause.** The layout is locked.
:   **Fix.** Choose **Window > Unlock Layout** or the lock button.

**Lock Layout** and **Unlock Layout** are both greyed after a view was restored
:   **Cause.** The view was maximized while the layout was locked, and the restore leaves both entries greyed.
:   **Fix.** Press the lock button once to lock again, and a second time to unlock.

**Group All Images** pulled a floating window back into the main window
:   **Cause.** The **Window** commands arrange every view, floating or not.
:   **Fix.** Float the area again, and use **Current Window** inside a floating window to arrange it alone.

**Maximize image view** is greyed
:   **Cause.** The view is in a floating window.
:   **Fix.** Choose **Float / Detach** to dock its area, or double-click its tab, then maximize it.

The arrangement is gone after a restart
:   **Cause.** AfterNight keeps only the main window's size and place, with **Remember window size and position**
    (`interface/rememberWindowSize`), which is on unless you turn it off. The note "Window layout is automatically saved when closing the application" in
    Settings means that alone.
:   **Fix.** Arrange the views again, with **Group All Images** or **Split All Images** for a start.

## Related

- [Interface Overview](overview.md), for the main window around the workspace.
- [Image Viewer](image-viewer.md), for a view's own zoom, **Fit To Window** and **Maximize image view**.
- [Toolbar & Menus](toolbar-and-menus.md), for the other menus and the view's right-click menu.
- [Session Console](log-viewer.md), the other panel that docks.
- [Process Windows](process-windows.md), for the windows that close with their image.
- [Settings](settings-dialog.md), for **Lock dock layout by default** and **Remember window size and position**.

## Credits

- [KDDockWidgets](https://github.com/KDAB/KDDockWidgets), by Klarälvdalens Datakonsult AB (KDAB), under the GNU
  General Public License version 2.0 or 3.0, draws the areas, the tabs, the floating windows and their gestures.
