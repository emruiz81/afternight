---
kind: interface
summary: >-
  The Session Console lists AfterNight's messages since it started, each with its time and severity, so that you can
  follow what it did and read why a step failed: filter, search, export or clear them.
menu: View > Session Console
---
# Session Console

The Session Console lists AfterNight's messages since it started: what it opened, ran and saved, its warnings and its
errors, each with the time and a severity. It is a drawer of the main window, above the status line and as wide as the
centre and the [readout](../reference/glossary.md#readout) together, and it can float in a window of its own. It only shows messages, and changes no image
and no image's [history](../reference/glossary.md#history).

It keeps the messages of this session in memory. The [log file](../reference/glossary.md#log-file), which the
Logging page of [Settings](settings-dialog.md) sets up, keeps them on disk between sessions. The two are separate:
clearing or filtering the console leaves the file as it is.

```figure
image: ../assets/images/interface/log-viewer.webp
alt: >-
  The main window with the Carina Nebula master in its workspace, its readout at the right, and the Session Console's
  drawer open under them, above the status line. The drawer's toolbar reads Session Console, then holds zoom buttons,
  a wrap button, a bin, a reload arrow, a highlighted terminal icon and a gear, Show >= with a box reading info, the
  words File log: info, and at the far right a magnifier, a disk, a window icon and a cross. Below the toolbar the list
  of messages is empty, its session just cleared. At the rail's foot a terminal icon, and at the status line's right
  end a button reading Console.
caption: >-
  The Session Console's drawer open under the Carina master's workspace, its session just cleared.
pins:
  - {n: 1, x: 3.0, y: 74.6}
  - {n: 2, x: 21.7, y: 77.8}
  - {n: 3, x: 36.4, y: 77.8}
  - {n: 4, x: 48.7, y: 77.8}
  - {n: 5, x: 92.0, y: 77.8}
  - {n: 6, x: 97.2, y: 77.8}
  - {n: 7, x: 50.0, y: 85.0}
  - {n: 8, x: 3.6, y: 92.9}
  - {n: 9, x: 94.3, y: 98.4}
```

## What you see

1. **The drawer.** Its title, **Session Console**, leads its toolbar. Drag the drawer's top edge to make it taller
   or shorter.
2. **The buttons.** **Zoom In**, **Zoom Out** and **Zoom 100%** for the text size, word wrap, **Clear Session**,
   **Reload session lines**, **ANSI Colors** and **Settings**.
3. **Show >=.** The least severe kind of message the console shows.
4. **File log.** The level of the log file, as the Logging page of Settings sets it.
5. **Search and export.** **Search...** finds a word in the lines, and **Export visible session text** saves them
   to a text file. Before them, a tag counts the session's warnings and errors, once there are any.
6. **Float and Close.** **Float** moves the console into a window of its own, and the cross closes the drawer.
7. **The messages.** One line for each message: the date and time to the millisecond, the severity in its color,
   and the message.
8. **Session Console in the rail.** Opens or closes the drawer. Its count is the warnings and errors that arrived
   while the console was closed.
9. **Console in the status line.** Opens the drawer, with the same count.

## What you can do

### Open and close the console

Choose **Session Console** at the foot of the [rail](../reference/glossary.md#rail), **Console** in the status line, or **View > Session Console**. The
drawer opens above the status line, whatever place the centre shows, and the console shows every message kept since
AfterNight started, not only the ones after you open it. Close it with the rail's item, the menu entry or the cross
at the end of its toolbar.

The console is made the first time you open it and kept while AfterNight runs, so its lines, its scroll position, its
zoom and **Show >=** stay as you left them. Whether it is open, its height and whether it floats are kept for the
next start.

### Resize or float the console

Drag the drawer's top edge: the drawer is 220 pixels high at first, and between 120 pixels and 60 % of the window's
height. Choose **Float** to move the console into a window of its own, over the lower part of the main window, which
you can move and size; **Attach** in its toolbar brings it back into the drawer. Closing that window closes the
console, which opens floating again next time.

### Read a message

A line reads `2026-11-10 21:04:13.512 - info  - AfterNight version ... - engine ...`, which is the first message
of every session. The severities are `trace`, `debug`, `info`, `warn`, `error` and `fatal`. A [task](../reference/glossary.md#task) writes a line
when its run starts and when it ends or fails, as the "In the Session Console" section of
[Tasks Reference](tasks-reference.md) lists. A file that cannot be opened leaves its error here too.

!!! warning "The lines name your folders"
    A line about a file names it with its full path, and some name the folders AfterNight uses. Read an export
    before you share it, and choose **Clear Session** before you take a picture of the console.

### Choose what the console shows

Choose the least severe kind of message in **Show >=**: **info** shows `info`, `warn`, `error` and `fatal`, and
**error** only the last two. The console shows the lines again at once, from the messages it kept.

### Find a message

1. Choose **Search...** or press ++ctrl+f++. The **Search log** window opens.
2. Type the text in **Find**, tick the options you need and choose **Find next**, or press ++enter++.
3. The console selects the next place the text appears. Choose **Find next** again for the one after.

### Keep the messages for a report

Choose **Export visible session text**, or press ++ctrl+s++, then name the file. It holds the lines as the console
shows them, as plain text. Lower **Show >=** first when the report needs the lines it hides.

### Clear the console

Choose **Clear Session**. Every message kept in memory for this session is gone, and **Reload session lines** cannot
bring it back. New messages appear as they come. The log file keeps what its level let through.

## Controls

### The drawer

```controls
window: The drawer
controls:
  - name: Top edge
    kind: action
    default: "220"
    effect: Drag the drawer's top edge up to make it taller, down to make it shorter. Its tooltip reads "Drag to resize the Session Console".
    range: From 120 pixels to 60 % of the main window's height, and kept inside them when the window is resized.
    persistence: Kept between sessions as `interface/consoleDrawerHeight`.
  - name: Warnings
    kind: readout
    default: ""
    effect: A tag before **Search...** that counts the warnings and errors of the session, with the tooltip "Warnings and errors in this session".
    enabled: Shown once the session has one. **Clear Session** sets it back to none.
  - name: Float
    kind: toolbutton
    default: ""
    effect: Moves the console into a window of its own, with the title Session Console. In that window it reads **Attach**, which brings the console back into the drawer.
    persistence: Kept between sessions as `interface/consoleFloating`.
  - name: Close
    kind: toolbutton
    default: ""
    effect: Closes the drawer, as the rail's **Session Console** and **View > Session Console** do. Its tooltip reads "Close the Session Console".
    persistence: Whether the console is open is kept between sessions as `interface/consoleDrawerOpen`.
```

### The toolbar

```controls
window: The toolbar
controls:
  - name: Zoom In
    kind: toolbutton
    default: ""
    effect: Makes the text one point larger, and shows the lines again.
    range: There is no limit. **Zoom 100%** returns to the size of **Default font Size**.
    persistence: While AfterNight runs. Closing the drawer keeps it.
  - name: Zoom Out
    kind: toolbutton
    default: ""
    effect: Makes the text one point smaller, and shows the lines again.
  - name: Zoom 100%
    kind: toolbutton
    default: ""
    effect: Returns the text to **Default font Size**. The font and the colors stay as they are.
  - name: Disable word wrap
    kind: toolbutton
    default: "On"
    effect: While long lines wrap at the console's width, its tooltip reads **Disable word wrap**, and it shows them on one line each, with a scroll bar. It then reads **Enable word wrap**.
    persistence: Kept between sessions as `log/line_wrap`, written when AfterNight closes, when **ANSI Colors** changes and when the settings are applied.
  - name: Clear Session
    kind: toolbutton
    default: ""
    effect: Removes every message the console keeps for this session, and resets the search.
    tradeoff: It cannot be undone, and it asks nothing. The log file, `AfterNight.log`, is not touched.
  - name: Reload session lines
    kind: toolbutton
    default: ""
    effect: Shows the kept messages again, under the present **Show >=**, colors, font and **Max lines**, and moves to the last line.
    range: It clears the selection and the search position. It reads nothing from the log file.
    note: A change of **Show >=**, **ANSI Colors**, the zoom or the settings shows the lines again the same way.
  - name: ANSI Colors
    kind: toolbutton
    default: "On"
    effect: On, the color and style codes some messages carry are drawn - bold, faint, italic, underline, inverse, and the standard, bright, 256 and RGB colors of text and background. Off, the text is plain. Either way the codes themselves are not shown.
    tradeoff: A colored message can be hard to read on the console's background. The severity keeps its own color.
    persistence: Kept between sessions as `log/render_ansi_colors`, saved at once with every other value of the console.
    note: Its tooltip reads "Render ANSI terminal color sequences in session output when present."
  - name: Settings
    kind: toolbutton
    default: ""
    effect: Opens **Log viewer settings**, for the console's font, colors and line count.
    enabled: One settings window at a time. The button is disabled while it is open.
  - name: Show >=
    kind: combo
    default: info
    options: [error, warn, info, debug, trace]
    effect: Shows the messages of the chosen severity and every more severe one. **error** also shows `fatal`, which has no entry of its own.
    tradeoff: "**debug** and **trace** show far more lines, which take longer to show and to search."
    scope: The console only. It changes neither what AfterNight records nor the log file. Its tooltip says so.
    persistence: Kept while AfterNight runs, the drawer closed and opened again. **info** at the next start.
    note: AfterNight keeps the messages of every severity, so choosing **debug** shows the debug lines of the whole session.
  - name: File log
    kind: readout
    default: "File log: info"
    effect: Shows the level the log file keeps, as `logging.conf` states it, such as `warning` or `off`, and `info` when it states none.
    range: Read again each time the console opens, in the drawer or in its own window, each time its window is chosen, and at **Reload session lines**, so a level changed in Settings shows when you turn to the console.
    note: Its tooltip reads "Current file logging level from Settings > Logging."
  - name: Search...
    kind: toolbutton
    default: ""
    effect: Opens **Search log**. ++ctrl+f++ does the same.
    enabled: One search window at a time. The button is disabled while it is open.
  - name: Export visible session text
    kind: toolbutton
    default: ""
    effect: Asks for a file in "Export visible session text...", suggesting `session_console.txt`, and writes the console's lines to it as plain text. ++ctrl+s++ does the same.
    range: A name without `.txt` gets it. **Cancel** writes nothing. The file window asks before it replaces a file.
    tradeoff: Lines **Show >=** hides, lines past **Max lines**, colors and the search are not written.
    persistence: The folder is remembered for the next export, even when the file then cannot be written.
    note: A file that cannot be written gives "Unable to open file for writing", titled "File Error".
```

### The messages

```controls
window: The messages
controls:
  - name: Message lines
    kind: readout
    default: ""
    effect: Shows each kept message that passes **Show >=**, as `<date> <time> - <severity> - <message>`, with the severity's word in its color. You can select and copy the text, but not change it.
    range: AfterNight keeps the last 10,000 messages of the session in memory, from every part of it, in the order they arrive, whatever the Logging page's file size and count. Messages from before the console's record started, while AfterNight was starting, are not among them. The console shows at most **Max lines** lines, and a message of several lines takes several.
    tradeoff: The log file's lines also name the process and the thread, so they read differently from the console's.
  - name: Following new lines
    kind: action
    default: "On"
    effect: While the view is at the last line, it moves down with each new message. Scroll up to read, and it stays where you are while messages keep arriving. Scroll back to the bottom to follow again.
    range: Within four steps of the scroll bar's end counts as the bottom. Without word wrap, following also returns to the left edge.
```

### Search log

```controls
window: Search log
controls:
  - name: Find
    kind: field
    default: ""
    effect: The text, or the pattern, to look for in the console's lines.
    range: Only the lines the console shows are searched.
  - name: Case sensitive
    kind: check
    default: "Off"
    effect: Finds the text only with the same capitals.
  - name: Match whole word
    kind: check
    default: "Off"
    effect: Finds the text only as a whole word.
    enabled: Disabled while **Regular expression** is ticked, but a tick it already had still counts.
  - name: Regular expression
    kind: check
    default: "Off"
    effect: Reads **Find** as a pattern. Spaces in it are ignored and `#` starts a comment, so `foo bar` finds `foobar`.
    tradeoff: A pattern that is not valid finds nothing, and no message says why.
  - name: Backward
    kind: check
    default: "Off"
    effect: Searches toward the first line.
    range: A backward search never wraps around to the last line.
  - name: Find next
    kind: button
    default: ""
    effect: Selects the next place the text appears. After the last one, a forward search starts again at the top.
    enabled: While **Find** holds text. ++enter++ does the same.
    note: When nothing is found, a message titled "Search" says "No occurrences found." and the position moves to the end.
  - name: Clear
    kind: button
    default: ""
    effect: Starts the search again from the end. The text and the console's lines stay.
  - name: Close
    kind: button
    default: ""
    effect: Closes **Search log** and starts the next search from the end.
```

### Log viewer settings

```controls
window: Log viewer settings
controls:
  - name: Font color
    kind: field
    default: The theme's text color
    effect: The color of the console's text, shown by its value. Its **Select color** button opens a color window that starts from the color the console uses now.
    persistence: Kept between sessions once applied, as every value of this window, in AfterNight's own settings file. This one is `log/font_color`, and the others `log/background_color`, `log/font_highlight_color`, `log/background_highlight_color`, `log/font_size`, `log/font_family`, `log/font_bold`, `log/font_italic` and `log/max_lines`.
  - name: Background color
    kind: field
    default: The theme's background for code
    effect: The color behind the lines, with its own **Select color** button.
    tradeoff: It also sets how faint and inverse text from **ANSI Colors** look.
  - name: Font Highlight
    kind: field
    default: The theme's text on the accent color
    effect: The color of selected text, as a found line is, with its own **Select color** button.
  - name: Background Highlight
    kind: field
    default: The theme's highlight color
    effect: The color behind selected text, with its own **Select color** button.
    tradeoff: Too little contrast hides what a search selects.
  - name: Default font Size
    kind: spinbox
    default: "12"
    widget: {min: 4, max: 30, step: 1}
    effect: The text size in points, which **Zoom 100%** returns to.
    tradeoff: Larger text shows fewer lines, and wraps sooner.
    note: Applying a new size also undoes the toolbar's zoom.
  - name: Font
    kind: combo
    default: The system's fixed-width font
    options: [The system's fixed-width font, Every font installed]
    effect: The font of the console's text.
    tradeoff: A font of varying width makes columns of numbers harder to read.
  - name: Bold
    kind: check
    default: "Off"
    effect: Draws all the console's text in bold, which hides the bold of **ANSI Colors**.
  - name: Italic
    kind: check
    default: "Off"
    effect: Draws all the console's text in italics, which hides the italics of **ANSI Colors**.
  - name: Max lines
    kind: spinbox
    default: "1000"
    widget: {min: 50, max: 50000, step: 1}
    effect: The most lines the console shows. Older ones leave the top as new ones arrive.
    range: Above 10,000 it shows no more messages, since that is all AfterNight keeps. Raising it cannot bring back a message already dropped.
    tradeoff: More lines take more memory and time to show and to search.
  - name: Sample
    kind: readout
    default: ""
    effect: Six sample lines in the font and colors you choose, of the severities info, debug, info, warn, warn and error, with the third and fourth selected to show the highlight colors.
    note: It follows each change at once. The console changes only on **Apply** or **OK**.
  - name: OK
    kind: button
    default: ""
    effect: Applies every value, shows the lines again, saves the values and closes the window.
  - name: Apply
    kind: button
    default: ""
    effect: Applies and saves every value, and keeps the window open.
    tradeoff: A lower **Max lines** drops the oldest lines from the console at once. AfterNight still keeps them, and a higher value shows them again.
  - name: Reset
    kind: button
    default: ""
    effect: Returns the four colors to the theme's, the font to the fixed-width one, the size to 12, **Max lines** to 1000, and turns **Bold** and **Italic** off, in this window only. Apply them to use them.
    range: It leaves word wrap, **ANSI Colors**, **Show >=**, the zoom, the messages and the log file as they are.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window without applying what you changed since the last **Apply**.
```

## When it goes wrong

An older message is missing
:   **Cause.** It came before the console's record started, more than 10,000 messages ago, before a **Clear
    Session**, below **Show >=**, or past **Max lines**.
:   **Fix.** Lower **Show >=** and raise **Max lines**. When it is still missing, open the log file from the Logging
    page of Settings, if its level kept the message.

The **info** and **warn** lines are gone
:   **Cause.** **Show >=** is at **error**, which shows only errors.
:   **Fix.** Choose **info** in **Show >=**.

The console still shows messages with the log file's level at **Off (Disable Logging)**
:   **Cause.** The level of the Logging page applies to the log file only.
:   **Fix.** Use **Show >=** to see fewer messages, or close the console.

"No occurrences found."
:   **Cause.** The text is not in the lines shown, an option excludes it, the pattern is not valid, or **Backward**
    started at the first line.
:   **Fix.** Lower **Show >=**, untick **Case sensitive**, **Match whole word** and **Regular expression**, untick
    **Backward**, and choose **Find next** again.

The exported file has fewer lines than you expected
:   **Cause.** The export writes the lines the console shows, after **Show >=** and **Max lines**.
:   **Fix.** Lower **Show >=**, raise **Max lines** and export again, or send the log file.

The text or the selection cannot be read
:   **Cause.** The console keeps the colors of the theme in force when it first closed, and keeps them after the
    theme changes, or a message's own colors clash with the background.
:   **Fix.** Open **Settings**, choose **Reset** and then **Apply**, or turn **ANSI Colors** off.

## Related

- [The Main Window](overview.md), for the status line and the other regions around the drawer.
- [The Rail and Its Places](rail-and-places.md), for the rail's **Session Console** and its count.
- [Tasks Reference](tasks-reference.md), for the lines a task writes here.
- [Settings](settings-dialog.md), for the log file's folder, level, size and count on the Logging page.
- [Troubleshooting And Diagnostics](../reference/troubleshooting.md), for what to send when you report a problem.
- [Top Bar and Menus](top-bar-and-menus.md), for **View > Session Console** among the other menus.
